# PiLab Panel


PiLab Panel is an experimental open-source HMI panel runtime for the Waveshare ESP32-S3 7-inch touch LCD. It uses a browser-based editor to create HMI screen JSON and AngelScript event-handler code, then runs that layout natively on the ESP32-S3 with LVGL.

The important idea is that the web page is the editor, while the microcontroller is the runtime. The browser edits the screen layout, tags, and script. The ESP32-S3 stores the layout/script in flash, compiles the AngelScript event handlers, and renders the saved HMI as native LVGL controls on the LCD.

Development of this project was done in co-op with AI.

###  Web flasher for PiLab Panel

Only this specific Waveshare ESP32-S3 7" LCD board has been tested
https://www.amazon.ca/dp/B0D2NMCY7K

https://openpilab.github.io/PiLab-Panel/webflasher/

###  Single File Online demo of Web UI

Try out the Web UI to see how easy it is to build your own Screens
https://openpilab.github.io/PiLab-Panel/docs/pilab_panel_single_file_demo.html

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
- ESP32-S3 with PSRAM
- 800x480 RGB LCD
- GT911 touch controller
- CH422G I/O expander used by the Waveshare board

The firmware is not a generic ESP32-S3 display project. It contains board-specific display, touch, reset, and backlight handling for this Waveshare panel.

### Flash / PSRAM variants

The Waveshare ESP32-S3 7-inch LCD board appears to exist in at least two memory configurations:

- 16MB Flash / 8MB PSRAM
- 8MB Flash / 8MB PSRAM

Both variants use the same PiLab Panel source code, but they require different ESP-IDF flash-size settings and different partition tables.

If the wrong firmware is flashed, the install may appear to complete, but the board can reboot before Wi-Fi starts. A typical monitor message for flashing the 16MB firmware onto an 8MB board is:

```text
Detected size(8192k) smaller than the size in the binary image header(16384k)
```

If you see that message, build or flash the 8MB variant.

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
  embed_web_assets.py      Converts built HTML into C source for firmware embedding
  build_8mb.ps1           Builds the 8MB flash variant
  build_16mb.ps1          Builds the 16MB flash variant
  copyFirmware.ps1        Copies built firmware into the web flasher folder

webflasher/
  index.html              ESP Web Tools browser flasher page
  manifest_8mb.json       Web flasher manifest for 8MB flash boards
  manifest_16mb.json      Web flasher manifest for 16MB flash boards
  firmware/8mb/           8MB build output copied here for GitHub Pages
  firmware/16mb/          16MB build output copied here for GitHub Pages

docs/
  Architecture, build, validation, diagnostics, and contributor notes
```

## Quick start

### 1. Set the ESP-IDF target

Run this once after cloning or unpacking the project:

```bash
idf.py set-target esp32s3
```

### 2. Choose the correct flash variant

PiLab Panel now supports separate 8MB and 16MB flash builds.

Use the 16MB build for original Waveshare ESP32-S3N16R8-style boards:

```bash
idf.py -B build_16mb -DPILAB_FLASH_SIZE=16MB build
idf.py -B build_16mb flash monitor
```

Use the 8MB build for Waveshare boards that report 8MB flash:

```bash
idf.py -B build_8mb -DPILAB_FLASH_SIZE=8MB build
idf.py -B build_8mb flash monitor
```

The `-B` option keeps each variant in a separate build folder. This is important because ESP-IDF stores the selected sdkconfig in the build directory.

### 3. Connect to the panel

After flashing, connect a PC, tablet, or phone to:

```text
SSID:     PiLab-Panel
Password: pilabpanel
URL:      http://192.168.4.1
```

If the panel has been configured to join your Wi-Fi network, it may also be reachable by mDNS:

```text
http://pilab-panel.local
```

### Rebuild the web editor and firmware

The zip intentionally does not include `node_modules`. To rebuild the browser editor and embed it into the ESP32 firmware:

```bash
cd panel_web
npm install
npm run build
npm run embed
cd ..
```

Then build the firmware variant you need:

```bash
idf.py -B build_16mb -DPILAB_FLASH_SIZE=16MB build
```

or:

```bash
idf.py -B build_8mb -DPILAB_FLASH_SIZE=8MB build
```

## Flash variant files

The project uses these files for the two hardware variants:

```text
sdkconfig.defaults.common
sdkconfig.defaults.16mb
sdkconfig.defaults.8mb
partitions_16mb.csv
partitions_8mb.csv
```

`CMakeLists.txt` selects the correct sdkconfig defaults from the `PILAB_FLASH_SIZE` option.

The default is still 16MB if no option is provided.

## Updating the web flasher firmware files

After building a firmware variant, copy the generated ESP-IDF binaries into the `webflasher` folder.

For the 16MB variant:

```powershell
.\tools\copyFirmware.ps1 -Variant 16mb -BuildDir build_16mb
```

For the 8MB variant:

```powershell
.\tools\copyFirmware.ps1 -Variant 8mb -BuildDir build_8mb
```

The web flasher has two manifests:

```text
webflasher/manifest_16mb.json
webflasher/manifest_8mb.json
```

The GitHub Pages web flasher page provides separate install buttons for the 8MB and 16MB firmware builds.

## Main documentation

Start with these files:

- [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)
- [`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md)
- [`README_FLASH_VARIANTS.md`](README_FLASH_VARIANTS.md)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
- [`docs/RUNTIME_LIFECYCLE.md`](docs/RUNTIME_LIFECYCLE.md)
- [`docs/DIAGNOSTICS.md`](docs/DIAGNOSTICS.md)
- [`docs/VALIDATION_CHECKLIST.md`](docs/VALIDATION_CHECKLIST.md)
- [`docs/CONTRIBUTING_NOTES.md`](docs/CONTRIBUTING_NOTES.md)
- [`CHANGELOG.md`](CHANGELOG.md)

## Safety and project status

This is an experimental open project. It is not a certified industrial control product. Do not use it as the only safety layer for machinery, heating equipment, motion systems, or other hazardous systems.

The current branch is best treated as a stable research baseline for the Waveshare ESP32-S3 7-inch HMI runtime. The next development phase should be done in small, testable branches so this baseline remains easy to return to.
