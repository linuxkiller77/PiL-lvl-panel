#include "app_tasks.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.h"
#include "hmi/hmi_lvgl_runtime.h"
#include "hmi/hmi_tag_db.h"
#include "hmi/hmi_diag_log.h"
#include "hmi/hmi_memory_diag.h"
#include "hmi/hmi_script_engine.h"

static const char *TAG = "app_tasks";
static TaskHandle_t s_logic_handle;
static TaskHandle_t s_ui_handle;
static TaskHandle_t s_diag_handle;
static uint32_t s_last_ui_ticks;

static void app_logic_task(void *arg)
{
    (void)arg;
    const int64_t start_us = esp_timer_get_time();
    uint32_t last_heartbeat_s = UINT32_MAX;
    uint32_t last_panel_tick_ms = 0;

    while (true) {
        uint32_t uptime_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
        uint32_t uptime_s = uptime_ms / 1000;
        app_state_logic_tick(uptime_ms);

        // Keep firmware-side logic intentionally tiny. AngelScript event calls
        // are queued and executed by the dedicated hmi_script_evt task. The
        // optional OnPanelTick() is a controlled HMI logic hook, not a PLC scan.
        if (uptime_s != last_heartbeat_s) {
            last_heartbeat_s = uptime_s;
            hmi_tag_db_set_bool("Panel_Heartbeat", (uptime_s & 1U) != 0);
            // This tag is diagnostic only. Updating once per second avoids turning
            // the HMI renderer into a constant redraw loop.
            hmi_tag_db_set_number("Panel_Uptime_s", (float)uptime_s);
        }

        // Do not simulate Motor_Running or Motor_Speed_PV here. Those values are
        // intentionally controlled by AngelScript event handlers or future driver
        // addons. The HMI panel firmware only provides diagnostics such as uptime
        // and heartbeat outside of explicit event/script logic.

        uint32_t panel_tick_ms = hmi_lvgl_runtime_panel_tick_rate_ms();
        if (panel_tick_ms > 0 && (uint32_t)(uptime_ms - last_panel_tick_ms) >= panel_tick_ms) {
            last_panel_tick_ms = uptime_ms;
            (void)hmi_script_engine_post_panel_tick();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void ui_update_task(void *arg)
{
    (void)arg;
    uint32_t last_generation = UINT32_MAX;
    while (true) {
        if (hmi_tag_db_generation_changed(&last_generation)) {
            int64_t start_us = esp_timer_get_time();
            hmi_lvgl_runtime_update();
            uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - start_us);
            app_state_set_ui_update_time_us(elapsed_us);
            app_state_ui_tick();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static uint32_t stack_words(TaskHandle_t handle)
{
    return handle ? (uint32_t)uxTaskGetStackHighWaterMark(handle) : 0;
}

static void diagnostics_task(void *arg)
{
    (void)arg;
    while (true) {
        app_state_snapshot_t before;
        app_state_get_snapshot(&before);

        app_state_set_heap(heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                           heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                           heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                           heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

        uint32_t ui_updates_per_sec = before.ui_ticks - s_last_ui_ticks;
        s_last_ui_ticks = before.ui_ticks;
        uint32_t display_load = (before.ui_update_last_us * 100U) / 100000U;
        if (display_load > 100U) display_load = 100U;

        app_state_set_runtime_stats((uint32_t)uxTaskGetNumberOfTasks(),
                                    display_load,
                                    ui_updates_per_sec,
                                    stack_words(s_logic_handle),
                                    stack_words(s_ui_handle),
                                    stack_words(s_diag_handle));
        app_state_diagnostics_tick();

        app_state_snapshot_t snapshot;
        app_state_get_snapshot(&snapshot);
#if HMI_LOG_SUMMARY
        hmi_script_engine_stats_t script_stats;
        hmi_script_engine_get_stats(&script_stats);
        ESP_LOGI(TAG,
                 "health: uptime=%lums state=%s widgets=%lu tags=%lu ui_last=%luus tasks=%lu internal=%lu psram=%lu script={state=%s valid=%d gen=%lu modules=%lu engines=%lu event_task=%d worker=%d compile_task=%d}",
                 (unsigned long)snapshot.uptime_ms,
                 hmi_lvgl_runtime_state_name(),
                 (unsigned long)hmi_lvgl_runtime_widget_count(),
                 (unsigned long)hmi_tag_db_count(),
                 (unsigned long)snapshot.ui_update_last_us,
                 (unsigned long)snapshot.task_count,
                 (unsigned long)snapshot.free_internal_bytes,
                 (unsigned long)snapshot.free_psram_bytes,
                 script_stats.compile_state ? script_stats.compile_state : "?",
                 script_stats.runtime_valid ? 1 : 0,
                 (unsigned long)script_stats.compile_generation,
                 (unsigned long)script_stats.module_count,
                 (unsigned long)script_stats.engine_create_count,
                 script_stats.event_task_running ? 1 : 0,
                 script_stats.compile_worker_running ? 1 : 0,
                 script_stats.compile_task_active ? 1 : 0);
#endif

        hmi_memory_diag_periodic(snapshot.uptime_ms, hmi_lvgl_runtime_state_name());

        vTaskDelay(pdMS_TO_TICKS(HMI_DIAG_TASK_PERIOD_MS));
    }
}

esp_err_t app_tasks_start(void)
{
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(app_logic_task, "app_logic", 4096, NULL, 5, &s_logic_handle, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "failed to create app_logic task");
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(ui_update_task, "hmi_update", 6144, NULL, 4, &s_ui_handle, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "failed to create hmi_update task");
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(diagnostics_task, "diagnostics", 4096, NULL, 3, &s_diag_handle, 0) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "failed to create diagnostics task");
    ESP_LOGI(TAG, "PiLab Panel tasks started");
    return ESP_OK;
}
