#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
const char *hmi_layout_default_json(void);
esp_err_t hmi_layout_store_load(char **out_json);
esp_err_t hmi_layout_store_save(const char *json);
esp_err_t hmi_layout_store_reset_default(void);
#ifdef __cplusplus
}
#endif
