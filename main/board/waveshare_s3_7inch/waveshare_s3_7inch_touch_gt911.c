#include "waveshare_s3_7inch_touch_gt911.h"

#include "waveshare_s3_7inch_config.h"
#include "waveshare_s3_7inch_state.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ws_s3_gt911";

esp_err_t waveshare_s3_7inch_touch_gt911_reset(void)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    ESP_LOGI(TAG, "Resetting GT911 through CH422G + INT GPIO");

    gpio_config_t int_out = {
        .pin_bit_mask = 1ULL << WS_GT911_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_out), TAG, "GT911 INT output config failed");
    gpio_set_level(WS_GT911_INT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(ch422g_write_pin(&st->expander, WS_CH422G_TP_RST_PIN, false), TAG, "TP reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(ch422g_write_pin(&st->expander, WS_CH422G_TP_RST_PIN, true), TAG, "TP reset high failed");
    vTaskDelay(pdMS_TO_TICKS(200));

    gpio_reset_pin(WS_GT911_INT_GPIO);
    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_touch_gt911_init(void)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_touch_gt911_reset(), TAG, "GT911 reset failed");

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(st->i2c_bus, &tp_io_config, &tp_io_handle), TAG, "touch panel IO failed");

    esp_lcd_touch_io_gt911_config_t gt911_io_config = {
        .dev_addr = tp_io_config.dev_addr,
    };

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = WS_LCD_H_RES,
        .y_max = WS_LCD_V_RES,
        .rst_gpio_num = WS_GT911_RST_GPIO,
        .int_gpio_num = WS_GT911_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .driver_data = &gt911_io_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &st->touch), TAG, "GT911 init failed");
    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_touch_register_lvgl(lv_indev_t **out_touch)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = st->display,
        .handle = st->touch,
    };
    st->touch_indev = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(st->touch_indev, ESP_FAIL, TAG, "LVGL touch add failed");

    if (out_touch) {
        *out_touch = st->touch_indev;
    }
    return ESP_OK;
}
