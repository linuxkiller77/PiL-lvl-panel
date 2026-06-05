#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hmi_lvgl_runtime_create(lv_display_t *display);
esp_err_t hmi_lvgl_runtime_load_layout(const char *json);
esp_err_t hmi_lvgl_runtime_request_reload_json(const char *json);
esp_err_t hmi_lvgl_runtime_request_reload_current(void);
esp_err_t hmi_lvgl_runtime_get_current_layout_json(char **out_json, const char **out_source);
esp_err_t hmi_lvgl_runtime_show_page(const char *page_name_or_id);
void hmi_lvgl_runtime_update(void);
size_t hmi_lvgl_runtime_widget_count(void);
uint32_t hmi_lvgl_runtime_panel_tick_rate_ms(void);

// External activity guard for non-LVGL work that can disturb the RGB panel
// indirectly (heavy PSRAM/cache/memory traffic, script compilation, etc.).
// It disables touch/script events and live tag-to-widget updates until the
// external activity is complete and LVGL has had a few cycles to settle.
void hmi_lvgl_runtime_external_activity_begin(const char *reason);
void hmi_lvgl_runtime_external_activity_end(const char *reason);
bool hmi_lvgl_runtime_wait_until_ready(uint32_t timeout_ms);
const char *hmi_lvgl_runtime_state_name(void);

// Foreground blocking operation support for long jobs such as AngelScript
// compilation. begin() displays a status overlay, lets it render, then holds
// the LVGL port lock to stop lv_timer_handler()/rendering while the caller does
// non-LVGL work. end() resumes LVGL, shows OK/error text briefly, and removes
// the overlay. Do not call any LVGL APIs between begin() and end().
bool hmi_lvgl_runtime_blocking_status_begin(const char *title, const char *message, uint32_t render_ms, const char *reason);
void hmi_lvgl_runtime_blocking_status_end(const char *title, const char *message, bool is_error, uint32_t hold_ms, const char *reason);

#ifdef __cplusplus
}
#endif
