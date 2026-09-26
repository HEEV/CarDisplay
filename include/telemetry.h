/*
 * The telemetry the dashboard draws.
 *
 * Lives here rather than in race_dashboard.h so that a source producing this
 * data does not have to include a UI header to learn its shape. The arrow
 * points from the dashboard to the model, not the other way round.
 *
 * Every field that can be absent carries a validity flag. With sources on
 * their own threads, "the BMS is idle" and "the BMS thread died" look
 * identical unless freshness is part of the data, so each group also carries
 * the time it was last updated.
 */

#ifndef CD_TELEMETRY_H
#define CD_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

/* A group whose timestamp is older than this reads invalid. Two seconds is
   long enough for the BMS's 5 Hz poll to miss a few replies and short enough
   that a dead thread shows up before anyone trusts the number. */
#define CD_STALE_AFTER_MS 2000u

typedef struct {
    /* ---- from the Arduino, via SensorHub ---- */
    float speed_mph;
    float airspeed_mph;
    float distance_ft;
    float voltage_v;          /* the Arduino's analog sense, not the pack */
    bool  voltage_valid;
    bool  engine_armed;
    bool  engine_armed_valid;
    bool  engine_on;
    bool  engine_on_valid;
    bool  timer_reset;
    uint64_t sensors_updated_ms;
    bool     sensors_valid;

    /* ---- from the BMS, via BmsHub ---- */
    float pack_volts;
    float pack_amps;          /* negative is discharge, as the BMS reports */
    float pack_watts;         /* positive means drawing from the pack */
    uint8_t soc_percent;
    float energy_kj;          /* integrated volts x amps since start */
    float energy_kj_coulomb;  /* from the BMS's own capacity count */
    uint16_t cell_spread_mv;
    uint16_t protection;      /* 0 when healthy */
    uint64_t bms_updated_ms;
    bool     bms_valid;

    /* ---- from the strategizer ---- */
    float burn_start_mph;
    float burn_stop_mph;
    bool  burn_window_valid;
} race_telemetry_t;

#endif /* CD_TELEMETRY_H */
