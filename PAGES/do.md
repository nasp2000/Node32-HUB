# Digital Outputs — `/do`

> One-stop reference for the **Digital Outputs (DO) page**: purpose, layout,
> every button/input, client logic, API endpoints, persistence and the
> firmware-side TCA9554 relay engine. Written for both humans and AI agents.

---

## 1. Objective

Manual control panel for the **8 relay outputs** on the Waveshare 8DI8DO POE board,
driven by a **TCA9554 I2C expander** (not direct GPIO). Operators can toggle each
output, switch all ON/OFF at once, and rename channels plus set per-channel
inversion. State changes persist **immediately** (no reboot); names/inversion apply
live. The whole page compiles to nothing on boards without the expander.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /do` |
| Feature flag | `BOARD_HAS_TCA9554_DO` (`config_defaults.h:223`, default `0`; `board_pins.h:53` sets `1` on 8DI8DO) — gates asset, renderer, routes, handlers, engine; entire files are `#if` guards, stub is empty when off |
| WiFi server | `web_routes_do.cpp:17` — `server.on("/do", HTTP_GET, …)`, streams via `streamDoPageToPrint()` (`do_renderer.cpp:10`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:284` → `streamSharedDoPage` (`web_shared_pages.cpp:203` → `streamDoPageToPrint`) |
| API (GET) | `GET /api/do/status`, `GET /api/do/config` via `registerApiRoute` (`web_routes_do.cpp:38-39`); **no Ethernet GET mirror** (see DO-1) |
| API (POST) | `POST /api/do/set` + `POST /api/do/config` via body-accumulation `server.on` (`handlers/do_handler.cpp:25,65`); Ethernet mirrors `ethernet_bootstrap_routes.h:768-771` |
| Auth | `ensureBasicAuth` + `allowHtmlClient` (`web_routes_do.cpp:18,20`) — same session/auth gate as other pages |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_do.cpp:28-29`) |

Renderer order: `streamDoPageToPrint` emits `DO_PAGE` PROGMEM verbatim (`do_renderer.cpp:10-13`); page loads `/auto.js` only (no `/config-manager.js`).

---

## 3. Layout

```
div.wrap
 ├ div.actions.nav-inicio > a.btn[href=/]  "🏠 Home"   ← always first child of .wrap (project rule)
 ├ H1 "🔌 Digital Outputs (DO)"                        ← hardcoded EN, no data-i18n
 ├ section (blue panel)
 │   ├ H2 "Output Control"                             ← hardcoded EN
 │   ├ div.all-bar
 │   │   ├ button.btn-all.btn-all-on  "▶ All ON"       ← onclick setAllDO(true)
 │   │   └ button.btn-all.btn-all-off "■ All OFF"      ← onclick setAllDO(false)
 │   ├ div.do-grid (8x)
 │   │   └ div.do-card > div.do-label (strong "DO N" + span#doNameN "Relay N")
 │   │                  + button#doN.btn-off[data-state=false] "OFF"  ← onclick toggleDO(N,this)
 │   └ p.note "💡 State saved automatically after each change and restored on boot."
 └ section (blue panel)
     ├ H2 data-i18n=doCfgTitle "Output Configuration"
     ├ table.do-cfg > thead (DO | data-i18n=doCfgName | data-i18n=doCfgInvert)
     │   + 8 rows: strong "DO N" | input#doNameInputN[type=text,maxlength=16] | input#doInvN[type=checkbox]
     ├ div.all-bar > button.btn-all.btn-all-on[onclick=saveDoConfig()] data-i18n=doCfgSave "Save Config"
     └ p.note "💡 Names and inversion are saved to /config/do.json and applied immediately."
```

Element IDs: `do0..do7` (toggle buttons), `doName0..doName7` (labels),
`doNameInput0..doNameInput7` (name fields), `doInv0..doInv7` (invert checkboxes).

---

## 4. Inputs

| Input | ID | Type / range | Notes |
|---|---|---|---|
| Channel name N (1–8) | `doNameInputN` | text, `maxlength=16`, client-trims + `substring(0,16)` (`page_do_asset.h:136`) | empty = fallback label `Relay N`; server also clamps to 16 (`do_handler.cpp:89`) |
| Invert N (1–8) | `doInvN` | checkbox | bit N of invert mask; active-low load; applied live via `tca9554SetInvert` |

No hidden `*_present` sentinels (unlike `/access`): the config POST always sends the
full 8-name array plus the invert mask.

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| ▶ All ON | `setAllDO(true)` → `POST /api/do/set {"mask":255}`; on `d.success` sets all 8 buttons ON locally, else `customAlert` |
| ■ All OFF | `setAllDO(false)` → `POST /api/do/set {"mask":0}`; same handling |
| `doN` ON/OFF toggle (×8) | `toggleDO(N,btn)` → flips `dataset.state` → `POST /api/do/set {"ch":N,"state":st}`; updates only on `d.success`, else `customAlert` |
| Save Config | `saveDoConfig()` → collects trimmed names + invert bitmask → `POST /api/do/config`; on success `applyDoNames(names)` + `customAlert('✅ Config saved')` |

All failure paths use `customAlert(t('doErrPrefix','❌ Error: ') + (d.message||t('doFailed','failed')))` or `t('doConnErr','❌ Connection error')`; no arrow functions — project rules honoured.

---

## 6. Client-side logic (inline JS in `page_do_asset.h:85-148`)

- **`t(k,fb)`** (`:86`) — `window._t` if present, else `ui_lang_cache` from localStorage, else fallback.
- **`_updBtn(btn,st)`** (`:87`) — sets `dataset.state`, class `btn-on`/`btn-off`, text `ON`/`OFF` (hardcoded EN).
- **`toggleDO(ch,btn)`** (`:88-94`) — `POST /api/do/set {ch,state}`; updates button only on success.
- **`setAllDO(st)`** (`:95-102`) — `POST /api/do/set {mask:255|0}`; on success updates all `do0..do7`.
- **`loadStatus()`** (`:103-108`) — `GET /api/do/status` (`no-store`); loops `i<d.ch_count`, null-guards missing buttons; failures silent (empty catch).
- **`applyDoNames(names)`** (`:109-114`) — label = custom name or `Relay N` fallback.
- **`loadDoConfig()`** (`:115-131`) — `GET /api/do/config`; fills `doNameInputN`, labels (with fallback), `doInvN.checked = !!((invert>>i)&1)`; failures silent.
- **`saveDoConfig()`** (`:132-147`) — trims names to 16 chars, builds invert mask, `POST /api/do/config`.
- **`window.onload`** (`:148`) — `loadStatus(); loadDoConfig();` (parallel, no ordering dependency).

---

## 7. Server API

Handlers: GETs in `handlers_unified/do_handler.cpp`, POSTs in `handlers/do_handler.cpp`.

### `GET /api/do/status` (`do_handler.cpp:12`)

```json
{"ch_count":8,"mask":3,"outputs":[true,true,false,false,false,false,false,false]}
```

`mask` = cached logical mask (`tca9554GetMask()`); no I2C read. Off-board stub → 404 `{"error":"not found"}` (`:49-54`).

### `GET /api/do/config` (`do_handler.cpp:31`)

```json
{"ch_count":8,"invert":5,"names":["Pump","","Valve 3","","","","",""]}
```

Reads `configManager.getSection("do")`; missing names default to `""`.

### `POST /api/do/set` (`handlers/do_handler.cpp:25`)

Request: `{"ch":0-7,"state":bool}` **or** `{"mask":0-255}` (`mask` wins if both present).
Responses: `200 {"success":true,"message":"OK"}` (+ `tca9554SaveState()`); `400 {"success":false,"message":"JSON invalido"}` on bad JSON; `400 {"success":false,"message":"Falta ch ou mask"}` when neither key; `500 {"success":false,"message":"I2C NACK"}` on I2C failure **or** out-of-range channel (see DO-4).

### `POST /api/do/config` (`handlers/do_handler.cpp:65`)

Request: `{"names":[...8 strings],"invert":0-255}`. Names clamped to 16 chars; `saveSection("do")` → 500 `{"success":false,"message":"Save failed"}` on failure; success applies `tca9554SetInvert(invert)` live + `sendVirtualMessage("DO: config saved via web", INFO)` (reaches Web Serial Monitor) → `200 {"success":true,"message":"OK"}`. **Unconditional invert overwrite** (see DO-2).

---

## 8. Persistence

Two files, both atomic `.tmp` → validate → remove → rename:

- **`/config/do.json`** (4 KB budget) via `ConfigManager` section `"do"` — `{invert:0-255, names:[8 strings]}`. Defaults `invert=0`, 8×`""` (`ConfigManager.cpp:205-209`); ensured on load (`:490-495`); saved by `saveStem(STEM_DO, stemPath("do"), …, 4096)` (`:891-893`); `do` in `stems[]` (`:1036`) and own-file exclusions (`:1159`, `:2193-2195`).
- **`/config/tca9554_do.json`** — `{mask:N}` output state, written by `tca9554SaveState()` on **every** successful `/api/do/set` (`tca9554_do.cpp:77-91`); restored by `tca9554LoadState()` at boot (`:93-110`, all-off default when missing/corrupt).

---

## 9. Runtime engine (TCA9554 driver)

`chatgpt/src/tca9554_do.{h,cpp}`, active only when `BOARD_HAS_TCA9554_DO==1`.

- **`tca9554Begin()`** (`tca9554_do.cpp:39`) — `Wire.begin(SDA=42, SCL=41)`, polarity reg `0x00`, config reg `0x00` (all outputs), then `tca9554LoadState()`; boot applies saved invert after (`setup_impl.h:272-274`).
- **Logical vs physical**: cache `_mask` holds LOGICAL state; chip gets `_mask ^ _invert` (`tca9554SetAll`, `:50-54`). Chip polarity register stays `0x00` — inversion is software-only (`tca9554_do.h:44-48`).
- **`tca9554SetOutput(ch,state)`** (`:56-62`) — range-checks `ch<8`, recomputes mask, delegates to `tca9554SetAll`; returns I2C ACK status.
- **`tca9554SetInvert(m)`** (`:68-71`) — stores mask, re-applies `_mask ^ _invert` immediately (no reboot).
- **Proximity coupling**: relay pulse outputs (`dodur`/`dodelay`) drive the same expander (`proximity_manager.cpp:206,769,820-835`); `/proximity` page also reads `GET /api/do/status` + `GET /api/do/config` (`page_proximity_asset.h:738-766`).

---

## 10. i18n keys

All confirmed present in `lang_en_ui.h` (PT mirrors in `lang_pt_ui.h:1685-1694,1725-1727`):

`btnHome` (`:7`, used as `data-i18n-title`), `doConnErr`, `doErrPrefix`, `doFailed`,
`doCfgTitle`, `doCfgName`, `doCfgInvert`, `doCfgSave`, `doCfgSaved`, `doCfgTipName`,
`doCfgInvertTip` (`:1685-1694`), `doTipToggle`, `doTipSetAllOn`, `doTipSetAllOff`
(`:1725-1727`). JS `t()` fallbacks are English (`'❌ Error: '`, `'failed'`,
`'❌ Connection error'`, `'✅ Config saved'`). Hardcoded (not i18n): `H1`, `Output
Control`, `DO 1..8`, `Relay N` fallback, `ON`/`OFF`, `▶ All ON` / `■ All OFF`, both
`💡 …` notes.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| DO-1 | **Ethernet GET APIs missing; dead handler.** `GET /api/do/status` + `GET /api/do/config` are registered only on WiFi (`web_routes_do.cpp:38-39`); `ethernet_bootstrap_routes.h` mirrors only the two POSTs (`:768-771`). `ethernetHandleDoStatusGet` (`ethernet_bootstrap_handlers.h:838`) has no caller, so over W5500 Ethernet `loadStatus()`/`loadDoConfig()` silently fail (empty catch) and the page shows stale OFF/empty labels. | High | Add Ethernet GET branches for `/api/do/status` + `/api/do/config` calling the existing handlers (as the `ethernetHandleProximityTca9554Get` pattern does). |
| DO-2 | **Config POST wipes invert when field absent.** `doSec["invert"] = (doc["invert"] \| 0)` is unconditional (`handlers/do_handler.cpp:82`, mirror `:898`). The proximity page POSTs `{names}` only (`page_proximity_asset.h:614`), which resets the invert mask to 0 as a side effect. | Medium | Only assign `invert` when `doc.containsKey("invert")` (same guard style as the names array). |
| DO-3 | **PT error strings in API responses.** `do_handler.cpp:35,49` (and Ethernet `:860,873`) return `"JSON invalido"` / `"Falta ch ou mask"` while the page and `lang_en_ui.h` are English; `customAlert` surfaces `d.message` verbatim. | Low | Return `"Invalid JSON"` / `"Missing ch or mask"` (project language rule: code in English). |
| DO-4 | **Out-of-range channel reports `I2C NACK`.** `tca9554SetOutput` returns false for `ch>=8` (`tca9554_do.cpp:57`), but the handler maps any false to `500 "I2C NACK"` (`do_handler.cpp:57-60`); `mask>255` is silently truncated by the `uint8_t` cast. | Low | Validate `ch<8` → `400` with a clear message; mask out of range → `400` instead of truncating. |
| DO-5 | **Route registry omits the config endpoints.** `route_registry_api.cpp` lists `GET /api/do/status` (`:123`) and `POST /api/do/set` (`:284`) but not `GET /api/do/config` nor `POST /api/do/config`, so `isKnownApiPath()` misses two live endpoints. | Low | Add `{"GET", "/api/do/config"}` and `{"POST", "/api/do/config"}` to the tables. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_do_asset.h` |
| Renderer | `chatgpt/src/webpages/renderers/do_renderer.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_do.{h,cpp}` |
| GET handlers (unified) | `chatgpt/src/webpages/handlers_unified/do_handler.{h,cpp}` |
| POST handlers (WiFi body-accum) | `chatgpt/src/webpages/handlers/do_handler.{h,cpp}` |
| Ethernet page + POST mirrors | `chatgpt/src/ethernet_bootstrap_routes.h:284-290,767-772`, `chatgpt/src/ethernet_bootstrap_handlers.h:834-921` |
| Ethernet shared page stream | `chatgpt/src/web_shared_pages.cpp:202-206`, `web_shared_pages.h:39` |
| Relay engine | `chatgpt/src/tca9554_do.{h,cpp}` |
| Config storage | `chatgpt/src/ConfigManager.cpp:205-209,490-495,891-893,1036` (`do` stem → `/config/do.json`; state → `/config/tca9554_do.json`) |
| Boot init | `chatgpt/src/setup_impl.h:272-275` |
| Board flag | `chatgpt/src/board_pins.h:53` (`1` on 8DI8DO), `chatgpt/src/config_defaults.h:223-224` (default `0`) |
| Manifest / self-test | `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp:290`, `selftest_handler.cpp:159` |
| Web tests | `tests/web/test_do.js` (toggle, all-on/off, names fallback, config fill, trimmed POST + invert mask) |
| i18n | `chatgpt/src/languages/lang_en_ui.h:1685-1694,1725-1727`, `lang_pt_ui.h:1685-1694,1725-1727` |
