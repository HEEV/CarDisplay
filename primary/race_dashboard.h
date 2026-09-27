#ifndef RACE_DASHBOARD_H
#define RACE_DASHBOARD_H

#include "lvgl/lvgl.h"
#include "telemetry.h"
#include <stdbool.h>

/* race_telemetry_t lives in telemetry.h: the sources produce it and should
   not have to include a dashboard header to know its shape. */


/* Create the dashboard beneath parent (usually lv_scr_act()). */
void race_dashboard_create(lv_obj_t * parent);

/* Update displayed telemetry. Call from the LVGL-owning/UI task. */
void race_dashboard_set_telemetry(const race_telemetry_t * telemetry);

/* The two speeds the strategy is built around: burn up to the high one, then
   coast down to the low one.  The wing ticks fill as the car nears whichever
   of the two is coming next, so the driver watches the call arrive instead of
   being told the moment it lands.  Until this is set the ticks stay dark. */
void race_dashboard_set_burn_window(float burn_start_mph, float burn_stop_mph);

/* Start a visual burn/coast countdown, equivalent to React animate={true}. */
void race_dashboard_start_sequence(uint32_t burn_ms, uint32_t coast_ms);

#endif
