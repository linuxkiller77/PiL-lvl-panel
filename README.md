# PiLab Panel


PiLab Panel is an experimental open-source HMI panel runtime for the Waveshare ESP32-S3 7-inch touch LCD. It uses a browser-based editor to create HMI screen JSON and AngelScript event-handler code, then runs that layout natively on the ESP32-S3 with LVGL.

The important idea is that the web page is the editor, while the microcontroller is the runtime. The browser edits the screen layout, tags, and script. The ESP32-S3 stores the layout/script in flash, compiles the AngelScript event handlers, and renders the saved HMI as native LVGL controls on the LCD.

Development of this project was done in co-op with AI.

###  Web flasher for PiLab Panel

Only this specific Waveshare ESP32-S3 7" LCD board has been tested
https://www.amazon.ca/dp/B0D2NMCY7K

https://openpilab.github.io/PiLab-Panel/webflasher/

## Current baseline

- clean boot on the Waveshare ESP32-S3 7-inch panel
- AngelScript compiles before the HMI becomes visible
- manual Compile-to-RAM uses maintenance mode
- LCD backlight stays off until HMI reload settles
- on-demand page loading is used to keep LVGL object pressure down
- tags are prefilled immediately when widgets are created
- persistent AngelScript compile worker is used
- normal monitor output is reduced while warnings, errors, compile summaries, HMI reload summaries, and health summaries remain visible

## What this is

PiLab Panel is closest to a small open HMI panel, but the workflow is more like a self-hosted web IDE running on the device:

1. The ESP32-S3 starts as a Wi-Fi access point.
2. A browser connects to the device.
3. The browser editor creates or edits HMI layout JSON.
4. The browser editor creates or edits AngelScript event handlers.
5. The firmware saves the layout/script to SPIFFS.
6. The firmware compiles the AngelScript into RAM.
7. LVGL renders the saved layout on the physical 800x480 LCD.
8. Touch events call script handlers, and script handlers read/write normalized PiLab-style tags.

The local tag database is the normalization layer. Future drivers such as PiLink, Modbus TCP, local I/O, or other device protocols should write into tags rather than directly coupling themselves to widgets.

## Hardware target

Primary target:

- Waveshare ESP32-S3 7-inch RGB LCD touch panel
- ESP32-S3 with PSRAM, commonly the N16R8-style configuration
- 800x480 RGB LCD
- GT911 touch controller
- CH422G I/O expander used by the Waveshare board

The firmware is not a generic ESP32-S3 display project. It contains board-specific display, touch, reset, and backlight handling for this Waveshare panel.

## Repository layout

```text
main/
  app/                 Runtime state and health task
  board/               Waveshare LCD, touch, CH422G board support
  hmi/                 Tags, layout store, LVGL runtime, AngelScript runtime
  panel/               Wi-Fi AP and SPIFFS storage setup
  web/                 Embedded web server and generated web assets

panel_web/
  src/                 Vue/Vite web editor source
  dist/                Built web editor output
  tools/               Web build helper scripts

tools/
  embed_web_assets.py  Converts built HTML into C source for firmware embedding

docs/
  Architecture, build, validation, diagnostics, and contributor notes
```

## Quick start

### Build firmware only

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### Connect to the panel

After flashing, connect a PC, tablet, or phone to:

```text
SSID:     PiLab-Panel
Password: pilabpanel
URL:      http://192.168.4.1
```

### Rebuild the web editor and firmware

```bash
cd panel_web
npm install
npm run build
npm run embed
cd ..
idf.py build
idf.py flash monitor
```

The zip intentionally does not include `node_modules`.

## Main documentation

Start with these files:

- [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)
- [`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- [`docs/RUNTIME_LIFECYCLE.md`](docs/RUNTIME_LIFECYCLE.md)
- [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md)
- [`docs/VALIDATION_CHECKLIST.md`](docs/VALIDATION_CHECKLIST.md)
- [`docs/CONTRIBUTING_NOTES.md`](docs/CONTRIBUTING_NOTES.md)
- [`CHANGELOG.md`](CHANGELOG.md)

## Safety and project status

This is an experimental open project. It is not a certified industrial control product. Do not use it as the only safety layer for machinery, heating equipment, motion systems, or other hazardous systems.

The current branch is best treated as a stable research baseline for the Waveshare ESP32-S3 7-inch HMI runtime. The next development phase should be done in small, testable branches so this baseline remains easy to return to.
