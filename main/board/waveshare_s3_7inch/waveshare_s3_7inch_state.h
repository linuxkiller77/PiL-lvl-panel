#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#include "ch422g.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ch422g_t expander;
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *touch_indev;
} waveshare_s3_7inch_state_t;

waveshare_s3_7inch_state_t *waveshare_s3_7inch_state(void);

#ifdef __cplusplus
}
#endif
