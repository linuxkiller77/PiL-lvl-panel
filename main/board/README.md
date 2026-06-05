# PiLab Panel board backends

The shared PiLab Panel runtime should not contain LCD, touch, backlight, or board pin details. Those details live behind the `pilab_board_*` API in this folder.

Shared code calls:

```c
pilab_board_display_init(&display, &touch);
pilab_board_set_backlight(true);
pilab_board_get_info(&info);
```

The selected board backend owns:

- LCD bus and panel initialization
- touchscreen controller initialization
- LVGL display/input registration
- backlight control
- reset GPIOs / IO expanders
- display size, rotation, and touch mapping
- frame-buffer and PSRAM strategy

## Current backend

`waveshare_s3_7inch/` is the current ESP32-S3 Waveshare 7-inch backend. It is intentionally split into small hardware-specific files:

| File | Purpose |
| --- | --- |
| `waveshare_s3_7inch_config.h` | Board pin map, resolution, LCD timing constants, GT911 pins, CH422G pins |
| `waveshare_s3_7inch_board.c` | High-level board bring-up sequence |
| `waveshare_s3_7inch_lcd.c` | RGB LCD panel setup and LVGL display registration |
| `waveshare_s3_7inch_touch_gt911.c` | GT911 reset, touch init, and LVGL input registration |
| `waveshare_s3_7inch_backlight.c` | CH422G init, LCD reset, and backlight control |
| `waveshare_s3_7inch_state.c` | Shared handles used by the backend implementation |
| `ch422g.c` | Small driver for the Waveshare board IO expander |

## Adding a new board

A new board should add a sibling folder, for example:

```text
main/board/esp32p4_my_lcd/
```

The new backend should expose a small board-level API similar to `waveshare_s3_7inch_board.h`, then wire it into:

- `main/board/pilab_board.c`
- `main/CMakeLists.txt`

Do not fork the HMI runtime for each board. Keep layout JSON, tags, AngelScript, web server, and LVGL widget rendering shared.
