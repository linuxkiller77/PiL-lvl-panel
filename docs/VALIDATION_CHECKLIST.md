# Validation Checklist

Use this checklist before calling a build a known-good baseline.

## Cold boot

- [ ] Flash firmware.
- [ ] Open serial monitor.
- [ ] Confirm boot reaches the app.
- [ ] Confirm PSRAM test passes.
- [ ] Confirm SPIFFS mounts.
- [ ] Confirm LCD/LVGL initializes.
- [ ] Confirm AngelScript compiles before HMI is visible.
- [ ] Confirm backlight turns on only after HMI reload settles.
- [ ] Confirm final runtime state is `READY`.

Expected script lifetime:

```text
modules=1 engines=1
```

## Browser connection

- [ ] Connect to Wi-Fi SSID `PiLab-Panel`.
- [ ] Open `http://192.168.4.1`.
- [ ] Confirm web editor loads.
- [ ] Confirm device/status page responds.
- [ ] Confirm tags page shows tags.
- [ ] Confirm HMI/designer page loads.

## HMI runtime

- [ ] Confirm physical LCD displays the active page.
- [ ] Change pages from the HMI.
- [ ] Confirm only active page widgets are shown in health summary.
- [ ] Confirm no visible offset or partial screen render.
- [ ] Confirm touch input works.

## Layout save/reload

- [ ] Move or edit a widget.
- [ ] Save layout to panel.
- [ ] Trigger reload or reboot.
- [ ] Confirm edited layout returns correctly.
- [ ] Confirm runtime returns to `READY`.

## Script Compile-to-RAM success path

- [ ] Open Script page.
- [ ] Compile the current working script to RAM.
- [ ] Confirm LCD backlight turns off during maintenance mode.
- [ ] Confirm HMI returns after reload.
- [ ] Confirm backlight turns on after reload settles.
- [ ] Confirm health returns to `READY`.
- [ ] Confirm `modules=1 engines=1`.

Repeat this several times.

## Script Compile-to-RAM failure path

- [ ] Intentionally introduce a small script error.
- [ ] Compile-to-RAM.
- [ ] Confirm compile error appears.
- [ ] Confirm firmware does not crash.
- [ ] Confirm HMI returns.
- [ ] Confirm old valid module remains active.
- [ ] Confirm `modules=1 engines=1`.
- [ ] Fix the script.
- [ ] Compile-to-RAM again.
- [ ] Confirm script state returns to `ok`.

## Stress sanity check

- [ ] Compile-to-RAM repeatedly.
- [ ] Try a larger script.
- [ ] Switch pages after compiles.
- [ ] Save/reload layout after compiles.
- [ ] Watch for internal RAM trend.
- [ ] Watch for module/engine growth.
- [ ] Watch for stuck `SCRIPT_COMPILING`.

## Pass/fail criteria

A build can be considered a good baseline if:

- no crash occurs
- no stuck black screen occurs
- no persistent screen offset occurs
- runtime always returns to `READY`
- failed compile recovers cleanly
- successful compile recovers cleanly
- `modules=1 engines=1` remains stable
- normal logs are readable without enabling deep diagnostics
