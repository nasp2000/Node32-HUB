# Home — `/`

> Landing page of the **Node32-Hub** firmware: a stateless, server-rendered link hub. No inputs, no saves, no `fetch` calls — every section is a set of `<a class='btn'>` navigation links injected at request time by `streamHomePageToPrint()`, gated by compile-time `ENABLE_*` flags, `SELECTED_PACK`, and two runtime config switches. Written for both humans and AI agents.

---

## 1. Objective

Serve `GET /` as the device's **index of all pages**: hero banner with pack-specific subtitle, then link sections (Modules → Monitoring → Recipients/Alerts → Commands → Networks → System) plus footer with version and project links. The page itself collects nothing and persists nothing; all gating is decided server-side per request so disabled features leave zero HTML.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /` (also `GET /index.html` on Ethernet) |
| Feature flag | None — the landing page is always served; individual *buttons* are `#if`-gated per section (see §5) |
| WiFi server | `chatgpt/src/webpages/routes/web_routes_core.cpp:185` — AP-only mode serves minimal WiFi page (`PAGE_WIFI_AP_MINIMAL`, `:188`); otherwise `ensureBasicAuth` + `allowHtmlClient`, then streams via `streamHomePageToPrint()` (`:216`) |
| Ethernet (W5500) | `chatgpt/src/ethernet_bootstrap_routes.h:155` (`/` or `/index.html`) → `streamSharedHomePage()` (`chatgpt/src/web_shared_pages.cpp:70`), same renderer |
| Renderer | `chatgpt/src/webpages/renderers/home_page_renderer.h` — `streamHomePageToPrint()` (`:54`), `buildHomePageEtag()` (`:26`), `shouldShowWifiButtonOnHome()` (`:13`) |
| Page asset | `chatgpt/src/webpages/assets/page_home_asset.cpp:3` (`PAGE_HOME[] PROGMEM`; header `page_home_asset.h:5`) — skeleton with `%%TOKENS%%` only, no inline JS |
| System fragment | `chatgpt/src/webpages/assets/fragments_system_asset.cpp:3-27` (`SYSTEM_SECTION`, `..._SETTINGS_BUTTON`, `..._OTA_BUTTON`, `..._ACCESS_BUTTON`, `..._API_REF_BUTTON`, `..._CLOSE`) |
| Auth | WiFi: `ensureBasicAuth` + `allowHtmlClient` (`web_routes_core.cpp:194-195`); Ethernet: `ethernetIsAuthorized` except `/healthz` + `/favicon.ico` (`ethernet_bootstrap_routes.h:136`) |
| Caching | `ETag = buildHomePageEtag()` (firmware build + charts/serial/wifi flags, `home_page_renderer.h:26-37`); `Cache-Control: private, max-age=15, must-revalidate`; 304 on match (WiFi `:197-204`, Ethernet `:156-162`) |

---

## 3. Layout

```
main.page.home > div.wrap
 ├ div.home-hero
 │   ├ H1 "🤖 Node32-Hub"
 │   └ %%HERO_SUBTITLE%%            ← pack-specific subtitle (see §9)
 ├ %%MODULES_SECTION%%              ← "🔬 Modules" + module links (§5)
 ├ div.home-section (Monitoring)
 │   ├ H2 "📊 Monitoring"
 │   ├ %%MONITOR_BUTTON%%           ← /monitor (gated)
 │   ├ %%EXPLORADOR_FICHEIROS%%     ← /modbus-live, /files, /dualfile, /sensor-charts
 │   ├ %%SERIAL_MONITOR_BTN%%       ← /serial-monitor (runtime-gated)
 │   └ [📊 Status]                  ← always present, /status
 ├ %%RECIPIENTS_SECTION%%           ← /recipients and/or /alerts-config
 ├ %%COMMANDS_SECTION%%             ← /commands-by-datetime, /commands
 ├ div.home-section (Networks)
 │   ├ H2 "🌐 Networks"
 │   └ %%WIFI_BUTTON%% %%ETHERNET_BUTTON%% %%NETWORKS_BUTTON%% %%EMAIL_BUTTON%% %%GOOGLE_SCRIPT_BUTTON%% %%FTP_BUTTON%%
 ├ %%SYSTEM_SECTION%%               ← "⚙️ Sistema" + /sdcard, /printer, /api-reference, /access, /ota
 └ div.footer-container > footer.footer
     ├ link "Node32-HUB" (github.com/nasp2000/Node32-HUB) + "since July 2025"
     ├ "Node32-Hub %%VERSION%%" + "Developed with Artificial Intelligence"
     └ ""There's a storm coming in""
```

Only `auto.js` is loaded (`<script src='/auto.js?v=%%VERSION%%'>`, `page_home_asset.cpp:54`); there is no inline `<script>` and no Home button (this page *is* home).

---

## 4. Inputs

None. The page contains **zero** `<input>`, `<select>`, or `<form>` elements — it is a pure navigation hub. (Kept as a separate section to preserve the 12-section page-doc structure; the full link inventory is in §5.)

---

## 5. Buttons

Section headers render only when at least one button in the section exists (compile-time `constexpr` checks). Fallback (pre-i18n) text is quoted verbatim.

| Button (fallback text) | Target | Gate |
|---|---|---|
| "🔬 Módulos" header | — | `kHasAnyModule` (`home_page_renderer.h:133-141`) |
| "🎛️ Player Harmonica Box" | `/radio-ui` (new tab) | `SELECTED_PACK == PACK_RADIO` (`:145`) |
| "🛠️ GcodeSender" | `/grbl-controller` (new tab) | `ENABLE_GRBL==1` (`:148`) |
| "🖨 Impressora LPT DB25" | `/lpt-db25` | `ENABLE_DB25==1` (`:151`) |
| "⚙ Modbus RTU" | `/modbus-rtu` | `ENABLE_MODBUS_RTU==1` (`:154`) |
| "🌐 Modbus TCP" | `/modbus-tcp` | `ENABLE_MODBUS_TCP==1` (`:157`) |
| "🧩 USB Server (USB/IP)" | `/usb-server` | `ENABLE_USBIP_SERVER==1` (`:160`) |
| "🔌 Interface CAN Bus" | `/can` | `ENABLE_CAN==1` (`:163`) |
| "📍 Gestão de Produção e OEE" + "📊 Gráficos e OEE" (new tab) | `/proximity`, `/proximity-dashboard` | `ENABLE_PROXIMITY==1` (`:166-169`) |
| "🔔 Buzzer" | `/buzzer` | `ENABLE_BUZZER==1` (`:170`) |
| "🐦 Unikon RAMBOX" | `/unikon` | `ENABLE_UNIKON==1` (`:173`) |
| "🕊 Bricon Rambox" (green `background:#15803d`, dove `&#128330;` — deliberately distinct from the blue `&#128038;` Unikon button) | `/bricon` | `ENABLE_BRICON==1` (`home_page_renderer.h:182-184`) |
| "🔥 IPG Laser RS232" | `/ipg-laser` | `ENABLE_IPG_LASER==1` (`:176`) |
| "🔊 Placa de Som UDA1334A" | `/uda1334a` | `ENABLE_UDA1334A==1` (`:179`) |
| "📷 Câmara" (new tab) | `/camera` | `ENABLE_CAMERA==1` (`:182`) |
| "🌡️ Sensor do Ambiente" | `/sensor-config` | `ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1` (`:185`) |
| "🌫️ Sensor de Qualidade do Ar" | `/mq135-config` | `ENABLE_MQ135==1` (`:188`) |
| "⚖ Sensor de Peso HX711" | `/hx711` | `ENABLE_HX711==1` (`:191`) |
| "⚡ Monitor INA226 Corrente/Potência" | `/ina226` | `ENABLE_INA226==1` (`:194`) |
| "🔊 Sensor de Nível de Som" | `/sound` | `ENABLE_SOUND_SENSOR==1` (`:197`) |
| "🌧 Sensor RTD MAX31865" | `/max31865` | `ENABLE_MAX31865==1` (`:200`) |
| "🧱 Amada Welding" | `/structures` | `ENABLE_STRUCTURES_PAGE==1` (`:203`) |
| "🔌 Porta RS232" (kept last deliberately) | `/rs232` | `ENABLE_RS232==1 \|\| ENABLE_SNIFFER==1` (`:207`) |
| "📥 Receção de Dados" | `/monitor` | `ENABLE_PAGE_MONITOR==1` + `(SNIFER\|\|RS232)` + not RAMBOX pack + not 8DI8DO_POE board (`:307-314`) |
| "📈 Modbus — Dados Live" | `/modbus-live` | runtime: modbus section enabled + (rtu or tcp) (`:262-271`) |
| "🗂️ Explorador de Ficheiros" | `/files` | `ENABLE_PAGE_FILES==1` (`:273`) |
| "🗃️ Dual File Manager" (new tab) | `/dualfile` | `ENABLE_DUALFILE==1` (`:276`) |
| "📈 Monitorização Gráfica de Dados" (new tab) | `/sensor-charts` | `kHasChartsRoute` (monitor flag + data source) **and** runtime `getChartsPageEnabled()` (`:279-289`) |
| "📋 Serial Monitor" (new tab) | `/serial-monitor` | runtime `getSerialMonitorEnabled()` (`:295-303`) |
| "📊 Status" | `/status` | always (`page_home_asset.cpp:17`) |
| "📧 Destinatários" / "🚨 Configurar Alertas" (headers vary: both / recipients-only / alerts-only) | `/recipients`, `/alerts-config` | `ENABLE_PAGE_RECIPIENTS==1` / `ENABLE_PAGE_ALERTS==1` (`:221-238`) |
| "📡 Envio comandos por Hora e Data" / "📡 Envio comandos por Intervalo de Tempo" | `/commands-by-datetime`, `/commands` | `ENABLE_PAGE_COMMANDS==1` (`:246-254`) |
| "📶 Configurar Wi-Fi" (`id='btn-wifi-cfg'`) | `/wifi` | `ENABLE_WIFI==1` + runtime `shouldShowWifiButtonOnHome()` (`:322-326`) |
| "🌐 Configurar Ethernet" | `/ethernet` | `ENABLE_ETHERNET==1` (`:344`) |
| "🌐 Outras Redes" (no i18n key) | `/networks` | `ENABLE_TELNET==1` (`:354`) |
| "✉️ Configurar E-mail" | `/email` | `ENABLE_SMTP==1 \|\| ENABLE_GOOGLE_EMAIL==1` (`:334`) |
| "🌐 Google App Script" | `/google-script` | `ENABLE_PAGE_GOOGLE_SCRIPT==1` (`:364`) |
| "📤 FTP Upload" | `/ftp` | `ENABLE_PAGE_FTP==1` |
| "💾 Configurar microSD / PSRAM" | `/sdcard` | `ENABLE_PAGE_SDCARD==1` (`:375`) |
| "⚙️ Definições do Sistema" | `/printer` | always (`fragments_system_asset.cpp:9`) |
| "🔌 Referência de API" (no i18n key) | `/api-reference` | `ENABLE_API_REFERENCE_PAGE==1` (`home_page_renderer.h:380`) |
| "🔑 Acessos" | `/access` | always — **not** gated by `ENABLE_PAGE_ACCESS` (`:383`) |
| "🔄 Atualização Firmware (OTA)" | `/ota` | `ENABLE_OTA==1` (`:384`) |

---

## 6. Client-side logic

There is **none** on this page: `page_home_asset.cpp:3-54` contains no inline JS, no `fetch`, no `XMLHttpRequest`, no form handlers — only `<script src='/auto.js?v=%%VERSION%%'>`. All rendering is server-side token substitution in `streamHomePageToPrint()` (`home_page_renderer.h:82-416`): PROGMEM scan for `%%TOKEN%%`, `streamHomeProgmemSpan()` the literal spans, `out.print()` the gated buttons. `auto.js` supplies only shared services (i18n `data-i18n` replacement, `customAlert`, loading overlay).

---

## 7. Server API

The home page calls **no** `/api/*` endpoint itself (verified: no `fetch` in `page_home_asset.cpp`, no URL building in `home_page_renderer.h`). Related server surface:

- `GET /` (WiFi `web_routes_core.cpp:185`; Ethernet `ethernet_bootstrap_routes.h:155`) — the only route this doc covers.
- `GET /auto.js`, `GET /core.css` — shared assets referenced by the page.
- Runtime flags read per request, not via API: `configManager.getChartsPageEnabled()` (`:287`), `getSerialMonitorEnabled()` (`:297`), modbus section `enabled/rtu_enabled/tcp_enabled` (`:264-266`), `getWifiDisabled()` + `wifi_hybrid.enabled` (`:13-24`).

---

## 8. Persistence

Stateless. The route performs **no writes** — no `ConfigManager` setters, no LittleFS/NVS access, no `markDirtyAndScheduleSave()`. Per-request reads only (see §7). AP-only fallback (`web_routes_core.cpp:188-193`) serves `PAGE_WIFI_AP_MINIMAL` with `Cache-Control: no-cache` instead of the hub.

---

## 9. Runtime engine

- **Token streamer** (`home_page_renderer.h:82-416`): single pass over `PAGE_HOME` in PROGMEM, 512 B chunked copy (`streamHomeProgmemSpan`, `:39-52`); unknown `%%..%%` logs to Serial and is skipped (`:411-413`).
- **Hero subtitle** (`:101-128`): pack switch — RAMBOX → `"Pigeon Race Management System"`; CAM/CAM_P4 → `"IP Camera &mdash; Live Stream & Record"`; RADIO → `"Network Radio & MP3 Player"`; ENVIRONMENT → `"Environmental Sensors Dashboard"`; P4_CNC/S3_CNC → `"CNC Motion & G-code Control"`; P4_USB_SERVER → `"USB/IP Device Sharing Hub"`; PSRAM → `"Development & Testing Platform"`; 8DI8DO_POE board → `"Industrial I/O &mdash; 8DI 8DO Controller"`; default → `"Serial Parallel Automation Node"`.
- **ETag inputs** (`:26-37`): `FIRMWARE_BUILD` + charts flag + serial-monitor flag + WiFi-button visibility, so any runtime toggle invalidates the browser cache.
- **Version**: both `%%VERSION%%` occurrences (footer text + `auto.js?v=`) print `FIRMWARE_VERSION` (`:403-409`).

---

## 10. i18n keys

All keys below were confirmed present in `chatgpt/src/languages/lang_en_ui.h` (including `homeAlertsTitle`, fixed 2026-10-18 — it used an HTML entity `&#128680;`, shown literally because `auto.js` applies `data-i18n` via `textContent`; now literal `🚨` in both languages). Fallback text in the asset/renderer is PT; `auto.js` replaces via `data-i18n` at load.

`loading`, `homeMonTitle`, `homeStatus`, `homeSince`, `footerDev`, `footerQuote` (asset, `page_home_asset.cpp`); `homeModulesTitle`, `homeGrblController`, `homeUsbServer`, `navSensorAmbient`, `navMq135`, `homeRS232`, `homeRecipientsTitle`, `homeRecipients`, `homeAlerts`, `homeCmdsTitle`, `homeCmdsDatetime`, `homeCmdsInterval`, `navFiles`, `homeDualFile`, `navCharts`, `homeSerialMon`, `homeDataRecv`, `homeWifi`, `homeEmailCfg`, `navEthernet`, `homeGoogleScript`, `navSdcard` (renderer); `navSistemaTitle`, `navSystemSettings`, `navOtaBtn`, `navAccessBtn` (fragments). Buttons with **no** `data-i18n` (never translated): `/radio-ui`, `/lpt-db25`, `/modbus-rtu`, `/modbus-tcp`, `/can`, `/proximity*`, `/buzzer`, `/unikon`, `/ipg-laser`, `/uda1334a`, `/camera`, `/hx711`, `/ina226`, `/sound`, `/max31865`, `/structures`, `/modbus-live`, `/networks`, `/api-reference`.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| HOME-1 | ~~**`homeAlertsTitle` key does not exist.**~~ **fixed 2026-10-18:** key exists but held an HTML entity (`&#128680;`), rendered literally as `&#128680; Alerts` because `auto.js` applies `data-i18n` via `textContent`. | Medium | ~~Add `homeAlertsTitle` to both language files (EN `"🚨 Alerts"`, PT `"🚨 Alertas"`).~~ Done — both files now use literal `🚨`. |
| HOME-2 | **WiFi-hybrid flag can never hide the button.** `home_page_renderer.h:18`: `return wifiHybrid["enabled"] \| true;` — bitwise-OR with `true` is always truthy, so disabling WiFi via hybrid config never removes `btn-wifi-cfg`. | Medium | Use `\| false` (missing-key defaults false) or explicit `containsKey` check. |
| HOME-3 | **IPG Laser omitted from section-header check.** `kHasAnyModule` (`home_page_renderer.h:133-141`) lists every module flag except `ENABLE_IPG_LASER`, yet `:176-178` emits the button — with only IPG enabled the button renders with no "Modules" header. | Low | Add `(ENABLE_IPG_LASER==1)` to `kHasAnyModule`. |
| HOME-4 | **Dead token branches.** `%%MENU_FEATURE_BUTTONS%%` (`:93-99`) and `%%TELNET_SCRIPT%%` (`:393-401`) are handled in the streamer but neither placeholder exists in `page_home_asset.cpp` — unreachable code on every request. | Low | Delete both branches (keep the tokens out of the asset). |
| HOME-5 | **Unknown-token log bypasses the Web Monitor.** `home_page_renderer.h:411-413` uses `Serial.print` with a PT string (`"DEBUG: Token %% NAO RECONHECIDO"`) instead of `sendVirtualMessage`, so the message never reaches `/monitor` per the project logging rule. | Low | Replace with `sendVirtualMessage(F(...), INFO)` in English. |
| HOME-6 | **`/access` button ungated + most module buttons untranslatable.** `:383` prints the Access button unconditionally (flag only gates manifests, cf. `PAGES/access.md` ACC-4); ~19 module buttons carry PT-only literals with no `data-i18n` (see §10 list). | Info | Gate `:383` with `#if ENABLE_PAGE_ACCESS==1`; add `data-i18n` keys for the PT-only buttons. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page skeleton (PROGMEM, tokens only) | `chatgpt/src/webpages/assets/page_home_asset.cpp` (+ `page_home_asset.h:5`) |
| Token streamer, gating, ETag, WiFi helper | `chatgpt/src/webpages/renderers/home_page_renderer.h` |
| WiFi route `GET /` (+ AP-only fallback) | `chatgpt/src/webpages/routes/web_routes_core.cpp:185-227` |
| Ethernet route `GET /`, `/index.html` | `chatgpt/src/ethernet_bootstrap_routes.h:155-162` |
| Ethernet shared entry point | `chatgpt/src/web_shared_pages.cpp:70-81` (+ `web_shared_pages.h:8`) |
| System section fragment | `chatgpt/src/webpages/assets/fragments_system_asset.{h,cpp}` |
| i18n dictionaries | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (home*/nav* keys) |
| Page smoke test | `tests/test_pages.py:21` (`test_home_page`); asset mapped in `tests/web/audit_i18n.py:9` |
