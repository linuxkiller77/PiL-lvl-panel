# PiLab Panel ESP32-S3 Web Flasher

This folder is a GitHub Pages-ready browser flasher for the PiLab Panel ESP32-S3 / Waveshare 7-inch LCD target.

It uses ESP Web Tools, which depends on the browser Web Serial API. Use Chrome, Edge, or Brave on desktop.

## Folder layout

```text
webflasher/
  index.html
  manifest.json
  firmware/
    .gitkeep
    bootloader.bin           generated/copied by tools/copyFirmware.ps1
    partition-table.bin      generated/copied by tools/copyFirmware.ps1
    PiLab_Panel.bin          generated/copied by tools/copyFirmware.ps1
```


## Verified ESP-IDF flash layout

The current `idf.py flash monitor` output for the PiLab Panel S3 build shows this command:

```powershell
esptool --chip esp32s3 -p COM13 -b 460800 --before=default-reset --after=hard-reset write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 PiLab_Panel.bin
```

So the web flasher manifest uses these same image parts:

| File | Offset hex | Offset decimal |
|---|---:|---:|
| `firmware/bootloader.bin` | `0x0` | `0` |
| `firmware/partition-table.bin` | `0x8000` | `32768` |
| `firmware/PiLab_Panel.bin` | `0x10000` | `65536` |

There is no separate `ota_data_initial.bin` in the current Panel flash command.

## Update the firmware files

From the repository root, build the normal project first:

```powershell
cd panel_web
npm run build
npm run embed
cd ..
idf.py build
```

Then copy the ESP-IDF build artifacts into the web flasher folder:

```powershell
.\tools\copyFirmware.ps1
```

The script copies firmware files into:

```text
webflasher/firmware/
```

and regenerates:

```text
webflasher/manifest.json
```

When `build/flasher_args.json` exists, the script uses ESP-IDF's own flash metadata so the manifest offsets match the real build output. If that file is not available, it falls back to the standard PiLab Panel ESP32-S3 layout:

| File | Offset hex | Offset decimal |
|---|---:|---:|
| `firmware/bootloader.bin` | `0x0` | `0` |
| `firmware/partition-table.bin` | `0x8000` | `32768` |
| `firmware/PiLab_Panel.bin` | `0x10000` | `65536` |

## First boot / setup access point

After a full erase, saved WiFi credentials are removed from NVS. On first boot, connect to the setup access point:

```text
SSID: PiLab-Panel
Password: pilabpanel
URL: http://192.168.50.1
``

Then use the web UI to enter your local WiFi credentials. Once the panel has joined your network, mDNS is available at `http://pilab-panel.local` when your computer/network supports it.

## Local test

Web Serial requires a secure context. `localhost` is allowed:

```powershell
cd webflasher
python -m http.server 8000
```

Then open:

```text
http://localhost:8000
```

## GitHub Pages

A simple publishing option is to keep this folder in your repo and configure GitHub Pages to serve from a branch/folder that includes `webflasher/`.

If GitHub Pages serves the repository root, the flasher URL will look like:

```text
https://openpilab.github.io/PiLab-Panel/webflasher/
```

## Notes

- The current Panel target is ESP32-S3 / Waveshare 7-inch LCD.
- Flash mode from the verified build output is `dio`.
- Flash frequency from the verified build output is `80m`.
- Flash size from the verified build output is `16MB`.
- The current stable baseline is `v6.45.41-s3-lvgl-runtime-rollback-stable`.
- Do not edit `manifest.json` offsets by hand unless you are intentionally overriding the ESP-IDF build metadata.
- If ESP Web Tools reports unsupported hardware, the manifest may still be correct but the ESP Web Tools package/browser support may need to be updated.

## Embedded factory demo images

The flasher page includes embedded base64 images showing the same PiLab Panel factory demo as a browser-designed HMI screen and as the real Waveshare ESP32-S3 LCD output. Because the images are embedded directly in `index.html`, there are no extra image files to publish with GitHub Pages.
