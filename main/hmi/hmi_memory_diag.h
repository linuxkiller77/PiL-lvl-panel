#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t internal_free;
    uint32_t internal_largest;
    uint32_t internal_min_ever;
    uint32_t psram_free;
    uint32_t psram_largest;
    uint32_t psram_min_ever;
    uint32_t task_count;
} hmi_memory_snapshot_t;

// Print a named memory checkpoint when HMI_LOG_MEMORY_CHECKPOINTS is enabled.
// The checkpoint also updates the internal baseline used by the periodic
// stability check below.
void hmi_memory_diag_checkpoint(const char *phase);

// Called from the diagnostics task after the normal health line.  It establishes
// a steady-state baseline after the runtime has settled, then warns if internal
// RAM or largest-block size keeps dropping beyond the configured threshold.
void hmi_memory_diag_periodic(uint32_t uptime_ms, const char *runtime_state);

// Lightweight snapshot helper for future diagnostics without forcing a log.
hmi_memory_snapshot_t hmi_memory_diag_snapshot(void);

#ifdef __cplusplus
}
#endif
