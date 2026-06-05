#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_s3_7inch_board_display_init(lv_display_t **out_display, lv_indev_t **out_touch);
esp_err_t waveshare_s3_7inch_board_set_backlight(bool on);

#ifdef __cplusplus
}
#endif
