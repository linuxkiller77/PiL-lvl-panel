# Runtime Lifecycle

This document describes the important startup and Compile-to-RAM sequencing used by the v6.15/v6.16 baseline.

## Startup lifecycle

Expected startup sequence:

1. ESP-IDF bootloader starts the app.
2. PSRAM is detected and added to the heap.
3. SPIFFS storage is mounted.
4. LCD, touch, RGB panel, and LVGL port are initialized with the backlight off.
5. AngelScript is initialized and compiled before the HMI is visible.
6. Saved HMI layout is loaded from SPIFFS.
7. The active page is built by the LVGL runtime.
8. `OnPanelStart()` runs.
9. The backlight is enabled after the HMI is ready.
10. Wi-Fi AP and HTTP server start.
11. Periodic health summaries begin.

The key principle is that the user should not see the HMI until script and layout are ready.

## Compile-to-RAM lifecycle

Compile-to-RAM is the most timing-sensitive operation in the system. The intended sequence is:

1. Browser sends script compile request.
2. Runtime marks script state as queued.
3. Runtime enters `SCRIPT_COMPILING`.
4. LCD backlight turns off.
5. Active HMI page tree is dropped.
6. LVGL timer/rendering is paused.
7. Persistent compile worker performs AngelScript compilation.
8. LVGL timer/rendering resumes.
9. HMI layout rebuild is deferred until the runtime guard releases.
10. Active HMI page is rebuilt from the saved layout model.
11. `OnPanelStart()` runs after successful compile.
12. Runtime enters `LAYOUT_RELOADING`, then `READY`.
13. LCD backlight turns on after the reload has settled.

## Successful compile result

Expected result after a successful Compile-to-RAM:

```text
state=READY
script={state=ok valid=1 ... modules=1 engines=1 ...}
```

Generation count should increase. The active module name may change, but module count should remain 1.

## Failed compile result

Expected result after a failed Compile-to-RAM:

- compile error is reported
- runtime script state becomes `error`
- previous valid module remains usable
- active HMI layout is rebuilt
- runtime returns to `READY`
- module count remains 1
- engine count remains 1

After fixing the script, another Compile-to-RAM should return script state to `ok`.

## Backlight rule

The backlight should be considered a visual commit signal.

If the screen is being compiled/reloaded/rebuilt, the backlight should stay off. It should turn on only after the active HMI page is rebuilt and the reload has settled.

This rule avoids visible partial renders, offset screens, and half-built widgets.

## LVGL rule

Large LVGL object deletion and rebuild should happen in controlled runtime paths, not randomly from unrelated tasks.

The active page tree is intentionally dropped during maintenance mode. It is rebuilt after compile completion through the runtime reload path.

## Script lifetime rule

The intended steady-state script lifetime is:

```text
engines=1
modules=1
```

If a future change causes module count or engine count to grow after repeated Compile-to-RAM operations, treat it as a regression.
