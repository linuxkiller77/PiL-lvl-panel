#include "pilab_board.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"

#if defined(PILAB_BOARD_WAVESHARE_S3_7INCH)
#include "waveshare_s3_7inch/waveshare_s3_7inch_board.h"
#include "waveshare_s3_7inch/waveshare_s3_7inch_config.h"
#else
#error "No PiLab Panel board backend selected. Define PILAB_BOARD_WAVESHARE_S3_7INCH or add a new backend in main/board/pilab_board.c."
#endif

static const char *TAG = "pilab_board";

esp_err_t pilab_board_display_init(lv_display_t **out_display, lv_indev_t **out_touch)
{
#if defined(PILAB_BOARD_WAVESHARE_S3_7INCH)
    ESP_LOGI(TAG, "Initializing board backend: Waveshare ESP32-S3 7-inch");
    return waveshare_s3_7inch_board_display_init(out_display, out_touch);
#endif
}

esp_err_t pilab_board_set_backlight(bool on)
{
#if defined(PILAB_BOARD_WAVESHARE_S3_7INCH)
    return waveshare_s3_7inch_board_set_backlight(on);
#endif
}

esp_err_t pilab_board_get_info(pilab_board_info_t *out_info)
{
    ESP_RETURN_ON_FALSE(out_info != NULL, ESP_ERR_INVALID_ARG, TAG, "out_info is NULL");
    memset(out_info, 0, sizeof(*out_info));

#if defined(PILAB_BOARD_WAVESHARE_S3_7INCH)
    out_info->board_name = "waveshare_s3_7inch";
    out_info->mcu_name = "ESP32-S3";
    out_info->display_name = "Waveshare 7-inch RGB LCD";
    out_info->touch_name = "GT911";
    out_info->width = WS_LCD_H_RES;
    out_info->height = WS_LCD_V_RES;
    out_info->has_touch = true;
    out_info->has_backlight = true;
    out_info->framebuffers_in_psram = true;
    return ESP_OK;
#endif
}
