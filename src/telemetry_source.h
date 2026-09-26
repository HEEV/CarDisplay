/* Maps SensorHub packets onto the race_telemetry_t the dashboard draws, and
   integrates distance, which the packet does not carry. Falls back to a demo
   when no serial device is present. */
#ifndef TELEMETRY_SOURCE_H
#define TELEMETRY_SOURCE_H

#include "race_dashboard.h"

#include <stdbool.h>
#include <stdint.h>

/* Which channel carries what. Explicit code rather than a JSON file that a
   git pull or an MQTT message could rewrite, which is how the old Python
   server lost its channel names.

   engine_armed and engine_on stay unmapped: the dashboard has drawn
   indicators for them since it was written but no channel has ever carried
   them, and a confidently wrong light is worse than a dark one. */
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

/* device may be NULL for SensorHub's default port. A port that will not open
   still succeeds, in demo mode: a dashboard that refuses to start without a
   car is useless on a bench. Check cd_telemetry_source_is_live(). */
cd_telemetry_source *cd_telemetry_source_open(const char *device,
                                              const cd_channel_map_t *map);

/* True when real packets are arriving from hardware. */
bool cd_telemetry_source_is_live(const cd_telemetry_source *source);

/* Non-blocking. Drains whatever is waiting and keeps the newest packet, so a
   slow UI frame never backs up the serial buffer. True if anything changed. */
bool cd_telemetry_source_poll(cd_telemetry_source *source, uint32_t elapsed_ms,
                              race_telemetry_t *out);

void cd_telemetry_source_close(cd_telemetry_source *source);

#endif /* TELEMETRY_SOURCE_H */
