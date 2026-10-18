# IPG Laser — `/ipg-laser`

> One-stop reference for the **IPG Laser RS232 page**: purpose, layout, every
> button/input, client logic, API endpoints, persistence (stateless) and the
> firmware-side laser engine. Written for both humans and AI agents.

---

## 1. Objective

Browser console for IPG YLR/Y14-series fiber lasers over the shared RS232 port:
send raw laser commands (e.g. `STA`, `ROP`, `EMON`), watch TX/RX responses captured
from the RS232 stream, verify the RS232 profile matches the laser manual
(57600 baud 8N1, no flow control, null-modem 3-wire), and browse a built-in
command catalog (consolidated from **xLR-Y14 Interface Specification rev.2** and
**YLR-Series Users Guide**). Includes guided preset buttons and multi-step
sequences (snapshot, emission start/stop). Stateless — nothing is saved; all
state lives in the RAM ring buffer of the laser engine.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /ipg-laser` |
| Feature flag | `ENABLE_IPG_LASER` (`config_defaults.h:239-240`, default `0`; commented-out opt-in in `user_config.h:207`; forced `0` in all packs, e.g. `packs/pack_waveshare.h:111-112`) — gates page, WiFi APIs and Ethernet mirrors; shared `available`/`clear` routes registered unconditionally so the page can detect "disabled" |
| WiFi server | `web_routes_ipg_laser.cpp:14` — `server.on("/ipg-laser")`, chunked 3-segment stream of `PAGE_IPG_LASER`; `ensureBasicAuth` + `allowHtmlClient` (`:15,17`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:416-422` → `streamSharedIpgLaserPage` (`web_shared_pages.cpp:245-247`) → `streamIpgLaserPageToPrint` (`renderers/ipg_laser_page_renderer.cpp:7-20`); 404 when flag off |
| Home link | `renderers/home_page_renderer.h:177` — `🔥 IPG Laser RS232` button, shown only `#if ENABLE_IPG_LASER==1` |
| Shared APIs (auto WiFi+Ethernet) | `GET /api/ipg-laser-available`, `POST /api/ipg-laser-clear` (`web_routes_ipg_laser.cpp:11-12`, handlers in `handlers_unified/ipg_laser_handler.cpp`) |
| WiFi-only APIs + Ethernet mirrors | `GET /api/ipg-laser-status`, `/api/ipg-laser-commands`, `/api/ipg-laser-log`, `POST /api/ipg-laser-send` — WiFi `server.on` in `handlers/ipg_laser_handler.cpp:50-160`; Ethernet `ethernet_bootstrap_routes.h:586-591` + `:728-729` → `ethernetHandleIpgLaser*` (`ethernet_bootstrap_handlers.h:2754-2832`) |
| Route registry | `routes/route_registry_api.cpp:138-141` (GET), `:236` (page), `:315-316` (POST) |
| Self-test / manifest | `handlers_unified/test_manifest_handler.cpp:278` (`PAGE("/ipg-laser", ENABLE_IPG_LASER)`), `handlers_unified/selftest_handler.cpp:77,147` |
| Status payload | `system_status_payload.h:552` — `feature_ipg_laser` |
| Auth | Basic auth on page + all WiFi APIs; page subject to HTML session limits |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |
| Caching | Page: `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_ipg_laser.cpp:37-38`); API fetches use `cache:'no-store'`/`'no-cache'` |

Renderer order: shell → `PAGE_IPG_LASER` (HTML + inline JS, PROGMEM) → auto.js footer
(Ethernet: shared page stream without the WiFi chunked wrapper).

---

## 3. Layout

```
div.page.ipg-laser > div.wrap
 ├ <style> (.section-box, .section-title, mobile hint)  ← precedes Home link
 ├ div.actions.nav-inicio > a.btn[href='/'] "🏠 Início"
 ├ H1 "🔥 IPG Laser RS232"
 ├ div.section-box (quick nav: anchors #sec-ipg-link/#sec-ipg-console/#sec-ipg-log/#sec-ipg-catalog)
 ├ div#sec-ipg-link.section-box "1) Ligação RS232"
 │   ├ #ipgStatus ("A carregar estado do módulo...")
 │   ├ RS232-active strip: #ipg-rs232-text + link /rs232 "🛠️ configurar"
 │   ├ blue "ⓘ Recomendação de Ligação" box (cable, DB9, 57600 8N1, REMOTE+interlocks, BCMD tip)
 │   └ profile reminder paragraph (57600, REMOTE, BCMD)
 ├ div#sec-ipg-console.section-box "2) Console de Comandos"
 │   ├ label + input#ipgCommand ("Ex.: STA, ROP, EMON, SDC 35.0")
 │   ├ [➤ Enviar comando] [🗑 Limpar respostas]
 │   ├ 2.1 "⚙ Presets Operacionais" (10 preset buttons)
 │   └ 2.2 "⏳ Sequências automáticas" (4 sequence buttons + [Cancelar sequência] + #ipgSequenceStatus)
 ├ div#sec-ipg-log.section-box "3) Respostas do Laser"
 │   └ pre#ipgLog ("Sem respostas ainda.")
 └ div#sec-ipg-catalog.section-box "4) Comandos Disponíveis"
     ├ input#ipgSearch + select#ipgCategory + [Limpar filtros]
     ├ #ipgFilterCount + mobile scroll hint
     └ table (Cmd/Categoria/Uso/Função/Resposta típica/Notas/Ação) > tbody#ipgCommandsBody
```

When the module is disabled, console/log/catalog sections are hidden and
`#ipgStatus` shows a red setup panel (`showIpgModuleDisabledMessage()`).

---

## 4. Inputs

| Input | ID | Type | Notes |
|---|---|---|---|
| Command to send | `ipgCommand` | text | placeholder `Ex.: STA, ROP, EMON, SDC 35.0`; Enter key sends; trimmed client-side, empty → `customAlert` error |
| Quick search | `ipgSearch` | text | live `input` → `renderFilteredCommands()`; matches code/category/usage/description/response/notes, case-insensitive |
| Category filter | `ipgCategory` | select | options built from catalog categories (sorted); `change` → re-render |

No form element — all sends go through `fetch` POSTs. No `ConfigManager`-bound fields.

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Início | link `/` |
| ➤ Enviar comando | `sendIpgCommand()` → `POST /api/ipg-laser-send` `{command}`; success → `customAlert('✅ …')` + `loadStatus()` + `loadLog()` at +250 ms / +1200 ms |
| 🗑 Limpar respostas | `clearIpgLog()` → `POST /api/ipg-laser-clear` → `customAlert` + `loadLog()` |
| 10 presets (STA/ROP/RPP/RCS/RPRR/RPW/RFV/EMON/EMOFF/HELP) | `sendPreset(cmd)` → fills input + sends immediately; EMON green, EMOFF red |
| 4 sequences (Snapshot potência / Estado completo / Arranque emissão / Paragem segura) | `runPresetSequence(name, cmds, delayMs)` — serial send with 400–500 ms gaps, aborts on first failure, refreshes status+log on completion |
| Cancelar sequência | `cancelPresetSequence()` — clears timer, `customAlert('ℹ️ Sequência cancelada.')` |
| Catalog row "Usar" | `useCommand(code)` — copies code into `#ipgCommand` (does not send) |
| Limpar filtros | `clearCommandFilters()` — resets search + category to first option |
| 🛠️ configurar | link `/rs232` (RS232 settings page) |

---

## 6. Client-side logic (inline JS in `page_ipg_laser_asset.cpp:159-569`)

- **`checkIpgModuleAvailable()`** (`:512`) — `GET /api/ipg-laser-available`; hides console/log/catalog via `showIpgModuleDisabledMessage()` (`:527`) when `available` is false (or fetch fails); `hideIpgModuleDisabledMessage()` (`:542`) restores them.
- **`loadStatus()`** (`:286`) → `GET /api/ipg-laser-status` → **`renderStatus(data)`** (`:266`): compares current RS232 profile vs required (57600/8/`none`/1/no-flow) and renders a green `● Perfil compatível` or amber `● Ajustar perfil RS232` badge plus current/required/template/last-command/capture/remote-mode lines (`escapeHtml` on all interpolated values).
- **`loadCommands()`** (`:295`) → `GET /api/ipg-laser-commands` → fills `gIpgCommands`, then `updateCategoryFilter()` (`:314`, builds sorted category list) + `renderFilteredCommands()` (`:340`, search+category filter, `N de M comandos visíveis.` counter, per-row `Usar` button).
- **`loadLog()`** (`:406`) → `GET /api/ipg-laser-log` → renders `[TX|RX|SYS] text` lines into `pre#ipgLog`.
- **`sendIpgCommandWithOptions(command, showSuccessAlert, doneCallback)`** (`:436`) → `POST /api/ipg-laser-send` JSON `{command}`; used directly (with alert) and by sequences (with callback, no alert).
- **`loadRs232Status()`** (`:489`) → `GET /api/config?section=rs232` → renders `baud · DBP · ● ativo/○ desabilitado` into `#ipg-rs232-text`.
- **Boot + polling** (`:552-569`): `DOMContentLoaded` runs all five loaders, wires `input`/`change`/`Enter` handlers, then `setInterval(loadStatus, 1500)`, `setInterval(loadLog, 1500)`, `setInterval(loadRs232Status, 3000)`.
- No arrow functions; `customAlert` for all feedback; no `auto.js` `_t()` calls (see IPG-1).

---

## 7. Server API

Shared handlers in `handlers_unified/ipg_laser_handler.cpp`; WiFi-only routes in `handlers/ipg_laser_handler.cpp`; Ethernet mirrors in `ethernet_bootstrap_handlers.h:2754-2838`.

### `GET /api/ipg-laser-available` (`handlers_unified/ipg_laser_handler.cpp:8`)

```json
{"available":true,"message":"IPG Laser module is enabled."}
```
Flag off → `{"available":false,"message":"IPG Laser module is disabled. Enable ENABLE_IPG_LASER in user_config.h."}` (same on Ethernet, `:2741-2747`). Always registered, so the page works as a "not enabled" notice.

### `GET /api/ipg-laser-status` (`handlers/ipg_laser_handler.cpp:17-45`)

```json
{
  "captureActive": false, "captureEndsInMs": 0, "lastRxAgeMs": 0,
  "logCount": 3, "lastCommand": "STA",
  "currentBaudrate": 57600, "currentDataBits": 8, "currentParity": "none",
  "currentStopBits": 1, "currentFlowControl": false, "currentTemplate": "…",
  "requiredBaudrate": 57600, "requiredDataBits": 8, "requiredParity": "none",
  "requiredStopBits": 1, "requiredFlowControl": "none",
  "transport": "RS232 null-modem 3-wire",
  "remoteModeNote": "Laser must be in REMOTE mode with interlocks satisfied before EMON."
}
```

Current profile is read live from `ConfigManager` RS232 getters (`:19-33`).

### `GET /api/ipg-laser-commands` (`:55-76`)

`{"count": N, "commands": [{"code","category","usage","description","response","notes"}]}` — the full PROGMEM catalog (`ipg_laser_module.cpp:24-86`, ~62 entries). ~24 KB JSON (`SpiRamJsonDocument doc(24576)`).

### `GET /api/ipg-laser-log` (`:78-96`)

`{"count": n, "entries": [{"ageMs","direction":"TX|RX|SYS","text"}]}` — up to 24 newest entries (`ipgLaserCopyLog(entries, 24)`).

### `POST /api/ipg-laser-send` (`:98-160`)

JSON body `{command, template?}`, max 1024 bytes (`IPG_LASER_SEND_MAX_PAYLOAD`, `:14`; 413 when exceeded, 400 on invalid JSON, 400 when no body). Empty-after-trim → `ipgLaserSendCommand` returns false → 200 `{"success":false,"message":"Unable to queue IPG command."}`; queued → 200 `{"success":true,"message":"Command queued. Waiting for laser response."}`. Ethernet mirror (`:2818-2832`) parses without the size guard. The `template` field is accepted but the page never sends it (see IPG-5).

### `POST /api/ipg-laser-clear` (`handlers_unified/ipg_laser_handler.cpp:17-26`)

`ipgLaserClearLog()` → `{"success":true,"message":"IPG response log cleared."}`; flag off → 503 `{"success":false,"message":"IPG Laser not enabled."}`.

---

## 8. Persistence

Stateless. No `ConfigManager` section, no `saveSection("ipg…")`, no `/config/*.json`
stem — verified by absence of any `saveSection`/stem reference to IPG in `src/`.
The RS232 profile the page displays (baud/bits/parity/stop/flow/template) belongs
to the **RS232** config section and is edited on `/rs232`, not here. All laser
state (log ring, capture window, last command) is RAM-only and lost on reboot
(`ipgLaserBegin()` in `setup_impl.h:293` zeroes it).

---

## 9. Runtime engine

`chatgpt/src/ipg_laser_module.{h,cpp}` (entire TU gated `#if ENABLE_IPG_LASER == 1`).

- **`ipgLaserBegin()`** (`:137`) — zeroes log ring, capture state, RX line buffer.
- **`ipgLaserSendCommand(command, templateName)`** (`:172`) — trims; empty → false. Opens a 2500 ms capture window (`IPG_LASER_CAPTURE_WINDOW_MS`, `:14`), records `s_lastCommand`, pushes a `TX` entry, and forwards via `enqueueRS232Command(normalized, templateName, "IPG", "rs232")`; on queue failure pushes `SYS "Queue full or RS232 unavailable."` and closes the window.
- **`ipgLaserOnRxByte(byteValue)`** (`:154`) — called from the RS232 RX path (`loop_impl.h:700-701`); only while capture is active; splits on CR/LF into ≤159-char printable-only lines (`:98-129`).
- **`ipgLaserUpdate()`** (`:147`) — called every loop (`loop_impl.h:32`); closes the capture window at deadline, flushing a partial line.
- **`ipgLaserClearLog()`** (`:194`), **`ipgLaserGetStatus()`** (`:200`), **`ipgLaserCopyLog(entries, max)`** (`:210`, returns newest-first window of the 24-entry ring), **`ipgLaserGetCommandCatalog(&count)`** (`:230`, PROGMEM table `:24-86`: Guide/Control/Emission/Readback/Setpoint/Front Panel/Ethernet/Waveform/Utility/Service categories).
- Transport dependency: requires the RS232 queue (`runtime_rs232_queue.h`) and the laser at 57600 8N1; invalid commands get `BCMD` from the laser itself.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 17 key(s) — `ipgColAction`, `ipgColFunction`, `ipgColResponse`, `ipgConsoleDesc`, `ipgErrCatalog`, `ipgErrState`, `ipgLastCmd`, `ipgLoading`, `ipgNoCommands`, `ipgNoSequence`, `ipgNotDetected`, `ipgNotEnabled`, `ipgPresetDesc`, `ipgProfileOk`, `ipgQuickSearch`, `ipgScrollHint`, `ipgSequenceDesc`. HTML fallback text is English; PT is served from the packs.

Page uses `data-i18n="btnHome"` and `data-i18n-title` tooltips only — all confirmed
in `languages/lang_en_ui.h` (and PT twins in `lang_pt_ui.h`):

`btnHome` (`:7`), `ipgTipHome` (`:1958`), `ipgTipCategory` (`:1959`),
`ipgTipCommand` (`:1960`), `ipgTipSearch` (`:1961`), `ipgTipSend` (`:1962`),
`ipgTipClearLog` (`:1963`), `ipgTipPreset` (`:1964`), `ipgTipSequence` (`:1965`),
`ipgTipCancel` (`:1966`), `ipgTipClearFilters` (`:1967`). Inline JS uses no
`_t()` lookups — all `customAlert`/status strings are hardcoded Portuguese.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| IPG-1 | Visible UI is hardcoded Portuguese with no i18n coverage. Only tooltips have `data-i18n-title`; every heading, label, button and log string (`page_ipg_laser_asset.cpp:33-155`, JS alerts `:198,210,430,478`) stays PT when the UI language is English, while `title` attributes are hardcoded English (`:78`). The server `remoteModeNote` (`handlers/ipg_laser_handler.cpp:40`) is also English shown verbatim on a PT page. | Medium | Add `data-i18n` keys for visible strings (or document the page as PT-only); route JS alerts through `_t()` like other pages. |
| IPG-2 | First load pre-filters the catalog. `updateCategoryFilter()` (`page_ipg_laser_asset.cpp:335-337`) selects the first alphabetical category when there is no stored choice, and `renderFilteredCommands()` (`:343`) then hides every other category — the user initially sees a subset with no indication a filter is active. | Low | Add an "All categories" option (default) instead of auto-selecting `keys[0]`. |
| IPG-3 | Any fetch failure hides working sections. `checkIpgModuleAvailable()` (`:522-524`) calls `showIpgModuleDisabledMessage()` on network error too, hiding console/catalog/log even when the module is enabled. | Low | On fetch error show a transient warning in `#ipgStatus` instead of hiding sections; only hide on explicit `available:false`. |
| IPG-4 | Three overlapping pollers with no overlap guard (`:560-562`: status+log every 1.5 s, RS232 every 3 s). `loadRs232Status()` hits `/api/config?section=rs232` (`:490`) every 3 s forever, even when the tab is hidden. | Low | Skip ticks while a request is in flight and pause intervals on `document.hidden`. |
| IPG-5 | Dead `template` parameter. Both send handlers parse `doc["template"]` (`handlers/ipg_laser_handler.cpp:151`, `ethernet_bootstrap_handlers.h:2822`) but the page body only ever contains `{command}` (`page_ipg_laser_asset.cpp:440`). | Info | Either expose a template selector or drop the field. |
| IPG-6 | Wrong escaping context in catalog rows. The `Usar` button interpolates `escapeHtml(row.code)` into a JS single-quoted `onclick` string (`:391`) — HTML-escaping does not neutralise quotes for JS context. Currently safe only because catalog codes are `[A-Z0-9]+` (`ipg_laser_module.cpp:24-86`). | Info | Use `data-code` + delegated listener instead of inline `onclick`. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_ipg_laser_asset.cpp` (+ `page_ipg_laser_asset.h:5`) |
| Renderer (WiFi) / shared (Ethernet) | `chatgpt/src/webpages/renderers/ipg_laser_page_renderer.cpp` (`streamIpgLaserPageToPrint`), `chatgpt/src/web_shared_pages.cpp:245-247` |
| Routes (WiFi page + shared APIs) | `chatgpt/src/webpages/routes/web_routes_ipg_laser.cpp` |
| WiFi-only APIs (status/commands/log/send) | `chatgpt/src/webpages/handlers/ipg_laser_handler.{h,cpp}` (`registerIpgLaserApiRoutes`) |
| Shared handlers (available/clear) | `chatgpt/src/webpages/handlers_unified/ipg_laser_handler.{h,cpp}` |
| Ethernet page + API mirrors | `chatgpt/src/ethernet_bootstrap_routes.h:416-422,586-591,728-729`, `chatgpt/src/ethernet_bootstrap_handlers.h:2741-2839` |
| Runtime engine | `chatgpt/src/ipg_laser_module.{h,cpp}` (catalog, ring log, capture window, RS232 queueing) |
| Wiring (setup/loop/RS232 tap) | `chatgpt/src/setup_impl.h:292-293`, `chatgpt/src/loop_impl.h:31-32,700-702`, `chatgpt/src/webpages/webpage_impl.h:47,109` |
| Feature flag default / opt-in / packs | `chatgpt/src/config_defaults.h:238-241`, `chatgpt/user_config.h:207`, `chatgpt/packs/pack_*.h` (all `0`) |
| i18n keys | `chatgpt/src/languages/lang_en_ui.h:1958-1967` (+ `:7`), `chatgpt/src/languages/lang_pt_ui.h:1958-1967` (+ `:7`) |
| Tests | `tests/web/test_ipg.js`, `tests/web/pages/ipg.html` (escapeHtml, renderStatus badge, all 19 JS functions); `tests/native/test_config_defaults.cpp:63,124` (flag is 0/1, default 0) |
| Manifest / self-test / status | `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp:278`, `.../selftest_handler.cpp:77,147`, `chatgpt/src/system_status_payload.h:552` |
