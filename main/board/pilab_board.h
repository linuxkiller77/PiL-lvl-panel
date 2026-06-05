#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *board_name;
    const char *mcu_name;
    const char *display_name;
    const char *touch_name;
    int width;
    int height;
    bool has_touch;
    bool has_backlight;
    bool framebuffers_in_psram;
} pilab_board_info_t;

/**
 * Initialize the selected board display, touch controller, and LVGL port.
 *
 * The shared PiLab Panel runtime should call this API instead of calling a
 * board-specific LCD driver directly. Each board backend owns its own LCD bus,
 * touch driver, backlight, reset GPIOs/expanders, timing, and LVGL registration.
 */
esp_err_t pilab_board_display_init(lv_display_t **out_display, lv_indev_t **out_touch);

/** Set the selected board's LCD backlight. */
esp_err_t pilab_board_set_backlight(bool on);

/** Return static display/board information for layout/runtime code and logs. */
esp_err_t pilab_board_get_info(pilab_board_info_t *out_info);

#ifdef __cplusplus
}
#endif
