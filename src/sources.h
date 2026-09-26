/*
 * Telemetry sources, each on its own thread.
 *
 * The three inputs have incompatible shapes. The Arduino pushes frames at
 * 20 Hz. The BMS is request/response and a reply can take most of a second.
 * The strategizer computes for tens of milliseconds. None of that fits in a
 * render loop that must not stall for more than a frame.
 *
 * So each source blocks freely on its own thread and publishes into its own
 * slot behind its own mutex. The UI takes a snapshot per frame. A slow BMS
 * cannot stall the sensors, and neither can stall the display.
 *
 * Both sources are optional at build time, so this still compiles and runs as
 * a demo on a laptop with no hardware and no sibling checkouts.
 */

#ifndef CD_SOURCES_H
#define CD_SOURCES_H

#include "telemetry.h"

#include <stdbool.h>
#include <stdint.h>

#define CD_CHANNEL_UNMAPPED (-1)

/* Which digital or analog channel carries what. Explicit code rather than a
   config file a git pull could rewrite, which is how the old Python server
   lost its channel names.

   engine_armed and engine_on stay unmapped: no channel has ever carried them,
   and a confidently wrong light is worse than a dark one. Mapping engine_on
   also unblocks the strategizer, which needs it. */
typedef struct {
    int   voltage_channel;
    float voltage_scale;
    int   timer_reset_channel;
    int   engine_armed_channel;
    int   engine_on_channel;
} cd_channel_map_t;

cd_channel_map_t cd_default_channel_map(void);

typedef struct cd_sources cd_sources;

typedef struct {
    const char *sensor_device; /* NULL for SensorHub's default, or none */
    const char *bms_device;    /* NULL for none */
    uint32_t    bms_interval_ms;
    const cd_channel_map_t *map; /* NULL for the default */
} cd_sources_config;

/*
 * Start whatever is available. Never fails for want of hardware: a device
 * that will not open leaves that source dark and the rest running, because a
 * dashboard that refuses to start without a car is useless on a bench.
 */
cd_sources *cd_sources_start(const cd_sources_config *config);

/* True when real frames are arriving from the Arduino. */
bool cd_sources_sensors_live(const cd_sources *sources);

/* True when the BMS is answering. */
bool cd_sources_bms_live(const cd_sources *sources);

/*
 * Copy the current state out. Cheap, takes each lock briefly, safe to call
 * every frame. elapsed_ms advances the demo when no sensor hardware is
 * present, and integrates distance when it is.
 */
void cd_sources_snapshot(cd_sources *sources, uint32_t elapsed_ms,
                         race_telemetry_t *out);

/* Publish a burn window, from the strategizer or a constant. */
void cd_sources_set_burn_window(cd_sources *sources, float start_mph,
                                float stop_mph);

void cd_sources_stop(cd_sources *sources);

#endif /* CD_SOURCES_H */
