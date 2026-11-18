# Web Pages Reference — Node32-HUB

> One table with every page: what it does, local vs static (poll), access, never-kick, flag.
> Keep in sync on any page/route/poll/flag/access change (same change as `PAGES/<page>.md`).

Legend: `local` = polls continuously in the browser; `static` = config page (one GET + buttons);
`stream` = data stream; `hybrid` = real-time-ish. Auth: `B` = Basic, `V` = viewer account may open
(`/`, `/status`, `/sensor-charts`, `/monitor`, `/files` — rest → 403).
Never kick: `A` = **never closes by time**; `no` = static page (see rules below).
Session engine (multi-client): every request from a tracked IP is activity — a page that only
polls APIs keeps its slot (the 30 s stale reset needs real silence, `web_session_tracker.h`).

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
| `/sensor-charts` | Real-time sensor charts + buffers; after reboot/late `from`, merges SD day `.chart` head with live ring (`streamMergedWindow`); pre-reboot gaps bridged from the RTC RAM tail (`sensor_rtc_tail.h`) | local | hb 45 s + 1 s | B + V | **A** (*) | charts enable + sensor flags (**2 viewer slots, and with `charts_exempt_sessions` ON they never expire — abandoned tabs wedge the page until reboot, `BACKLOG.md` SC-9; cleared by a reboot on 2026-11-17, capture in `PAGES/desktop/sensor_charts.png`)** |
| `/buffer-viewer` | GRBL serial buffer viewer | local | 5 s | B | **A** | `ENABLE_PAGE_MONITOR` |
| `/grbl-controller` | CNC/GRBL: jog, MDI, files, height map. **Phones are redirected to `/m`** (UA-based, `?desk=1` forces this page) | hybrid | WS 100 ms / REST 800 ms | B | **A** | `ENABLE_GRBL` (**1 only in `pack_p4_cnc.h:73` / `pack_s3_cnc_usb.h:44` → 404 on all live boards; the desktop image is a harness render, `PAGES/desktop/README.md` §3.12**) |
| `/m` (phone default) | Mobile wizard **Send · Preview(+FRAME) · Play · Jog · System**, own screens (SD list, preview canvas, jog cross, OTA/log/restart); vertical, no page scroll, content centred, arrows+swipe always navigate, 180 ms tap guard | hybrid | on load: token/psram-file/status/SD list; status 1 s on Play/Jog; summary+log 2 s only while the System tab is open | B | no | `ENABLE_GRBL` + CNC packs |
| `/camera` | Live camera + motion/playback (MJPG) | local | img poll 5 s | B | **A** | `ENABLE_CAMERA` + `ENABLE_SDCARD` |
| `/m` (camera packs) | Camera phone page: **Live (MJPEG, PHOTO, REC, full screen) · Files (play, download ⬇, delete) · Camera (mode/res/fps/quality/image/motion/timelapse) · FTP (status, send now, test) · System (shared)**; `GET /camera` from a phone → 302 here (`?desk=1` keeps the desktop page) | hybrid | status 2 s only on Live; System 2 s while its tab is open | B | no | `ENABLE_CAMERA` + Cam-M/Cam-P4 |
| `/hub` | Camera hub/aggregator | local | 10 s | B | **A** | `ENABLE_CAMERA` |
| `/cam/snapshot` · `/cam/stream` · `/cam/raw` · `/cam/playback` · `/cam/motion/diff` | JPEG/MJPEG/raw/motion/playback streams | stream | on demand / long-lived; raw +10 s status watchdog (CAM-63) | B | **A** (~) | `ENABLE_CAMERA` |
| `/proximity` | Proximity config + OEE shift/day | local | 2 s | B | **A** | `ENABLE_PROXIMITY` |
| `/proximity-dashboard` | OEE dashboard, donuts + shift table + 10-min stop strip | local | 2 s + 3 s hist + 2 min stops | B | **A** | `ENABLE_PROXIMITY` |
| `/modbus-rtu` · `/modbus-tcp` · `/modbus-live` | Modbus scan/monitor/live-viewer | local | 800 ms / 3–5 s | B | **A** | `ENABLE_MODBUS` (+RTU/TCP) — per-page flags `ENABLE_MODBUS_RTU` / `ENABLE_MODBUS_TCP`; only Cam-P4 (`.18`) has TCP on among the live boards, so `/modbus-rtu` is 404 there and **`/modbus-live` answers 302 → `/`** until Modbus is active in config (`web_routes_modbus.cpp:254-262`) (**desktop images are harness renders: `/modbus-tcp` is live on `.18`, `/modbus-rtu` is 404 everywhere and its page script is dead (`BACKLOG.md` MODBUS-7), `/modbus-live` 302s to `/` unless Modbus is active (`MODBUS-8` for its write dropdown), `PAGES/desktop/README.md` §3.12**) |
| `/can` | CAN bus monitor/trace | local | 2 s + 1 s frames | B | **A** | `ENABLE_CAN` |
| `/radio-ui` | Radio/audio player (full-screen player, `#app` capped at 780 px; 9 panels behind a rotating bottom nav) | hybrid | setInterval | B | **A** | `ENABLE_UDA1334A` (**desktop image is a harness render — the Harmonica Box is offline and only `pack_radio.h` enables the flag; `PAGES/desktop/README.md` §3.12**) |
| `/uda1334a` | Audio DAC/PCM test | static | manual | B | no | `ENABLE_UDA1334A` (**desktop image is a harness render: only `pack_radio.h` enables the flag, and its three cards never become tabs — the injected tab script runs before the body, `PAGES/uda1334a.md` UDA-6; `PAGES/desktop/README.md` §3.12**) |
| `/ipg-laser` | IPG laser control/monitor | local | 3 setInterval | B | **A** | `ENABLE_IPG_LASER` (**desktop image is a harness render** — no reachable pack enables the flag; as shipped the inline script is a `SyntaxError`, so the page is inert and none of the three pollers ever starts — `PAGES/BACKLOG.md` IPG-7) |
| `/rambox` | Unified RAMBOX page: device selector (Unikon RAMBOX / Bricon) + 5 tabs (Config/Arrivals/Pigeons/Race/Status); first status load adopts `activeModule`; shared poll interval in minutes (1/3/5/10/15/30/60, default 3); bench mode (`manual_serial`) switches the module to the RS232 page settings | local | 5 s (arrivals/status tabs only) | B | **A** | `ENABLE_UNIKON` \|\| `ENABLE_BRICON` (**only with `ENABLE_UNIKON`/`ENABLE_BRICON` — the Rambox board is offline, so the desktop image is a harness render, `PAGES/desktop/README.md` §3.12**) |
| `/m` (Rambox32) | Clock phone page: **Status (fancier/race/arrivals/RS232) · Arrivals (Pombal/Races tabs) · Config (Bricon/Unikon, polling on-off+interval, RS232 baud/parity) · System (shared)**; `GET /rambox` from a phone → 302 here (`?desk=1` escape) | hybrid | status 3 s only on Status | B | no | `ENABLE_UNIKON`/`ENABLE_BRICON` |
| `/unikon` | Unikon device page — **302** to `/rambox?dev=unikon` | local | 5 s | B | **A** | `ENABLE_UNIKON` |
| `/bricon` | Bricon module page — **302** to `/rambox?dev=bricon` (wire driver implemented from REA; exclusive with Unikon — the choice sets the RS232 profile: Bricon 9600 8N2, Unikon 9600 8N1) | local | 5 s | B | **A** | `ENABLE_BRICON` |
| `/ecra` | Touch7 exact LCD framebuffer mirror + weather/location/status (**desktop image is a harness render** — Touch7-only route, board offline; the 768 KB frame is answered 304 so the canvas stays black, `PAGES/BACKLOG.md` T7-40/T7-41) | hybrid | conditional RGB565 frame 0.25–2 fps; weather status on load and 1 s while updating | B | **A** | `PACK_S3_TOUCH7` |
| `/falas` | Local WAV phrases (list/play/delete) + peers JSON — desktop image is a harness render (`PACK_S3_TOUCH7` only, board offline) and the page is inert as shipped: nothing defines `$`, so no button is bound and the list never loads — `PAGES/BACKLOG.md` T7-38 | static | manual | B | no | `PACK_S3_TOUCH7` |
| `/dualfile` | Copy/write LittleFS↔SD | static | 30 s poll | B | **A** (*) | `ENABLE_DUALFILE` (**1 only in the P4 USB-server pack** → 404 on all live boards; desktop image is a harness render of the shipped asset, `PAGES/desktop/README.md` §3.12) |
| `/usb-server` | USB/IP status + clients (**desktop image is a harness render** — the page and its four APIs need `ENABLE_USBIP_SERVER`, only `pack_p4_usb_server.h` sets it and that board is offline; `PAGES/BACKLOG.md` USB-7) | local | 6 s | B | **A** | `ENABLE_USBIP_SERVER` |
| `/ethernet` | Ethernet + DHCP config | static | 6 s + 8 s | B | no | `ENABLE_ETHERNET` |
| `/networks` | Other Networks — **Telnet only** (enable/disable + password; not an overview of Wi-Fi/Ethernet) | static | manual | B | **A** (*) | — (**page compiled only with `ENABLE_TELNET==1`, and that is 0 in `user_config.h:291` and in every pack → 404 on all live boards; the desktop image is a harness render, `PAGES/desktop/README.md` §3.12**) |
| `/wifi` | WiFi AP/STA + scan | static | scan 5 s | B | no | — |
| `/google-script` | Google Apps Script setup | static | manual | B | no | `ENABLE_PAGE_GOOGLE_SCRIPT` (**never defined** — commented out in `user_config.h`, no pack enables it → 404 on every board; desktop image is a harness render, `PAGES/BACKLOG.md` GS-7) |
| `/access` | Sessions/users/access control + live session list (`/api/session-history`, terminate-all) | static + on demand | one GET on load; session list reloaded by the Refresh button and after a kick | B | **A** (*) | `ENABLE_PAGE_ACCESS` |
| `/alerts-config` | Inactivity/OEE alert rules + live state per slot (sent/cap today, sent-this-period) | static + live state | `/api/status` 30 s (skipped while the tab is hidden) | B | no | `ENABLE_PAGE_ALERTS` + `ENABLE_PROXIMITY` |
| `/email` | SMTP config + test | static | manual | B | no | `ENABLE_SMTP` |
| `/recipients` | Email recipient groups | static | manual | B | no | `ENABLE_PAGE_RECIPIENTS` |
| `/ftp` | FTP upload client + test + last-sends history (persisted `/config/ftp_status.json`, loaded once on page open) | static | manual | B | no | `ENABLE_FTP_CLIENT` + `ENABLE_PAGE_FTP` |
| `/structures` | Amada ISQ20 welding page (overview / parsing rules / diagnostics / error dictionary) | static | manual | B | no | `ENABLE_STRUCTURES_PAGE` (**0** in `user_config.h:287` and every reachable pack, only `pack_rs232.h:77-78` sets 1 → 404 on all live boards; desktop image is a harness render, `PAGES/desktop/README.md` §3.12) |
| `/rs232` · `/lpt-db25` | RS232/LPT-DB25 config + pins; always shows the values **in force** (live runtime values, so a Rambox clock profile appears here) and stays editable — `active_module` / `active_module_manual` from `GET /api/config?section=rs232` | static | manual | B | no | `ENABLE_RS232` / `ENABLE_DB25` (**the `/rs232` page is registered even when `ENABLE_RS232==0` while `/api/rs232-*` are not — `BACKLOG.md` RS232-8**) (**the `/lpt-db25` desktop image is a harness render — `ENABLE_DB25` is 0 in `user_config.h:423`, `PAGES/desktop/README.md` §3.12**) |
| `/printer` · `/buzzer` · `/do` | **System Settings** (name/mDNS/language/NTP — not a printer form) / buzzer config + pattern test / digital outputs | static | manual | B | no | — / `ENABLE_BUZZER` (route only exists when 1; forced 0 in `pack_waveshare.h:74-75`, undefined in the S3/cam builds → 404 on every live board; desktop image is a harness render, `PAGES/desktop/README.md` §3.12) / `BOARD_HAS_TCA9554_DO` |
| `/sdcard` · `/files` | Storage config (SD/Flash/PSRAM) / browser, default+selector order SD → LittleFS → PSRAM | static | manual (estado imediato no load: `/api/sd-status` + `/api/sdcard-config`, ambos <20 ms) | B (+V files) | no | `ENABLE_PAGE_SDCARD` / `ENABLE_PAGE_FILES` + `ENABLE_SDCARD` |
| `/mq135-config` · `/hx711` · `/ina226` · `/sound` · `/max31865` | Sensor measure + calibration | static | manual POST | B | no | sensor flags (each route only exists in the pack that owns the sensor — `/mq135-config` 404 on Waveshare, 200 on HomeE32) (**`/mq135-config` is live on HomeE32 `.24`; `/hx711`, `/ina226`, `/sound` and `/max31865` are 404 everywhere — their desktop images are harness renders, `PAGES/desktop/README.md` §3.12**) |
| `/sensor-config` | Sensor channel/caps config | static | manual POST | B | no | `ENABLE_SENSOR_CONFIG_PAGE` |
| `/commands` · `/commands-by-datetime` | Scheduled commands (`/commands` = interval, `/commands-by-datetime` = date/time, 16 slots each side of the API) | static | manual (1× `/api/scheduled-command-slots`; templates só se `chart-caps.rs232`) | B | no | `ENABLE_PAGE_COMMANDS` (both pages) + `ENABLE_PAGE_COMMANDS_INTERVAL` (`/commands` only; `=0` keeps datetime alone, e.g. Waveshare). API `/api/daily-reboot` also with `ENABLE_DAILY_REBOOT` alone — no page |
| `/ota` | OTA upload + apply | static | post-reboot poll | B | no | `ENABLE_OTA` |
| `/api-reference` | API reference | static | one GET | B | no | `ENABLE_API_REFERENCE_PAGE` (**0** in `user_config.h:285` and in every pack → 404 on all live boards; the desktop image is a harness render of the shipped asset, `PAGES/desktop/README.md` §3.12) |

## Language (i18n) — applies to every page

Every page is bilingual (PT/EN). The default is **English**.

| Piece | Where | Notes |
|---|---|---|
| Pack EN | `chatgpt/src/languages/lang_en_ui.h` | 2765 keys, `R"JSON(...)JSON"` PROGMEM |
| Pack PT | `chatgpt/src/languages/lang_pt_ui.h` | 2765 keys, byte-for-byte key parity |
| Selection | `system_language` config (`"en"` default) | `ConfigManager`, applied by `setupLanguage()` |
| Delivery | `GET /api/lang` (ETag) → `window._t(key, fallback)` | `web_routes_core.cpp`, `auto_js.h`; the gzip path counts declared vs sent at client close (WARN + `/api/health` `lang_gzip`, SD-11) |
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

## Endpoints transversais (2026-11-10)

| Endpoint | Uso | Notas |
|---|---|---|
| `GET /api/version` | cache-buster de firmware em `auto.js` | ~64 B; substitui `/api/status` no load de cada página |
| `GET /api/status-basic` | pollers/dashboards | ~116 B (`version`, `uptime`, `free_heap`, `rssi`, `ip`) |
| `GET /api/status` | página Status | deixou de bloquear (47-100 ms; era 1,0-20 s) |
| `GET /vendor/chart*.js` | Chart.js local | gzip pré-gerado, cache `immutable` |
| `POST /api/sensor-history-compact` | manutenção: deduplica `.chart`/`.csv` de um dia (`date=YYYY-MM-DD`) | mantém a 1.ª ocorrência; só admins |
