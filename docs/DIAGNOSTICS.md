# Diagnostics

v6.15 reduced normal serial monitor noise while keeping the logs that matter for operation and troubleshooting.

## Normal logs that should remain visible

The normal monitor output should include:

- boot progress
- storage mount result
- LCD/LVGL initialization
- AngelScript compile success/failure summaries
- HMI reload summaries
- Wi-Fi and HTTP server startup
- warning/error logs
- periodic health summaries
- memory steady baseline and memory warnings

## Deep diagnostics

The most repetitive diagnostics are controlled by flags in:

```text
main/hmi/hmi_diag_log.h
```

Important flags include:

```c
HMI_LOG_MEMORY_CHECKPOINTS
HMI_LOG_SCRIPT_RUNTIME_DETAIL
HMI_LOG_SCRIPT_LIFETIME_OK
HMI_LOG_SCRIPT_MODULE_DISCARD
HMI_LOG_SCRIPT_MEMORY_RECOVERY
HMI_LOG_BACKLIGHT_DETAIL
HMI_LOG_HTTP_TIMING
```

Set a flag to `1` when investigating a specific issue. Keep them at `0` for normal operation.

## Health summary

The periodic health summary is intentionally kept because it gives a compact view of system state.

Example healthy idle pattern:

```text
health: uptime=... state=READY widgets=13 tags=9 ui_last=... tasks=... internal=... psram=... script={state=ok valid=1 gen=... modules=1 engines=1 event_task=1 worker=1 compile_task=0}
```

Important fields:

- `state=READY` means the runtime is not compiling or rebuilding.
- `widgets=<n>` should match the active page widget count.
- `internal=<bytes>` gives free internal RAM.
- `psram=<bytes>` gives free PSRAM.
- `modules=1 engines=1` confirms script runtime lifetime is stable.
- `worker=1` means the persistent compile worker exists.
- `compile_task=0` means no compile is currently active.

## Expected temporary states

During Compile-to-RAM, it is normal to see:

```text
state=SCRIPT_COMPILING
widgets=0
compile_task=1
```

During layout reload, it is normal to see:

```text
state=LAYOUT_RELOADING
```

After the reload settles, it should return to:

```text
state=READY
```

## Compile failure logs

A bad script should generate warnings/errors from AngelScript and a compile failure summary.

That is not automatically a firmware problem. It is only a firmware problem if the panel fails to recover to `READY`, leaks modules/engines, crashes, or leaves the LCD blank.

## HTTP 404 noise

Browsers or operating systems may probe local network services and request URLs that PiLab Panel does not implement. A warning such as `/ubus` not found can appear when a client probes the device.

This is usually harmless unless it happens continuously or is associated with memory loss, crashes, or HTTP server failure.

## When to enable deep diagnostics

Enable detailed logs only when investigating a specific regression:

- display offset or partial render
- stuck black screen
- stuck `SCRIPT_COMPILING`
- growing `modules` or `engines`
- unexplained internal RAM loss
- failed HMI rebuild after compile
- HTTP request timing issue

After fixing the issue, turn the detailed flags back off so normal logs remain readable.
