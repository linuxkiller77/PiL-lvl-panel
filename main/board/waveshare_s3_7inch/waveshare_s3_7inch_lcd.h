#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_s3_7inch_lcd_init_rgb_panel(void);
esp_err_t waveshare_s3_7inch_lcd_register_lvgl_display(lv_display_t **out_display);

#ifdef __cplusplus
}
#endif
