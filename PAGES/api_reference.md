# API Reference — `/api-reference`

> One-stop reference for the **API reference page**: purpose, layout, filters/navigation,
> client logic, backing data source, feature-flag filtering engine and i18n. Written for
> both humans and AI agents.

---

## 1. Objective

Browsable catalog of the firmware HTTP API surface: 16 collapsible per-feature sections
listing method + route + description for 274 `/api` endpoints (173 GET + 154 POST routes
in the registry). Every `GET` row is live-probed —
clicking the route opens a pretty-printed JSON response in a new tab, and a `▶` try-button
expands the response inline. Sections/rows not compiled into the firmware are hidden
client-side via injected feature flags. Fully stateless: no forms, no saves.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /api-reference` |
| Feature flag | `ENABLE_API_REFERENCE_PAGE` (`chatgpt/src/config_defaults.h:102-103`, default `1`); forced `0` in `chatgpt/user_config.h:285` and in packs `radio`, `environment`, `psram`, `cam`, `waveshare`, `rambox` (e.g. `chatgpt/packs/pack_waveshare.h:159-160`, `chatgpt/packs/pack_rambox.h:116-117`) |
| WiFi server | `chatgpt/src/webpages/routes/web_routes_core.cpp:273-292` — streams via `streamApiReferencePage()` |
| Ethernet (W5500) | `chatgpt/src/ethernet_bootstrap_routes.h:235` (`GET /api-reference` branch) → `streamSharedApiReferencePage()` (`chatgpt/src/web_shared_pages.cpp:260`; decl `chatgpt/src/web_shared_pages.h:71`) |
| Registry entry | `{"GET", "/api-reference"}` (`chatgpt/src/webpages/routes/route_registry_api.cpp:295`, 173 GET + 154 POST routes total, 274 under `/api`) |
| Manifests | `PAGE("/api-reference", ENABLE_API_REFERENCE_PAGE)` in `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp:313` and `chatgpt/src/webpages/handlers_unified/selftest_handler.cpp:164` |
| Home link | `SYSTEM_SECTION_API_REF_BUTTON` gated at `chatgpt/src/webpages/renderers/home_page_renderer.h:412`; also linked from `chatgpt/src/webpages/assets/fragments_system_asset.cpp:21` (`"🔌 Referência de API"`) |
| Auth | WiFi: `ensureBasicAuth` + `allowHtmlClient` (`web_routes_core.cpp:275-276`) — subject to session limits |
| Caching | `Cache-Control: no-cache, no-store, must-revalidate`, **no ETag** (`web_routes_core.cpp:285`) |

Renderer order: shell (`streamPageShellStart`) → `<script>var _F={...}</script>` → `API_REFERENCE_PAGE` (HTML + inline JS) → auto-JS footer → shell end (`chatgpt/src/webpages/renderers/api_reference_renderer.h:11-45`).

---

## 3. Layout

```
div.page.api-reference > div.wrap
 ├ div.actions.nav-inicio > a.btn[href='/'][data-i18n='btnHome']  ← always first child of .wrap
 ├ H1 "🔍 API Reference"  [data-i18n='apiRefTitle']  (English fallback; PT served from packs)
 ├ 16× <details>  (native disclosure widgets, no JS needed to expand)
 │   └ summary [data-i18n] + <span class="cnt">N</span>
 │   └ div.table-wrapper > table
 │       ├ tr header: Método | Rota | Descrição
 │       └ tr per endpoint: <span class="m g|p|d">GET|POST|DEL</span> | <td class="r">/api/…</td> | <td class="ds">…</td>
 └ <script>  (link wiring + try-buttons + _F filtering)
```

Sections (badge → verified row count): Status 11, Config Global 4, RS232 11, Rede 20,
Email 12, Comandos Agendados 10, Sensores 18, Proximidade 14, Modbus 15, SD Card 14,
OTA & Sistema 6, GRBL 28→**26**, Câmara 3, Áudio 16→**17**, Controlo de Acesso 4,
Saídas Digitais 2 (see finding APIR-1).

---

## 4. Inputs / buttons

The page has **no form inputs** (`<input>`/`<select>`/submit). All interaction:

| Control | Behaviour |
|---|---|
| 🏠 Home link (`btnHome`, fallback `"🏠 Home"`) | link `/` — first child of `.wrap` |
| `<details>/<summary>` per section | native expand/collapse; no server round-trip |
| Route link (GET rows only) | opens pretty-printed JSON in a new tab via blob URL (`page_api_reference_asset.h:346-357`) |
| `▶` try-button (GET rows only) | toggles hidden response row with `<pre class="api-pre">` showing pretty JSON or raw text inline (`:360-384`); `…` while loading, `▲` when open |
| Feature filtering | automatic: rows/sections with `data-feat="<key>"` hidden when `_F[key]` is falsy (`:387`) |

`POST`/`DEL` rows render badge + route + description only — no link, no try-button (see APIR-3).

---

## 5. Client-side logic (inline JS in `page_api_reference_asset.h:334-388`)

- **Base-URL bootstrap** (`:335-337`) — `fetch('/api/status', {cache:'no-store'})`, builds
  `base = 'http://' + (d.ip_address || location.hostname) + port` so endpoint links use the
  device IP even when opened via mDNS/hostname.
- **GET-row wiring** (`:338-384`) — selects `span.g` only (i.e. GET badges): extracts the
  route from the sibling cell, replaces cell text with `<a class="r" target="_blank">`,
  click handler `preventDefault` → `fetch(url)` → `JSON.stringify(JSON.parse(t), null, 2)`
  fallback raw → blob → `window.open(blob)`. A `▶` button + hidden 3-colspan response row
  is inserted after each GET row for inline testing (`'Erro: ' + e` on failure).
- **Feature filtering** (`:387`) — `if (typeof _F !== 'undefined')` hide every
  `[data-feat="<key>"]` whose `_F[key]` is falsy. No arrow functions — project rule honoured.
- **auto.js exemption** — the firmware-version hard-reload check skips this page and
  `/status` (`chatgpt/src/webpages/auto_js.h:984`).

---

## 6. Server API

The page has **no dedicated JSON endpoint** — the endpoint table is a hand-maintained
static table in the asset, NOT generated from `route_registry_api.cpp`. It therefore
drifts from the real registry (see APIR-2). Runtime fetches issued by the page:

| Fetch | Purpose |
|---|---|
| `GET /api/status` (`{cache:'no-store'}`) | resolve device IP for absolute endpoint URLs |
| `GET <any GET route in the table>` | try-button inline response / new-tab pretty JSON |

---

## 7. Persistence

Stateless reference page: no saves, no NVS, no LittleFS, no config sections. Nothing to
persist; verified — the asset contains no form and the JS issues only `GET` requests.

---

## 8. Runtime engine (flag injection + granularity)

`streamApiReferencePage()` emits `var _F={modbus, proximity, sdcard, mq135, ota, sniffer,
db25, debug_api, ethernet, rs232, telnet, sensors, do_out, grbl, camera, uda1334a}`
from compile-time flags (`api_reference_renderer.h:16-37`). Notable mappings:
`sensors = (ENABLE_AHT20_BMP280 || ENABLE_MQ135)`, `do_out = BOARD_HAS_TCA9554_DO`,
`debug_api` is `#ifdef`-guarded (defaults `0`). Filtering granularity is mixed:
`Status`, `Rede` and `Comandos Agendados` `<details>` carry **no** `data-feat` — only
individual rows are flagged (`sniffer`, `telnet`/`ethernet`, `modbus` respectively),
while `RS232`, `Sensors`, `Proximity`, `Modbus`, `SDCard`, `OTA`, `GRBL`, `Camera`,
`Audio`, `DigitalOut` flag the whole `<details>` block (row-level extras: `mq135`,
`do_out` under Proximity, `debug_api` under OTA).

---

## 9. i18n keys

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 209 key(s) — `apiRefDescAAccessConfig`, `apiRefDescAAccessConfig02`, `apiRefDescAAccessKickAll`, `apiRefDescACameraStart`, `apiRefDescACameraStatus`, `apiRefDescAChartCaps`, `apiRefDescAClearApLock`, `apiRefDescAClearBuffer`, `apiRefDescAConfig`, `apiRefDescAConfig02`, `apiRefDescAConfigAccessPoint`, `apiRefDescAConfigAccessPoint02`, `apiRefDescAConfigSavedNetworks`, `apiRefDescAConfigSavedNetworks02`, `apiRefDescAConfigSavedNetworks03`, `apiRefDescAConfigs`, `apiRefDescADailyReboot`, `apiRefDescADailyReboot02`, `apiRefDescADb25Inject`, `apiRefDescADoSet`, `apiRefDescADoStatus`, `apiRefDescAEmailTest`, `apiRefDescAEmaildayExtraEmails`, `apiRefDescAEmaildayExtraEmails02`, `apiRefDescAEnableOta`, `apiRefDescAEthConfig`, `apiRefDescAEthStatus`, `apiRefDescAFactoryReset`, `apiRefDescAFirmwareUpload`, `apiRefDescAGrblAutoDetectBaud`, `apiRefDescAGrblCancel`, `apiRefDescAGrblControlToke` … (209 no total). HTML fallback text is English; PT is served from the packs.

Used by the page: `btnHome`, `apiRefTitle`, `apiRefMethod`, `apiRefRoute`,
`apiRefDescription`, `apiRefSecStatus/Config/Rs232/Network/Email/Scheduled/Sensors/
Proximity/Modbus/Sdcard/Ota/Grbl/Camera/Audio/Access/DigitalOut`, `apiRefTestBtn`
(`data-i18n-title` on try-buttons). All exist in `chatgpt/src/languages/lang_en_ui.h`
(`apiRef*` block, e.g. `apiRefTitle`/`apiRefHome` at `:1729-1730`)
with matching entries in `lang_pt_ui.h`. `apiRefHome`
(`lang_en_ui.h:1730`) is defined but **never used** — the Home link uses `btnHome`
(`page_api_reference_asset.h:33`); see APIR-4.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| APIR-1 | **Stale count badges.** GRBL badge says `28` but the section holds 26 endpoint rows (`page_api_reference_asset.h:248` vs `:251-276`); Audio badge says `16` but holds 17 rows (`:291` vs `:294-310`). Verified by script; all other 14 badges match. | Low | Fix badges to `26`/`17`, or generate counts from the DOM at load instead of hard-coding. |
| APIR-2 | **Static table omits registered API areas.** No CAN (`/api/can/*`, `route_registry_api.cpp:108-110`), buzzer/sound (`:104`, `:196`), HX711/INA226/MAX31865 (`:136-137`, `:145`, POSTs `:309-319`), unikon/usb-server/ipg-laser/printer/structures families, `/api/do/config` (`web_routes_do.cpp:39-40`), or radio extras (`toggle/seek/eq/schedule/resume/sleep/upnp/library/playlists`, `web_routes_audio.cpp:142-172`). Table is hand-edited so it silently drifts. | Medium | Generate the table from `route_registry_api.cpp` at build time (pre-script) or add a checklist step to `agents/docs/self-test.md`. |
| APIR-3 | **Only GET rows are interactive.** Row wiring selects `span.g` (`page_api_reference_asset.h:338`), so POST/DEL routes get no link and no try-button — over half the Modbus/GRBL/SD rows are inert. | Low | Extend try-buttons to POST (empty-body probe with confirm) or mark non-GET rows as reference-only via a legend. |
| APIR-4 | **Dead `apiRefHome` key + hardcoded PT `<title>`.** `apiRefHome` is defined in both language files (`lang_en_ui.h:1730`) but never referenced (Home link uses `btnHome`). HTML fallbacks are English now (fixed since the finding was written); remaining PT hardcoded string is the `<title>` emitted by `streamApiReferencePage()` (`api_reference_renderer.h:12`, UTF-8 bytes for "Referência de API", no `data-i18n`). | Low | Drop `apiRefHome`; pass the title through the i18n dict like the H1. |
| APIR-5 | **No web tests.** `tests/web/` has zero coverage for this page (grep for `api-reference\|apiRef` finds nothing); badge drift (APIR-1) would have been caught by a row-count assertion. | Low | Add a DOM test: badge number equals row count per `<details>`, and every `td.r` route exists in `route_registry_api.cpp`. |

---

## 11. Source map

| Concern | File |
|---|---|
| Page HTML/CSS/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_api_reference_asset.h` |
| Renderer + `_F` injection | `chatgpt/src/webpages/renderers/api_reference_renderer.h` |
| WiFi route | `chatgpt/src/webpages/routes/web_routes_core.cpp:273-292` |
| Ethernet route | `chatgpt/src/ethernet_bootstrap_routes.h:231-237` |
| Ethernet shared wrapper | `chatgpt/src/web_shared_pages.cpp:260`, `chatgpt/src/web_shared_pages.h:71` |
| Ground-truth endpoint list | `chatgpt/src/webpages/routes/route_registry_api.cpp` (327 entries: 173 GET + 154 POST, 274 under `/api`) |
| Flag default | `chatgpt/src/config_defaults.h:102-103` (local override `chatgpt/user_config.h:285`) |
| Home entry point | `chatgpt/src/webpages/renderers/home_page_renderer.h:412` |
| Manifests (self-test / test contract) | `chatgpt/src/webpages/handlers_unified/selftest_handler.cpp:160`, `.../test_manifest_handler.cpp:291` |
| i18n strings | `chatgpt/src/languages/lang_en_ui.h` + `lang_pt_ui.h` (`apiRef*` block, 209 keys each) |
| Flag sanity test | `tests/native/test_config_defaults.cpp:35` |
