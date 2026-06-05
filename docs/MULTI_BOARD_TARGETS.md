# Multi-board Target Layout

PiLab Panel is being separated into two layers:

1. **Shared PiLab Panel runtime**
   - web server
   - embedded web assets
   - HMI layout JSON runtime
   - LVGL widget builder
   - AngelScript compile/runtime
   - tag registry
   - storage/API logic

2. **Board backend**
   - LCD panel driver setup
   - display bus/timing setup
   - LVGL display registration
   - touchscreen driver setup
   - touch coordinate transforms
   - backlight control
   - reset GPIOs / IO expanders
   - framebuffer and draw-buffer strategy

The shared runtime should not call a specific LCD driver directly. It should call the board API in:

```c
#include "board/pilab_board.h"
```

## Current backend

The default backend is:

```text
waveshare_s3_7inch
```

It lives in:

```text
main/board/waveshare_s3_7inch/
```

That backend currently owns:

- ESP32-S3 Waveshare 7-inch RGB LCD setup
- GT911 touch setup
- CH422G reset/backlight expander setup
- 800x480 geometry
- RGB timing
- PSRAM framebuffer strategy

## Board API

The shared runtime uses these generic calls:

```c
esp_err_t pilab_board_display_init(lv_display_t **out_display, lv_indev_t **out_touch);
esp_err_t pilab_board_set_backlight(bool on);
esp_err_t pilab_board_get_info(pilab_board_info_t *out_info);
```


## Recommended backend file pattern

Each board backend should keep the hardware pieces obvious. The current Waveshare S3 backend uses this pattern:

```text
main/board/waveshare_s3_7inch/
├─ waveshare_s3_7inch_config.h        # pins, resolution, LCD timing, touch/expander constants
├─ waveshare_s3_7inch_board.c         # high-level bring-up sequence
├─ waveshare_s3_7inch_lcd.c           # LCD panel + LVGL display registration
├─ waveshare_s3_7inch_touch_gt911.c   # GT911 reset/init + LVGL input registration
├─ waveshare_s3_7inch_backlight.c     # CH422G reset/backlight control
├─ waveshare_s3_7inch_state.c         # backend handle storage
└─ ch422g.c                           # board IO-expander helper
```

A future ESP32-P4 backend should follow the same idea, but the exact LCD/touch files should match that board's actual hardware. For example, an RGB P4 board and a MIPI-DSI P4 board may have different LCD setup files even though both use the same shared PiLab runtime.

## Adding a new board

To add a new ESP32-P4 or other LCD target:

1. Create a new folder under `main/board/`, for example:

```text
main/board/esp32p4_some_lcd/
```

2. Add that board's LCD/touch/backlight implementation files.

3. Add a branch in `main/board/pilab_board.c` using a new compile definition, for example:

```c
#if defined(PILAB_BOARD_ESP32P4_SOME_LCD)
#include "esp32p4_some_lcd/esp32p4_some_lcd.h"
#endif
```

4. Add the board sources and compile definition to `main/CMakeLists.txt`.

5. Build with a selected board name, for example:

```powershell
idf.py -D PILAB_BOARD=esp32p4_some_lcd build
```

## Important rule

Do not fork the full PiLab Panel runtime for each board. The project should stay as:

```text
same web app
same HMI JSON runtime
same tag system
same AngelScript system
different board backend
```

This keeps the ESP32-S3 and ESP32-P4 versions from drifting apart.
