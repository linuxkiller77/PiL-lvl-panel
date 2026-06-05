#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_s3_7inch_expander_init_and_reset_lcd(void);
esp_err_t waveshare_s3_7inch_backlight_set(bool on);

#ifdef __cplusplus
}
#endif
