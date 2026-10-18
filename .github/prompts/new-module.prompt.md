---
agent: agent
description: Step-by-step checklist for adding a new runtime module (src/ extraction). Guides research, file creation, integration, and verification.
---

# New Module Checklist

## Step 1 — Identify scope
- Single responsibility? Which functions move here? Dependencies in/out?

## Step 2 — Create `src/<module>.h` + `src/<module>.cpp`

**Header**: `#pragma once`, `#include "feature_flags.h"`, public types + API.
**Cpp**: `static` for all internal variables. Feature-flag guard: `#if ENABLE_X==1` (never `#ifdef`).

### Persistence (LittleFS, MANDATORY atomic write)
```cpp
String tmp = MY_CFG_PATH ".tmp";
File f = LittleFS.open(tmp, "w");
size_t bytes = serializeJsonPretty(doc, f);
f.close();
if (bytes == 0) { LittleFS.remove(tmp); return; }
LittleFS.remove(MY_CFG_PATH);
LittleFS.rename(tmp, MY_CFG_PATH);
```
Use `littlefsBegin()` — NEVER `littlefsMounted()`. NVS = boot-critical only (WiFi, SMTP, passwords).

## Step 3 — Wire into `chatgpt.ino`
1. `#include "src/<module>.h"`
2. `setup_impl.h`: `moduleInit()`
3. `loop_impl.h`: `moduleUpdate()` with timer if periodic
4. Remove old inline code

## Step 4 — Update `LAST GLOBAL CHANGE` in `chatgpt.ino` (Europe/Lisbon time)

## Step 5 — Verify
- `get_errors` clean
- Public API called correctly
- No accidental globals (scan `.h` for `extern`)
- No removed functionality

## Web page checklist (if adding a page)
- No DOCTYPE/html/head/body in asset
- Renderer: `streamPageShellStart` → asset → `config-manager.js` → `streamAutoJsFooter` → `streamPageShellEnd`
- Home button as first child of `.wrap`
- Save: `customAlert('✅')` + `showSavedCard()`, error: `customAlert('❌')`
- API: `{"success":true|false,"message":"..."}`
- Feature-flag guard with `(void)context;` in `#else`
- **Handler**: put business logic in `handlers_unified/<name>_handler.h` (shared WiFi+Ethernet)
- **Route parity**: register endpoint in `routes/web_routes_*.cpp` (WiFi) AND `ethernet_bootstrap_routes.h` + wrapper in `ethernet_bootstrap_handlers.h` (Ethernet)
