# API Overview

The browser editor communicates with the firmware through HTTP endpoints. This document is a high-level orientation, not a complete formal API contract.

## Main endpoint groups

Common endpoint groups include:

```text
GET  /
GET  /api/device/status
GET  /api/tags
POST /api/tags/write
GET  /api/hmi/layout
POST /api/hmi/layout
POST /api/hmi/reload
GET  /api/script
POST /api/script
POST /api/script/compile_ram
```

Endpoint names may evolve. Check `main/web/panel_web_server.c` and the web API wrappers under `panel_web/src/api/` for the exact current implementation.

## Layout API

The HMI layout API stores and retrieves the JSON model used by both the browser editor and the LVGL runtime.

The layout model contains pages/widgets and their properties. The firmware parses this model and builds only the active LVGL page.

## Tag API

The tag API exposes the normalized tag database to the browser.

Typical usage:

- browser reads tag list
- browser writes a tag from the editor or test UI
- widgets bind to tag names
- scripts read/write tag values

## Script API

The script API stores AngelScript source and allows Compile-to-RAM testing.

Compile-to-RAM should not be treated as a simple background compile. It intentionally triggers maintenance mode in the runtime so the HMI and display recover cleanly.

## Status API

The status API is intended to expose device/runtime health to the browser. Important status concepts include:

- runtime state
- widget count
- tag count
- script state
- script generation
- module/engine count
- memory health

## Future API documentation

A future documentation pass should extract the exact request/response JSON from:

```text
main/web/panel_web_server.c
panel_web/src/api/*.js
```

Then this file can become a stable API reference.
