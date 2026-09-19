/*
 * Live telemetry for the dashboard.
 *
 * Bridges SensorHub's wire packets to the race_telemetry_t the dashboard
 * draws.  Two jobs live here that nothing else does:
 *
 *   1. Channel mapping.  The Arduino sends anonymous channel0..4 and
 *      channelA0; what they *mean* is a wiring decision.  The old Python
 *      server kept this in car_config.json, where both a git pull and an MQTT
 *      message could rewrite it.  Here it is an explicit struct, so the
 *      mapping is visible in the code that depends on it.
 *
 *   2. Distance.  The packet has no odometer, so distance is integrated from
 *      speed.  It therefore restarts at zero whenever this process does.
 *
 * When no serial device is present the dashboard falls back to the built-in
 * demo, which is what makes the simulator useful on a laptop.
 */

#ifndef TELEMETRY_SOURCE_H
#define TELEMETRY_SOURCE_H

#include "race_dashboard.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Which physical channel carries what.
 *
 * Defaults follow the last known-good car_config.json from the Python server,
 * which is the only written record of this wiring we have.
 *
 * engine_armed and engine_on are deliberately left unmapped.  The dashboard
 * has drawn indicators for them since it was written, but no channel has ever
 * carried them: the old config never defined them either, so those lights
 * have been dark in every run so far.  Set the channel indices below once
 * someone confirms which pins they are on, rather than guessing and shipping
 * a confidently wrong light.
 */
#define CD_CHANNEL_UNMAPPED (-1)

typedef struct {
    int   voltage_channel;      /* index into the analog channel, A0 only */
    float voltage_scale;        /* raw count -> volts */
    int   timer_reset_channel;  /* 0..4, digital */
    int   engine_armed_channel; /* 0..4, or CD_CHANNEL_UNMAPPED */
    int   engine_on_channel;    /* 0..4, or CD_CHANNEL_UNMAPPED */
} cd_channel_map_t;

/* The mapping the car currently ships with. */
cd_channel_map_t cd_default_channel_map(void);

typedef struct cd_telemetry_source cd_telemetry_source;

/*
 * Open a source.
 *
 * device may be NULL to use SensorHub's default port.  If the port cannot be
 * opened, this still succeeds and returns a source in demo mode, because a
 * dashboard that refuses to start without a car attached is useless on a
 * bench.  Check cd_telemetry_source_is_live() to tell the two apart.
 */
cd_telemetry_source *cd_telemetry_source_open(const char *device,
                                              const cd_channel_map_t *map);

/* True when real packets are arriving from hardware. */
bool cd_telemetry_source_is_live(const cd_telemetry_source *source);

/*
 * Advance by elapsed_ms and write the latest telemetry into *out.
 *
 * Returns true if anything changed.  Non-blocking: in live mode it drains
 * whatever bytes are waiting and keeps the newest packet, so a slow UI frame
 * never backs up the serial buffer.
 */
bool cd_telemetry_source_poll(cd_telemetry_source *source, uint32_t elapsed_ms,
                              race_telemetry_t *out);

void cd_telemetry_source_close(cd_telemetry_source *source);

#endif /* TELEMETRY_SOURCE_H */
