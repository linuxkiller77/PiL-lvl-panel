#pragma once

/*
 * Waveshare ESP32-S3 7-inch board hardware configuration.
 *
 * This file is intentionally board-specific. Shared PiLab Panel runtime code
 * should not include this file directly; it should use main/board/pilab_board.h.
 */

#define WS_LCD_H_RES       800
#define WS_LCD_V_RES       480
#define WS_LCD_PIXEL_CLOCK (14 * 1000 * 1000)

#define WS_LCD_HSYNC_GPIO  46
#define WS_LCD_VSYNC_GPIO  3
#define WS_LCD_DE_GPIO     5
#define WS_LCD_PCLK_GPIO   7
#define WS_LCD_DISP_GPIO   -1

#define WS_LCD_DATA0_GPIO  14
#define WS_LCD_DATA1_GPIO  38
#define WS_LCD_DATA2_GPIO  18
#define WS_LCD_DATA3_GPIO  17
#define WS_LCD_DATA4_GPIO  10
#define WS_LCD_DATA5_GPIO  39
#define WS_LCD_DATA6_GPIO  0
#define WS_LCD_DATA7_GPIO  45
#define WS_LCD_DATA8_GPIO  48
#define WS_LCD_DATA9_GPIO  47
#define WS_LCD_DATA10_GPIO 21
#define WS_LCD_DATA11_GPIO 1
#define WS_LCD_DATA12_GPIO 2
#define WS_LCD_DATA13_GPIO 42
#define WS_LCD_DATA14_GPIO 41
#define WS_LCD_DATA15_GPIO 40

#define WS_I2C_SDA_GPIO    8
#define WS_I2C_SCL_GPIO    9
#define WS_I2C_FREQ_HZ     (400 * 1000)

#define WS_GT911_INT_GPIO  4
#define WS_GT911_RST_GPIO  -1

/* CH422G output expander pins used by the Waveshare board. */
#define WS_CH422G_TP_RST_PIN   1
#define WS_CH422G_LCD_BL_PIN   2
#define WS_CH422G_LCD_RST_PIN  3
