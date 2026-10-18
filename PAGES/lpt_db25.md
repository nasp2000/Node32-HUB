# LPT / DB25 — `/lpt-db25`

> One-stop reference for the **LPT / DB25 printer page**: purpose, layout, every
> button/input, client logic, API endpoints, persistence and the firmware-side
> Nano-bridge / printer-emulation engine. Written for both humans and AI agents.

---

## 1. Objective

Configure the external **DB25/Centronics printer emitter** (Arduino Nano bridge on
UART1) and its email pipeline: printer **emulation dialect** (Generic / Epson ESC/P /
IBM Proprinter), **charset mapping** (UTF-8 / Latin-1 / CP850), and the LPT-only
**auto-send timeouts and limits** (min chars, auto-send switch, active/inactive
timeouts, max lines per email). All writes go through `POST /api/config` with
`section=system` and apply without reboot (`syncGlobalVariables()` +
`applyRS232Config()`, `chatgpt/src/webpages/handlers_unified/config_handler.cpp:448-449`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /lpt-db25` |
| Feature flag | `ENABLE_DB25` (`chatgpt/src/config_defaults.h:81-82`, default `0`; `chatgpt/user_config.h:190` commented out) — page AND WiFi route are gated |
| WiFi server | `chatgpt/src/webpages/routes/web_routes_core.cpp:250-271` — `#if ENABLE_DB25==1`, streams via `streamLptDb25PageToPrint()`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |
| Ethernet (W5500) | `chatgpt/src/ethernet_bootstrap_routes.h:300-306` — `#if ENABLE_DB25==1` → `streamSharedLptDb25Page()` (`chatgpt/src/web_shared_pages.cpp:116-118`); `404 not found` when off |
| API (GET) | `GET /api/config?section=system` (page JS, `page_lpt_db25_asset.cpp:59`); Ethernet-only `GET /api/config/printer` (`ethernet_bootstrap_routes.h:522`) |
| API (POST) | `POST /api/config` with `section=system` (`config_handler.cpp:208`) — shared with all system settings, not LPT-specific |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) — same guard chain as every page route |
| Registry / manifests | `route_registry_api.cpp:237` lists `GET /lpt-db25`; self-test `PAGE("/lpt-db25", ENABLE_DB25)` (`test_manifest_handler.cpp:270`, `selftest_handler.cpp:139`); home link `home_page_renderer.h:151-153` (`#if ENABLE_DB25==1`) |
| UART1 conflict | Sniffer+DB25 and ModbusRTU+DB25 are **compile errors** (`chatgpt/packs/pack_validator.h:20-26`); `user_config.h:386-389` force-disables DB25 when sniffer is on; `user_config.h:448-449` re-checks the Modbus clash |

Renderer order: shell → `PAGE_LPT_DB25` (HTML + inline JS) →
`/config-manager.js` → auto.js footer (`lpt_db25_page_renderer.cpp:8-18`).

---

## 3. Layout

```
main.page > div.wrap
 ├ div.actions.nav-inicio > a.btn[href='/']  ← always first child of .wrap (project rule)
 │    "🏨 Home" (data-i18n='btnHome')
 ├ H1 "🖨️ LPT DB25 Printer"                  ← hardcoded, no data-i18n (see LPT-3)
 └ form#lpt-form (action='/api/config', POST)
    ├ input[type=hidden] _html=1
    ├ section "🖨️ Printer Emulation" (blue card)
    │   ├ label "Emulation Type" + description span (hardcoded EN, see LPT-3)
    │   │   └ select#emulation_mode[name='emulation_mode'] (0/1/2)
    │   └ description span (charset mapping, hardcoded EN)
    │       └ select#charset_mode[name='charset_mode'] (0/1/2)
    ├ section "⏱️ Timeouts e Limites (Apenas LPT/DB25)" (blue card, data-i18n='sendTimeoutsTitle')
    │   ├ div label (data-i18n='sendMinCharsLbl') + select#min_email_chars[name='email_min_content_length'] (1/3/5/10)
    │   ├ div label (data-i18n='sendAutoSendLbl') + hidden email_auto_send_present=1 + checkbox#email_auto_send
    │   ├ div label (data-i18n='sendActiveTimeoutLbl') + select#timeout_active[name='timeout_active'] (10s/20s/30s)
    │   ├ div label (data-i18n='sendInactiveTimeoutLbl') + select#timeout_inactive[name='timeout_inactive'] (5min–24h)
    │   └ div label (data-i18n='sendMaxLinesLbl') + select#max_lines[name='email_max_lines'] (66–1000)
    └ div > button[type=submit].btn "💾 Save"
```

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Emulation Type | `emulation_mode` | select `0` Generic (Standard), `1` Epson ESC/P (Experimental), `2` IBM Proprinter (Experimental) | → `config["printer"]["emulation_mode"]` |
| Charset mapping | `charset_mode` | select `0` UTF-8 (Default), `1` Latin-1 (Experimental), `2` CP850 (Experimental) | → `config["printer"]["charset_mode"]` |
| Min chars before auto-send | `min_email_chars` / `email_min_content_length` | select 1/3/5/10 | avoids unnecessary sends |
| Enable auto-send (LPT) | `email_auto_send` (+ hidden `email_auto_send_present=1`) | checkbox `value=1` | master switch that "activates Timeouts below" |
| Active timeout | `timeout_active` | select 10000/20000/30000 ms (10s/20s/30s) | fast sends while auto-send is on |
| Inactive timeout | `timeout_inactive` | select 300000–86400000 ms (5min–24h, 10 options) | spaced sends while auto-send is off |
| Max lines per email | `max_lines` / `email_max_lines` | select 66/90/180/360/720/1000 | per-email LPT line cap |

The JS adds `section=system` to the POST body (`page_lpt_db25_asset.cpp:82`); the
static form also carries hidden `_html=1` (`:11`).

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 💾 Save (submit) | `preventDefault` → `disableBtn` + `showLoading()` → `POST /api/config` (urlencoded) → `d.success` → `customAlert("✅ Configuration saved!" \| message)` + `showSavedCard()`; failure → `customAlert("❌ Error: " + …)`; `finally` → `hideLoading()` + `enableBtn` (`page_lpt_db25_asset.cpp:78-87`) |

---

## 6. Client-side logic (inline JS in `page_lpt_db25_asset.cpp:55-89`)

- **`t(k, fb)`** (`:56`) — `window._t` if present, else `ui_lang_cache` in
  `localStorage`, else fallback; project pattern, no arrow functions.
- **`loadConfig()`** (on window `load`, `:58-76`) — `GET /api/config?section=system`
  and fills the 7 controls; on failure retries after 2 s (`setTimeout(loadConfig,2000)`).
  **Broken in practice**: the system-section GET never returns any of the keys read
  here (see LPT-1), so controls always show HTML defaults.
- **Submit handler** (`:78-87`) — honours project rules (`disableBtn`/`enableBtn`,
  `showLoading`/`hideLoading`, `customAlert`, `showSavedCard`, `d.success` check).

---

## 7. Server API

Handlers in `chatgpt/src/webpages/handlers_unified/config_handler.cpp`.

### `GET /api/config?section=system` (`:143-204`)

Returns only `{system_name, system_language, mdns_enabled, ntp:{timezone, server}}`
(`:184-189`) — **no** `printer_emulation_mode`, `charset_mode`, `email.*`,
`autoEmailEnabledTimeout`/`autoEmailDisabledTimeout` (see LPT-1).

### `POST /api/config`, `section=system` (`:208-486`)

| Field | Handler | Setter |
|---|---|---|
| `emulation_mode` | `:265-266`, `v.toInt()`, **no range check** (see LPT-4) | `setPrinterEmulationMode` |
| `charset_mode` | `:268-269`, `v.toInt()`, **no range check** | `setPrinterCharsetMode` |
| `email_auto_send` (+`_present`) | `:334-341`, only applied when changed | `setEmailAutoSend` |
| `timeout_active` | `:355-356` | `setTimeoutAutoEmailEnabled` |
| `timeout_inactive` | `:358-359` | `setTimeoutAutoEmailDisabled` |
| `email_max_lines` (alias `max_lines`) | `:361-363` | `setEmailMaxLines` |
| `email_min_content_length` (alias `min_email_chars`) | `:365-367`, also refreshes global `minEmailContentLength` | `setEmailMinContentLength` |

Responses: no changes → 200 `{"success":true,"message":"No changes detected"}`;
saved → 200 `{"success":true,"message":"Config saved"}`; save failure → 500
`{"success":false,"message":"Save failed"}` (`:431-485`). Persist stem for
`section=system` is `"system"` (`:450-458`).

### `GET /api/config/printer` — Ethernet only (`ethernet_bootstrap_routes.h:522-541`)

Returns `{printer_emulation_mode, charset_mode, ntp_timezone, timezone, ntp_server,
system_name, system_language, autoEmailEnabledTimeout, autoEmailDisabledTimeout,
ntp:{…}, email:{auto_send, max_lines, min_content_length, …}}`. No WiFi equivalent;
the page JS does not call it.

---

## 8. Persistence

`configManager.saveSection("system")` → **`/config/system.json`** (8 KB budget).
The system stem payload is exactly `printer` + `system_name`/`system_language`/
`mdns_enabled` + `system` subtree (`ConfigManager.cpp:2115-2130`), written atomically
(`.tmp` → verify → remove → rename, `:1966-2015`). `pages.reg` budgets
`system:4096` (`:2022`).

```json
{ "printer": { "emulation_mode": 0, "charset_mode": 0 },
  "system_name": "…", "system_language": "…", "mdns_enabled": true,
  "system": { … } }
```

- Defaults `0`/`0` set at `ConfigManager.cpp:137-138` and backfilled at `:292-294`;
  getters default to `0` when absent (`ConfigManager.h:973-980`).
- The `email.*` / timeout keys this page also modifies live in the **`smtp_email` /
  `timeouts`** area, **not** in the system stem payload — their durability on this
  page relies on the deferred dirty-save path, while printer modes are written
  immediately (see LPT-5).
- Load path merges the system file back (`ConfigManager.cpp:1244-1253`).

---

## 9. Runtime engine (Nano bridge + printer emulation)

- **Bridge** (`chatgpt/src/arduino_bridge.cpp:5-143`, `#if ENABLE_DB25==1`):
  `HardwareSerial NanoUART(1)` at `NANO_UART_BAUD 500000`, RX=`GPIO41`/TX=`GPIO40`
  on S3 (`arduino_bridge.h:30-47`); `nanoBridgeBegin()` reboots the Nano, holds BUSY,
  applies tunables (`NANO_CMD_SET_ACK_WIDTH/STROBE_MIN/PAUSE_MODE/FORCE_BUSY/…`,
  `:26-35`) and releases; XON/XOFF flow control (`nanoBridgeSendXoff/Xon`).
- **Emulation** (`chatgpt/src/printer_emulation.cpp:9-251`, `#if ENABLE_DB25==1`):
  `PRINTER_EMULATION_MODES[] = {"Genérico", "Epson ESC/P", "IBM Proprinter"}`;
  `processGenericCommand` (pass-through), `processEpsonCommand` (ESC-sequence +
  graphics-mode state machine), IBM Proprinter equivalent.
- **Globals sync**: `printerEmulationMode`/`charsetMode` reloaded from ConfigManager
  (`wifi_config_impl.h:178-179`); serial `setPrinterEmulationMode()` validates
  `0 <= mode < PRINTER_EMULATION_MODES_COUNT` (`:107-115`); telnet/serial JSON
  commands validate `0..2` (`process_line_impl.h:893-909`, `command_executor.cpp:385`).
- **Loop/pipeline**: DB25 capture guarded by `#if ENABLE_DB25==1 || ENABLE_RS232==1`
  (`loop_impl.h:1013`); DB25-specific processing at `process_line_impl.h:472,1154-1192`;
  reboot-on-boot hook at `rs232_config_impl.h:407`.

---

## 10. i18n keys

All confirmed present in both `lang_en_ui.h` and `lang_pt_ui.h` (EN lines cited):
`btnHome` (`:7`), `sendTimeoutsTitle` / `sendMinCharsLbl` / `sendAutoSendLbl` /
`sendActiveTimeoutLbl` / `sendInactiveTimeoutLbl` / `sendMaxLinesLbl` (`:754-759`),
`lptConfigSaved` / `lptErrSaving` (`:1665-1666`), `lptTipHome` / `lptTipEmulation` /
`lptTipCharset` / `lptTipMinChars` / `lptTipAutoSend` / `lptTipTimeoutActive` /
`lptTipTimeoutInactive` / `lptTipMaxLines` / `lptTipSave` (`:1813-1821`),
`accessUnknown` (`:614`, used as JS fallback `:85`). Tooltips ride on
`data-i18n-title`; labels/values on `data-i18n` + `window._t`.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| LPT-1 | **Page can never load its own settings.** `loadConfig()` reads `d.printer_emulation_mode`, `d.charset_mode`, `d.email.*`, `d.autoEmailEnabledTimeout/DisabledTimeout` (`page_lpt_db25_asset.cpp:62-73`), but `GET /api/config?section=system` returns only `system_name/system_language/mdns_enabled/ntp` (`config_handler.cpp:184-189`). Every field falls back to HTML defaults (e.g. min-chars shows `1` instead of the saved value). | High | Include `printer` + `email` + timeout keys in the system-section GET (mirroring the Ethernet `GET /api/config/printer` at `ethernet_bootstrap_routes.h:522-541`), or point the page at that shape. |
| LPT-2 | **Stray closing tag.** `page_lpt_db25_asset.cpp:22` ends the charset description with `</label>` although the `<span>` has no opening `<label>` (the emulation block at `:15` opens one, the charset block does not). | Low | Remove the orphan `</label>` or wrap the charset row in a matching `<label>`. |
| LPT-3 | **Half the page is untranslatable + fallback language is mixed.** `H1` (`:8`), `Emulation Type` label + both description spans (`:15-16`, `:22`) have no `data-i18n` and stay English under PT; the timeouts section fallback text is hardcoded PT (`:31-45`). | Low | Add `data-i18n` keys for the H1/emulation/charset strings; keep fallback text in English per project rule. |
| LPT-4 | **No server-side range validation for emulation/charset.** POST accepts any int via `v.toInt()` (`config_handler.cpp:265-269`), while the serial path validates `0..2` (`process_line_impl.h:893-909`, `wifi_config_impl.h:107-115`). `emulation_mode=99` persists silently and the getter just returns it (`ConfigManager.h:973-980`). | Low | Clamp/validate `0..2` in `handleConfigPost` like the serial path does. |
| LPT-5 | **Email/timeout fields bypass the immediate save.** POST `section=system` calls `saveSection("system")`, whose payload is only `printer` + system keys (`ConfigManager.cpp:2115-2130`); the `email.*`/timeout keys live in other stems (`:2131-2144`), so on this page only the printer modes are guaranteed durable immediately. | Low | Also `saveSection("smtp_email")` when email/timeout keys were touched, or document the deferred-save dependency. |
| LPT-6 | **Zero test coverage.** `tests/web/` has no `test_lpt*`, `test_db25*` or `test_printer*` file (verified by directory listing + content grep — only `node_modules` matches), so findings LPT-1–4 have no regression net. | Info | Add a `tests/web/test_lpt_db25.js` asserting GET-shape keys vs JS-read keys, plus POST round-trip of the 7 fields. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_lpt_db25_asset.{h,cpp}` |
| Renderer (WiFi) | `chatgpt/src/webpages/renderers/lpt_db25_page_renderer.{h,cpp}` (`streamLptDb25PageToPrint`) |
| Shared renderer (Ethernet) | `chatgpt/src/web_shared_pages.{h,cpp:116-118}` (`streamSharedLptDb25Page`, unguarded — gating at call sites) |
| Routes (WiFi + Ethernet) | `chatgpt/src/webpages/routes/web_routes_core.cpp:250-271`, `ethernet_bootstrap_routes.h:300-306`, `route_registry_api.cpp:237` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/config_handler.{h,cpp}` (`handleConfigGet` `:143`, `handleConfigPost` `:208`) |
| Ethernet printer API | `chatgpt/src/ethernet_bootstrap_routes.h:522-541` (`GET /api/config/printer`) |
| Config storage | `chatgpt/src/ConfigManager.{h:973-980,1508-1509,cpp:137-138,292-294,1946-2144}` (`printer` subtree, `system.json` stem) |
| Runtime engine | `chatgpt/src/arduino_bridge.{h,cpp}`, `chatgpt/src/printer_emulation.{h,cpp}`, globals sync `wifi_config_impl.h:107-115,178-179`, serial cmds `process_line_impl.h:472,885-909,1154-1192`, `command_executor.cpp:353,385,1074` |
| Self-test / manifests | `test_manifest_handler.cpp:270`, `selftest_handler.cpp:67,139`, `system_status_payload.h:539,608` |
| UART1 conflicts | `chatgpt/packs/pack_validator.h:20-26`, `user_config.h:386-389,448-449`, `config_defaults.h:81-82,131-132` |
