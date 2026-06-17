# v6.45.41 - LVGL Runtime Rollback Stable

- Restored the proven pre-refactor LVGL runtime implementation from v6.45.35.
- Backs out the experimental split-runtime refactor that caused LCD tearing/glitching and crashes during page changes.
- Keeps current stable UI/MQTT/OnPanelTick/project workflow features up through v6.45.35.

# Changelog

## v6.45.34 - Neutral Designer Alignment Guides

- Changed Designer canvas smart/alignment helper lines from bright pink to a neutral slate color.
- Updated helper label background to a neutral slate tone for a more professional design-tool appearance.


## v6.45.33 - Widget Font and ReadOut Color Alignment

- Updated browser designer widget preview typography to better match the compact LCD/LVGL runtime text style.
- Reduced oversized web preview value/caption fonts so web widgets look closer to the physical LCD widgets.
- Fixed LVGL ReadOut value labels so they honor the widget color selected in the web designer.


## v6.45.32 - Tags Summary Compact

- Compacted the Tags page summary counters so Total/Bool/Numeric/String/Writable/System fit on one row.
- Preserved system-tag delete guard behavior.


## v6.45.31 - LVGL widget build fix

- Fixed firmware build error from missing `PILAB_PANEL_BUILD_NOTE` definition.
- Preserved v6.45.30 LVGL setpoint/toggle visual fixes.

## v6.45.30 - LVGL Widget Visual Fixes

- Fixed LCD setpoint widgets so the LVGL spinbox cursor/focus rectangle is hidden; the blue box should no longer cover the last digit.
- Fixed LCD toggle widgets so the ON color is applied only to the native switch indicator/knob, not the surrounding card.

## v6.45.29 - Tag Picker Escape Repeat Fix

- Fixed repeated Escape/cancel behavior in the shared persistent tag picker.
- MQTT publish/subscribe tag fields and Designer widget Tag Binding now start an edit session before text modification, so Escape reliably restores the value that was present at the beginning of that edit.
- Escape and the picker close button continue to cancel/restore; selecting a tag commits the selected value.


## v6.45.28 - Tag Picker Escape Restore

- Fixed MQTT publish/subscribe tag picker cancel behavior so Escape restores the original tag value.
- Fixed Designer widget tag binding picker so Escape restores the original binding.
- Picker close X now cancels/restores consistently.

# PiLab Panel Changelog

## v6.45.26 - MQTT Tag Picker Stable

- Replaced MQTT publish/subscribe native datalist tag selectors with a custom persistent tag picker.
- The tag picker stays open while filtering/typing and only closes when a tag is selected, the close button is pressed, Escape is used, or another picker is opened.
- Keeps current MQTT behavior and system-tag delete guard fixes from v6.45.25.


## v6.45.25 - System Tag Build Fix

- Fixed ESP-IDF 6.0.1 C build failure in `hmi_tag_db.c` by adding the required forward declaration for `find_or_create_locked()` before `ensure_system_tags_locked()`.
- Keeps the v6.45.24 system tag delete guard behavior unchanged.


## v6.45.24 - System Tag Delete Guard

- Added system-tag metadata for runtime-generated tags.
- Tags page now displays a System badge instead of Remove for system tags.
- Exact tag replacement keeps system tags read-only and present.


## v6.45.23 - Tag Replace/Delete Fix

- Changed Tag RAM/Flash upload to exact replacement instead of merge/upsert.
- Deleted tags now stay deleted after Load to RAM or Save to Flash.
- Kept the PSRAM tag table allocation stable while clearing/rebuilding contents in place.


## v6.45.22 - Tag Delete Dialog Removed

- Removed the confirmation dialog from the Tags page delete button.
- Tag deletion now immediately removes the row from the browser-side tag table and marks the tag list dirty.
- Preserved unified project operations, Device page polish, MQTT bridge, and OnPanelTick behavior.

# Changelog

## v6.45.21 - Device Page Header Removed

- Removed the Device page intro/header block to reclaim vertical space.
- Kept the compact diagnostics dashboard and moved Refresh to the footer controls.
- Build marker: `v6.45.21-s3-device-page-header-removed`

## v6.45.20 - Device Page Polish

- Redesigned the Device page into a compact professional diagnostics dashboard.
- Added live status tiles for runtime, WiFi STA, MQTT, and AngelScript.
- Added compact memory bars for internal RAM and PSRAM.
- Added runtime, network, MQTT, and OnPanelTick/script diagnostics using existing APIs.
- Kept raw JSON available behind a compact details section.


## v6.45.19 - Script OnPanelStart Button Removed

- Removed the redundant **Run OnPanelStart** button from the Script page toolbar.
- Manual no-argument function calls remain available through the existing function dropdown and Call button.


## v6.45.17 - UI Text Cleanup

- Removed stale guidance text from the Script page.
- Avoid duplicate `ScreenShow(...)` examples in the Designer screen inspector when screen name and ID match.
- Regenerated embedded web assets.


## v6.45.16 - Unified MQTT Start After Load to RAM

- Fixed unified Load to RAM so applying MQTT config starts MQTT when the active broker has Auto-connect enabled.
- Updated MQTT page Save behavior to match: save config, then start MQTT when Auto-connect is selected.
- Preserves successful config application even if MQTT start fails; the UI reports the start failure separately.

## v6.45.15 - Unified Load to RAM Sequencing Fix

- Fixed a crash when using the unified **Load to RAM** dialog with multiple sections selected.
- The web app now waits for async AngelScript compilation to finish before applying the Screen layout.
- The web app now waits for the HMI runtime to return to READY after compile/layout operations.
- Added `hmi_runtime_state` to `/api/device/status` for safer project operation sequencing.


## v6.45.14 - Unified Project Buttons

- Added top-level Import Project, Export Project, Load to RAM, and Save to Flash project actions.
- Added checkbox dialogs for Load to RAM and Save to Flash covering Screen, Script, Tags, and MQTT config.
- Removed redundant page-level persistence buttons from Designer and Tags, and simplified the Script toolbar.
- Added shared MQTT config state so project-level operations can apply the currently edited MQTT config.

## v6.45.13 - Project export/import includes MQTT config

- Added `mqtt_config.json` to PiLab Panel project export packages when the MQTT config endpoint is available.
- Updated the project manifest with MQTT summary information: enabled state, active broker, broker count, publish row count, and subscribe row count.
- Updated project import to restore `/spiffs/mqtt_config.json` through the existing MQTT config API.
- Import remains backward compatible with older project zips that do not contain MQTT configuration.
- Added a web-app refresh signal so the MQTT page reloads its config/status after a project import restores MQTT settings.
- Kept v6.45.12 OnPanelTick missing-function safety behavior.


## v6.45.12 - OnPanelTick missing-function safety

- Fixed ESP-IDF 6.0.1 C++ build failure caused by incrementing volatile-qualified OnPanelTick counters.
- Added safe missing-function handling for enabled `OnPanelTick()` when `void OnPanelTick()` is not present in the compiled script.
- Added tick posted/executed/skipped/missing/queue-full counters to script runtime status.
- Added Designer warning when OnPanelTick is enabled but missing from the compiled AngelScript runtime.
- Kept app_logic as the 100 ms tick producer and hmi_script_evt as the AngelScript executor.


## v6.43-s3-mdns-hostname

- Added mDNS startup for the panel HTTP service.
- Added configurable device hostname in the WiFi cog dialog.
- Saved hostname to NVS and restored it on reboot.
- Added `/api/wifi/hostname` and extended `/api/wifi/status` with `hostname` and `mdns_url`.
- Kept AP fallback behavior so the device remains reachable at `http://192.168.4.1`.


## v6.42-s3-wifi-client-config

- Added top-right WiFi configuration cog in the web UI.
- Added WiFi scan/select/password dialog.
- Added NVS-saved STA credentials for joining a local WiFi network.
- Added Delete Saved WiFi / AP Mode action to clear NVS credentials and return to AP/recovery behavior.
- Added firmware endpoints: `/api/wifi/status`, `/api/wifi/scan`, `/api/wifi/connect`, `/api/wifi/forget`.
- Kept the built-in `PiLab-Panel` AP active as a fallback while STA mode connects.

## v6.41-s3-setpoint-no-surprise-rounding

- Setpoint keypad values now apply the number the user typed after min/max clamping.
- The `step` property is now treated as an increment/display hint for setpoint controls, not automatic keypad rounding.
- Added optional `snapToStep: true` for layouts that intentionally want typed values rounded to the nearest step.
- Factory demo Speed SP step changed from 250 to 1 so the demo never appears to invent a different speed.
- Setpoint commit log now shows entered value, applied value, snap flag, and step.

## v6.40-s3-setpoint-commit-fix

- Fixed setpoint numeric keypad commit behavior.
- READY/CANCEL is now handled from both the LVGL keyboard and textarea, matching LVGL event forwarding differences.
- Committed setpoint values now force-refresh the runtime widget immediately and log the tag/value committed.

## v6.39-s3-toggle-event-fix

- Unified toggle switch touch behavior with normal button/card behavior.
- The whole toggle card is now clickable, not only the inner LVGL switch.
- Native LVGL switch `VALUE_CHANGED` events are still supported.
- Prevented double-dispatch when tapping directly on the inner switch.
- Toggle tag values are now updated before script callbacks.
- Toggle `onChange` is the primary event, with `onClick` retained as a fallback for older layouts.

# Changelog

## v6.45.21 - Device Page Header Removed

- Removed the Device page intro/header block to reclaim vertical space.
- Kept the compact diagnostics dashboard and moved Refresh to the footer controls.
- Build marker: `v6.45.21-s3-device-page-header-removed`

## v6.38-s3-factory-demo-fix

- Promoted the S3 build to a GitHub-ready factory demo baseline.
- Added a fresh-flash multi-screen HMI demo: Main, Process, and Diagnostics.
- Added embedded AngelScript event handlers for buttons, toggle change, setpoint change, and screen navigation.
- Added factory demo tags for process values, status text, heartbeat, and setpoints.
- Added `examples/factory_demo/` with importable layout, script, and tag files.
- Added `docs/FACTORY_DEMO_QUICK_START.md`.

## v6.36 - Board backend file split

- Cleaned up the Waveshare ESP32-S3 7-inch backend file layout.
- Split board-specific code into obvious LCD, GT911 touch, backlight, config, state, and board bring-up files.
- Kept the shared `pilab_board_*` API from v6.35.
- Updated board backend documentation.
- No intentional changes to HMI runtime behavior, tags, scripts, widgets, maintenance mode, or backlight policy.


## v6.34 - Tag Registry PSRAM Storage

- Moved the fixed tag registry table out of static internal `.bss` storage.
- Allocates the tag registry from PSRAM at boot using `heap_caps_calloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
- Keeps an internal-RAM fallback with a warning if PSRAM allocation fails, so the firmware can still boot on non-PSRAM builds.
- Moved the tag-registry flash-load JSON buffer to PSRAM-preferred heap allocation as well.
- Intended to recover roughly 49 KB of internal RAM previously consumed by `hmi_tag_db.c.obj` in `idf.py size-files`.
- No intentional changes to tag page behavior, LVGL rendering, Script editor behavior, compile worker, maintenance mode, or backlight timing.

## v6.33 - Tag Initial Value + Save Status Cleanup

- Restored the editable **Initial Value** field on the Tags page.
- Added a read-only **Value** column for current runtime values from the LCD.
- Removed the confusing `liveValue` model; current runtime value is `value`.
- `GET /api/tags` now exposes both `value` and `initialValue`.
- Registry saves update tag definitions and initial values without overwriting current values for existing runtime tags.
- New tags start with their configured initial value.
- Added Tags page dirty/synchronized state and visible save success feedback.
- Save To RAM and Save To Flash responses now include tag generation/count metadata.


## v6.32 - Tag Registry Update-Only Runtime Safety

- Changed tag registry firmware updates to upsert/update-only behavior.
- Existing runtime tags are no longer destroyed when `/api/tags` receives a partial browser registry.
- Removed browser/runtime confusion between `value`, `initialValue`, and `liveValue`; the API now uses `value` as the current tag value.
- Paused Designer background tag polling while Tags page edits are dirty, preventing new browser-created tags from being overwritten by `/api/tags` refreshes.
- Save To RAM and Save To Flash now update existing LCD tags in place and add new tags without deleting omitted tags.
- Added `README_V6_32_TAG_REGISTRY_UPDATE_ONLY.md`.


## v6.31 - Tag Registry RAM/Flash Save Split

- Renamed the Tags page `Value` column to `Initial Value`.
- The Tags page now treats Initial Value as configuration data and does not auto-update it from live runtime tag changes.
- Removed the per-row `Write` button from the tag registry editor.
- Changed `Save To LCD` to `Save To RAM` for runtime-only tag registry application.
- Added explicit `Save To Flash` for persistent tag registry storage in `/spiffs/hmi_tags.json`.
- Firmware now keeps initial/configured tag values separate from live runtime tag values.
- `GET /api/tags` returns `value` / `initialValue` for configuration plus `liveValue` for runtime monitoring.
- Adjusted Tags page table sizing: smaller Units/Min/Max fields and larger Description field.
- Rebuilt and embedded updated web assets.

## v6.30 - Tag Registry LCD Save/Load

- Reworked the Tags page into an editable tag registry.
- Added add/delete/edit support for bool, int, float, and string tags.
- Added Load From LCD and Save To LCD workflow for tag definitions.
- Added firmware POST `/api/tags` to replace and persist the tag registry.
- Added SPIFFS persistence for `/spiffs/hmi_tags.json`.
- Kept `/api/tags/write` for live runtime value writes.
- Rebuilt and embedded updated web assets.


## v6.29 - Script editor mouse-wheel correction

- Fixed remaining mouse-wheel scrolling issue in the bounded Prism Script editor.
- Let Prism's internal wrapper use natural code height instead of forcing `height: 100%`.
- Added robust wheel capture and delta normalization for pixel, line, and page wheel events.
- Kept Designer event jump-to-function behavior from v6.25-v6.28.
- Rebuilt and embedded the web app assets.


## v6.28 - Script Editor Mouse Wheel Fix

- Fixed mouse-wheel scrolling inside the bounded Prism Script editor.
- Captures wheel events from the editor host, wrapper, overlay, and textarea, then applies them to the real Prism editor scroll container.
- Keeps the transparent textarea scroll position synchronized with the visible highlighted Prism layer.
- Preserves v6.27 handler jump/focus behavior and all LCD runtime behavior.


## v6.27 - Script editor scroll/focus fix

- Fixed mouse-wheel scrolling inside the bounded Script page Prism editor.
- Improved Designer event jump-to-handler so the target line is focused and revealed in the editor viewport.
- Added wheel forwarding from the transparent textarea layer to the Prism scroll container.
- Kept v6.26 bounded editor layout and previous runtime behavior unchanged.




## v6.25 - Designer event jump to Script

- Added Edit buttons beside Designer `onClick` and `onChange` event fields.
- Event Edit jumps to the matching AngelScript function on the Script page.
- Missing handlers can be created as safe no-argument stub functions.
- Empty event fields are auto-filled with a widget-based handler name before jumping.
- Preserved the v6.24b Script page regression fix and Call Function dropdown.

## v6.24b - Script page regression fix

- Fixed a Script page regression introduced in v6.24 where the Prism editor lifecycle helpers were accidentally removed.
- Restored script loading from LCD/runtime into the editor.
- Kept the Call Function dropdown and removed Common Snippets sidebar behavior from v6.24.
- Rebuilt embedded web assets.

## v6.24 - Script Function Dropdown

- Replaced the Script page manual function-call textbox with a dropdown populated from the current AngelScript editor text.
- Shows no-argument functions because the manual call endpoint invokes functions as `Name()`.
- Removed the visible Common Snippets section from the Script page sidebar while keeping Prism editor autocomplete enabled.
- Rebuilt and embedded the updated web app assets.

## v6.23 - LCD Runtime Sync and Setpoint Event Cleanup

- Added LCD-runtime layout loading so the web designer can pull the currently active/applied LCD JSON instead of only the flash-saved layout.
- Added LCD-runtime script loading so the Script page can pull the last successfully compiled RAM script when it differs from the flash-saved script.
- Updated project export loading to prefer current LCD runtime layout/script when editor RAM is empty.
- Renamed the web buttons from `Load from Panel` to `Load from LCD` to make the source clearer.
- Added an explicit `onChange` field in the designer inspector for widgets that need value-change events.
- Setpoint `onClick` now fires when the keypad opens; Setpoint `onChange` fires when OK commits a new numeric value.
- Kept native LVGL toggle switch, Setpoint keypad visibility polish, and tank/gauge spacing polish.

## v6.22 - Setpoint Keypad Visibility Polish

- Improved the LCD Setpoint numeric-entry popup so the active text cursor is white and easier to see.
- Added a clearer popup title using the Setpoint widget label, shown as `Edit <label>`.
- Added a small tag subtitle in the popup so the operator can tell which value is being edited.
- Kept the LVGL Text Area plus built-in LVGL numeric Keyboard implementation from v6.21.
- No intentional changes were made to AngelScript compile, maintenance mode, backlight timing, page loading, toggle behavior, or script runtime lifetime.

## v6.21 - Setpoint Numeric Keypad

- Added a runtime numeric-entry popup for Setpoint widgets on the LCD.
- Tapping a Setpoint now opens an LVGL Text Area with the built-in LVGL numeric Keyboard attached.
- Pressing OK commits the edited value to the bound tag and calls the Setpoint change handler when present.
- Pressing Cancel or tapping the dimmed background closes the popup without changing the tag.
- Preserved the v6.18 native LVGL toggle switch and the v6.20 tank/gauge visual spacing changes.
- No intentional changes were made to AngelScript compile, maintenance mode, backlight timing, page loading, or script runtime lifetime.

## v6.18 - Native LVGL Toggle Switch

- Replaced the temporary custom LCD toggle drawing with the native LVGL `lv_switch`.
- Styled the native switch to better match the browser designer preview.
- Routed toggle updates from `LV_EVENT_VALUE_CHANGED` and used the LVGL checked state as the source of truth.
- Preserved the v6.x stability behavior around AngelScript compile, maintenance mode, backlight timing, and on-demand page loading.

## v6.17 - LCD Widget Visual Polish

- Reworked the LCD runtime toggle rendering to use a safe PiLab-owned switch track and knob.
- Preserved the non-native toggle implementation so the firmware still avoids LVGL `lv_switch` theme/application issues.
- Kept this as a visual cleanup pass only; no script/runtime/backlight/maintenance-mode feature changes were intended.


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

## v6.19 - Gauge and Tank Value Label Spacing

- Adjusted LCD runtime rendering for Gauge and Tank widgets.
- Moved the numeric value labels into a clearer lower value band.
- Reduced/raised the Gauge bar so the value text no longer overlaps the bar.
- Shortened/repositioned the Tank bar so the value text no longer collides with the tank fill area.
- Preserved v6.18 native LVGL switch behavior and the v6.14+ runtime lifecycle fixes.

## v6.20 - Tank Value Spacing Polish

- Kept the native LVGL toggle switch from v6.18.
- Adjusted the LCD tank widget vertical layout so the numeric value label has a larger reserved band below the tank bar.
- Shortened the tank fill bar and moved the value label slightly upward from the bottom border to avoid visual collision on the Waveshare 7-inch LCD.
- No intentional changes to AngelScript compile, maintenance mode, backlight timing, on-demand page loading, or runtime lifecycle behavior.

## v6.26 - Script editor handler jump focus polish

- Bounded the Script page Prism editor so it scrolls internally instead of growing the whole page.
- Updated Designer event handler jump logic to use the Prism editor selection/scroll APIs.
- Handler jump now focuses the editor and scrolls the target function line into view more reliably.
- Kept v6.25 Designer event Edit buttons, v6.24b Script page regression fix, native LVGL toggle, Setpoint keypad, and tank/gauge visual polish.
## v6.30b - Tag Registry Build Fix

- Fixed a GCC build failure in `hmi_tag_db.c` caused by embedded NUL characters being written into character literals while checking for string terminators.
- Replaced those literals with normal `'\\0'` checks.
- No intentional runtime behavior changes from v6.30.
## v6.35 - Multi-board layout preparation

- Added a shared PiLab board abstraction layer in `main/board/pilab_board.h` and `main/board/pilab_board.c`.
- Moved the Waveshare ESP32-S3 7-inch LCD/touch/backlight implementation into `main/board/waveshare_s3_7inch/`.
- Updated `app_main.c` and the HMI runtime to call generic board APIs instead of calling Waveshare-specific functions directly.
- Added CMake board selection with `PILAB_BOARD`, defaulting to `waveshare_s3_7inch`.
- Preserved the existing Waveshare S3 backend behavior, including RGB LCD setup, GT911 touch, CH422G reset/backlight control, PSRAM framebuffers, and deferred backlight sequencing.
- No intentional changes to web app behavior, tags, AngelScript, LVGL widget rendering, Setpoint keypad, or script editor behavior.
- v6.45.11: Added configurable AngelScript OnPanelTick() from Designer; app_logic queues the periodic callback safely without tick backlog.
