#include "hal.h"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

lv_display_t * hal_init()
{

  #if LV_USE_SDL
    lv_group_set_default(lv_group_create());

    lv_display_t * disp = lv_sdl_window_create(SCREEN_WIDTH, SCREEN_HEIGHT);

    lv_display_set_default(disp);
  #else
    const char * device = "/dev/fb0";
    lv_display_t * disp = lv_linux_fbdev_create();

    if(disp == NULL) {
        return NULL;
    }

    lv_result_t res = lv_linux_fbdev_set_file(disp, device);
    if(res != LV_RESULT_OK) {
        lv_display_delete(disp);
        return NULL;
    }
  #endif


  return disp;
}
