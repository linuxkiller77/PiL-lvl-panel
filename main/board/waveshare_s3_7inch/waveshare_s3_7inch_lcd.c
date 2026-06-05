#include "waveshare_s3_7inch_lcd.h"

#include "waveshare_s3_7inch_config.h"
#include "waveshare_s3_7inch_state.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lvgl_port.h"

static const char *TAG = "ws_s3_lcd";

esp_err_t waveshare_s3_7inch_lcd_init_rgb_panel(void)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    ESP_LOGI(TAG, "Initializing RGB LCD panel");

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = WS_LCD_PIXEL_CLOCK,
            .h_res = WS_LCD_H_RES,
            .v_res = WS_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .bounce_buffer_size_px = WS_LCD_H_RES * 20,
        .dma_burst_size = 64,
        .hsync_gpio_num = WS_LCD_HSYNC_GPIO,
        .vsync_gpio_num = WS_LCD_VSYNC_GPIO,
        .de_gpio_num = WS_LCD_DE_GPIO,
        .pclk_gpio_num = WS_LCD_PCLK_GPIO,
        .disp_gpio_num = WS_LCD_DISP_GPIO,
        .data_gpio_nums = {
            WS_LCD_DATA0_GPIO, WS_LCD_DATA1_GPIO, WS_LCD_DATA2_GPIO, WS_LCD_DATA3_GPIO,
            WS_LCD_DATA4_GPIO, WS_LCD_DATA5_GPIO, WS_LCD_DATA6_GPIO, WS_LCD_DATA7_GPIO,
            WS_LCD_DATA8_GPIO, WS_LCD_DATA9_GPIO, WS_LCD_DATA10_GPIO, WS_LCD_DATA11_GPIO,
            WS_LCD_DATA12_GPIO, WS_LCD_DATA13_GPIO, WS_LCD_DATA14_GPIO, WS_LCD_DATA15_GPIO,
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &st->panel), TAG, "new RGB panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(st->panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(st->panel), TAG, "panel init failed");
    return ESP_OK;
}

esp_err_t waveshare_s3_7inch_lcd_register_lvgl_display(lv_display_t **out_display)
{
    waveshare_s3_7inch_state_t *st = waveshare_s3_7inch_state();

    ESP_LOGI(TAG, "Registering RGB LCD with LVGL");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = st->panel,
        .buffer_size = WS_LCD_H_RES * 80,
        .double_buffer = true,
        .hres = WS_LCD_H_RES,
        .vres = WS_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = false,
        },
    };

    st->display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(st->display, ESP_FAIL, TAG, "LVGL display add failed");

    if (out_display) {
        *out_display = st->display;
    }
    return ESP_OK;
}
