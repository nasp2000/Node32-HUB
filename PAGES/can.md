# CAN Bus — `/can`

> One-stop reference for the **CAN bus (TWAI) monitor page**: live bus status, frame monitor, frame transmitter, and driver configuration. Written for both humans and AI agents.

---

## 1. Objective

Monitor and control the ESP32 **TWAI (CAN 2.0B)** controller from the browser: live bus state + error counters, last-received frame list (auto-refresh), manual transmission of one frame (STD/EXT, RTR, DLC 0–8), and persistent driver configuration (enable-on-boot, bitrate, mode, GPIO pins, hardware acceptance filter). Requires an external SN65HVD230 / MCP2551 transceiver (`can_core.h:5`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /can` |
| Feature flag | `ENABLE_CAN` (`config_defaults.h:244`, default `0`; opt-in via `user_config.h:201`, commented out) — gates page, APIs and Ethernet mirrors; disabled builds serve 404 / 503 stubs |
| WiFi server | `web_routes_can.cpp:22` — `server.on("/can", HTTP_GET, …)` with `ensureBasicAuth` + `allowHtmlClient`, streams via `streamCanPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:360` → `streamSharedCanPage` (`web_shared_pages.cpp:209`) |
| API (shared GET/POST) | `GET /api/can/status`, `GET /api/can/frames`, `GET /api/can/config`, `POST /api/can/clear`, `POST /api/can/restart` via `registerApiRoute` (`web_routes_can.cpp:12-16`; listed in `route_registry_api.cpp:108-110,220,274-277`) |
| API (manual POST) | `POST /api/can/send`, `POST /api/can/config` stay manual body-accumulators (`web_routes_can.cpp:17`, `can_handlers.cpp:19,55`); explicit Ethernet mirrors `ethernet_bootstrap_routes.h:718-721` → `ethernetHandleCanSendPost` / `ethernetHandleCanConfigPost` (`ethernet_bootstrap_handlers.h:2925,2954`) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_can.cpp:33-34`) |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |

Renderer order: shell (`streamPageShellStart`, title "🔌 CAN Bus (TWAI)") → `CAN_TABS_SCRIPT` → `PAGE_CAN` (HTML + inline JS) → auto.js footer → `/config-manager.js` (`can_page_renderer.cpp:18-30`).

---

## 3. Layout

```
main.page > div.wrap
 ├ div.actions.nav-inicio > a.btn[href='/'] "🏠 Início"   ← first child of .wrap (project rule)
 ├ H1 "🔌 CAN Bus (TWAI)"
 ├ div.can-tab-bar (inline, asset)                        ← 4 pill buttons (see CAN-1: a SECOND bar is injected by the renderer)
 │   ├ button.can-tab[data-tab='status'] "🚥 Status" (active)
 │   ├ button.can-tab[data-tab='frames'] "📊 Frames"
 │   ├ button.can-tab[data-tab='send']   "📤 Send"
 │   └ button.can-tab[data-tab='config'] "⚙️ Config"
 ├ div.can-panel[data-tab='status']
 │   ├ H2 "📡 Bus Status" + span#busState + span#busMode + span#busBitrate
 │   ├ .stat-row: #sTxF #sRxF #sBuf #sTec #sRec #sErr
 │   ├ #busOffWarn (hidden; "⚠ Bus-Off detected — auto-recovery in progress…")
 │   └ [🧹 Clear buffer] [⏸ Pause monitor#pauseBtn]
 ├ div.can-panel[data-tab='frames'] (hidden)
 │   └ table.frame-tbl > tbody#frameTbody ("No frames received" when empty)
 ├ div.can-panel[data-tab='send'] (hidden)
 │   ├ input#txId + select#txDlc + checkbox#txExt + checkbox#txRtr
 │   ├ #dataInputsCont > inputs #d0…#d7 + [▶ Send Frame] + #sendMsg (dead, see CAN-4)
 └ div.can-panel[data-tab='config'] (hidden)
     └ form#canForm: #cfgEnabled #cfgLogSd #cfgBitrate #cfgMode #cfgTxPin #cfgRxPin #cfgFiltId #cfgFiltMask #cfgFiltExt + [💾 Save] [⟳ Restart driver] + #msgBox
```

---

## 4. Inputs

| Input | ID | Type / range | Notes |
|---|---|---|---|
| Frame ID | `txId` | text, hex, `maxlength=8`, default `123` | parsed `parseInt(v,16)`; no client validation (garbage → `NaN` → ID 0 server-side) |
| DLC | `txDlc` | select 0–8, default `8` | `onchange='showDataInputs()'` hides unused `dN` inputs |
| Extended ID | `txExt` | checkbox | 29-bit ID |
| RTR | `txRtr` | checkbox | Remote Transmission Request |
| Data bytes | `d0`–`d7` | text, hex 2 chars, default `00` | only first `dlc` bytes sent; each `parseInt(v,16)\|\|0` |
| Enable on boot | `cfgEnabled` | checkbox | `enabled` |
| Log to SD/PSRAM | `cfgLogSd` | checkbox | `log_to_sd` |
| Bitrate | `cfgBitrate` | select 25/50/100/125/250/500/800/1000 Kbps, default `500` | server rejects anything else with 400 |
| Mode | `cfgMode` | select 0 Normal / 1 Listen-only / 2 Self-test | `0` = TX+RX, `1` = sniffer, `2` = loopback |
| GPIO TX / RX | `cfgTxPin` / `cfgRxPin` | number 0–48 | empty field becomes GPIO 0 (see CAN-5) |
| Filter ID / mask | `cfgFiltId` / `cfgFiltMask` | text hex, defaults `0` / `FFFFFFFF` | `FFFFFFFF` = accept all (matches `can_core.cpp:60-62`) |
| Filter extended | `cfgFiltExt` | checkbox | applies filter to 29-bit IDs |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home (`btnHome`) | link `/` — first child of `.wrap` |
| 🚥/📊/📤/⚙️ tab pills | switch `.can-panel` visibility client-side; no server round-trip (`page_can_asset.h:327`) |
| 🧹 Clear buffer | `customConfirm(canConfirmClearBuffer)` → `POST /api/can/clear` → `customAlert(canBufferCleared / canErrGeneric)` + `loadFrames()` |
| ⏸ Pause monitor (`#pauseBtn`) | toggles `_paused`; label swaps `canPauseMonitor` ↔ `canResumeMonitor`; only gates `loadFrames()` |
| ▶ Send Frame | `disableBtn` + `showLoading()` → `POST /api/can/send` (JSON) → `customAlert("✅ " + canFrameSent / "❌ " + canErrGeneric)` → `hideLoading()` + `enableBtn` |
| 💾 Save | `canSaveConfig()` → `POST /api/can/config` (JSON) → `customAlert("✅ " + proxConfigSaved)` (wrong-namespace key, see CAN-3) + `showSavedCard()` + `loadStatus()`; driver restarts |
| ⟳ Restart driver | `customConfirm(canConfirmRestart)` → `POST /api/can/restart` → `customAlert(canDriverRestarted / canErrGeneric)` + `loadStatus()` |

---

## 6. Client-side logic (inline JS in `page_can_asset.h:194-328`)

- **Polling** — `loadStatus()` every 2 s (`_stTimer`), `loadFrames()` every 1 s (`_frTimer`); `loadFrames()` skipped while `_paused`. `canLoadConfig()` + `showDataInputs()` run once at init.
- **`loadStatus()`** (`:204`) — `GET /api/can/status` → `busState` text + `stateClass()` (`RUNNING→cs-run`, `BUS_OFF→cs-off`, `RECOVERING→cs-rec`, else `cs-stop`); `busMode` via `{0:'Normal',1:'Listen-only',2:'Self-test'}` map; `busBitrate` as `"<kbps>Kbps"`; counters `total_tx/total_rx/buffer_frames/tec/rec`, errors = `rx_errors+tx_errors`; shows `#busOffWarn` only on `BUS_OFF`. Failures swallowed (`.catch(function(){})`).
- **`loadFrames()`** (`:219`) — `GET /api/can/frames?limit=128` → renders newest-first rows: `ts`, ID padded (`padStart(3)` STD / `padStart(8)` EXT), `EXT`/`RTR` badges or `STD`, `dlc`, data bytes as `.data-byte` spans.
- **`sendFrame()`** (`:257`) — builds `{id (hex), dlc, ext, rtr, data[]}` JSON; `disableBtn`/`showLoading()` with `finally` restore; uses `customAlert`, `disableBtn/enableBtn`, no arrow functions.
- **`canLoadConfig()`** (`:273`) — `GET /api/can/config` → fills all `cfg*` fields; `filter_id/mask` rendered as uppercase hex.
- **`canSaveConfig()`** (`:290`) — posts full config JSON; `tx_pin/rx_pin` via `parseInt(…,10)` (empty → `NaN` → `null` on the wire, see CAN-5).
- **Tabs** — inline handler (`:327`) toggles panels by `data-tab` and repaints active pill (`#1d4ed8` vs transparent). Uses `t(k,fb)` → `window._t` with `ui_lang_cache` fallback (`:195`).
- **`showDataInputs()`** (`:200`) hides `dN` inputs with index ≥ DLC.

---

## 7. Server API

Shared handlers in `handlers_unified/can_handler.cpp` (guarded `#if ENABLE_CAN==1`; `#else` stubs return 200 `{"success":false,"message":"CAN nao compilado (ENABLE_CAN=0)"}` (`:170-184`)); manual JSON-body routes in `handlers/can_handlers.cpp:14-93` (disabled build registers 503 stubs `:95-103`).

### `GET /api/can/status` (`can_handler.cpp:33`)

```json
{"state":"RUNNING","mode":0,"bitrate_kbps":500,"tec":0,"rec":0,"total_rx":12,
 "total_tx":3,"rx_errors":0,"tx_errors":0,"bus_off_count":0,"buffer_drops":0,
 "buffer_frames":12,"msgs_to_rx":0,"success":true}
```

`state` from `stateStr()` (`RUNNING/BUS_OFF/RECOVERING/ERROR/STOPPED`); `tec/rec/rx/tx_errors/msgs_to_rx` live from `canRuntimeGetBusStats()`; totals from `canRuntimeGetStats()`; `buffer_frames` from `canRuntimeFrameCount()`.

### `GET /api/can/frames?limit=128` (`:74`)

`{success,count,frames:[{ts,id,dlc,ext,rtr,data:[…]}]}` (`id` decimal, `ts` = `millis()` at RX). `limit==0` or `>1024` resets to 128 (`:76`); `malloc` per request, OOM → `{"success":false,"message":"OOM"}`.

### `GET /api/can/config` (`:121`)

`{success,config:{enabled,bitrate_kbps,mode,tx_pin,rx_pin,filter_id,filter_mask,filter_ext,log_to_sd}}` (all numbers decimal).

### `POST /api/can/clear` (`:151`) / `POST /api/can/restart` (`:157`)

Clear → `canRuntimeClearFrames()`, returns PT `"Buffer limpo"`. Restart → `canRuntimeApplyConfig(current, false)` (reinstall driver, no save); PT success/fail messages.

### `POST /api/can/send` (`can_handlers.cpp:19-52`)

JSON `{id,dlc,ext,rtr,data[]}`; `dlc>8` clamped; only first `min(dlc,8)` data bytes copied; invalid JSON → 400 `"JSON invalido"`. Result: `"Frame enviado"` vs `"Falha no envio (buffer cheio ou bus inativo)"`. No ID-range validation (standard vs extended unchecked).

### `POST /api/can/config` (`can_handlers.cpp:55-93`)

Merges present keys over current config, validates bitrate ∈ `{25,50,100,125,250,500,800,1000}` else 400 `"Bitrate invalido"`, then `canRuntimeApplyConfig(cfg, true)` (save + restart). No GPIO/mode/filter validation — `tx_pin`/`rx_pin`/`mode` pass straight to `canCoreInit`.

---

## 8. Persistence

Standalone file **`/config/can.json`** (not via `ConfigManager` sections): `loadConfig()` applies defaults first (`enabled=true`, `500 Kbps`, `CAN_MODE_NORMAL`, `tx=CAN_TX_PIN` (20), `rx=CAN_RX_PIN` (19), `filter_id=0`, `filter_mask=0xFFFFFFFF`, `filter_ext=false`, `log_to_sd=false`), then overlays present JSON keys (`can_runtime.cpp:35-64`). `saveConfig()` (`:66-90`) uses atomic write (`.tmp` → `bytes>0` → `remove` + `rename`). Only written when `POST /api/can/config` succeeds (`save=true`); restart alone does not touch flash.

---

## 9. Runtime engine

- **Core driver** (`can_core.cpp`, TWAI): `canCoreInit(tx,rx,bitrate,mode,filter_id,filter_mask,filter_ext)` (`:30`) — maps bitrate to `TWAI_TIMING_CONFIG_*` (`:16`), mode to `TWAI_MODE_NORMAL/LISTEN_ONLY/NO_ACK` (self-test uses `NO_ACK`, `:43-48`), builds hardware filter (`filter_mask==0xFFFFFFFF` → `ACCEPT_ALL`, `:60`; else single-filter code/mask shifted for 11-/29-bit, `:64-74`); RX queue 64, TX queue 8 (`:52-53`). `canCoreSend` (10 ms TX wait, `:102`), `canCoreReceive` (non-blocking poll, `:114`), `canCoreGetStats` (TEC/REC, missed/failed counts, state map, `:128`), `canCoreInitiateRecovery` (`:167`). All status messages go through `sendVirtualMessage` (Web Monitor visible).
- **Runtime** (`can_runtime.cpp`): `canRuntimeInit()` (`:148`) loads config, allocates 1024-frame PSRAM ring (`CAN_FRAME_BUFFER_SIZE`, `can_runtime.h:19`, DRAM fallback), skips driver when `enabled=false`, starts `canRxTask` (Core 0, 2 ms poll, `:120`) which drains the TWAI queue into the ring (`bufferPush` drops oldest + `buffer_drops++`, `:95`), auto-recovers on `BUS_OFF` (counts `bus_off_events`, `:134`), snapshots `s_bus_stats`. `canRuntimeApplyConfig(cfg,save)` (`:237`) deletes task, stops driver, swaps config, optionally saves, re-inits. Ring reads are mutex-guarded chronological copies (`:107-116`).
- **Boot wiring**: `setup_impl.h:295` calls `canRuntimeInit()`; `chatgpt.ino:74` includes CAN under `#if ENABLE_CAN==1`.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

All confirmed present in `lang_en_ui.h` (EN) and `lang_pt_ui.h` (PT): `btnHome` (`:7`), `canConfirmClearBuffer`, `canBufferCleared`, `canFrameSent`, `canConfirmRestart`, `canDriverRestarted`, `canErrGeneric`, `canResumeMonitor`, `canPauseMonitor` (`:1635-1642`), `canTipHome`, `canTipTxId`, `canTipTxDlc`, `canTipTxExt`, `canTipTxRtr`, `canTipTxData`, `canTipSendFrame`, `canTipClearBuffer`, `canTipPause`, `canTipEnable`, `canTipLogSd`, `canTipBitrate`, `canTipMode`, `canTipTxPin`, `canTipRxPin`, `canTipFilterId`, `canTipFilterMask`, `canTipFilterExt`, `canTipSaveConfig`, `canTipRestart` (`:1992-2011`), plus borrowed `proxConfigSaved` (`:1567`) used for the save toast. `data-i18n` covers only `btnHome`; all `canTip*` keys ride on `data-i18n-title` tooltips (`page_can_asset.h:43-186`).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| CAN-1 | **Two competing tab bars.** The asset ships its own `.can-tab-bar` (`page_can_asset.h:45-50`) and the renderer injects a second `CAN_TABS_SCRIPT` bar with PT labels (`Estado/Monitor/Enviar/Config`, `can_page_renderer.cpp:9-16`) that targets any `div[style*='border:2px solid #3b82f6']` — the page renders two tab rows driving the same panels. | Medium | Delete one system: keep the asset tabs and drop `CAN_TABS_SCRIPT`, or vice versa. |
| CAN-2 | **Hardcoded PT fallback strings.** `Início` (`page_can_asset.h:43`), `Dados` (`:86`), `Modo` (`:153`), `Filtro ID (hex)` (`:171`), `Filtro para IDs extended` (`:179`) have no `data-i18n` binding, so EN users see PT until/unless `auto.js` replaces `btnHome`. | Medium | Add `data-i18n` spans for every label or switch fallbacks to English. |
| CAN-3 | **Server messages are PT-only / unaccented.** `"Buffer limpo"` (`can_handler.cpp:154`), `"Driver CAN reiniciado"` (`:161`), `"JSON invalido"`, `"Bitrate invalido"`, `"Frame enviado"`, `"Config guardada e driver reiniciado"` (`can_handlers.cpp:27,63,84,49,90`), `"CAN nao compilado"` (`:99`) mix with an otherwise EN page. | Low | Return EN messages (or i18n keys) consistent with page fallbacks. |
| CAN-4 | **Dead `#sendMsg` element.** `<div id='sendMsg' class='msg-ok'>` (`page_can_asset.h:125`) is never written — `sendFrame()` reports only via `customAlert`. | Low | Remove the div or render the result into it. |
| CAN-5 | **Empty GPIO fields silently become GPIO 0.** `canSaveConfig()` sends `parseInt('')` = `NaN` → `null` (`page_can_asset.h:297-298`); server `.as<int>()` coerces to `0` (`can_handlers.cpp:72-73`), reprogramming TX/RX to GPIO 0 without warning. Same for garbage hex in `txId`/`cfgFiltId` (→ 0). | Medium | Validate pins client + server side (0–48, non-empty) and reject with 400 instead of coercing. |
| CAN-6 | **Weak web test.** `tests/web/test_can.js:21-22` looks for `#log`/`#canLog`, which do not exist in the asset, and passes vacuously; no test covers `sendFrame` payload shape or `canSaveConfig` NaN handling. | Low | Assert the real IDs (`frameTbody`, `txId`, `cfgBitrate`) and add ≥2 payload-validation cases. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_can_asset.h` |
| Renderer + duplicate tab script | `chatgpt/src/webpages/renderers/can_page_renderer.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_can.{h,cpp}` |
| Shared API handlers (status/frames/config/clear/restart) | `chatgpt/src/webpages/handlers_unified/can_handler.{h,cpp}` |
| Manual JSON-body routes (send/config + 503 stubs) | `chatgpt/src/webpages/handlers/can_handlers.{h,cpp}` |
| Ethernet page mirror | `chatgpt/src/ethernet_bootstrap_routes.h:360` → `web_shared_pages.cpp:209` |
| Ethernet API mirrors | `chatgpt/src/ethernet_bootstrap_handlers.h:2841-2988`, dispatched at `ethernet_bootstrap_routes.h:718-721` |
| Route registry | `chatgpt/src/webpages/routes/route_registry_api.cpp:108-110,220,274-277` |
| TWAI driver | `chatgpt/src/can_core.{h,cpp}` |
| Runtime (PSRAM ring, RX task, `/config/can.json`) | `chatgpt/src/can_runtime.{h,cpp}` |
| Feature flag default | `chatgpt/src/config_defaults.h:244` (opt-in at `user_config.h:201`); defaults `CAN_TX_PIN 20` / `CAN_RX_PIN 19` (`can_core.h:13-17`) |
| Home link | `chatgpt/src/webpages/renderers/home_page_renderer.h:164` |
| Self-test / manifest | `chatgpt/src/webpages/handlers_unified/selftest_handler.cpp:72,143`, `test_manifest_handler.cpp:274` |
| Web test | `tests/web/test_can.js`, fixture `tests/web/pages/can_asset.html` (no native CAN suite; `tests/native/test_config_defaults.cpp:64,125` only checks the flag is 0/1) |
