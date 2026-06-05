# Architecture

PiLab Panel separates editing from runtime execution.

The browser is the editor. The ESP32-class firmware is the runtime. The current default target is the Waveshare ESP32-S3 7-inch panel.

```text
Browser editor
  | edits
  v
HMI layout JSON + AngelScript source
  | save/compile API
  v
PiLab Panel firmware
  | stores in SPIFFS
  | compiles AngelScript
  | renders with LVGL
  | uses selected board backend
  v
LCD/touch target, currently Waveshare ESP32-S3 800x480
```

## Major firmware blocks

### Board support

```text
main/board/pilab_board.*
main/board/waveshare_s3_7inch/*
```

`pilab_board.*` is the shared board abstraction used by the runtime. Each board backend owns its LCD, touch, reset, backlight, display timing, and framebuffer setup.

The current default backend is `waveshare_s3_7inch`, which handles the Waveshare ESP32-S3 7-inch RGB LCD, GT911 touch, CH422G reset/backlight expander, and 800x480 display geometry.

### Storage and Wi-Fi

```text
main/panel/panel_storage.*
main/panel/panel_wifi.*
```

`panel_storage` mounts SPIFFS for saved layout/script/project data.

`panel_wifi` starts the panel in Wi-Fi access-point mode.

### Web server

```text
main/web/panel_web_server.*
main/web/panel_web_assets.*
```

The web server provides:

- the embedded browser editor
- layout APIs
- tag APIs
- script APIs
- device/status APIs

`panel_web_assets.c` is generated from the built web editor.

### HMI runtime

```text
main/hmi/hmi_layout_store.*
main/hmi/hmi_lvgl_runtime.*
main/hmi/hmi_tag_db.*
main/hmi/hmi_script_engine.*
main/hmi/hmi_memory_diag.*
main/hmi/hmi_diag_log.h
```

Responsibilities:

- load/save layout and script data
- parse HMI layout JSON
- build active LVGL page trees
- handle page changes
- manage normalized tags
- compile and run AngelScript event handlers
- report memory/runtime health

### App state and tasks

```text
main/app/app_state.*
main/app/app_tasks.*
main/app_main.c
```

Coordinates high-level runtime state, boot sequencing, and periodic health summaries.

## Runtime state model

The runtime has named states so the monitor output and internal sequencing are easier to reason about.

Important states include:

```text
BOOTING
READY
SCRIPT_COMPILING
LAYOUT_RELOADING
```

A healthy idle state should be `READY`.

Compile-to-RAM temporarily enters `SCRIPT_COMPILING`, then `LAYOUT_RELOADING`, then returns to `READY`.

## Why on-demand page loading is used

Earlier versions tried to keep all HMI widgets alive and only hide inactive pages. That is not ideal on the ESP32-S3 because LVGL still owns and manages those objects.

The current approach keeps only the active page tree alive. Inactive pages remain in the layout model but are not active LVGL object trees. This reduces memory pressure and greatly reduces the chance of reload/render timing problems.

## Why maintenance mode exists

AngelScript compilation, LVGL object deletion/rebuild, PSRAM use, and RGB LCD refresh can overlap in bad ways on the ESP32-S3.

Maintenance mode deliberately makes Compile-to-RAM boring:

- turn off the backlight
- drop active HMI page tree
- pause LVGL timer/rendering around the sensitive compile path
- compile AngelScript
- rebuild HMI layout
- turn the backlight on only after reload settles

This prevents the user from seeing half-built screens and reduces timing-sensitive display glitches.

## AngelScript model

AngelScript is event-handler based.

It is not currently a continuous PLC scan engine in this panel firmware.

Main script entry patterns:

```cpp
void OnPanelStart()
void SomeButton_Click()
void SomeToggle_Changed()
```

The script runtime is intended to keep one engine and one active module:

```text
engines=1
modules=1
```

A successful Compile-to-RAM discards the old module only after the new module is ready.

A failed Compile-to-RAM keeps the previous valid module active.

## Tag normalization layer

Widgets should not be tightly coupled to a specific protocol or I/O source.

Instead:

```text
Driver / script / future protocol
  -> writes tags
  -> HMI widgets read tags
```

This is the intended path for future PiLink, Modbus TCP, local I/O, or other integrations.
