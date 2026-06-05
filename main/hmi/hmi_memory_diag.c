#include "hmi_memory_diag.h"

#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hmi/hmi_diag_log.h"

static const char *TAG = "hmi_mem";

static bool s_have_last;
static hmi_memory_snapshot_t s_last;
static bool s_have_steady;
static hmi_memory_snapshot_t s_steady;
static uint32_t s_steady_uptime_ms;
static bool s_drop_warning_printed;
static bool s_fragment_warning_printed;

static uint32_t diff_down(uint32_t before, uint32_t after)
{
    return before > after ? (before - after) : 0U;
}

hmi_memory_snapshot_t hmi_memory_diag_snapshot(void)
{
    hmi_memory_snapshot_t s = {
        .internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .internal_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .internal_min_ever = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .psram_largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .psram_min_ever = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .task_count = (uint32_t)uxTaskGetNumberOfTasks(),
    };
    return s;
}

void hmi_memory_diag_checkpoint(const char *phase)
{
#if HMI_LOG_MEMORY_CHECKPOINTS
    hmi_memory_snapshot_t now = hmi_memory_diag_snapshot();
    if (s_have_last) {
        int32_t d_int = (int32_t)now.internal_free - (int32_t)s_last.internal_free;
        int32_t d_lrg = (int32_t)now.internal_largest - (int32_t)s_last.internal_largest;
        int32_t d_psr = (int32_t)now.psram_free - (int32_t)s_last.psram_free;
        ESP_LOGI(TAG,
                 "checkpoint: %s internal=%lu largest=%lu min=%lu psram=%lu psram_largest=%lu tasks=%lu delta_int=%ld delta_largest=%ld delta_psram=%ld",
                 phase ? phase : "?",
                 (unsigned long)now.internal_free,
                 (unsigned long)now.internal_largest,
                 (unsigned long)now.internal_min_ever,
                 (unsigned long)now.psram_free,
                 (unsigned long)now.psram_largest,
                 (unsigned long)now.task_count,
                 (long)d_int,
                 (long)d_lrg,
                 (long)d_psr);
    } else {
        ESP_LOGI(TAG,
                 "checkpoint: %s internal=%lu largest=%lu min=%lu psram=%lu psram_largest=%lu tasks=%lu",
                 phase ? phase : "?",
                 (unsigned long)now.internal_free,
                 (unsigned long)now.internal_largest,
                 (unsigned long)now.internal_min_ever,
                 (unsigned long)now.psram_free,
                 (unsigned long)now.psram_largest,
                 (unsigned long)now.task_count);
    }
    s_last = now;
    s_have_last = true;
#else
    (void)phase;
#endif
}

void hmi_memory_diag_periodic(uint32_t uptime_ms, const char *runtime_state)
{
#if HMI_LOG_MEMORY_SUMMARY
    hmi_memory_snapshot_t now = hmi_memory_diag_snapshot();

    bool ready = runtime_state && strcmp(runtime_state, "READY") == 0;
    if (!s_have_steady && ready && uptime_ms >= HMI_MEMORY_STEADY_BASELINE_AFTER_MS) {
        s_steady = now;
        s_have_steady = true;
        s_steady_uptime_ms = uptime_ms;
        ESP_LOGI(TAG,
                 "steady baseline: uptime=%lums internal=%lu largest=%lu min=%lu psram=%lu psram_largest=%lu tasks=%lu",
                 (unsigned long)uptime_ms,
                 (unsigned long)now.internal_free,
                 (unsigned long)now.internal_largest,
                 (unsigned long)now.internal_min_ever,
                 (unsigned long)now.psram_free,
                 (unsigned long)now.psram_largest,
                 (unsigned long)now.task_count);
        return;
    }

    if (!s_have_steady || !ready) return;

    uint32_t free_drop = diff_down(s_steady.internal_free, now.internal_free);
    uint32_t largest_drop = diff_down(s_steady.internal_largest, now.internal_largest);

    if (!s_drop_warning_printed && free_drop >= HMI_MEMORY_WARN_INTERNAL_DROP_BYTES) {
        s_drop_warning_printed = true;
        ESP_LOGW(TAG,
                 "internal RAM below steady baseline: uptime=%lums baseline=%lu now=%lu drop=%lu bytes baseline_at=%lums",
                 (unsigned long)uptime_ms,
                 (unsigned long)s_steady.internal_free,
                 (unsigned long)now.internal_free,
                 (unsigned long)free_drop,
                 (unsigned long)s_steady_uptime_ms);
    }

    if (!s_fragment_warning_printed && largest_drop >= HMI_MEMORY_WARN_LARGEST_DROP_BYTES) {
        s_fragment_warning_printed = true;
        ESP_LOGW(TAG,
                 "largest internal block below steady baseline: uptime=%lums baseline=%lu now=%lu drop=%lu bytes baseline_at=%lums",
                 (unsigned long)uptime_ms,
                 (unsigned long)s_steady.internal_largest,
                 (unsigned long)now.internal_largest,
                 (unsigned long)largest_drop,
                 (unsigned long)s_steady_uptime_ms);
    }
#else
    (void)uptime_ms;
    (void)runtime_state;
#endif
}
