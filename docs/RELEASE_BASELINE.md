# Release Baseline Notes

This package is intended as the v6.16 GitHub documentation baseline for the PiLab Panel ESP32-S3 Waveshare 7-inch LVGL HMI project.

## Baseline intent

v6.16 does not intentionally add runtime features. It packages the working v6.15 behavior with documentation so another person can understand, build, flash, test, and cautiously extend the project.

## Why this baseline matters

The project became stable only after the runtime stopped trying to keep too much LVGL state active during script compile/reload operations.

The key architecture is:

- active page only, loaded on demand
- compile before visible HMI at boot
- maintenance mode for Compile-to-RAM
- backlight as visual commit after HMI reload settles
- persistent script compile worker
- script lifetime held to one engine and one active module

## Recommended next phase

Use this package as the rollback point before starting new feature work.

Good next phase options:

- GitHub cleanup and screenshots
- API documentation extraction
- sample projects
- PiLink planning
- Modbus TCP planning
- portability review for ESP32-P4 or other display targets

Avoid adding major features directly on top of this baseline without first tagging or archiving it.
