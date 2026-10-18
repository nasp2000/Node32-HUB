# Node32-HUB — Pages Backlog

Central fix backlog discovered while auditing pages documented in this folder.
One row per issue. When an item is fixed: mark status, move nothing — history stays here.
Fixing an item ALWAYS includes updating the corresponding `PAGES/<page>.md`.

Status values: `open` · `in-progress` · `fixed` · `wontfix`

Pages audited after 2026-10-17 (`bricon.md`, `unikon.md`, `api_reference.md`) keep
their findings tables in the page doc itself; this file stays the cross-page
history and is still the place to look before changing shared wiring.

---

## TouchMeteo-7 (`touch7.md` → `/falas`, pack `S3_TOUCH7`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| T7-HW1 | High | First build verified 14-09-2026: SUCCESS 20m30s, RAM 36.4% / Flash 59.6%, signed OTA in `build/esp32s3/S3_TOUCH7/`. **Boot abort on HW traced to RGB-ISR/PSRAM vs SPI1-flash race (addr2line); fix package = FB-direct + panel-last + WAV preload + flash-hold + OTA del. Owner rebuilds.** | `touch7_display.cpp`, `touch7_audio.cpp`, `rs232_config_impl.h`, `ota_admin_handler.cpp` | in-progress |
| T7-HW2 | High | I2C sharing (Arduino Wire1 sensors + panel I2C0 touch/expander on pins 8/9) + RGB timings (PCLK/porches) + touch chip confirm + backlight dim-PWM vs on/off + CH422G init. | `board_pins.h`, `touch7_display.cpp` | open |
| T7-HW3 | High | PIR GPIO unassigned (`TOUCH7_PIR_PIN=-1`): PIR/F3b inert until schematic review picks a free pad. | `board_pins.h`, `touch7_pir.cpp` | open |
| T7-HW4 | High | TF slot is SPI (GPIO11/12/13) with active-low CS on CH422G EXIO4, but Touch7 left `SDCARD_USE_SPI_MODE` undefined (default 0), so `sdcardBegin()` called SDMMC on SPI pins. Fix implemented (code only, no firmware build/flash yet): `board_pins.h` selects the SPI backend with Touch7 pins + external CS (`SDCARD_SPI_CS_PIN=-1`, `TOUCH7_SD_CS_EXIO=4`); `touch7SdAssertCs()` reuses the Board's expander handle (no second CH422G) and is re-asserted after every panel rebuild; boot mounts LittleFS first and triggers `sdcardBeginAsync()` after `screenManager.begin()`; `sdcardBegin()` defers when the expander is not ready; SPI auto-mount re-enabled for this mode. Pending: owner build/flash + HW verify (mount/read/write/remove, `/files`, LittleFS intact). | `board_pins.h`, `touch7_display.h/.cpp`, `sd_status_impl.h`, `rs232_config_impl.h`, `docs/waveshareLCDTouch7.md` | in-progress |
| T7-HW5 | Medium | PSRAM budget on HW: draw 64 KB + icons 153 KB + tiles 460 KB + snapshot 256 KB + LVGL heap 48 KB (internal) — confirm `minHeap/maxAlloc` curve 24 h. | `touch7_display.cpp`, `touch7_peers.cpp` | open |

---

## Inactivity alerts (`inactivity_alerts.md` → `/alerts-config`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| ALERTS-1 | High | No "Send test alert" button — validating setup requires waiting for real inactivity. **Fixed:** per-slot 📤 Test button + `POST /api/inactivity-alert-test` reusing `startInactivityAlertAsync()`. | `recipients_handler.cpp:121`, `web_routes_config.cpp:54`, `alerts_page_renderer.cpp`, `page_alerts_asset.cpp` | fixed |
| ALERTS-2 | Medium | Email validation only checks `indexOf('@') >= 0` (client + server) — `"a@"` passes everywhere. Minimal sanity check: local part + domain with dot. | `alerts_page_renderer.cpp:152`, `recipients_handler.cpp:102` | open |
| ALERTS-3 | Low | Option labels hardcode PT *"avisos/dia"* in HTML fallback — converted to English *"alerts/day"*. | `alerts_page_renderer.cpp:100` | fixed |
| ALERTS-4 | Low | UI select caps alerts/day at 60, API accepts 1–100 — silent mismatch. Align ranges and document the chosen limit. | renderer `kMaxPerDayVals`, `recipients_handler.cpp:94` | open |
| ALERTS-5 | Low | Page shows no live state (`alerts_sent_today` / period flag already exposed by `system_status_payload.h:733`). Small GET fetch + render per slot. | renderer inline JS, system status payload | open |
| ALERTS-6 | Low | Saving overwrites disabled slots with form defaults (5 min / 6 day) — tuned values of disabled slots silently lost. Skip fields or preserve prior values server-side. | `recipients_handler.cpp:108` | open |
| ALERTS-7 | Low | Page depends on SMTP but links nowhere — cross-link to email settings page in info box. | `page_alerts_asset.cpp` | open |
| ALERTS-8 | High | **alerts.json written as `{}` on .29** — `copyJsonCleanControlChars` passed child destinations as by-value `JsonVariant`, writes silently dropped by the board's ArduinoJson; rewrite with handle-based overloads. | `ConfigManager.cpp` (helper) | fixed |
| ALERTS-9 | High | **Reboot restored slots to disabled defaults on .29** — alerts import ran mid-load; later config writes relocated the pool and invalidated the copied tree; import moved to the final config mutation. | `ConfigManager.cpp` (`loadFromLittleFS`) | fixed |
| ALERTS-10 | Medium | Save buffer: alerts.json uses 8192 both for the write cap and the sanitize doc — verified ≥ 2× worst case (6 slots × ~700 B worst). | `ConfigManager.cpp` | fixed |
| ALERTS-11 | High | **OEE tab never persisted on .29 (2026-10-12): page validated + answered ok, GET/page rendered blanks.** Live `/config/alerts.json` carried `"oee_slots":[]` — key exists as empty array, so `_getOeeAlertSlots()` skipped the default-fill; every `setOeeAlertSlot` silently no-op'd, every getter returned hardcoded defaults. **Fixed:** getter grows the array back to 3 default slots whenever `size < OEE_ALERT_SLOT_COUNT` (self-heals RAM on first access, disk on next save) + native regression test with the board's exact `[]` state (fails pre-fix 60-vs-77/empty-emails, passes post-fix). Inactivity unaffected (load path already ensures 6 `slots`). | `ConfigManager.h` (`_getOeeAlertSlots`), `test_alerts_persist_real.cpp` | fixed |
| ALERTS-12 | High | **Alert slots + OEE slots lost on every reboot (.29, v1.119 WAVESHARE, repro 15-10-2026): POST ok → reboot → defaults (20/12); `/config/alerts.json` grows garbage keys (`""`, raw MAC bytes, then whole compact-log text) whose bytes CHANGE per save. Root cause proven in source: `StringAdapter<const char*>` = Link policy (ArduinoJson 6.21.6 `Strings/Adapters/RamString.hpp:87`) — every `config[sec][k]=v` with `const char* k` from a temp doc stores a DANGLING key pointer; `alertDoc` (8KB, freed last at end of load) is reused first by ~8KB response Strings. Second half: `config` fixed 16KB pool nearly full at end-of-load → array deep-copies silently OOM-dropped. ALERTS-9 "pool relocation" theory wrong; its import-last reorder could not help. Fix (working tree): `kv.key()` (JsonString/Copy) at all 7 store sites + `config` 16KB→24KB; recip/sched/modbus loops already used `kv.key()`. Fixed 2026-10-15 and device-proven on .29 (values survive 2 reboots, file clean, regression 8 OK / 0 FAIL). Binary `chatgpt/build/esp32s3/WAVESHARE/v1.119/20261015_1327/waveshare_8di8do_v1.119_WAVESHARE_20261015_1327.bin`.** | `ConfigManager.cpp` (import loops), `ConfigManager.h:283` | fixed |

---

## System settings (`system_settings.md` → form on `/printer`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| SYS-1 | High | **mDNS toggle lost on direct API saves (.29).** Handler only read `mdns_enabled` when the hidden marker `mdns_enabled_present` was present; harness/direct calls without it lost the value (reboot → default `true`). Fixed: key presence (or legacy marker) triggers the branch, value guarded. | `config_handler.cpp:245-255` | fixed |
| SYS-2 | High | **`mdns_enabled` never written.** `saveSection("system")` wrote only `{printer,system_name,system_language}`; now also `mdns_enabled` + full `config["system"]` subtree; load restores both. | `ConfigManager.cpp` (sect. 8) | fixed |
| SYS-3 | High | **Timezone/NTP never persisted (.29).** `ntpManager.save()` never called outside `begin()`; now called by `handleConfigPost` when NTP touched; proven by `test_ntp_persist_real`. | `config_handler.cpp`, `NtpManager.cpp` | fixed |
| SYS-4 | Medium | **Buffer < 2× worst case.** `sysDoc`/`systemPageDoc` 4096→8192; `recipDoc` 4096→8192; `schedDoc` 2048→4096; `saveSection("recipients"/"schedule")` write cap 4096→8192; proximity load/save 4096→8192. | `ConfigManager.{h,cpp}`, `proximity_manager.cpp` | fixed |

---

## microSD / PSRAM (`microsd_card.md` → `/sdcard`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| SD-1 | High | Save from this page wipes `structured`, `email_from_sd`, `max_size_mb`, `emailday_extra_emails` — **fixed:** `saveConfig()` now preserves + re-sends those fields from `GET /api/sdcard-config`. | `auto_js.h:776` (`saveConfig`), `storage_handler.cpp:132`, `web_shared_logic.cpp:469` | fixed |
| SD-2 | Medium | Dead duplicate `initSdcardPage()` in `page_sdcard_asset.cpp:132-205` — shadowed by auto.js version; uses undefined helpers (`bindClick`/`apiGet`/`apiPost`) and posts mount/unmount to `/api/sdcard-config-save` (wrong endpoint). | `page_sdcard_asset.cpp` | open |
| SD-3 | Medium | Commands card (`SDCARD_COMMANDS_SECTION`, EMAILDAY/LOGDAY/SENSORDAY help) defined but never rendered by the renderer. | `page_sdcard_asset.cpp:3`, `sdcard_page_renderer.cpp` | open |
| SD-4 | Low | `#sd_msg` element missing (action feedback dropped) and `j.disabled` never set by `/api/sd-status` ("SD desativado" unreachable). | `auto_js.h:722,744-745`, `web_shared_logic.cpp:418` | open |
| SD-5 | Low | PT runtime fallbacks in auto.js (`Sem dados SD`, `Montado`, `Configuração guardada!`) + PT server codes/messages (`flush_interval_invalido`, `Config guardada`, `Montagem iniciada em background`, `Contador zerado com sucesso`) — **fixed:** all English. | `auto_js.h:744-845`, `web_shared_logic.cpp:472,496,503`, `storage_handler.cpp:118` | fixed |
| SD-6 | Medium | `keep_days` "∞" (value 0, UI default) coerced to `7` on save — **fixed:** handler preserves `0` (∞), only clamps negatives. | `storage_handler.cpp:150`, `page_sdcard_asset.cpp:112` | fixed |
| SD-7 | Medium | Storage-standard audit 2026-10-14 (ARCHITECTURE §6.1): sensor `.chart` API already merges (PSRAM / merged / file three-way via `sensorHistorySourceFor` + `streamMergedWindow`) — no gap. Verified, nothing to do. | `sensor_handler.cpp:373`, `sensor_config.cpp:1700` | fixed |
| SD-8 | Medium | Storage-standard gap (audit 2026-10-14): RS232 `.chart` has no LittleFS persisted mirror (inline SD-only writer; serving already PSRAM-first via `rs232LiveBuffer`). Fix: FS branch + configured-interval throttle, same pattern as prox/modbus. | `data_sd_pipeline_impl.h` (RS232 flush block), `user_config.h:467` | open |
| SD-9 | Low | Storage-standard gap (audit 2026-10-14): RS232 chart API serves PSRAM-first-if-covering with no union merge (prox/modbus merge). Fix: same `ReadRawPoints`+merge+serve (RS232 record type). | `rs232_history_handler.cpp` | open |

---

## Access (`access.md` → `/access`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| ACC-1 | Medium | `POST /api/access-kick-all` and `GET /api/session-history` registered + documented in `/api-reference` but no UI on the page (no "terminate all sessions" button, no session list). | `config_handler.cpp:614,622`, `page_access_asset.h` | open |
| ACC-2 | Low | Invalid/absent numeric fields silently skipped (`getInt` returns `-1`); out-of-range posts keep old value with no error feedback. | `config_handler.cpp:551-556` | open |
| ACC-3 | Low | Dead PT password sentinel `"(não alterado)"`/`"(nao alterado)"` vs EN client placeholder `(unchanged)`; empty field already means "keep current". | `config_handler.cpp:576,587` | open |
| ACC-4 | Low | Page/API routes registered unconditionally — `ENABLE_PAGE_ACCESS` only gates the home button + self-test manifest, unlike `/sdcard`. | `web_routes_access.cpp` | open |
| ACC-5 | Low | UI ranges narrower than API (idle 5–60 vs 1–120; abs 15–120 vs 5–240) — silent mismatch. | `page_access_asset.h`, `config_handler.cpp:561,564` | open |
| ACC-6 | Info | Viewer fields always visible even when `viewer_enabled` is off — collapse until checkbox checked. | `page_access_asset.h` | open |

---

## Recipients (`recipients.md` → `/recipients`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| RECIP-1 | High | UI fixed at 10 rows, `MAX=20` unreachable, no Add-Row button — **fixed:** `MAX=50`, ➕ Add Rule button, per-row 🗑️ remove (contiguous rebuild). | `page_recipients_asset.cpp` (`rowHTML`/`addRow`/`removeRow`/`init`) | fixed |
| RECIP-2 | Medium | Day Command Sends card uses hardcoded PT text (no `data-i18n`); page sends `Content-Language: pt` — **fixed:** text converted to English; header removed. | `page_recipients_asset.cpp` (DAY_CMDS fragments), `web_routes_config.cpp:130` | fixed |
| RECIP-3 | Medium | `saveDontCopy()` is fire-and-forget (`.catch(function(){})`) — failed sender-copy save is silent. | `page_recipients_asset.cpp` (`saveDontCopy`) | open |
| RECIP-4 | Low | "Save All" is non-atomic — N sequential 120 ms POSTs; mid-loop failure leaves partial save with no error surfaced. | `page_recipients_asset.cpp` (`attachSaveAll`) | open |
| RECIP-5 | Medium | `freq` gap: server default `0` (= always) not in the UI option list (starts at 180) — stored `freq:0` renders as 600 and a save converts it. | `page_recipients_asset.cpp` (freq options), `ConfigManager.h:1724-1726` | open |
| RECIP-6 | Low | `/api/recipients` omits the `active` field (only `/api/recipient-slots` includes it). | `handlers/recipients_handler.cpp:59-66`, `ethernet_bootstrap_handlers.h:3068-3075` | open |
| RECIP-7 | Low | `saveSlot` falls back to `freq:'0'` — not in the option list (should be `'600'`). | `page_recipients_asset.cpp:122` | open |
| RECIP-8 | Info | `handleRecipientSlotPost` truncates emails to 800 then ConfigManager to 256 — the 800 cap is dead code. | `recipients_handler.cpp:179` (unified handler), `ConfigManager.h:1723` | open |

---

## Monitor / Serial Monitor (`monitor.md` → `/monitor` + `/serial-monitor`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| MON-1 | Medium | `/monitor` i18n via ad-hoc `_rxT(key,pt,en)` selecting fallback by browser-language heuristic — bypasses `data-i18n`/`/api/lang`, can show PT to EN browsers. | `web_monitor_pages.cpp:39-149` | open |
| MON-2 | Low | `monitor-open/close` counters only feed an `override` flag — unclear if they gate anything (dead-ish state). | `monitor_handler.cpp`, `monitor_session_state.h` | open |
| MON-3 | Low | `/serial-monitor` re-renders up to 500 rows every 3 s poll; batch rendering + adaptive interval when `buffer_seq` stalls. | `web_monitor_pages.cpp:577-597` | open |
| MON-4 | Low | `sendCmd`/`send()` assume JSON responses; non-JSON (503) responses surface as network errors. | `web_monitor_pages.cpp:373,677` | open |
| MON-5 | Low | `#srch` has no `input` listener — list only re-filters on next poll. | `web_monitor_pages.cpp:541` | open |
| MON-6 | High | **TWDT crash (`CRASH_TASK_WDT`, task=async_tcp) at boot when `/serial-monitor` is the first page opened.** Root cause: `async_tcp` is subscribed to the Task Watchdog (AsyncTCP.cpp `esp_task_wdt_add`) and `GET /api/serial-monitor` builds the whole JSON dump synchronously in that task; payload (≤500 messages + rs232 + `received_data`) escaped char-by-char is written into an `AsyncResponseStream` whose cbuf does `resize` (O(n) copy) per overflowing write → O(n²), so a large first poll takes >5 s with no `esp_task_wdt_reset()` → reboot. **Fixed:** (a) `esp_task_wdt_reset()` every 32 messages / 512 raw bytes inside `sharedWebStreamSerialMonitorJson` loops (`web_shared_logic.cpp`), (b) `beginResponseStream(..., 16384)` in `monitor_buffer_handler.cpp` so the stream rarely overflows. NOTE (not addressed): char-by-char escape writing still makes a big dump CPU-heavy, see MON-3. | `web_shared_logic.cpp:929,1008`, `monitor_buffer_handler.cpp:57` | fixed |

---

## Commands (`commands.md` → `/commands` + `/commands-by-datetime`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| CMD-1 | Medium | Both pages create exactly 10 rows, `MAX=20`, no add-row control — **fixed:** interval `MAX=30`, datetime `MAX=16`; ➕ Add Command + per-row 🗑️ remove. | `page_commands_asset.cpp` (`init`), `page_commands_datetime_asset.cpp` (`init`) | fixed |
| CMD-2 | Medium | `/api/scheduled-datetime-slots` serialises only **8** slots while GET/POST accept 1..16 — bulk endpoint under-reports. | `commands_handler.cpp:51` | open |
| CMD-3 | Low | HTML fallbacks hardcoded PT (`Ativo`, `Dias`, `Unid`, `Reinício diário`, …) + `Content-Language: pt` on both routes — **fixed:** English fallbacks; headers removed. | `page_commands_asset.cpp`, `page_commands_datetime_asset.cpp`, `web_routes_commands.cpp:37,59` | fixed |
| CMD-4 | Low | "Save All" non-atomic sequential POSTs (160/180 ms) — partial save on mid-loop failure with no error surfaced. | `page_commands_asset.cpp:120`, `page_commands_datetime_asset.cpp:21` | open |
| CMD-5 | Low | Daily-reboot also writes legacy NVS `cfg_drbt` — duplicated persistence path that can drift from `/config/schedule.json`. | `commands_handler.cpp:257-274` | open |
| CMD-6 | Info | `getParamInt` treats missing interval as 0; enabled rows without interval get 400 — inconsistent but arguably correct. | `commands_handler.cpp:343-364` | open |
| CMD-7 | High | **One-shot datetime slots fire TWICE (live .29, 2026-10-14):** `SYS:EMAILDAY`@12:05 → archives 12:05 + 12:06; `SYS:LOGDAY`@12:11 → archives 12:11 + 12:13. Non-daily clear (`clearSchedDtSlot` + `clearSchedDtLastRunDay`) does not hold — slot still `enabled:true` afterwards, `now >= slotEpoch` stays true and it refires next minute. Duplicate emails per one-shot. **Plus:** LOGDAY entries retried at 12:33/12:53/13:13 (20-min journal) AND landed in inbox → sends deliver but the SENT receipt fails (`dayReportEndAttempt` never sees success?) so every LOGDAY is re-sent 3×. EMAILDAY same window did not retry (receipt ok). Slot 8 disabled remotely to stop new fires; journal self-exhausts (3 attempts). **Fix applied (por validar em build + live):** synchronous `saveSection("schedule")` + RAM day-guard on one-shot fire (SYS + RS232 paths); funnel-level 3-min same-(folder,subject) resend suppress (`email_day_dedupe.h`, 6 testes nativos); duplicate PENDING journal re-fires keep the original retry clock. | `runtime_scheduler.cpp`, `runtime_email.cpp`, `email_day_dedupe.h`, `day_report_fallback.cpp` | fixed |

---

## Files (`files.md` → `/files`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| FIL-1 | Medium | All fallback text hardcoded PT (`Explorador de Ficheiros`, `Apagar`, `Selecionar`, `PÁGINA`, dialogs `Cancelar`/`Confirmar`/`OK`, 503 pages) + `doc.lang='pt'` — **fixed:** all English fallbacks, `lang='en'`, 503 pages translated. | `files_page_handler.cpp` | fixed |
| FIL-2 | Low | `/api/sd/download` guarded only client-side (2 s in-progress flag); no server-side single-download guard. | `files_page_handler.cpp:721-729` | open |
| FIL-3 | Low | `sortByType()` (client reorder) competes with URL `sort=name|date` server sort — two behaviours. | `files_page_handler.cpp:382` | open |
| FIL-4 | Low | Keyboard handler uses arrow functions (`e=>`), diverging from the no-arrow project rule. | `files_page_handler.cpp:825,833` | open |
| FIL-5 | Low | PSRAM selector shows `(não disponível)` when VFS disabled but stays clickable → dead option. | `files_page_handler.cpp:462-467` | open |
| FIL-6 | Info | Move/rename/create use hidden-form POST (page navigates) vs delete uses JSON+reload — inconsistent UX. | `files_page_handler.cpp:839-840` | open |

---

## GRBL Controller (`grbl_controller.md` → `/grbl-controller`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| GRBL-1 | Medium | Mojibake in hardcoded fallbacks: `'âœ…'` (✅), `'â€œ'` (❌) visible when i18n key missing. | `assets/grbl/grbl_actions.h:250,276,437,447,458,461`, `grbl_light.h:390` | open |
| GRBL-2 | Low | `\u{…}` escapes in HTML render literally until `data-i18n`; rotate-warning emoji (line 4 of `grbl_html_shell.h`) has no `data-i18n` and always shows raw escape. | `assets/grbl/grbl_html_shell.h` | open |
| GRBL-3 | Low | 28 i18n keys missing from both `lang_en_ui.h`/`lang_pt_ui.h` (`grbl2d*`, `grblHeightMap*`, `grblPreview*`, `grblSdLoadWorkspace`, `grblToolChangeLbl`, `grblUploadFile`, `grblViewMode*`, `grblTip*`, …). | `languages/lang_*_ui.h` | open |
| GRBL-4 | Low | Dead DOM refs / orphaned controls: `grbl_poll_ms`, `btn_apply_poll`+`applyPollInterval()`, `grbl_step`, `grbl_sent`/`grbl_received`, `btn_backplot_toggle`, `btn_3d_proj`/`btn_3d_motion`, `grbl_local_file`. | `assets/grbl/grbl_status.h`, `grbl_actions.h`, `grbl_ui.h`, `grbl_sd.h` | open |
| GRBL-5 | Low | Registered-but-unused APIs: `/api/grbl/queue-start`, `queue-stop`, `events` (SSE), `auto-detect-baud`, `probe-grid` GET, `height-map` GET, `debug`. | `web_routes_monitor.cpp:330-364` | open |
| GRBL-6 | Low | `setCommandMode('psram-stream')` not in mode map (`manual|queue-local|sd-stream|runtime`) → badge stays MANUAL during PSRAM stream. | `assets/grbl/grbl_ui.h:470-478`, `grbl_actions.h:209` | open |
| GRBL-7 | Low | `poll-interval` endpoint orphaned — no `grbl_poll_ms` input exists. | `grbl_web_api.cpp:1433-1441` | open |
| GRBL-8 | Info | Non-CNC packs with `ENABLE_GRBL` serve the page but every `/api/grbl/*` returns 404 (route block pack-gated). | `web_routes_monitor.cpp:44` | open |

---

## Radio UI (`radio_ui.md` → `/radio-ui`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| RAD-1 | Medium | Hardcoded PT: shell `loading` overlay (`A processar...`), file-browser bridge ("UI Rádio", "Erro", "falha", "Selecione ficheiros .mp3", "Sem fila ativa"), `/api/radio/random` API-ref row. **Fixed:** all English. | `page_render_common.h`, `ethernet_bootstrap_handlers.h:1645,1790`, `page_api_reference_asset.h:299` | fixed |
| RAD-2 | Medium | Duplicated station lists: page hardcodes `STATIONS` (PT), server `/api/radio/stations/default` differs and is never called → dead endpoint + data drift. | `page_radio_ui_asset.cpp:693`, `radio_audio_runtime.cpp:1723-1756` | open |
| RAD-3 | Low | Dead endpoints: `/api/radio/toggle`, `/random`, `/i2s-poc`, `/mp3-test`, `/stations/tags|countries|default`, GET `/api/radio/eq` — no UI caller. | `web_routes_audio.cpp` | open |
| RAD-4 | Low | Dead Ethernet handlers `ethernetHandleRadio*/Upnp*` never dispatched (RouteRegistry fallback used); target pack has `ENABLE_ETHERNET 0`. | `ethernet_bootstrap_handlers.h:2252-2478` | open |
| RAD-5 | Low | Route-registry legacy table omits many radio paths → wrong 405-vs-404. | `route_registry_api.cpp:99+` | open |
| RAD-6 | Medium | Volume/shuffle/repeat/seek POSTs swallow errors (`.catch(function(){})`); seeking a stream returns 400 with no UI feedback. | `page_radio_ui_asset.cpp:646,653,655,669` | open |
| RAD-7 | Low | Format gate `chkSup()` only on Discover; manual URL/stations/recents bypass it (`.m4b` playable server-side but rejected by page whitelist). | `page_radio_ui_asset.cpp:376,772` | open |
| RAD-8 | Low | Queue GET caps at 120 vs POST 256 → page can show a shorter queue than real. | `radio_audio_runtime.cpp:2470-2477` | open |

---

## Dual File Manager (`dualfile.md` → `/dualfile`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| DF-1 | Medium | `/api/dualfile/list` on PSRAM returns "PSRAM VFS listing not supported" but the UI offers/cycles a PSRAM storage option — dead/broken option. | `dualfile_handler.cpp:140-151`, `page_dualfile_asset.h:112` | open |
| DF-2 | Medium | Native `prompt()`/`confirm()` used (rename/mkdir/delete/wildcard) — violates `customAlert`/`customConfirm` project rule; page's own overlay `customConfirm` is unused. | `page_dualfile_asset.h:204,559,575,594` | open |
| DF-3 | Low | Page `lang="pt"` but all UI text hardcoded EN; no `data-i18n` beyond tooltips — **fixed:** `lang="en"`. | `page_dualfile_asset.h` | fixed |
| DF-4 | Low | Copy limited to 8 MB with no UI hint — >8 MB files fail per-file silently. | `dualfile_handler.cpp:366` | open |
| DF-5 | Info | `customConfirm` defined but delete uses native `confirm()` path; overlay dialog effectively dead. | `page_dualfile_asset.h:204,594` | open |

---

## Proximity Dashboard (`proximity_dashboard.md` → `/proximity-dashboard`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| PD-1 | Low | `loadHistoricalData` buckets only the current day (skips `dt.getDate() !== curDay`) — after midnight/reboot earlier hours often empty. **fixed 2026-09-22:** merges previous-date file (from boundary hour) with aligned-date file into production-day columns. | `page_proximity_dashboard_asset.h:952-1055` | fixed |
| PD-2 | Low | Hourly chart derives buckets from `count_in` deltas only — Count OUT not charted. | `page_proximity_dashboard_asset.h:263-293,323-331` | open |
| PD-3 | Info | ETA uses `count_in` vs goal + ppm — approximate by design; undocumented. | `page_proximity_dashboard_asset.h:223-227` | open |
| PD-4 | Info | No pause/filter controls — auto-active channels only. | `page_proximity_dashboard_asset.h` | open |
| PD-5 | Medium | `loadHistoricalData` fetched both production-day segments with a parallel `Promise.all`; `/api/proximity-chart-data` is 429-rate-limited (~1 req/s, `HeavyApiRequestGuard`), so one request failed and its segment was silently dropped — some hourly columns stayed empty. **fixed 2026-09-23:** sequential `fetchChartSegments()`/`fetchChartSegmentOnce()` fetch one URL at a time and retry on 429/503 with ~1.1 s backoff (up to 3 tries; persists `null` = graceful skip). | `page_proximity_dashboard_asset.h` (`fetchChartSegments`/`fetchChartSegmentOnce`), `heavy_api_request_guard.h` | fixed |

## Proximity Sensors (`proximity_sensors.md` → `/proximity`, shift-end emails)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| PX-1 | High | **Shift-end email totals truncated after mid-shift reboot (23-09-2026, Equip15 14H–22H: 28 510 pcs / OEE 20% instead of 106 580 / ~74%).** Live counters restore from flash but RAM-only shift baselines were re-captured at boot, dropping all pre-reboot production. **fixed 2026-09-23 (A):** shift baselines persisted (`/config/sensor_prox_shiftbase.json` + counter co-save); first detection after boot restores them when slot/day/shift match and counters ≥ bases, else fresh capture. | `proximity_manager.cpp` (`saveProximityShiftBaselines`, `_proxRestoreShiftBaselines`), `test_proximity_shiftbase.cpp` | fixed |
| PX-2 | High | **Same email: 22H chart column showed 19 850 (7 660 flush + 12 190 from the previous evening).** Chart columns were `[start+1, end]`, dropping the first shift hour and crediting the flush to a fictional end-hour column that also collected the previous day's tail. **fixed 2026-09-23 (C):** columns are the actual shift hours `[start, end)`; negative tick delta (counter reset) counts `ch0` as production-since-reset instead of 0. | `proximity_manager.cpp` (`proximityGetShiftWindow`, `_proxReadHourlyCounts`), `test_shift_window.cpp`, `test_proximity_chart.cpp` | fixed |
| PX-3 | Medium | **Silent truncation had no diagnostic.** **fixed 2026-09-23 (D):** shift-end cross-checks live delta vs CSV hourly sum (`_proxWriteWeeklyOee()` out-param); CSV higher by >10%+500 pcs ⇒ WARN on Serial/monitor + `NOTE:` line in the email body. | `proximity_manager.cpp` (shift-end block) | fixed |

---

## Status (`status.md` → `/status`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| ST-1 | Medium | Section headers/module lines hardcoded English inside `M`/`L` pushes — **fixed:** 22 section headers routed through `_st` with `stSec*` i18n keys (EN+PT added). | `status_page_renderer.cpp` (`L.push`/`M.push`), `lang_*_ui.h` | fixed |
| ST-2 | Low | `opMap` in monitoring render doesn't match the real operator set (`I/D/+/−/G/L/R/DR/V/C/P/Cw/Hy`) — labels can be wrong. | `status_page_renderer.cpp` | open |
| ST-3 | Low | Rapid refresh can hit the 500 ms 429; the page renders the `{error}` body as data instead of showing stale/graceful state. | `status_page_renderer.cpp` (`u()`), `core_admin_handler.cpp:31` | open |
| ST-4 | Info | Single ~12 KB JSON rebuilt per request — no ETag/caching on the payload. | `core_admin_handler.cpp:38` | open |

---

## Wi-Fi (`wifi.md` → `/wifi`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| WIFI-1 | Medium | Inline JS hardcodes PT fallbacks (`'✅ Configuração guardada!'`, `'❌ Erro ao guardar'`, `'Erro: '`) — violates EN-fallback rule. | `page_wifi_asset.cpp` | open |
| WIFI-2 | Low | Arrow functions throughout (`r=>r.json()`, `.catch(()=>{})`) — violates no-arrow rule. | `page_wifi_asset.cpp` | open |
| WIFI-3 | Low | Scan rows interpolate raw SSIDs into inline `onclick="sel('${n.ssid}',…)"` — quotes/backslashes break handler or inject attributes (SSID is attacker-controlled in range). | `page_wifi_asset.cpp` (scan) | open |
| WIFI-4 | Info | `/api/wifi-scan` synchronous scan blocks async_tcp; auto-scans on every page load. | `network_handler.cpp:97`, `web_routes_network.cpp:62` | open |
| WIFI-5 | Info | Scan results capped top-10 server-side with no UI hint; saving a new SSID drops the browser session mid-request (success alert often unseen). | `handleWifiScanGet`, page JS | open |

---

## Email (`email.md` → `/email`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| EMAIL-1 | Medium | Entire HTML fallback body is Portuguese (entities like `In&iacute;cio`, `Servidor SMTP`, `Guardar`) — EN-fallback violation for raw PROGMEM text. | `page_email_asset.cpp` | open |
| EMAIL-2 | Medium | `/api/email-test` messages hardcoded PT server-side (`⚠️ Teste bloqueado…`, `⏱️ Aguarde N segundos…`, `❌ Configuração incompleta`, 503 `Transporte desativado em compilação`). | `email_handler.cpp:20-34` | open |
| EMAIL-3 | Low | Inline JS fallbacks PT too (`et('emailTestStarted','Teste iniciado com sucesso!')`, `'✅ Configuração guardada!'`). | `page_email_asset.cpp` | open |
| EMAIL-4 | Info | `smtp_tls_insecure` defaults to **checked** (insecure allowed) when unset. | `page_email_asset.cpp` (loadEmailConfig) | open |
| EMAIL-5 | Low | Port field unvalidated both sides (`v.toInt()` accepts 0/99999 silently). | `config_handler.cpp` (section=email), asset | open |
| EMAIL-6 | Low | `email_destination` tooltip says *"Sender email address"* but field is the destination/recipient. | asset, `emailTipSender` key | open |

---

## Ethernet (`ethernet.md` → `/ethernet`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| ETH-1 | Medium | `/api/wifi-disable` has two divergent implementations: WiFi AsyncWebServer timer-based radio stop vs unified persist-only registry path — behavior differs per transport. | `web_routes_network.cpp:151`, `network_handler.cpp`, `ethernet_bootstrap_handlers.h:491` | open |
| ETH-2 | Low | Arrow functions in poll/save handlers — violates no-arrow rule. | `page_ethernet_asset.cpp` | open |
| ETH-3 | Low | Server error strings PT (`{status:"erro", msg:"modo_invalido"/"ipv4_invalido"/"mac_invalido"}` + wifi disable/enable messages) — EN-fallback violation. | `web_shared_logic.cpp:386+`, `network_handler.cpp` | open |
| ETH-4 | Info | Polling runs forever (6 s / 8 s) even when tab hidden; MAC override stored verbatim without normalization. | `page_ethernet_asset.cpp` | open |

---

## RS232 (`rs232.md` → `/rs232`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| RS232-1 | Medium | WiFi route not feature-gated (served even with `ENABLE_RS232==0`; Ethernet mirror is gated) and sends `Content-Language: pt` header despite `lang='en'`. | `web_routes_config.cpp:162-176` | open |
| RS232-2 | Low | Widespread arrow functions + `async/await` in inline JS — violates no-arrow rule. | `page_rs232_asset.cpp` | open |
| RS232-3 | Low | Tab labels hardcoded PT `['Config','Avancado']` (also misspelled, missing ç). | `page_rs232_asset.cpp` (tab IIFE) | open |
| RS232-4 | Low | Delete flow fires N parallel DELETEs without awaiting; each response re-renders list + pops its own alert (stacked dialogs). | `page_rs232_asset.cpp` (delete-template) | open |
| RS232-5 | Low | `read_timeout`: client enforces 100–10000 ms but server stores any int via `toInt()`. | `config_handler.cpp`, asset | open |
| RS232-6 | Low | `rs232StatusSniffer` i18n key missing from both language packs — sniffer status always English. | `languages/lang_en_ui.h`, `lang_pt_ui.h` | open |
| RS232-7 | Info | Baud persisted as index into server `baudrate_list` — order change silently remaps rates; template names injected into innerHTML unescaped. | `page_rs232_asset.cpp` | open |

---

## Modbus (`modbus.md` → `/modbus-rtu`, `/modbus-tcp`, `/modbus-live`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| MODBUS-1 | High | TCP register types corrupted on save: page sends `type`/`order` as strings (`"FLOAT32"`, `"ABCD"`); handler coerces `r["type"] \| (uint8_t)0` → always 0; runtime parses uint8. Any save from `/modbus-tcp` collapses all registers to UINT16/ABCD. | `page_modbus_asset.cpp:514-525`, `modbus_handler.cpp:271-272`, `modbus_runtime.cpp:56` | open |
| MODBUS-2 | Medium | `POST /api/modbus/config` accepts unbounded devices/registers; runtime clamps at 4/16 by silently dropping extras. | `modbus_handler.cpp` (handleModbusConfigPost), `modbus_runtime.cpp:32,60` | open |
| MODBUS-3 | Medium | Hardcoded PT across HTML fallbacks, alerts/confirms and tab labels; tab engine matches h2 text (`'configuração global'`…) so translating headers breaks tabs. | `page_modbus_asset.cpp`, `MODBUS_RTU_TABS_JS`/`MODBUS_TCP_TABS_JS` | open |
| MODBUS-4 | Low | Live endpoint formats with fixed 4 decimals regardless of `pretty` setting/register type. | live JSON build, `page_modbus_asset.cpp` | open |
| MODBUS-5 | Info | Write dialog falls through silently if `customConfirm` undefined (dead branch); scan results capped 32 without UI notice; scan tasks pinned core 0 with modest stacks. | `page_modbus_asset.cpp`, `web_routes_modbus.cpp:14-91` | open |

---

<!-- New sections appended per audited page: `## <Page name> (`<page_file>.md`)` -->

## Camera (`camera.md` → `/camera`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| CAM-1 | High | MJPEG phase-0 took the frame semaphore up to 1 s inside the AsyncTCP task — whole web UI froze while streaming. **Fixed:** non-blocking take + `RESPONSE_TRY_AGAIN`. | `web_routes_camera.cpp:25` | fixed |
| CAM-2 | High | Unlimited parallel MJPEG streams exhausted Async clients/heap. **Fixed:** single-viewer 503 + `cameraStreamStop()` on last disconnect. | `web_routes_camera.cpp:80` | fixed |
| CAM-3 | High | Photo/snapshot/record each did their own `fb_get` racing loop pre-capture (`fb_count=1`). **Fixed:** cache-first photo/snapshot + recorder fed from stream cache. | `camera_handler.cpp`, `camera_manager.cpp` | fixed |
| CAM-4 | High | `cfg()` read `$('chkAec2')` but the element did not exist → TypeError, no camera config ever saved. **Fixed:** checkbox added + null-guard + `loadCfg` hydration. | `page_camera_asset.cpp` | fixed |
| CAM-5 | Medium | Photo SD lock 8 s stalled the HTTP task. **Fixed:** 2 s fail-fast + `SD busy`. | `camera_handler.cpp` | fixed |
| CAM-6 | Medium | `timelapse_secs`/`aec2` persisted with no UI; no recording Stop; photo filename discarded; SD free ignored. **Fixed:** timelapse slider, Stop button, `photoInfo`/`sdInfo` readouts. | `page_camera_asset.cpp` | fixed |
| CAM-7 | Low | Motion ROI (`roi_x/y/w/h`) persists + API but has no UI inputs. | `page_camera_asset.cpp`, `camera_motion.h:50` | open |
| CAM-8 | High | Motion grayscale window raced photo/snapshot/record captures → gray thumbnail persisted as JPEG. **Fixed:** `cameraMotionSamplingActive()` guard (photo/snapshot wait ≤400 ms, record/timelapse skip). | `camera_motion.cpp`, `camera_record.cpp`, `camera_handler.cpp` | fixed |
| CAM-9 | High | Motion post-timeout finalised manual/dashcam recordings. **Fixed:** `s_ownRecording` ownership flag. | `camera_motion.cpp` | fixed |
| CAM-10 | Medium | Idle motion sample ~1.2 s + confirm 3 → ~3.6 s trigger latency. **Fixed:** idle skip 9→4 (~1.8 s). | `camera_motion.cpp` | fixed |
| CAM-11 | Medium | Direct-API POSTs unclamped (uint8 wrap on quality etc.). **Fixed:** clamp table on all config fields + framesize enum guard + `cameraClampFps()` in recording start. | `camera_handler.cpp`, `camera_manager.cpp` | fixed |
| CAM-12 | Low | `cameraMotionBuildDiffJpeg` dead code; `/cam/motion/diff` documented but missing. **Fixed:** wired route (WiFi + Ethernet) + Mot-tab Diff link. | `camera_handler.cpp`, `web_routes_camera.cpp`, `ethernet_*` | fixed |
| CAM-13 | Medium | `streamFps` reads 0 while frames flow (rolling window). **Fixed:** monotonic `streamFrames` in status + LIVE badge counter + curl increase-check. | `camera_manager.cpp`, `camera_handler.cpp`, asset, `test_real_cam.sh` | fixed |
| CAM-14 | Low | Dark theme, stretched Start/Stop, clipped tab/preset labels. **Fixed:** blue compact theme, fixed-size buttons, 2×2 full-label tabs. | `page_camera_asset.cpp` | fixed |
| CAM-15 | High | `cam_task` LoadProhibited (double `esp_camera_init` from boot auto-start vs page Start on different cores). **Fixed:** `s_camMux` spinlock + 200 ms fallback settle. | `camera_manager.cpp` | fixed |
| CAM-16 | High | `cameraStop()` deinit raced in-flight captures. **Fixed:** bounded init wait + ready-first + 400 ms grace + cond deinit; `test_camera_initguard`. | `camera_manager.cpp` | fixed |
| CAM-17 | Low | Layout restyle MJPEG2SD-inspired (rail + panels + statusbar), blue palette, original code only. **Fixed:** + footer fields + i18n panel keys. | `page_camera_asset.cpp` | fixed |
| CAM-18 | Medium | No motion event counter / best-frame / lamp-auto / ROI UI. **Fixed:** persistent counter + cooldown + `motionEvents` + reset endpoint + falling-edge best JPEG + lamp hold/restore + ROI sliders. | `camera_motion.*`, handler, asset | fixed |
| CAM-19 | Medium | Timelapse loose JPEGs; filenames without duration/origin. **Fixed:** `_T` AVI + `_<DUR>s_<TAG>` rename. | `camera_record.cpp` | fixed |
| CAM-20 | High | Stream froze after ~7 frames (TRY_AGAIN deadlock). **Fixed:** bounded wait + keep-alive resend. | `web_routes_camera.cpp` | fixed |
| CAM-21 | High | Manual recordings killed by tick; idle sessions recorded 0 frames. **Fixed:** dash ownership + generic idle pump. | `camera_record.cpp` | fixed |
| CAM-22 | Low | Stop returned stale path; viewer churn invisible. **Fixed:** post-finish path + `streamClients` + connect/disconnect logs. | handler, routes | fixed |
| CAM-23 | High | Camera options lost on reboot. RCA: JSON doc 896 B overflowed with 37-key schema → truncated save, failed load. **Fixed:** 1536 B + overflowed() guard. | `camera_config.cpp` | fixed |
| CAM-24 | High | Pack Cam-P4 wiring done but P4 build blocked: no upstream P4 camera driver (esp32-camera master has no P4/CSI; Arduino core ships none). **Fixed:** P4 backend on Arduino ESP_Video + prebuilt usb_host_uvc; `esp32p4:CAM_P4` links (ESP_Video @ 3.3.11 in graph). **Pending** HW validation on desk. | pack + scripts + backend | open |
| CAM-25 | High | P4 ESPVideo backend written (uncompiled, no HW here): MIPI CSI OV5647 → shared pump via fb shim; motion/diff off on P4 v1. **Fixed:** compiles+links on 55.03.311; include guards use plain board checks (LDF skips `__has_include`). **Pending** HW checklist. | backend + manager/motion branches | open |
| CAM-26 | Medium | P4 USB-UVC source written (uncompiled): prebuilt `usb_host_uvc` + MJPEG negotiation + PSRAM slot; `source` persisted/UI/status; pack USB_HOST on. **Fixed:** compiles (headers+archive in P4 framework). **Pending** USB webcam on desk (no hot-plug v1, MJPEG-only). | backend + config/UI | open |
| CAM-13 | High | `POST /api/camera/config` with a JSON body always returned "No changes" (form-only parser) — blocked API/test config writes. **Fixed:** JSON body support (flat keys + nested `motion{}`, bools/numbers normalised into the shared clamp table); malformed JSON → clear error. | `camera_handler.cpp` | fixed |
| CAM-27 | Medium | Second PC needed the full control page just to watch. **Fixed:** `/cam/raw` zero-chrome fullscreen page + Raw toolbar button (EN+PT); max viewers 1→2 (shared cache, private pump state each). | routes, asset, i18n | fixed |
| CAM-50 | High | Unchecking motion (or zeroing dashcam/timelapse) never stuck with matching `auto_mode` set: `cameraArmAutoMode()` re-enabled the engine on every config apply. **Fixed:** explicit engine-off demotes `auto_mode` 3/2/4→0 in the same apply (explicitly reselected mode still wins). | `camera_handler.cpp` | fixed |
| CAM-14 | High | Silent S3 freeze ~1 s after `[CAM] Camera init OK` (no panic, RTS-reset insufficient, power-cycle-only). RCA: ESP32-S3 dual-core + continuous LCD_CAM/GDMA JPEG capture into PSRAM under Wi-Fi STA — matches community reports (works unicore; Espressif mitigation `LCD_CAM_ISR_IRAM_SAFE=n` in the prebuilt framework camera lib, not settable from this repo). **Mitigations shipped:** XCLK 20→10 MHz (halved burst rate), `cameraStop()` now calls `esp_camera_deinit()` (parks capture engine between sessions). Needs HW validation; definitive candidates: rebuild framework camera with IRAM-safe ISR or unicore build. | `camera_manager.cpp` | open — awaiting HW test |
| CAM-59 | High | **USB webcam 1b3f:1167 accepts every UVC negotiation and transmits nothing.** HW-proven with a raw isochronous probe (own claim + hand-rolled VS_PROBE/VS_COMMIT + own transfers, `P4_USB_RAW_ISOC_PROBE=1`): 3644 isoch packets delivered, all 0 bytes, across a 3x3 format/frame sweep on the widest high-bandwidth alternate (alt 7, 0x85, 3072 B/us). P4 host, alt claim, probe/commit and isoch reception are healthy, so this is the camera unit, not the Node32-HUB backend. **Next:** plug a plain 640x480/720p MJPEG UVC camera (e.g. Logitech C980) and qualify it in one Start — the diagnostics (bus scan, 24-frame list, descriptor dump, smallest-first MJPEG candidates) are kept in for exactly that. If it also fails, the unit is the P4 board's USB HS isoch path and the fallback is a CSI/MIPI sensor. | `camera_p4_backend.cpp` (`p4UsbRawIsocProbe`, `p4UsbScanBus`, `p4UsbHarvestFrames`) | open |

| CAM-61 | High | P4 MIPI FHD camera start panics in `CamInit` (`Cause=0x5`, `Addr=0x20`; PC resolves to `csi_get_input_bayer_order`). RCA: `p4SetSensorFormat()` passed stack-local `esp_cam_sensor_format_t` and `isp_info`, while `esp_cam_sensor_set_format()` retains the format pointer and CSI dereferences it later. **Fixed in source:** persistent static format descriptors + ISP metadata; also config failure rollback/GET hydration and live AVI dimensions. Added an experimental 1280x720 RAW10 binned/cropped table at target 30 fps so photos/AVI can use the shared active mode. Not HW verified. Native resolution tests 5/5 (48 assertions) + web camera tests 88/88 pass. | backend, OV5647 settings, config/manager/handlers/assets, recording/motion | fixed in source — owner build + OTA + verify 1080p + 720p photo/stream/AVI |
| CAM-62 | High | **HW 2026-09-29 (FW v1.115 build 22:21:39 WET): 720p MIPI starts but snapshot wedges HTTP → TASK_WDT reboot.** Start at 1280x720 reported `ready:true`, OV5647 (custom S_FMT + persistent-descriptor fix HW-confirmed; prime capture got ≥1 JPEG). First `/cam/snapshot` never answered, board rebooted (`reset_reason=TASK_WDT`). Same FW at 800x800: start + valid 800x800 JPEG + photo all OK, so the fault is 720p frame delivery stalling with `captureBuffer()` blocking forever in the HTTP task. **Fixed in source:** 1.5 s driver DQBUF timeout (`VIDIOC_S_DQBUF_TIMEOUT`, when this core supports it) + dequeue-heartbeat fast-fail (4 s stall / 3 s retry) + loop-task session re-sync (max 1/10 s) + `capStall` in status + `[NO FRAMES]` page pill. Native guard tests 21/21 (4 new cases), web camera 91/91 (3 new pill tests). | `camera_p4_backend.cpp/.h`, `camera_capture_guard.h`, `camera_manager.cpp`, camera status handler/asset | fixed in source — owner build + OTA + HW retest 720p snapshot |
| CAM-63 | High | **Live view dies after minutes on `/camera` and `/cam/raw`; only a full refresh revives it.** RCA candidates: (1) page `poll()` killed the img on a SINGLE transient `ready:false` (`R=false`, no retry); (2) dead TCP without FIN leaks the 2 MJPEG slots so every in-page retry 503s forever (only page-unload FINs recover). **Fixed in source:** page stops the img only after 3 consecutive not-ready polls + auto `strStart()` on later ready; `streamFrames` freeze watchdog (3 polls) reconnects in-page on both pages (raw: 10 s poll); server evicts zombie viewer slots after 20 s without bytes (live responses untouched). Web camera tests 96/96 (5 new self-healing cases). Device .18 was unreachable (no ping) during implementation — owner build + OTA + soak test required. | stream route, camera page/raw assets | fixed in source — owner build + OTA + multi-minute soak test |
| CAM-67 | High | **ROOT CAUSE of the minutes-scale stream deaths (HW-proven 2026-10-02 on .9 S3): `AsyncClient` ACK timeout 5 s aborts actively-transferring MJPEG streams on any WiFi TX stall.** Evidence: Pi probe `GET /cam/stream` died at 168 s with 4.3 MB flowing (http 200, clean exit — server closed it); ESP WiFi healthy (-37 dBm, 0 reconnects, modem-sleep already off), pump/heap healthy, no reboot. 2.4 GHz TX stalls >5 s are routine interference; each abort looks like "image disappeared, only refresh revives". **Fixed in source:** `setAckTimeout(30000)` on `/cam/stream` + `/cam/playback` viewers; dead clients still reaped by the 20 s zombie eviction. Native stream tests 22/22 (2 new policy cases). | stream route | fixed in source — owner build + OTA + >10 min soak to confirm |
| CAM-64 | Medium | **Snapshot/photo 503s on EVERY attempt while streaming (HW .9, S3 OV2640 VGA, 2–3 fps effective).** `cameraGrabFileFrame()` demands a *fresh* published frame within 600 ms, but a slow viewer link keeps the shared cache pinned so the pump rarely publishes — the wait always loses the race. **Fixed in source:** prefer fresh, fall back to the latest cached JPEG younger than 5 s (`PHOTO_CACHE_MAX_AGE_MS`, publish timestamp on the shared cache, cleared on stop); older/never-published still fails honestly. Pure decision in `camera_capture_guard.h`, 5 new native cases (22/22, 94 assertions). | `camera_manager.cpp`, `camera_capture_guard.h` | fixed in source — owner build + OTA + snapshot-while-streaming HW check |
| CAM-65 | Low | **IDEA (from camthink-ai/lowpower_camera review, 2026-10-02): photo push via webhook/MQTT on alerts.** Their push dispatcher (queue + retry + RTC counters, MQTT-or-webhook JSON payload) is a capability we lack — our alerts are email-only. Would need a new endpoints/config page + queue worker; patterns only, NO code reuse (their Community license forbids commercial use on non-CamThink HW). | new page + worker (tbd) | idea — needs spec + owner go-ahead |
| CAM-66 | Low | **IDEA (from camthink-ai/lowpower_camera review, 2026-10-02): live UVC controls (brightness/contrast/saturation/AE) for USB webcams.** Only makes sense AFTER a transmitting MJPEG webcam is qualified (current 1b3f:1167 unit is TX-silent, CAM-59). Patterns only, NO code reuse (license, see CAM-65). | P4 UVC backend + camera page | idea — blocked on CAM-59 |
| CAM-68 | High | **P4 presets pushed S3 values: `preset()` posted only `framesize` enums (HD/Safe `fr:13` = 1280×720) + fps up to 25.** On Cam-P4 the enum is ignored for capture size (MIPI follows `p4_res_w/h`, USB follows negotiation) and fpsMax is 6 — so HD/Safe targeted the experimental 720p profile that stalls the pipeline (CAM-62, HW-proven on .18: `capStall:1`, snapshot 503) while claiming "tested stable". **Fixed in source:** per-sensor presets — OV5647 (RPi v1.3) posts `resolution=WxH` (HD 1920×1080, VGA/Fast/Eco 800×640, Night/Safe 800×800 verified; never 720p; fps ≤6) and USB-UVC posts quality/fps/picture only; tooltips follow the sensor; unknown/pre-start models keep the legacy path. Native resolution tests 7/7 (2 new contract cases), web camera tests 109/109 (13 new P4 cases). | camera page asset, `test_camera_resolution.cpp`, `test_camera.js`, `PAGES/camera.md` | fixed in source — owner build + OTA + click each preset on OV5647 + USB and confirm res/fps/picture |
| CAM-69 | High | **HW 2026-10-06 (.18, OV5647 RPi v1.3): all six MIPI modes stream + snapshot.** Stop/set/start per mode + 6 s `/cam/stream` probe: 800×800 33fr (~5.5fps, 41KB), 800×640 34fr, 800×1280 34fr, 1280×960 54fr (~9fps, 178KB), 1920×1080 21fr (~3.5fps, 658KB), 1280×720 22fr (~3.7fps) — incl. the 720p profile that wedged HTTP pre-CAM-62-guard (now snapshot 200 in 0.04 s, no reboot). Frames near-black (mean ~10–16/255): room is dark, sensor/AEC healthy. Device restored to 800×800. | none (live HW check only) | done — 1080p/720p photo/stream/AVI still want an OTA-fresh retest after next build |

## Digital Outputs (`do.md` → `/do`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| DO-1 | High | Ethernet GET APIs missing: `GET /api/do/status` + `GET /api/do/config` registered only on WiFi, so over W5500 the page shows stale OFF/empty labels. | `web_routes_do.cpp:38-39`, `ethernet_bootstrap_routes.h:768-771` | open |

## HX711 (`hx711.md` → `/hx711`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| HX-2 | High | Ethernet API surface incomplete: only `POST /api/hx711-config` wired; no GET config, measure or tare routes, so the page cannot load or act over Ethernet. | `ethernet_bootstrap_routes.h:683-688`, `ethernet_bootstrap_handlers.h:2113-2141` | open |

## LPT / DB25 (`lpt_db25.md` → `/lpt-db25`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| LPT-1 | High | Page can never load its own settings: `GET /api/config?section=system` omits every key the page reads, so all controls show HTML defaults. | `page_lpt_db25_asset.cpp:62-73`, `config_handler.cpp:184-189` | open |

## Google Script (`google_script.md` → `/google-script`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| GS-1 | High | Saved GAS URL never pre-fills: JS reads `d.google_email.google_script_url` but the API returns the URL at `smtp.google_script_url`, so the field renders blank even after saving. | `page_google_script_asset.cpp:94,97`, `config_handler.cpp:182` | open |

## Structures (`structures.md` → `/structures`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| STR-1 | High | Page loads from an endpoint that never returns its data: `GET /api/config?section=system` omits `structures`/`saved_configs`, so the form always shows defaults and diagnostics always report Mode "Manual". | `page_structures_asset.cpp:262-285`, `config_handler.cpp:184` | open |

## Sensor Charts (`sensor_charts.md` → `/sensor-charts`)

| ID | Pri | Issue | Files | Status |
|---|---|---|---|---|
| SC-7 | High | **Day preset missed hours 22–23 of the previous production day.** Day views for RS232/Modbus/Proximity anchored at calendar midnight (`?from=<midnight>`), so with a shift boundary (e.g. 22:00) the 22–23 block of the previous day's file was never fetched; backends correctly ignore `_now` (cache-buster only). Fixed client-side: `ensureDayBoundary()` fetches `/api/proximity/shift-config` once, `dayProdData()` merges prev tail (`?date=<prev>&mode=day&from=<dayBoundaryTs>`) + aligned day **sequentially** (legacy routes are `HeavyApiRequestGuard` ~1 req/s) via `_mergeData`, cached under `?pd=1&b=&p=&a=&_now=`; boundary 0 → legacy midnight fallback. Sensors stay calendar-day. Tests: 8 new jsdom cases in `test_sensor_charts.js` (84/84). | `page_sensor_charts_asset.h` (`:1660-1770` loaders `:1927+`), `tests/web/extract.py`, `tests/web/test_sensor_charts.js` | fixed |
| SC-8 | High | **Live/8h preset dropped the pre-reboot window after reboot.** `handleSensorHistoryGet` (`sensor_handler.cpp:351`) set `psramHasOverlap` from the ring's **newest** sample → once the first post-boot sample landed the ring falsely "covered" `from`, serving PSRAM-only and silently dropping the pre-reboot data (still on SD). Fixed server-side: overlap now = ring's **oldest** retained ≤ `from` (`SensorHistory::oldestTimestamp()` + pure `sensorHistorySourceFor`); gap → `streamMergedWindow()` backfills SD `.chart` head `[from, ringOldest)` + live ring tail in **one envelope** (dedup at `ringOldest`, newest `maxPoints` kept; missing/unparsable day file or 0 head → `streamHistoryJSON` fallback). Same pattern as the OEE dashboard backfill. Native tests: 8 cases / 116 assertions in `test_sensor_history_sources.cpp` (83/83 suites). | `chatgpt/src/sensor_history_merge.h` (new), `chart_history.{h,cpp}` (`chartHistoryReadSensorJson`/`Write`, `ChartSensorJsonSet`), `sensors/sensor_config.{h,cpp}` (`streamMergedWindow`), `handlers_unified/sensor_handler.cpp`, `tests/native/test_sensor_history_sources.cpp` | fixed |
