/**
 * @file main.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE /* needed for usleep() */
#endif

#include <stdlib.h>
#include <stdio.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
#endif
#include "../lvgl/lvgl.h"
#include <SDL.h>

#include "hal/hal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#include "race_dashboard.h"
#include "sources.h"

static void sleep_ms(uint32_t ms)
{
#ifdef _MSC_VER
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

race_telemetry_t t = {
    .speed_mph = 0.0f,
    .airspeed_mph = 8.1f,
    .distance_ft = 0.0f,
    .voltage_v = 27.8f,
    .voltage_valid = true,
    .engine_armed = true,
    .engine_armed_valid = true,
    .engine_on = true,
    .engine_on_valid = true,
   };

/* The demo's burn window is also what the dashboard draws its wing ticks
   against until a real strategizer supplies one.  The simulation itself now
   lives in telemetry_source.c alongside the live path, so that the dashboard
   has exactly one way in regardless of whether a car is attached. */
#define DEMO_BURN_TO_MPH    30.0f   /* shut the engine off at this speed */
#define DEMO_COAST_TO_MPH   14.0f   /* light it again at this speed */

int main(int argc, char **argv)
{
  cd_sources_config cfg = {
    .sensor_device   = (argc > 1) ? argv[1] : NULL,
    .bms_device      = (argc > 2) ? argv[2] : NULL,
    .bms_interval_ms = 200u,
    .map             = NULL,
  };
  cd_sources * sources;

  lv_init();
  sdl_hal_init(1024, 600);

  race_dashboard_create(lv_scr_act());

  sources = cd_sources_start(&cfg);
  if(sources == NULL) {
    fprintf(stderr, "could not start telemetry sources\n");
    return 1;
  }

  /* Until a strategizer supplies one, the demo's own window. */
  cd_sources_set_burn_window(sources, DEMO_COAST_TO_MPH, DEMO_BURN_TO_MPH);
  race_dashboard_set_burn_window(DEMO_COAST_TO_MPH, DEMO_BURN_TO_MPH);

  printf("sensors: %s   bms: %s\n",
         cd_sources_sensors_live(sources) ? "live" : "demo",
         cd_sources_bms_live(sources) ? "live" : "absent");

  uint32_t last_tick = lv_tick_get();

  while(1) {
    uint32_t sleep_time_ms;

    /* Everything touching LVGL holds the lock, lv_timer_handler included.
       Missing it here is the classic mistake: the corruption looks random and
       only shows up under load. */
    lv_lock();
    sleep_time_ms = lv_timer_handler();
    lv_unlock();

    if(sleep_time_ms == LV_NO_TIMER_READY){
	    sleep_time_ms = LV_DEF_REFR_PERIOD;
    }
    sleep_ms(sleep_time_ms);

    uint32_t now = lv_tick_get();
    cd_sources_snapshot(sources, now - last_tick, &t);
    last_tick = now;

    lv_lock();
    race_dashboard_set_telemetry(&t);
    lv_unlock();
  }

  cd_sources_stop(sources);
  return 0;
}
