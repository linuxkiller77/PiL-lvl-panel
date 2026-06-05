#include "waveshare_s3_7inch_backlight.h"

#include "waveshare_s3_7inch_config.h"
#include "waveshare_s3_7inch_state.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hmi/hmi_diag_log.h"

static const char *TAG = "ws_s3_backlight";

esp_err_t waveshare_s3_7inch_expander_init_and_reset_lcd(void)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    uint16_t outputs = 0;
    outputs |= (uint16_t)(1U << WS_CH422G_LCD_BL_PIN);   /* keep previous baseline behavior */
    outputs |= (uint16_t)(1U << WS_CH422G_LCD_RST_PIN);  /* reset inactive */
    outputs |= (uint16_t)(1U << WS_CH422G_TP_RST_PIN);   /* reset inactive */

    ESP_RETURN_ON_ERROR(ch422g_init(&st->expander, st->i2c_bus, outputs), TAG, "CH422G init failed");

    ESP_LOGI(TAG, "Resetting LCD through CH422G");
    ESP_RETURN_ON_ERROR(ch422g_write_pin(&st->expander, WS_CH422G_LCD_RST_PIN, false), TAG, "LCD reset low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(ch422g_write_pin(&st->expander, WS_CH422G_LCD_RST_PIN, true), TAG, "LCD reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_backlight_set(bool on)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();
    ESP_RETURN_ON_FALSE(st->i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "LCD I2C not initialized");
#if HMI_LOG_BACKLIGHT_DETAIL
    ESP_LOGI(TAG, "LCD backlight %s", on ? "on" : "off");
#endif
    return ch422g_write_pin(&st->expander, WS_CH422G_LCD_BL_PIN, on);
}
