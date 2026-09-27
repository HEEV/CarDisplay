#define _DEFAULT_SOURCE

#include "sources.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef CD_HAVE_SENSORHUB
#include <sensorhub/sensorhub.h>
#include <sensorhub/serial.h>
#endif

#ifdef CD_HAVE_BMSHUB
#include <jbd/jbd.h>
#include <jbd/serial.h>
#endif

#define FT_PER_SEC_PER_MPH 1.46667f

/* A supermileage car burns to a target then coasts a long way; a steady
   throttle drew the burn/coast rails as one flat colour. */
#define DEMO_BURN_TO_MPH  30.0f
#define DEMO_COAST_TO_MPH 14.0f
#define DEMO_BURN_ACCEL    2.5f
#define DEMO_COAST_DECEL   0.9f
#define DEMO_TIME_SCALE    2.0f

#ifdef CD_HAVE_SENSORHUB
_Static_assert(SH_ANALOG_BATTERY == 0,
               "battery moved; update cd_default_channel_map()");
#endif

/* One slot per source. Separate locks on purpose: a BMS reply taking most of
   a second must not hold up a 20 Hz sensor feed. */
typedef struct {
    pthread_mutex_t lock;
    race_telemetry_t data;
    bool     live;
    bool     stop;
    pthread_t thread;
    bool     running;
} slot_t;

struct cd_sources {
    slot_t sensors;
    slot_t bms;

    pthread_mutex_t burn_lock;
    float burn_start_mph;
    float burn_stop_mph;
    bool  burn_valid;

    cd_channel_map_t map;
    int  sensor_fd;
    int  bms_fd;
    uint32_t bms_interval_ms;

    /* Demo state, only touched by the snapshot caller when no sensor hardware
       is present, so it needs no lock. */
    race_telemetry_t demo;
};

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + (uint64_t)(tv.tv_usec / 1000);
}

cd_channel_map_t cd_default_channel_map(void)
{
    cd_channel_map_t map;

    /* analog[0] is battery voltage scaled by 0.35, from the last car config
       the Python server ran with. A literal, not SH_ANALOG_BATTERY, so this
       compiles without SensorHub present; the assertion above keeps them in
       step. */
    map.voltage_channel = 0;
    map.voltage_scale = 0.35f;
    map.timer_reset_channel = 1;
    map.engine_armed_channel = CD_CHANNEL_UNMAPPED;
    map.engine_on_channel = CD_CHANNEL_UNMAPPED;
    return map;
}

static void slot_init(slot_t *s)
{
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
}

/* Only the source threads ask this, so it is unused when both libraries are
   compiled out. Guard it rather than leave a warning in the demo build. */
#if defined(CD_HAVE_SENSORHUB) || defined(CD_HAVE_BMSHUB)
static bool slot_stopping(slot_t *s)
{
    bool stop;
    pthread_mutex_lock(&s->lock);
    stop = s->stop;
    pthread_mutex_unlock(&s->lock);
    return stop;
}
#endif

/* ------------------------------------------------------------------ *
 *  Sensor thread
 * ------------------------------------------------------------------ */

#ifdef CD_HAVE_SENSORHUB
static bool mapped_digital(const sh_packet_t *p, int index, bool *out)
{
    if (index < 0) return false;
    return sh_digital_in(p, (unsigned)index, out) == SH_OK;
}

static void *sensor_thread(void *arg)
{
    cd_sources *s = (cd_sources *)arg;
    sh_parser_t parser;
    sh_packet_t packet;

    sh_parser_init(&parser);

    while (!slot_stopping(&s->sensors)) {
        /* Blocking, which is the point of being on a thread. */
        sh_status_t st = sh_serial_read_packet(s->sensor_fd, &parser, &packet);

        if (st == SH_E_INTERRUPTED) continue;
        if (st != SH_OK) {
            if (st == SH_E_CLOSED || st == SH_E_IO) break;
            continue;
        }

        pthread_mutex_lock(&s->sensors.lock);
        {
            race_telemetry_t *t = &s->sensors.data;
            const cd_channel_map_t *m = &s->map;
            uint16_t raw = 0;

            t->speed_mph = packet.speed;
            t->airspeed_mph = packet.airspeed;

            if (m->voltage_channel >= 0 &&
                sh_analog(&packet, (unsigned)m->voltage_channel, &raw)
                    == SH_OK) {
                t->voltage_v = (float)raw * m->voltage_scale;
                t->voltage_valid = true;
            }
            else {
                t->voltage_valid = false;
            }

            (void)mapped_digital(&packet, m->timer_reset_channel,
                                 &t->timer_reset);
            t->engine_armed_valid =
                mapped_digital(&packet, m->engine_armed_channel,
                               &t->engine_armed);
            t->engine_on_valid =
                mapped_digital(&packet, m->engine_on_channel, &t->engine_on);

            t->sensors_updated_ms = now_ms();
            t->sensors_valid = true;
        }
        s->sensors.live = true;
        pthread_mutex_unlock(&s->sensors.lock);
    }

    pthread_mutex_lock(&s->sensors.lock);
    s->sensors.live = false;
    pthread_mutex_unlock(&s->sensors.lock);
    return NULL;
}
#endif /* CD_HAVE_SENSORHUB */

/* ------------------------------------------------------------------ *
 *  BMS thread
 * ------------------------------------------------------------------ */

#ifdef CD_HAVE_BMSHUB
static void *bms_thread(void *arg)
{
    cd_sources *s = (cd_sources *)arg;
    jbd_energy_t energy;
    uint64_t last = 0;

    jbd_energy_init(&energy);

    while (!slot_stopping(&s->bms)) {
        jbd_frame_t frame;
        jbd_basic_t basic;
        jbd_cells_t cells;
        uint16_t spread = 0;

        /* Blocking with a timeout, which is exactly right here and would have
           been wrong in the render loop. */
        jbd_status_t st = jbd_serial_read_register(s->bms_fd, JBD_REG_BASIC,
                                                  1000u, &frame, NULL);

        if (st == JBD_E_INTERRUPTED) continue;
        if (st != JBD_OK || jbd_decode_basic(&frame, &basic) != JBD_OK) {
            if (st == JBD_E_CLOSED || st == JBD_E_IO) break;
            usleep(s->bms_interval_ms * 1000u);
            continue;
        }

        {
            uint64_t t = now_ms();
            uint32_t dt = (last == 0u) ? 0u : (uint32_t)(t - last);
            last = t;
            jbd_energy_update(&energy, &basic, dt);
        }

        if (jbd_serial_read_register(s->bms_fd, JBD_REG_CELLS, 1000u, &frame,
                                     NULL) == JBD_OK
            && jbd_decode_cells(&frame, &cells) == JBD_OK) {
            spread = jbd_cell_spread_mv(&cells);
        }

        pthread_mutex_lock(&s->bms.lock);
        {
            race_telemetry_t *t = &s->bms.data;
            t->pack_volts = basic.volts;
            t->pack_amps = basic.amps;
            t->pack_watts = basic.volts * -basic.amps;
            t->soc_percent = basic.soc_percent;
            t->energy_kj = (float)(energy.joules / 1000.0);
            t->energy_kj_coulomb = (float)(energy.coulomb_joules / 1000.0);
            t->cell_spread_mv = spread;
            t->protection = basic.protection;
            t->bms_updated_ms = now_ms();
            t->bms_valid = true;
        }
        s->bms.live = true;
        pthread_mutex_unlock(&s->bms.lock);

        usleep(s->bms_interval_ms * 1000u);
    }

    pthread_mutex_lock(&s->bms.lock);
    s->bms.live = false;
    pthread_mutex_unlock(&s->bms.lock);
    return NULL;
}
#endif /* CD_HAVE_BMSHUB */

/* ------------------------------------------------------------------ *
 *  Lifecycle
 * ------------------------------------------------------------------ */

cd_sources *cd_sources_start(const cd_sources_config *config)
{
    cd_sources *s = calloc(1, sizeof(*s));
    if (s == NULL) return NULL;

    slot_init(&s->sensors);
    slot_init(&s->bms);
    pthread_mutex_init(&s->burn_lock, NULL);

    s->sensor_fd = -1;
    s->bms_fd = -1;
    s->bms_interval_ms = 200u;
    s->map = cd_default_channel_map();

    if (config != NULL) {
        if (config->map != NULL) s->map = *config->map;
        if (config->bms_interval_ms > 0u) {
            s->bms_interval_ms = config->bms_interval_ms;
        }
    }

    /* Demo baseline, used only when no sensor hardware answers. */
    s->demo.speed_mph = DEMO_COAST_TO_MPH;
    s->demo.airspeed_mph = 8.1f;
    s->demo.voltage_v = 27.8f;
    s->demo.voltage_valid = true;
    s->demo.engine_on = true;
    s->demo.engine_on_valid = true;
    s->demo.engine_armed = true;
    s->demo.engine_armed_valid = true;

#ifdef CD_HAVE_SENSORHUB
    s->sensor_fd = sh_serial_open(config ? config->sensor_device : NULL);
    if (s->sensor_fd >= 0) {
        s->sensors.running =
            pthread_create(&s->sensors.thread, NULL, sensor_thread, s) == 0;
        if (!s->sensors.running) {
            sh_serial_close(s->sensor_fd);
            s->sensor_fd = -1;
        }
    }
#endif

#ifdef CD_HAVE_BMSHUB
    if (config != NULL && config->bms_device != NULL) {
        s->bms_fd = jbd_serial_open(config->bms_device);
        if (s->bms_fd >= 0) {
            s->bms.running =
                pthread_create(&s->bms.thread, NULL, bms_thread, s) == 0;
            if (!s->bms.running) {
                jbd_serial_close(s->bms_fd);
                s->bms_fd = -1;
            }
        }
    }
#endif

    return s;
}

bool cd_sources_sensors_live(const cd_sources *sources)
{
    return sources != NULL && sources->sensors.running;
}

bool cd_sources_bms_live(const cd_sources *sources)
{
    return sources != NULL && sources->bms.running;
}

void cd_sources_set_burn_window(cd_sources *sources, float start_mph,
                                float stop_mph)
{
    if (sources == NULL) return;

    pthread_mutex_lock(&sources->burn_lock);
    sources->burn_start_mph = start_mph;
    sources->burn_stop_mph = stop_mph;
    sources->burn_valid = true;
    pthread_mutex_unlock(&sources->burn_lock);
}

static void advance_demo(race_telemetry_t *t, float elapsed_s)
{
    float dt = elapsed_s * DEMO_TIME_SCALE;

    if (t->engine_on) {
        t->speed_mph += DEMO_BURN_ACCEL * dt;
        if (t->speed_mph >= DEMO_BURN_TO_MPH) t->engine_on = false;
    }
    else {
        t->speed_mph -= DEMO_COAST_DECEL * dt;
        if (t->speed_mph <= DEMO_COAST_TO_MPH) t->engine_on = true;
    }

    t->distance_ft += t->speed_mph * FT_PER_SEC_PER_MPH * dt;
}

void cd_sources_snapshot(cd_sources *sources, uint32_t elapsed_ms,
                         race_telemetry_t *out)
{
    uint64_t now;
    float elapsed_s;

    if (sources == NULL || out == NULL) return;

    now = now_ms();
    elapsed_s = (float)elapsed_ms / 1000.0f;
    memset(out, 0, sizeof(*out));

    if (sources->sensors.running) {
        pthread_mutex_lock(&sources->sensors.lock);
        {
            race_telemetry_t *t = &sources->sensors.data;

            /* Integrate distance here rather than on the sensor thread: the
               packet carries no odometer, and the UI's clock is the one that
               matters for what the driver sees. */
            t->distance_ft += t->speed_mph * FT_PER_SEC_PER_MPH * elapsed_s;

            /* Freshness is data. Without this a dead thread and an idle link
               look the same. */
            if (now - t->sensors_updated_ms > CD_STALE_AFTER_MS) {
                t->sensors_valid = false;
            }

            out->speed_mph = t->speed_mph;
            out->airspeed_mph = t->airspeed_mph;
            out->distance_ft = t->distance_ft;
            out->voltage_v = t->voltage_v;
            out->voltage_valid = t->voltage_valid && t->sensors_valid;
            out->engine_armed = t->engine_armed;
            out->engine_armed_valid = t->engine_armed_valid && t->sensors_valid;
            out->engine_on = t->engine_on;
            out->engine_on_valid = t->engine_on_valid && t->sensors_valid;
            out->timer_reset = t->timer_reset;
            out->sensors_updated_ms = t->sensors_updated_ms;
            out->sensors_valid = t->sensors_valid;
        }
        pthread_mutex_unlock(&sources->sensors.lock);
    }
    else {
        advance_demo(&sources->demo, elapsed_s);
        sources->demo.sensors_updated_ms = now;
        sources->demo.sensors_valid = true;
        *out = sources->demo;
    }

    if (sources->bms.running) {
        pthread_mutex_lock(&sources->bms.lock);
        {
            race_telemetry_t *t = &sources->bms.data;

            if (now - t->bms_updated_ms > CD_STALE_AFTER_MS) {
                t->bms_valid = false;
            }

            out->pack_volts = t->pack_volts;
            out->pack_amps = t->pack_amps;
            out->pack_watts = t->pack_watts;
            out->soc_percent = t->soc_percent;
            out->energy_kj = t->energy_kj;
            out->energy_kj_coulomb = t->energy_kj_coulomb;
            out->cell_spread_mv = t->cell_spread_mv;
            out->protection = t->protection;
            out->bms_updated_ms = t->bms_updated_ms;
            out->bms_valid = t->bms_valid;
        }
        pthread_mutex_unlock(&sources->bms.lock);
    }

    pthread_mutex_lock(&sources->burn_lock);
    out->burn_start_mph = sources->burn_start_mph;
    out->burn_stop_mph = sources->burn_stop_mph;
    out->burn_window_valid = sources->burn_valid;
    pthread_mutex_unlock(&sources->burn_lock);
}

void cd_sources_stop(cd_sources *sources)
{
    if (sources == NULL) return;

    pthread_mutex_lock(&sources->sensors.lock);
    sources->sensors.stop = true;
    pthread_mutex_unlock(&sources->sensors.lock);

    pthread_mutex_lock(&sources->bms.lock);
    sources->bms.stop = true;
    pthread_mutex_unlock(&sources->bms.lock);

    /* The threads sit in blocking reads, so nudge them rather than waiting out
       a timeout. Closing the descriptor is what actually breaks the read. */
#ifdef CD_HAVE_SENSORHUB
    if (sources->sensor_fd >= 0) sh_serial_close(sources->sensor_fd);
#endif
#ifdef CD_HAVE_BMSHUB
    if (sources->bms_fd >= 0) jbd_serial_close(sources->bms_fd);
#endif

    if (sources->sensors.running) pthread_join(sources->sensors.thread, NULL);
    if (sources->bms.running) pthread_join(sources->bms.thread, NULL);

    pthread_mutex_destroy(&sources->sensors.lock);
    pthread_mutex_destroy(&sources->bms.lock);
    pthread_mutex_destroy(&sources->burn_lock);
    free(sources);
}
