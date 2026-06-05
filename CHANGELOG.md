# Changelog

## v6.16 — GitHub documentation baseline

Documentation-focused baseline built from v6.15.

Added:

- updated top-level `README.md`
- `docs/USER_GUIDE.md`
- `docs/BUILD_AND_FLASH.md`
- `docs/ARCHITECTURE.md`
- `docs/RUNTIME_LIFECYCLE.md`
- `docs/DIAGNOSTICS.md`
- `docs/VALIDATION_CHECKLIST.md`
- `docs/CONTRIBUTING_NOTES.md`
- `docs/API_OVERVIEW.md`
- `docs/RELEASE_BASELINE.md`

Intent:

- make the project understandable for someone finding it on GitHub
- document the working v6.15 runtime behavior
- document the fragile timing rules around AngelScript compile, LVGL reload, and LCD backlight timing
- avoid adding new runtime features

## v6.15 — Clean operational baseline

Cleanup baseline built on v6.14.

Preserved:

- clean boot
- pre-HMI AngelScript compile
- Compile-to-RAM maintenance mode
- deferred LCD backlight until HMI reload settles
- on-demand page loading
- immediate tag prefill
- persistent AngelScript compile worker
- intended script lifetime of `modules=1 engines=1`

Changed:

- reduced repetitive diagnostic log spam
- kept warnings, errors, compile summaries, HMI reload summaries, and health summaries
- moved deep diagnostics behind flags in `main/hmi/hmi_diag_log.h`

## v6.14 — Deferred backlight until HMI ready

Runtime stability baseline that kept the LCD backlight off until HMI reload settled after compile/reload operations.

## Earlier v6 milestones

Earlier v6 builds introduced and refined:

- on-demand pages
- offscreen page swap experiments
- immediate tag prefill
- script compile display guard
- LVGL pause around compile
- LCD blank maintenance mode
- boot pre-HMI script compile
- runtime state cleanup
- diagnostic and memory phase logging
- script lifetime diagnostics
- Compile-to-RAM stress diagnostics
- persistent script compile worker
