#include "app_state.h"

#include <string.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_state";
static SemaphoreHandle_t s_mutex;
static app_state_snapshot_t s_state;

static void lock_state(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t app_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.slider_value = 50;
    s_state.switch_enabled = true;
    s_state.mode_index = 0;
    s_state.min_free_internal_bytes = UINT32_MAX;

    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create state mutex");
    return ESP_OK;
}

void app_state_logic_tick(uint32_t uptime_ms)
{
    lock_state();
    s_state.uptime_ms = uptime_ms;
    s_state.logic_ticks++;
    unlock_state();
}

void app_state_ui_tick(void)
{
    lock_state();
    s_state.ui_ticks++;
    unlock_state();
}

void app_state_diagnostics_tick(void)
{
    lock_state();
    s_state.diagnostics_ticks++;
    unlock_state();
}

void app_state_set_slider_value(int value)
{
    lock_state();
    s_state.slider_value = value;
    unlock_state();
}

void app_state_set_switch_enabled(bool enabled)
{
    lock_state();
    s_state.switch_enabled = enabled;
    unlock_state();
}

void app_state_set_mode_index(uint8_t mode_index)
{
    lock_state();
    s_state.mode_index = mode_index;
    unlock_state();
}

void app_state_note_touch_click(void)
{
    lock_state();
    s_state.touch_clicks++;
    unlock_state();
}

void app_state_set_heap(uint32_t free_internal_bytes,
                        uint32_t min_free_internal_bytes,
                        uint32_t free_psram_bytes,
                        uint32_t largest_psram_block_bytes)
{
    lock_state();
    s_state.free_internal_bytes = free_internal_bytes;
    s_state.min_free_internal_bytes = min_free_internal_bytes;
    s_state.free_psram_bytes = free_psram_bytes;
    s_state.largest_psram_block_bytes = largest_psram_block_bytes;
    unlock_state();
}

void app_state_set_ui_update_time_us(uint32_t elapsed_us)
{
    lock_state();
    s_state.ui_update_last_us = elapsed_us;
    if (elapsed_us > s_state.ui_update_max_us) {
        s_state.ui_update_max_us = elapsed_us;
    }
    unlock_state();
}

void app_state_set_page_switch_time_us(uint32_t elapsed_us)
{
    lock_state();
    s_state.page_switch_last_us = elapsed_us;
    if (elapsed_us > s_state.page_switch_max_us) {
        s_state.page_switch_max_us = elapsed_us;
    }
    unlock_state();
}

void app_state_set_runtime_stats(uint32_t task_count,
                                 uint32_t display_task_load_percent_est,
                                 uint32_t ui_updates_per_sec,
                                 uint32_t stack_logic_words,
                                 uint32_t stack_ui_words,
                                 uint32_t stack_diag_words)
{
    lock_state();
    s_state.task_count = task_count;
    s_state.display_task_load_percent_est = display_task_load_percent_est;
    s_state.ui_updates_per_sec = ui_updates_per_sec;
    s_state.stack_logic_words = stack_logic_words;
    s_state.stack_ui_words = stack_ui_words;
    s_state.stack_diag_words = stack_diag_words;
    unlock_state();
}

void app_state_get_snapshot(app_state_snapshot_t *out_snapshot)
{
    if (!out_snapshot) {
        return;
    }

    lock_state();
    *out_snapshot = s_state;
    unlock_state();
}
