# Structures — `/structures`

> One-stop reference for the **Structures / Amada Welding page**: purpose, layout,
> every button/input, client logic, API endpoints, persistence and the firmware-side
> ISQ20 parsing/formatting engine. Written for both humans and AI agents.

---

## 1. Objective

Configure how the hub parses **Amada ISQ20 welding-machine lines** received over RS232:
auto-detection of the data structure vs manual selection of **ISQ20 MTF1** (classic RS232)
or **ISQ20 AWSE** (AWS3 mode), filtering of machine-sent header lines, and header
repeat interval. The page also shows live parsing diagnostics (from `/api/rs232-cols`)
and a static Amada error-code dictionary. Settings take effect **without reboot**
(read from `ConfigManager` at processing time).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /structures` |
| Feature flag | `ENABLE_STRUCTURES_PAGE` (`chatgpt/src/config_defaults.h:105`, default `1`; `chatgpt/user_config.h:252` sets `0`; several packs force `0`, e.g. `pack_waveshare.h:143`, `pack_radio.h:79`) |
| WiFi server | `chatgpt/src/webpages/routes/web_routes_config.cpp:98` — guarded by `#if ENABLE_STRUCTURES_PAGE==1`, streams via `streamStructuresPageToPrint()` |
| Ethernet (W5500) | `chatgpt/src/ethernet_bootstrap_routes.h:294` → `streamSharedStructuresPage()` (`chatgpt/src/web_shared_pages.cpp:120`) — **unguarded** (see STR-2) |
| API (POST) | `POST /api/config` with `section=system` + structures fields → `applyStructuresFromBody()` (`chatgpt/src/webpages/handlers_unified/config_handler.cpp:110`) |
| API (GET, load) | `GET /api/config?section=system` — returns **no** structures data (see STR-1) |
| API (diagnostics) | `GET /api/rs232-cols` (`chatgpt/src/webpages/handlers/rs232_history_handler.cpp:96`, gated `ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1`; Ethernet mirror `ethernet_bootstrap_routes.h:603`) |
| Extra API | `GET /api/config/structures` — Ethernet only (`ethernet_bootstrap_routes.h:545`); WiFi has no handler, only a manifest entry (`chatgpt/src/webpages/routes/route_registry_api.cpp:118`) |
| Auth | Basic auth (`ensureBasicAuth`) + HTML clients only (`allowHtmlClient`) on WiFi |
| Caching (WiFi) | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` |

Renderer order: shell → `STRUCTURES_TABS_JS` → `PAGE_STRUCTURES_BODY` →
`/config-manager.js` → `/auto.js` (`chatgpt/src/webpages/renderers/structures_page_renderer.cpp:86`).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home]                          ← always first child of .wrap (project rule)
 ├ H1 "🏭 Amada Welding"
 ├ .aw-tab-bar                        ← injected by STRUCTURES_TABS_JS before #sec-nav
 ├ #sec-nav      "🧬 Quick Navigation" (anchors: ℹ️ Overview, ⚙️ Configuration, 📈 Diagnostics, ⚠️ Errors)
 ├ #sec-overview "1) ℹ️ Amada Overview" (3 static info cards: auto detection, MTF1/AWSE, header filter)
 ├ #sec-config   "2) ⚙️ Amada Settings"
 │   ├ #amada-rs232-info → #amada-rs232-text + link "/rs232" ("✏️ configurar")
 │   └ form#structures-form (action='/api/config', method='POST')
 │       ├ hidden _html=1, structures_present=1
 │       ├ checkbox #structures_auto_detect
 │       ├ checkbox #amada_isq20_mtf1_enabled + checkbox #amada_isq20_awse_enabled
 │       ├ checkbox #amada_filter_headers
 │       └ select   #amada_header_every (0/10/20/30/50/100)
 ├ #sec-diagnostic "3) 📈 Quick Parsing Diagnostics" (#amada-diag-structure/cols/mode)
 ├ #sec-errors     "4) ⚠️ Quick Amada Error Dictionary" (static 7-row table, codes 61–83)
 └ [💾 Save]  (type=submit, form='structures-form', outside the <form>)
```

Tab bar groups: Overview (`sec-nav`+`sec-overview`), Configuration, Diagnostics, Errors;
labels via `t('tabStructOverview', 'Visão geral')` etc. (`structures_page_renderer.cpp:54`).

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Auto Detection | `structures_auto_detect` | checkbox | default on; when on, manual checkboxes are disabled client-side |
| ISQ20 MTF1 | `amada_isq20_mtf1_enabled` | checkbox | classic RS232 mode; mutually exclusive with AWSE in UI |
| ISQ20 AWSE | `amada_isq20_awse_enabled` | checkbox | AWS3 mode; server also forces exclusivity (see §7) |
| Filter headers | `amada_filter_headers` | checkbox | discards machine header lines; default on |
| Header every | `amada_header_every` | select 0/10/20/30/50/100 | `0` = "Only at start"; default `20` |
| RS232 status | `amada-rs232-text` | read-only div | filled from `saved_configs.rs232_*` (baud/parity/enabled) |

Presence marker: hidden `structures_present=1` distinguishes "form submitted" from
"field absent" (`config_handler.cpp:112`). Unchecked checkboxes are absent from
urlencoded `FormData` → read as false.

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| Quick-nav anchors | `href='#sec-overview/#sec-config/#sec-diagnostic/#sec-errors'` — in-page jumps, no fetch |
| Tab buttons (`.aw-tab-btn`) | injected (Overview/Configuration/Diagnostics/Errors); toggle section visibility, no server round-trip |
| ✏️ configurar | link `/rs232` next to the RS232 status line |
| 💾 Save (submit) | `disableBtn` + `showLoading()` → `POST /api/config` (urlencoded, `section=system`) → `customAlert` + `showSavedCard()` on `d.success` |

---

## 6. Client-side logic (inline JS in `page_structures_asset.cpp:173`)

- **`syncManualState()`** (`:190`) — disables both manual checkboxes while auto-detect is checked.
- **`bindEvents()`** (`:236`) — auto checkbox toggles `syncManualState()`; MTF1/AWSE change handlers uncheck the other (skipped while auto is on).
- **`loadPageData()`** (`:261`) — `GET /api/config?section=system`, fills the 4 checkboxes + select from `d.structures` (defaults: auto `true`, filter `true`, header_every `20`), renders the RS232 info line from `d.saved_configs.rs232_*` (fallback baud list, index 3 = 9600), then calls `refreshDiagnostic(d.saved_configs)`. **Both `d.structures` and `d.saved_configs` are never returned by this endpoint — see STR-1.**
- **`refreshDiagnostic(savedConfigs)`** (`:206`) — `GET /api/rs232-cols`, fills Detected Structure / Column Count; Mode = `"Auto detect"` only if `savedConfigs.structures.auto_detect` is truthy, else `"Manual"`; appends `(ok)` (green) for `amada_isq20_awse`/`amada_isq20_mtf1`, `(unrecognized)` (yellow) when cols > 0 but unknown.
- **Submit handler** (`:307`) — `preventDefault`; `fd.set('section','system')`; POST urlencoded; `d.success` → `customAlert(d.message || sst('alertSaved', ...))` + `showSavedCard()` + `refreshDiagnostic(null)`; failure → `sst('alertSaveError', ...)`; network error → `sst('emailErrorPrefix', 'Error: ') + message`; `finally` → `hideLoading()` + `enableBtn(btn)`.
- Uses `window._t` with `localStorage ui_lang_cache` fallback, `customAlert`, no arrow functions — project rules honoured.

---

## 7. Server API

### `POST /api/config` (`section=system`) — `applyStructuresFromBody()` (`config_handler.cpp:110`)

| Field | Handling |
|---|---|
| `structures_auto_detect` (or `structures_present` marker) | `setStructuresAutoDetectEnabled()` |
| `amada_isq20_mtf1_enabled` | `setAmadaISQ20MTF1Enabled()`; if `!auto && mtf1 && awse` → AWSE silently forced `false` (`:123`) |
| `amada_isq20_awse_enabled` | `setAmadaISQ20AWSEEnabled()` (after the collapse above) |
| `amada_filter_headers` | `setAmadaFilterHeaders()` |
| `amada_header_every` | `toInt()`; `<0` or `>100` → silently reset to `20` (`:133`); empty → unchanged |

No-op (no structures keys) → returns without touching `msg/upd` (`:115`). Runs for **any**
`section` value, not just `system` (called unconditionally at `:429` — see STR-5).
Success persists via `saveSection("system")` + per-setter `markDirtyAndScheduleSave()`.

### `GET /api/config?section=system` (`config_handler.cpp:143`)

Returns only `system_name`, `system_language`, `mdns_enabled`, `ntp` (`:184`) —
**no `structures`, no `saved_configs`**. The full structures snapshot is served instead by
`GET /api/configs` via `fillStructuresInConfigs()` (`chatgpt/src/structures.cpp:8`):

```json
{ "structures": { "auto_detect": true,
  "amada_isq20_mtf1": {"enabled": false}, "amada_isq20_awse": {"enabled": false},
  "amada_filter_headers": true, "amada_header_every": 20 } }
```

### `GET /api/rs232-cols` (`rs232_history_handler.cpp:96`)

`{"cols": N, "date": "DD-MM-YYYY", "structure": "amada_isq20_mtf1"|"amada_isq20_awse"|"chart", "col_names": [...]}`.
Auto-inference when neither flag is on but auto-detect is: cols ≥ 16 → AWSE (21 names),
cols 10/12 → MTF1 (12 names) (`:204`). 404 when charts page disabled; 30 s PSRAM date cache.

### `GET /api/config/structures` (Ethernet only, `ethernet_bootstrap_routes.h:545`)

`{"structures": {"auto_detect", "amada_filter_headers", "amada_header_every", "amada_isq20_mtf1": {"enabled"}, "amada_isq20_awse": {"enabled"}}}`.

---

## 8. Persistence

In-memory `config["structures"]` object; getters with defaults in
`chatgpt/src/ConfigManager.h:1300` (`mtf1/awse → false`; `auto_detect → true` at `:1318`;
`amada_header_every → 20` at `:1325`; `amada_filter_headers → true` at `:1336`);
defaults seeded in `chatgpt/src/ConfigManager.cpp:179` (+ backfill at `:420`).
Every setter calls `markDirtyAndScheduleSave()` (`ConfigManager.h:1526,1534,1541,1548`,
`setAmadaHeaderEvery` at `:1330`). The explicit POST save targets stem `"system"`
(`config_handler.cpp:453`) → `/config/system.json`, whose writer persists only
`printer`/`system_name`/`system_language`/`mdns_enabled`/`config["system"]`
(`ConfigManager.cpp:2115`) — the `structures` object itself travels in the `core` stem
(not excluded at `ConfigManager.cpp:2093`). Atomic `.tmp` → validate → rename writes.

---

## 9. Runtime engine (ISQ20 parsing / formatting)

All consumers read flags at processing time → changes apply immediately.

- **`chatgpt/src/structures.cpp`** — `looksLikeISQ20_MTF1/AWSE`, `buildISQ20_MTF1/AWSE_Table`,
  `tryFormatISQ20_MTF1/AWSE` (short names `ISQ20_MTF1_SHORT_NAMES[12]`, `ISQ20_AWSE_SHORT_NAMES[21]`);
  `processEmailBodyInsertISQ20Tables()` (`:575`) and `buildISQ20TablesChunkByLinesBudget()` (`:615`):
  auto-detect ignores manual toggles, manual mode follows flags; `appendISQ20StructuresToEmail()` (`:557`).
- **`chatgpt/src/diagnostics_impl.h`** — `getAmadaFilterHeaders()` gates header-line filtering (`:1205`);
  MTF1-vs-AWSE vote with threshold (`:1266`); `getAmadaHeaderEvery()` drives header re-insertion (`:1337`).
- **`chatgpt/src/data_sd_pipeline_impl.h:210,426`** — same auto/manual vote for SD/email formatting.
- **`chatgpt/src/webpages/handlers/rs232_history_handler.cpp:204`** — column-count auto-inference for `/api/rs232-cols`.
- **`chatgpt/src/manage_email_impl.h:19,406`**, **`chatgpt/src/loop_impl.h:613`** — `isq20Active` (auto or either manual flag) enables ISQ20 email path.
- Home link rendered when `ENABLE_STRUCTURES_PAGE==1` (`chatgpt/src/webpages/renderers/home_page_renderer.h:203`).

---

## 10. i18n keys

All confirmed in `chatgpt/src/languages/lang_en_ui.h`:
`structPageTitle` (`:443`), `btnHome` (`:7`), `btnSave` (`:8`), `structAutoDetectLbl` (`:770`),
`structFilterLbl` (`:773`), `structTipHome`, `structTipAutoDetect`, `structTipMtf1`,
`structTipAwse`, `structTipFilter`, `structTipHeaderEvery`, `structTipSave` (`:1822`),
`tabStructOverview`, `tabStructConfig`, `tabStructDiagnostic`, `tabStructErrors` (`:710`),
`alertSaved` (`:957`), `alertSaveError` (`:958`), `emailErrorPrefix` (`:503`).
Static bodies (overview cards, diagnostics hint, error dictionary) and select options have **no** i18n keys (see STR-4).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| STR-1 | **Page loads from an endpoint that never returns its data.** `loadPageData()` fetches `GET /api/config?section=system` (`page_structures_asset.cpp:262`) and reads `d.structures` (`:265`) plus `d.saved_configs` (`:285`), but `handleConfigGet` for `section=system` returns only `system_name`/`system_language`/`mdns_enabled`/`ntp` (`config_handler.cpp:184`). Consequence: the form always shows defaults, the RS232 info line always uses the hardcoded fallback, and `refreshDiagnostic()` always reports Mode `"Manual"` (`page_structures_asset.h:216`) even with auto-detect on. | High | Load from `GET /api/configs` (`fillStructuresInConfigs`, `structures.cpp:8`) or add the `structures` object to the `section=system` GET response. |
| STR-2 | **Ethernet `/structures` route ignores the feature flag.** `ethernet_bootstrap_routes.h:294` has no `#if ENABLE_STRUCTURES_PAGE==1` guard, unlike WiFi (`web_routes_config.cpp:98`) and neighbouring Ethernet routes; with the flag off it streams an empty shell (`web_shared_pages.cpp:120` returns 0). | Low | Wrap the Ethernet branch in `#if ENABLE_STRUCTURES_PAGE==1` with a 404 fallback. |
| STR-3 | **Silent server-side mutation of user input.** Both flags on with auto off → AWSE forced `false` (`config_handler.cpp:123`); out-of-range `amada_header_every` → silently reset to `20` (`:133`); any 0–100 value accepted while the UI offers only 0/10/20/30/50/100. Direct API callers get no error. | Low | Return 400 with a message for conflicting flags / out-of-range values, or at least echo the applied values. |
| STR-4 | **PT/EN mix and non-translated strings.** Select options `"10 linhas"`…`"100 linhas"` (`page_structures_asset.cpp:111`), link `"✏️ configurar"` (`:76`), tab fallback labels `'Visão geral'`/`'Configuração'`/`'Diagnóstico'`/`'Erros'` (`structures_page_renderer.cpp:55`); overview, diagnostic hint and error-dictionary bodies are hardcoded English with no `data-i18n`. | Low | Add i18n keys for options/link/tabs fallbacks (EN fallback) and the static bodies. |
| STR-5 | **Structures applied for every POST section.** `applyStructuresFromBody()` runs unconditionally (`config_handler.cpp:429`), so any `POST /api/config` (e.g. `section=wifi`) carrying those keys mutates structures state and appends `"Structures "` to an unrelated message. | Low | Call it only when `section == "system"`. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_structures_asset.cpp` (+ `.h`) |
| Renderer + tab bar | `chatgpt/src/webpages/renderers/structures_page_renderer.{h,cpp}` |
| Routes (WiFi + Ethernet) | `chatgpt/src/webpages/routes/web_routes_config.cpp:98`, `chatgpt/src/ethernet_bootstrap_routes.h:294` |
| POST/GET handlers | `chatgpt/src/webpages/handlers_unified/config_handler.cpp` (`applyStructuresFromBody`, `handleConfigGet/Post`) |
| Diagnostics API | `chatgpt/src/webpages/handlers/rs232_history_handler.cpp:96` |
| Ethernet structures API | `chatgpt/src/ethernet_bootstrap_routes.h:545` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`structures` object; `system`/`core` stems) |
| Runtime engine | `chatgpt/src/structures.{h,cpp}`, `chatgpt/src/diagnostics_impl.h`, `chatgpt/src/data_sd_pipeline_impl.h` |
| Feature flag | `chatgpt/src/config_defaults.h:105`, packs (e.g. `pack_waveshare.h:143`) |
| Tests / manifests | `tests/test_pages.py:53` (page-load only), `test_manifest_handler.cpp:292`, `selftest_handler.cpp:161`, `tests/native/test_config_defaults.cpp:36` |
