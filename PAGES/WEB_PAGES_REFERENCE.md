# Web Pages Reference — Node32-HUB

> One table with every page: what it does, local vs static (poll), access, never-kick, flag.
> Keep in sync on any page/route/poll/flag/access change (same change as `PAGES/<page>.md`).

Legend: `local` = polls continuously in the browser; `static` = config page (one GET + buttons);
`stream` = data stream; `hybrid` = real-time-ish. Auth: `B` = Basic, `V` = viewer account may open
(`/`, `/status`, `/sensor-charts`, `/monitor`, `/files` — rest → 403).
Never kick: `A` = **never closes by time**; `no` = static page (see rules below).

## Never-kick rules (essential)

**ALWAYS exempt — kick-off never exists** (local pages carry `_localPagesNeverExpire`
unconditionally via `streamNeverExpireFlag()`, `<head>` inject, or route prefix;
`*` = no `auto.js` at all; `~` = stream, no HTML session):

`/monitor`, `/serial-monitor`, `/buffer-viewer`, `/grbl-controller`, `/camera`, `/hub`,
`/cam/snapshot`, `/cam/stream`, `/cam/raw`, `/cam/playback`, `/cam/motion/diff` (~),
`/proximity`, `/proximity-dashboard`, `/modbus-rtu`, `/modbus-tcp`, `/modbus-live`,
`/can`, `/radio-ui`, `/ipg-laser`, `/unikon`, `/ecra`, `/usb-server`,
`/sensor-charts` (*), `/dualfile` (*), `/networks` (*), `/access` (*).

**Non-local (all `static` pages) — governed by the Access option** (`/access` →
`charts_exempt_sessions`, default ON): browser still redirects to `/` after 10 idle min;
single-client server timeouts (503) apply unless the option is ON. The option does
nothing to local pages.

Session engine: multi-client (default) counts **IPs, not tabs** (new IP at cap → 429,
never time-kicks); single-client kicks idle/abs unless the option is ON.

## Master table

| Page (`GET`) | What it does | Proc. | Poll / loop | Auth | Kick | Flag |
|---|---|---|---|---|---|---|
| `/` | Home dashboard, links + status cards | static | one GET | B | no | — |
| `/status` | Firmware/network/session info cards | static | 1 fetch + manual | B + V | no | `ENABLE_PAGE_MONITOR` |
| `/monitor` | Serial/event live tail (~900 ms) | local | 900 ms + 3 s | B + V | **A** | `ENABLE_PAGE_MONITOR` |
| `/serial-monitor` | Serial monitor variant | local | 3 s | B | **A** | `ENABLE_PAGE_MONITOR` |
| `/sensor-charts` | Real-time sensor charts + buffers; after reboot/late `from`, merges SD day `.chart` head with live ring (`streamMergedWindow`); pre-reboot gaps bridged from the RTC RAM tail (`sensor_rtc_tail.h`) | local | hb 45 s + 1 s | B + V | **A** (*) | charts enable + sensor flags |
| `/buffer-viewer` | GRBL serial buffer viewer | local | 5 s | B | **A** | `ENABLE_PAGE_MONITOR` |
| `/grbl-controller` | CNC/GRBL: jog, MDI, files, height map | hybrid | WS 100 ms / REST 800 ms | B | **A** | `ENABLE_GRBL` |
| `/camera` | Live camera + motion/playback (MJPG) | local | img poll 5 s | B | **A** | `ENABLE_CAMERA` + `ENABLE_SDCARD` |
| `/hub` | Camera hub/aggregator | local | 10 s | B | **A** | `ENABLE_CAMERA` |
| `/cam/snapshot` · `/cam/stream` · `/cam/raw` · `/cam/playback` · `/cam/motion/diff` | JPEG/MJPEG/raw/motion/playback streams | stream | on demand / long-lived; raw +10 s status watchdog (CAM-63) | B | **A** (~) | `ENABLE_CAMERA` |
| `/proximity` | Proximity config + OEE shift/day | local | 2 s | B | **A** | `ENABLE_PROXIMITY` |
| `/proximity-dashboard` | OEE dashboard, donuts + shift table + 10-min stop strip | local | 2 s + 3 s hist + 2 min stops | B | **A** | `ENABLE_PROXIMITY` |
| `/modbus-rtu` · `/modbus-tcp` · `/modbus-live` | Modbus scan/monitor/live-viewer | local | 800 ms / 3–5 s | B | **A** | `ENABLE_MODBUS` (+RTU/TCP) |
| `/can` | CAN bus monitor/trace | local | 2 s + 1 s frames | B | **A** | `ENABLE_CAN` |
| `/radio-ui` | Radio/audio player | hybrid | setInterval | B | **A** | `ENABLE_UDA1334A` |
| `/uda1334a` | Audio DAC/PCM test | static | manual | B | no | `ENABLE_UDA1334A` |
| `/ipg-laser` | IPG laser control/monitor | local | 3 setInterval | B | **A** | `ENABLE_IPG_LASER` |
| `/rambox` | Unified RAMBOX page: device selector (Unikon RAMBOX / Bricon) + 5 tabs (Config/Arrivals/Pigeons/Race/Status); first status load adopts `activeModule`; shared poll interval in minutes (1/3/5/10/15/30/60, default 3); bench mode (`manual_serial`) switches the module to the RS232 page settings | local | 5 s (arrivals/status tabs only) | B | **A** | `ENABLE_UNIKON` \|\| `ENABLE_BRICON` |
| `/unikon` | Unikon device page — **302** to `/rambox?dev=unikon` | local | 5 s | B | **A** | `ENABLE_UNIKON` |
| `/bricon` | Bricon module page — **302** to `/rambox?dev=bricon` (wire driver implemented from REA; exclusive with Unikon — the choice sets the RS232 profile: Bricon 9600 8N2, Unikon 9600 8N1) | local | 5 s | B | **A** | `ENABLE_BRICON` |
| `/ecra` | Touch7 exact LCD framebuffer mirror + weather/location/status | hybrid | conditional RGB565 frame 0.25–2 fps; weather status on load and 1 s while updating | B | **A** | `PACK_S3_TOUCH7` |
| `/falas` | Voice/audio page | static | manual | B | no | `PACK_S3_TOUCH7` |
| `/dualfile` | Copy/write LittleFS↔SD | static | 30 s poll | B | **A** (*) | `ENABLE_DUALFILE` |
| `/usb-server` | USB/IP status + clients | local | 6 s | B | **A** | `ENABLE_USBIP_SERVER` |
| `/ethernet` | Ethernet + DHCP config | static | 6 s + 8 s | B | no | `ENABLE_ETHERNET` |
| `/networks` | Network overview | static | manual | B | **A** (*) | — |
| `/wifi` | WiFi AP/STA + scan | static | scan 5 s | B | no | — |
| `/google-script` | Google Apps Script setup | static | manual | B | no | `ENABLE_PAGE_GOOGLE_SCRIPT` |
| `/access` | Sessions/users/access control | static | one GET | B | **A** (*) | `ENABLE_PAGE_ACCESS` |
| `/alerts-config` | Inactivity/OEE alert rules | static | manual | B | no | `ENABLE_PAGE_ALERTS` + `ENABLE_PROXIMITY` |
| `/email` | SMTP config + test | static | manual | B | no | `ENABLE_SMTP` |
| `/recipients` | Email recipient groups | static | manual | B | no | `ENABLE_PAGE_RECIPIENTS` |
| `/ftp` | FTP upload client + test + last-sends history (persisted `/config/ftp_status.json`, loaded once on page open) | static | manual | B | no | `ENABLE_FTP_CLIENT` + `ENABLE_PAGE_FTP` |
| `/structures` | Custom fixtures page | static | manual | B | no | `ENABLE_STRUCTURES_PAGE` |
| `/rs232` · `/lpt-db25` | RS232/LPT-DB25 config + pins; always shows the values **in force** (live runtime values, so a Rambox clock profile appears here) and stays editable — `active_module` / `active_module_manual` from `GET /api/config?section=rs232` | static | manual | B | no | `ENABLE_RS232` / `ENABLE_DB25` |
| `/printer` · `/buzzer` · `/do` | Printer / buzzer test / outputs | static | manual | B | no | — / `ENABLE_BUZZER` / `BOARD_HAS_TCA9554_DO` |
| `/sdcard` · `/files` | Storage config (SD/Flash/PSRAM) / browser, default+selector order SD → LittleFS → PSRAM | static | manual (`/sdcard` status 10 s poll in auto.js) | B (+V files) | no | `ENABLE_PAGE_SDCARD` / `ENABLE_PAGE_FILES` + `ENABLE_SDCARD` |
| `/mq135-config` · `/hx711` · `/ina226` · `/sound` · `/max31865` | Sensor measure + calibration | static | manual POST | B | no | sensor flags |
| `/sensor-config` | Sensor channel/caps config | static | manual POST | B | no | `ENABLE_SENSOR_CONFIG_PAGE` |
| `/commands` · `/commands-by-datetime` | Scheduled commands (`/commands` = interval, `/commands-by-datetime` = date/time) | static | manual | B | no | `ENABLE_PAGE_COMMANDS` (both pages) + `ENABLE_PAGE_COMMANDS_INTERVAL` (`/commands` only; `=0` keeps datetime alone, e.g. Waveshare). API `/api/daily-reboot` also with `ENABLE_DAILY_REBOOT` alone — no page |
| `/ota` | OTA upload + apply | static | post-reboot poll | B | no | `ENABLE_OTA` |
| `/api-reference` | API reference | static | one GET | B | no | `ENABLE_API_REFERENCE_PAGE` |

## Language (i18n) — applies to every page

Every page is bilingual (PT/EN). The default is **English**.

| Piece | Where | Notes |
|---|---|---|
| Pack EN | `chatgpt/src/languages/lang_en_ui.h` | 2765 keys, `R"JSON(...)JSON"` PROGMEM |
| Pack PT | `chatgpt/src/languages/lang_pt_ui.h` | 2765 keys, byte-for-byte key parity |
| Selection | `system_language` config (`"en"` default) | `ConfigManager`, applied by `setupLanguage()` |
| Delivery | `GET /api/lang` (ETag) → `window._t(key, fallback)` | `web_routes_core.cpp`, `auto_js.h` |
| Markup | `data-i18n` · `data-i18n-title` · `data-i18n-html` | inner text = **English** fallback |
| OLED screen | `LangStrings` struct + `L(activeLang->…)` | `lang_types.h`, `lang_active.cpp`, `screen.cpp` |
| TouchMeteo-7 OLED | same pack (`touch7*` fields) | `touch7_display.cpp`, `touch7_weather.cpp` |
| API JSON messages | same pack (`api*` fields) | every `handlers*/` file |
| Ethernet bootstrap pages | same pack (`eth*` fields) | `ethernet_bootstrap_response.h`, `ethernet_bootstrap_handlers.h` |
| Page `<title>` | same pack (`pageTitle*` fields) | each `renderers/*_page_renderer.cpp` |
| Validation | `chatgpt/scripts/validate_i18n.py` (pre-build) | fails on pack drift or undefined `data-i18n` key; warns on PT outside `data-i18n` |

Rules:

- Adding a visible string ⇒ add the key to **both** packs and put English in the HTML fallback. The pre-build
  validator fails the build otherwise.
- **Two different mechanisms, one selection.** Anything the *browser* shows is translated in the **UI packs**
  (`lang_*_ui.h`, served by `/api/lang`, read through `data-i18n` / `window._t`). Anything the *firmware*
  renders or returns — OLED labels, API `message`/`msg` fields, `<title>` — is translated in the **firmware
  pack** (`LangStrings` in `lang_types.h` / `lang_active.cpp`, read through `L(activeLang->…)`). The UI packs
  never reach the firmware side, so a JSON error message cannot be fixed by adding a `lang_*_ui.h` key.
- The firmware pack is `PROGMEM` (`const LangStrings LANG_PT PROGMEM`): adding fields costs flash, not heap.
- Bilingual labels that intentionally show both languages on purpose (`Modbus Ativo / Active`) are not a gap
  and are exempt from the PT-fallback warning.
- Page/LCD tab names rendered verbatim (`Receção de Dados`, `AGORA`, `PREVISÃO`, `Pombal`) are identity strings —
  never translated, and exempt from the same warning.
- Protocol sentinels (`(não alterado)`, `Padrão`, `Genérico`) are exchanged with the browser or stored in
  config byte-for-byte — never translate them.
- Diagnostic `Serial.println` strings are plain English literals (code is English); they do not go through
  the packs.
- Shared non-page assets: `config_manager_js_asset.cpp` (`cfgSaveErr`, `cfgRecipientsRequired`),
  `fragments_system_asset.cpp` (`navApiRef`), `auto_js.h` (`t7RefPlaceholder`, `t7EmailsHint`).

Key files: `webpage_state.cpp:236` (sessions) · `auto_js.h:99-138` (10-min watchdog) ·
`page_render_common.h` (`streamNeverExpireFlag`) · `web_monitor_pages.cpp` (monitor heads) ·
`routes/web_routes_*.cpp` · per-page detail in `PAGES/<page>.md` · backlog in `PAGES/BACKLOG.md`.
