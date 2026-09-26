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
#include "telemetry_source.h"

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
  /* Optional serial device. Without one the source falls back to the demo,
     which is what keeps this useful on a laptop with no car attached. */
  const char * device = (argc > 1) ? argv[1] : NULL;
  cd_telemetry_source * source;

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(1024, 600);

  race_dashboard_create(lv_scr_act());
  /* The real pair comes from the strategizer; the demo drives its own. */
  race_dashboard_set_burn_window(DEMO_COAST_TO_MPH, DEMO_BURN_TO_MPH);

  source = cd_telemetry_source_open(device, NULL);

  if(source == NULL) {
    fprintf(stderr, "could not allocate a telemetry source\n");
    return 1;
  }

  printf("telemetry: %s\n",
         cd_telemetry_source_is_live(source) ? "live from the car"
                                             : "demo (no serial device)");

  uint32_t last_tick = lv_tick_get();

  while(1) {
    /* Periodically call the lv_task handler.
     * It could be done in a timer interrupt or an OS task too.*/
    uint32_t sleep_time_ms = lv_timer_handler();
    if(sleep_time_ms == LV_NO_TIMER_READY){
	    sleep_time_ms = LV_DEF_REFR_PERIOD;
    }
    sleep_ms(sleep_time_ms);

    uint32_t now = lv_tick_get();
    cd_telemetry_source_poll(source, now - last_tick, &t);
    last_tick = now;
    race_dashboard_set_telemetry(&t);
  }

  cd_telemetry_source_close(source);
  return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
