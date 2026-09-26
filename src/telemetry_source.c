#define _DEFAULT_SOURCE

#include "telemetry_source.h"

#include <stdlib.h>
#include <string.h>

#ifdef CD_HAVE_SENSORHUB
#include <fcntl.h>
#include <sensorhub/serial.h>
#include <sensorhub/sensorhub.h>
#include <unistd.h>
#endif

#define FT_PER_SEC_PER_MPH 1.46667f

#ifdef CD_HAVE_SENSORHUB
/* If SensorHub ever renumbers its analog slots, fail here rather than
   silently reading the wrong channel for battery voltage. */
_Static_assert(SH_ANALOG_BATTERY == 0,
               "battery moved; update cd_default_channel_map()");
#endif

/* A supermileage car burns to a target then coasts a long way; a steady
   throttle drew the burn/coast rails as one flat colour. */
#define DEMO_BURN_TO_MPH  30.0f
#define DEMO_COAST_TO_MPH 14.0f
#define DEMO_BURN_ACCEL    2.5f
#define DEMO_COAST_DECEL   0.9f
#define DEMO_TIME_SCALE    2.0f

struct cd_telemetry_source {
    bool             live;
    int              fd;
    cd_channel_map_t map;
    race_telemetry_t telemetry;
#ifdef CD_HAVE_SENSORHUB
    sh_parser_t parser;
#endif
};

cd_channel_map_t cd_default_channel_map(void)
{
    cd_channel_map_t map;

    /* analog[0] carries battery voltage, scaled by 0.35, which is what the
       last car config the Python server ran with used.

       Spelled as a literal rather than SH_ANALOG_BATTERY so this function
       still compiles in the demo-only build, where SensorHub's header is not
       present at all. The static assertion below keeps the two in step. */
    map.voltage_channel = 0;
    map.voltage_scale = 0.35f;

    /* channel1 was timer_reset_button in that same config. */
    map.timer_reset_channel = 1;

    /* Never mapped by anything, see the header. */
    map.engine_armed_channel = CD_CHANNEL_UNMAPPED;
    map.engine_on_channel = CD_CHANNEL_UNMAPPED;

    return map;
}

#ifdef CD_HAVE_SENSORHUB
/* Read a mapped digital channel. An unmapped index (negative) or an
   out-of-range one leaves *out alone and reports false, so a bad map entry
   shows up as a dark indicator rather than a wrong one. */
static bool mapped_digital(const sh_packet_t *packet, int index, bool *out)
{
    if (index < 0) return false;
    return sh_digital_in(packet, (unsigned)index, out) == SH_OK;
}

static void apply_packet(cd_telemetry_source *source, const sh_packet_t *packet)
{
    race_telemetry_t *t = &source->telemetry;
    const cd_channel_map_t *map = &source->map;
    uint16_t raw = 0;

    t->speed_mph = packet->speed;
    t->airspeed_mph = packet->airspeed;

    if (map->voltage_channel >= 0 &&
        sh_analog(packet, (unsigned)map->voltage_channel, &raw) == SH_OK) {
        t->voltage_v = (float)raw * map->voltage_scale;
        t->voltage_valid = true;
    }
    else {
        t->voltage_valid = false;
    }

    (void)mapped_digital(packet, map->timer_reset_channel, &t->timer_reset);

    t->engine_armed_valid =
        mapped_digital(packet, map->engine_armed_channel, &t->engine_armed);
    t->engine_on_valid =
        mapped_digital(packet, map->engine_on_channel, &t->engine_on);
}
#endif /* CD_HAVE_SENSORHUB */

static void advance_demo(race_telemetry_t *t, float elapsed_s)
{
    float dt = elapsed_s * DEMO_TIME_SCALE;

    if (t->engine_on) {
        t->speed_mph += DEMO_BURN_ACCEL * dt;
        if (t->speed_mph >= DEMO_BURN_TO_MPH) {
            t->engine_on = false;
        }
    }
    else {
        t->speed_mph -= DEMO_COAST_DECEL * dt;
        if (t->speed_mph <= DEMO_COAST_TO_MPH) {
            t->engine_on = true;
        }
    }

    t->distance_ft += t->speed_mph * FT_PER_SEC_PER_MPH * dt;
}

cd_telemetry_source *cd_telemetry_source_open(const char *device,
                                              const cd_channel_map_t *map)
{
    cd_telemetry_source *source = calloc(1, sizeof(*source));

    if (source == NULL) {
        return NULL;
    }

    source->fd = -1;
    source->map = (map != NULL) ? *map : cd_default_channel_map();

    source->telemetry.speed_mph = DEMO_COAST_TO_MPH;
    source->telemetry.airspeed_mph = 8.1f;
    source->telemetry.voltage_v = 27.8f;
    source->telemetry.voltage_valid = true;
    source->telemetry.engine_on = true;
    source->telemetry.engine_on_valid = true;
    source->telemetry.engine_armed = true;
    source->telemetry.engine_armed_valid = true;

#ifdef CD_HAVE_SENSORHUB
    source->fd = sh_serial_open(device);

    if (source->fd >= 0) {
        int flags = fcntl(source->fd, F_GETFL, 0);

        /* Non-blocking: the UI thread cannot afford to wait on a byte. */
        if (flags != -1) {
            (void)fcntl(source->fd, F_SETFL, flags | O_NONBLOCK);
        }

        sh_parser_init(&source->parser);
        source->live = true;

        /* Live telemetry starts unknown, not at the demo's pleasant values. */
        memset(&source->telemetry, 0, sizeof(source->telemetry));
    }
#else
    (void)device;
#endif

    return source;
}

bool cd_telemetry_source_is_live(const cd_telemetry_source *source)
{
    return source != NULL && source->live;
}

bool cd_telemetry_source_poll(cd_telemetry_source *source, uint32_t elapsed_ms,
                              race_telemetry_t *out)
{
    float elapsed_s;

    if (source == NULL || out == NULL) {
        return false;
    }

    elapsed_s = (float)elapsed_ms / 1000.0f;

    if (!source->live) {
        advance_demo(&source->telemetry, elapsed_s);
        *out = source->telemetry;
        return true;
    }

#ifdef CD_HAVE_SENSORHUB
    {
        uint8_t buffer[256];
        sh_packet_t packet;
        bool got = false;
        ssize_t n;

        /* Drain what is waiting and keep the newest complete packet.  A
           backlog means the UI fell behind, and the freshest sample is the
           only one worth drawing. */
        while ((n = read(source->fd, buffer, sizeof(buffer))) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                if (sh_parser_feed(&source->parser, buffer[i], &packet)
                    == SH_OK) {
                    apply_packet(source, &packet);
                    got = true;
                }
            }
        }

        /* Integrate distance on wall clock regardless, so a quiet link holds
           the odometer steady instead of rewinding it. */
        source->telemetry.distance_ft +=
            source->telemetry.speed_mph * FT_PER_SEC_PER_MPH * elapsed_s;

        *out = source->telemetry;
        return got;
    }
#else
    *out = source->telemetry;
    return false;
#endif
}

void cd_telemetry_source_close(cd_telemetry_source *source)
{
    if (source == NULL) {
        return;
    }

#ifdef CD_HAVE_SENSORHUB
    if (source->fd >= 0) {
        sh_serial_close(source->fd);
    }
#endif

    free(source);
}
