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

/* Stand-in telemetry for a car that is not plugged in.  A supermileage car
   does not hold a steady throttle: it burns up to a target speed, shuts the
   engine off, coasts a long way while it slows, then burns again.  Reporting
   a permanently running engine, as this used to, left the dashboard pinned in
   its engine-on state and drew the burn/coast rails as one flat colour. */
#define DEMO_BURN_TO_MPH    30.0f   /* shut the engine off at this speed */
#define DEMO_COAST_TO_MPH   14.0f   /* light it again at this speed */
#define DEMO_BURN_ACCEL      2.5f   /* mph gained per second under power */
#define DEMO_COAST_DECEL     0.9f   /* mph lost per second coasting */
#define DEMO_TIME_SCALE      2.0f   /* run the fake race faster than life */
#define FT_PER_SEC_PER_MPH   1.46667f

static void advance_demo(race_telemetry_t * tm, float elapsed_s)
{
    float dt = elapsed_s * DEMO_TIME_SCALE;

    if(tm->engine_on) {
        tm->speed_mph += DEMO_BURN_ACCEL * dt;
        if(tm->speed_mph >= DEMO_BURN_TO_MPH) tm->engine_on = false;
    }
    else {
        tm->speed_mph -= DEMO_COAST_DECEL * dt;
        if(tm->speed_mph <= DEMO_COAST_TO_MPH) tm->engine_on = true;
    }

    tm->distance_ft += tm->speed_mph * FT_PER_SEC_PER_MPH * dt;
}

int main(int argc, char **argv)
{
  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(1024, 600);

  race_dashboard_create(lv_scr_act());

  t.speed_mph = DEMO_COAST_TO_MPH;
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
    advance_demo(&t, (now - last_tick) / 1000.0f);
    last_tick = now;
    race_dashboard_set_telemetry(&t);
  }

  return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
