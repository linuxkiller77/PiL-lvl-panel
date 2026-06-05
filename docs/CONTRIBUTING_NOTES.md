# Contributing Notes

PiLab Panel is timing-sensitive because it combines Wi-Fi, HTTP, SPIFFS, PSRAM, LVGL, RGB LCD refresh, touch input, and AngelScript compilation on an ESP32-S3.

The project is usable because several fragile operations are now sequenced carefully. Keep changes small and test one behavior at a time.

## Preserve the v6.15/v6.16 baseline behavior

Do not casually change these behaviors:

- AngelScript compiles before the HMI becomes visible at boot.
- Compile-to-RAM uses maintenance mode.
- Active HMI page tree is dropped during script compile maintenance.
- LVGL rendering is paused/resumed around the sensitive compile path.
- LCD backlight stays off until HMI reload settles.
- On-demand page loading keeps inactive pages out of the active LVGL object tree.
- Script runtime should remain `modules=1 engines=1`.

## Preferred development style

Use small branches or small zip increments:

```text
v6.16 baseline docs
v6.17 one focused fix
v6.18 one focused feature
```

Avoid mixing feature work, UI redesign, logging changes, and runtime timing changes in the same patch.

## Good first improvements

Good follow-on tasks are documentation and tooling tasks that do not touch runtime behavior:

- improve README screenshots
- add known-good monitor excerpts
- add API reference documentation
- add sample HMI project JSON
- add sample AngelScript event handlers
- add release checklist

## Risky changes

Treat these as high-risk and validate carefully:

- LVGL page/tree lifecycle changes
- RGB panel/backlight timing changes
- task stack/memory allocation changes
- PSRAM/internal RAM allocation strategy changes
- AngelScript engine/module lifetime changes
- SPIFFS access from tasks with PSRAM stacks
- web server request handler changes that allocate large buffers
- changing the order of boot, compile, HMI load, and backlight enable

## Script/runtime rule

AngelScript should remain event-handler based unless a future branch intentionally adds a separate scan/runtime model.

Do not accidentally create a continuous script loop that competes with LVGL, Wi-Fi, or HTTP without first defining the scheduling model.

## Tag model rule

Keep tags as the normalized interface between widgets, scripts, and future drivers.

Future protocols should write tags:

```text
PiLink / Modbus / local I/O / test data
  -> tag database
  -> widgets and scripts
```

Do not make widgets depend directly on a protocol driver.

## Logging rule

Warnings, errors, compile summaries, HMI reload summaries, and periodic health summaries should remain visible.

Deep diagnostics should stay behind flags in `main/hmi/hmi_diag_log.h`.

## Hardware safety

This project is experimental and should not be used as the only safety mechanism for real machinery. Use external safety devices, interlocks, emergency stops, and proper industrial safety practices when connecting any controller or HMI to physical equipment.
