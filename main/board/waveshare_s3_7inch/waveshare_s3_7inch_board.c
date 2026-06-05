#include "waveshare_s3_7inch_board.h"

#include "waveshare_s3_7inch_backlight.h"
#include "waveshare_s3_7inch_config.h"
#include "waveshare_s3_7inch_lcd.h"
#include "waveshare_s3_7inch_state.h"
#include "waveshare_s3_7inch_touch_gt911.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "driver/i2c_master.h"

static const char *TAG = "ws_s3_board";

static esp_err_t init_i2c(void)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = WS_I2C_SCL_GPIO,
        .sda_io_num = WS_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &st->i2c_bus), TAG, "I2C init failed");
    return ESP_OK;
}

static esp_err_t init_lvgl_port(void)
{
    ESP_LOGI(TAG, "Initializing LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");
    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_board_display_init(lv_display_t **out_display, lv_indev_t **out_touch)
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "I2C bring-up failed");
    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_expander_init_and_reset_lcd(), TAG, "expander/LCD reset bring-up failed");
    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_lcd_init_rgb_panel(), TAG, "RGB panel bring-up failed");
    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_touch_gt911_init(), TAG, "GT911 touch bring-up failed");
    ESP_RETURN_ON_ERROR(init_lvgl_port(), TAG, "LVGL port bring-up failed");
    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_lcd_register_lvgl_display(out_display), TAG, "LVGL display registration failed");
    ESP_RETURN_ON_ERROR(waveshare_s3_7inch_touch_register_lvgl(out_touch), TAG, "LVGL touch registration failed");

    ESP_LOGI(TAG, "Waveshare ESP32-S3 7-inch LCD/touch/LVGL initialized");
    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_board_set_backlight(bool on)
{
    return waveshare_s3_7inch_backlight_set(on);
}
