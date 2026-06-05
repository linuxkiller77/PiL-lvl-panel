#pragma once

// PiLab Panel diagnostic logging switches.
// Keep normal firmware logs readable by default. Turn the detail flags on only
// when chasing a specific runtime/display/memory issue.

#ifndef HMI_LOG_BOOT
#define HMI_LOG_BOOT 1
#endif

#ifndef HMI_LOG_SUMMARY
#define HMI_LOG_SUMMARY 1
#endif

#ifndef HMI_LOG_TIMING
#define HMI_LOG_TIMING 0
#endif

#ifndef HMI_LOG_TIMING_DETAIL
#define HMI_LOG_TIMING_DETAIL 0
#endif

#ifndef HMI_LOG_RUNTIME_STATE
#define HMI_LOG_RUNTIME_STATE 1
#endif

#ifndef HMI_LOG_EVENTS
#define HMI_LOG_EVENTS 0
#endif

#ifndef HMI_LOG_WIDGET_BUILD
#define HMI_LOG_WIDGET_BUILD 0
#endif

#ifndef HMI_LOG_PAGE_VISIBILITY_DETAIL
#define HMI_LOG_PAGE_VISIBILITY_DETAIL 0
#endif

#ifndef HMI_LOG_MEMORY_DETAIL
#define HMI_LOG_MEMORY_DETAIL 0
#endif

#ifndef HMI_DIAG_TASK_PERIOD_MS
#define HMI_DIAG_TASK_PERIOD_MS 10000
#endif

#ifndef HMI_LOG_MEMORY_SUMMARY
#define HMI_LOG_MEMORY_SUMMARY 1
#endif

#ifndef HMI_LOG_MEMORY_CHECKPOINTS
#define HMI_LOG_MEMORY_CHECKPOINTS 0
#endif

#ifndef HMI_LOG_SCRIPT_RUNTIME_DETAIL
#define HMI_LOG_SCRIPT_RUNTIME_DETAIL 0
#endif

#ifndef HMI_LOG_SCRIPT_LIFETIME_OK
#define HMI_LOG_SCRIPT_LIFETIME_OK 0
#endif

#ifndef HMI_LOG_SCRIPT_MODULE_DISCARD
#define HMI_LOG_SCRIPT_MODULE_DISCARD 0
#endif

#ifndef HMI_LOG_SCRIPT_MEMORY_RECOVERY
#define HMI_LOG_SCRIPT_MEMORY_RECOVERY 0
#endif

#ifndef HMI_LOG_BACKLIGHT_DETAIL
#define HMI_LOG_BACKLIGHT_DETAIL 0
#endif

#ifndef HMI_LOG_HTTP_TIMING
#define HMI_LOG_HTTP_TIMING 0
#endif

#ifndef HMI_MEMORY_STEADY_BASELINE_AFTER_MS
#define HMI_MEMORY_STEADY_BASELINE_AFTER_MS 4000
#endif

#ifndef HMI_MEMORY_WARN_INTERNAL_DROP_BYTES
#define HMI_MEMORY_WARN_INTERNAL_DROP_BYTES (12 * 1024)
#endif

#ifndef HMI_MEMORY_WARN_LARGEST_DROP_BYTES
#define HMI_MEMORY_WARN_LARGEST_DROP_BYTES (8 * 1024)
#endif
