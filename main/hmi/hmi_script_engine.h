#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hmi_script_engine_init(void);
// Start AngelScript compilation/execution on a dedicated task with a large stack.
// This avoids overflowing ESP-IDF's relatively small app_main task stack.
esp_err_t hmi_script_engine_start_async(void);
// v6.7 boot path: compile AngelScript before the HMI layout is shown.
// This avoids showing the real HMI and then hiding it again during startup.
esp_err_t hmi_script_engine_boot_compile_pre_hmi(uint32_t timeout_ms);
esp_err_t hmi_script_engine_compile(const char *script_text);
esp_err_t hmi_script_engine_call(const char *function_name);
// Queue a script event to be executed outside the LVGL task.
// Use this from LVGL touch/click callbacks so script execution never blocks redraw.
esp_err_t hmi_script_engine_post_call(const char *function_name);
// Queue OnPanelTick() without allowing periodic tick calls to stack up.
esp_err_t hmi_script_engine_post_panel_tick(void);
const char *hmi_script_engine_last_error(void);
const char *hmi_script_engine_compile_state(void);
bool hmi_script_engine_runtime_valid(void);
uint32_t hmi_script_engine_compile_generation(void);
const char *hmi_script_engine_active_script_name(void);

typedef struct {
    bool engine_created;
    bool module_active;
    bool runtime_valid;
    bool compile_task_active;
    bool compile_worker_running;
    bool event_queue_created;
    bool event_task_running;
    uint32_t compile_generation;
    uint32_t engine_create_count;
    uint32_t module_count;
    uint32_t compile_ok_count;
    uint32_t compile_fail_count;
    uint32_t old_module_discard_count;
    uint32_t event_queue_waiting;
    uint32_t event_task_stack_high_water;
    uint32_t compile_worker_stack_high_water;
    bool panel_tick_pending;
    bool panel_tick_missing;
    uint32_t panel_tick_posted_count;
    uint32_t panel_tick_skipped_count;
    uint32_t panel_tick_executed_count;
    uint32_t panel_tick_missing_count;
    uint32_t panel_tick_queue_full_count;
    const char *compile_state;
    const char *active_module_name;
    const char *active_script_name;
} hmi_script_engine_stats_t;

void hmi_script_engine_get_stats(hmi_script_engine_stats_t *out);
void hmi_script_engine_log_stats(const char *where);

esp_err_t hmi_script_engine_load_script(char **out_script);
esp_err_t hmi_script_engine_get_runtime_script(char **out_script, const char **out_source);
esp_err_t hmi_script_engine_save_script(const char *script_text);
esp_err_t hmi_script_engine_compile_async(const char *script_text);
esp_err_t hmi_script_engine_call_on_panel_start(void);
const char *hmi_script_engine_default_script(void);

#ifdef __cplusplus
}
#endif
