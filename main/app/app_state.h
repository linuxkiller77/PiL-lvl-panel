#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t uptime_ms;
    uint32_t logic_ticks;
    uint32_t ui_ticks;
    uint32_t diagnostics_ticks;
    uint32_t touch_clicks;
    int slider_value;
    bool switch_enabled;
    uint8_t mode_index;

    uint32_t free_internal_bytes;
    uint32_t min_free_internal_bytes;
    uint32_t free_psram_bytes;
    uint32_t largest_psram_block_bytes;

    uint32_t task_count;
    uint32_t display_task_load_percent_est;
    uint32_t ui_updates_per_sec;

    uint32_t ui_update_last_us;
    uint32_t ui_update_max_us;
    uint32_t page_switch_last_us;
    uint32_t page_switch_max_us;

    uint32_t stack_logic_words;
    uint32_t stack_ui_words;
    uint32_t stack_diag_words;
} app_state_snapshot_t;

esp_err_t app_state_init(void);
void app_state_logic_tick(uint32_t uptime_ms);
void app_state_ui_tick(void);
void app_state_diagnostics_tick(void);
void app_state_set_slider_value(int value);
void app_state_set_switch_enabled(bool enabled);
void app_state_set_mode_index(uint8_t mode_index);
void app_state_note_touch_click(void);
void app_state_set_heap(uint32_t free_internal_bytes,
                        uint32_t min_free_internal_bytes,
                        uint32_t free_psram_bytes,
                        uint32_t largest_psram_block_bytes);
void app_state_set_ui_update_time_us(uint32_t elapsed_us);
void app_state_set_page_switch_time_us(uint32_t elapsed_us);
void app_state_set_runtime_stats(uint32_t task_count,
                                 uint32_t display_task_load_percent_est,
                                 uint32_t ui_updates_per_sec,
                                 uint32_t stack_logic_words,
                                 uint32_t stack_ui_words,
                                 uint32_t stack_diag_words);
void app_state_get_snapshot(app_state_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
