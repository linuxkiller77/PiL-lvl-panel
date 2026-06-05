# Factory Demo Quick Start

This release boots into a small factory demo on the Waveshare ESP32-S3 7-inch panel after a fresh flash.

## Hardware target

- Waveshare ESP32-S3 7-inch RGB LCD
- ESP-IDF 6.0.1
- 16 MB flash / 8 MB PSRAM configuration

## Build

From a clean ESP-IDF 6.0.1 shell:

```powershell
cd PiLab_Panel
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

If you changed the web app, rebuild and embed it first:

```powershell
cd panel_web
npm install
npm run build
npm run embed
cd ..
idf.py build
```

## First run

The panel starts a WiFi access point:

- SSID: `PiLab-Panel`
- Password: `pilabpanel`
- URL: `http://192.168.4.1`

The LCD should show the factory HMI with Main, Process, and Diagnostics screens.

## Demo controls

- **START / STOP** toggles the demo process tags.
- **STOP** forces the process stopped.
- **Auto** toggles the `Auto_Mode` tag and calls an AngelScript change event.
- **Speed SP**, **Level SP**, and **Temp SP** open the numeric keypad and call AngelScript onChange handlers.
- **PROCESS**, **DIAG**, and **MAIN** navigate screens using `ScreenShow()`.

## Existing boards with saved SPIFFS content

If your board already has saved layout/script/tag files in SPIFFS, those saved files override the embedded demo. Use the browser buttons to load defaults/save again, or erase flash for a true first-run test.


## v6.38 note

The embedded factory demo layout, script, and tag defaults are now aligned. On a board with no saved `/spiffs/hmi_layout.json`, the LCD should boot directly into the factory demo layout. If you previously saved a layout to flash, use the web app to load the factory demo from `examples/factory_demo/hmi_layout.json`, or erase flash for a true first-boot test.
