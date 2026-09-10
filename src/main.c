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
#include <sys/time.h>
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
struct timeval old;
race_telemetry_t t = {
    .speed_mph = 0.0f,
    .airspeed_mph = 8.1f,
    .distance_ft = 0.0f,
    .voltage_v = 27.8f,
    .voltage_valid = true,
    .engine_armed = true,
    .engine_on = true,
   };

int main(int argc, char **argv)
{
  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(1024, 600);

  race_dashboard_create(lv_scr_act());

  t.speed_mph = 15;

  while(1) {
    /* Periodically call the lv_task handler.
     * It could be done in a timer interrupt or an OS task too.*/
    uint32_t sleep_time_ms = lv_timer_handler();
    if(sleep_time_ms == LV_NO_TIMER_READY){
	    sleep_time_ms = LV_DEF_REFR_PERIOD;
    }

    t.distance_ft += 1;
    race_dashboard_set_telemetry(&t);
  }

  return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
