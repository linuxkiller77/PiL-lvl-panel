# User Guide

This guide explains how to use PiLab Panel after the firmware is flashed to the Waveshare ESP32-S3 7-inch panel.

## Connect

1. Power the panel from a suitable USB supply.
2. Wait for the HMI screen to appear.
3. Connect your computer, tablet, or phone to the panel Wi-Fi access point.

```text
SSID:     PiLab-Panel
Password: pilabpanel
URL:      http://192.168.4.1
```

Open the URL in a browser. The browser page is the editor and management interface for the panel.

## Basic workflow

The normal workflow is:

1. Edit HMI screens in the browser.
2. Edit tags if needed.
3. Edit AngelScript event handlers if needed.
4. Save the layout/script to the panel.
5. Use Compile-to-RAM to test script changes without making them permanent.
6. Reboot or reload to confirm the saved project starts cleanly.

The physical LCD runs the saved HMI layout using native LVGL widgets. The browser is not the LCD runtime. The browser is the editor.

## HMI screens

The HMI layout is stored as JSON. It describes pages, widgets, geometry, labels, colors, and tag bindings.

The current runtime uses on-demand page loading. This means the active page is rendered on the LCD, while inactive page trees are not kept active inside LVGL. This is intentional and helps the ESP32-S3 stay stable with larger layouts.

## Tags

Tags are the shared data model between the HMI, scripts, and future device drivers.

Examples of tag uses:

- a readout displays a numeric tag
- a toggle writes a boolean tag
- a button calls a script handler that writes one or more tags
- a future PiLink or Modbus driver writes process values into tags

This normalized tag model is important because it keeps the widgets independent from the eventual data source.

## AngelScript event handlers

AngelScript is used for event handlers, not for a continuous PLC-style scan.

Typical examples:

```cpp
void OnPanelStart()
{
    TagWriteBool("Motor_Enable", false);
    TagWriteFloat("Motor_Speed_SP", 1000.0);
}

void StartButton_Click()
{
    TagWriteBool("Motor_Enable", true);
}

void StopButton_Click()
{
    TagWriteBool("Motor_Enable", false);
}
```

`OnPanelStart()` runs at startup and after successful Compile-to-RAM. Widget events call named functions such as `StartButton_Click()`.

## Compile-to-RAM

Compile-to-RAM tests script changes without requiring a full reboot. In the v6.15/v6.16 baseline, Compile-to-RAM uses maintenance mode:

1. the runtime enters script-compiling state
2. the LCD backlight turns off
3. the active LVGL page tree is dropped
4. LVGL timer/rendering is paused during the sensitive compile window
5. AngelScript compiles into RAM
6. LVGL rendering resumes
7. the HMI layout is rebuilt
8. the LCD backlight turns back on only after the HMI reload settles

This sequence is deliberate. It avoids the unstable case where a large active LVGL object tree competes with script compilation and layout reload on a memory-constrained ESP32-S3.

## Failed script compile behavior

A failed compile should not destroy the previously valid runtime module. The old valid module should remain active, and the HMI should return after the deferred rebuild.

A compile error should appear as an error/warning in the monitor and in the web UI. After fixing the script, Compile-to-RAM again.

## What to watch in the serial monitor

The most important health values are:

```text
state=READY
widgets=<active page widget count>
script={state=ok valid=1 gen=<n> modules=1 engines=1 ...}
```

During Compile-to-RAM, it is normal to briefly see:

```text
state=SCRIPT_COMPILING
widgets=0
```

After the rebuild settles, it should return to:

```text
state=READY
modules=1
engines=1
```
