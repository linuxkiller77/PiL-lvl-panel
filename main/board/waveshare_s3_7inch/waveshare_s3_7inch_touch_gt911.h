#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_s3_7inch_touch_gt911_reset(void);
esp_err_t waveshare_s3_7inch_touch_gt911_init(void);
esp_err_t waveshare_s3_7inch_touch_register_lvgl(lv_indev_t **out_touch);

#ifdef __cplusplus
}
#endif
