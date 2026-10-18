# Sensor Config — `/sensor-config`

> One-stop reference for the **Ambient Sensor (AHT20/BMP280) config page**: purpose, layout, every input/button, client logic, API endpoints, LittleFS persistence and the firmware-side `SensorConfigManager` engine. Written for both humans and AI agents.

---

## 1. Objective

Configure the ambient sensor block (AHT20 temperature/humidity + BMP280 temperature/pressure): master enable, auto-read interval, per-reading enable toggles, calibration offsets, sea-level pressure reference (altitude), monitor-injection output format, live readings display, 24 h min/max/avg statistics, and on-demand measurement. Settings persist to LittleFS and take effect **without reboot** (read at request time / next auto-read tick).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /sensor-config` |
| Feature flag | `ENABLE_AHT20_BMP280` (`config_defaults.h:165`, default `0`) **AND** `ENABLE_SENSOR_CONFIG_PAGE` (`config_defaults.h:168`, default `1`) — gates page, APIs, asset, and manifest entries |
| WiFi server | `web_routes_config.cpp:312` — chunked 3-segment stream (`buildPageFrame` + `SENSOR_CONFIG_PAGE`); Basic auth + `allowHtmlClient`; calls `sendNotModifiedIfCached` but sends **no** `ETag`/`Cache-Control` headers (unlike the `/max31865` route just above at `:301`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:328` → `streamSharedSensorConfigPage` (`web_shared_pages.cpp:148`) → `streamSensorConfigPageToPrint` (`sensor_config_page_renderer.cpp:74`); 404 when flag off |
| API (GET) | `GET /api/sensor-config` (`registerApiRoute`, `web_routes_config.cpp:460`; shared WiFi+Ethernet route table, `route_registry_api.cpp:186`) |
| API (POST) | `POST /api/sensor-config` — manual body-accumulation route on WiFi (`web_routes_config.cpp:509`); Ethernet mirror `ethernet_bootstrap_routes.h:669` → `ethernetHandleSensorConfigPost` (`ethernet_bootstrap_handlers.h:1984`) |
| Extra APIs | `POST /api/sensor-measure`, `POST /api/sensor-reset-stats`, `GET /api/sensor-slots`, `POST /api/sensor-flush`, `GET /api/sensor-flags` (`web_routes_config.cpp:446-464`); history APIs (`sensor-history*`, `:474`) shared with charts |
| Renderer order (Ethernet) | shell → `SENSOR_CONFIG_TABS_JS` → `SENSOR_CONFIG_PAGE` → `/config-manager.js` → auto.js footer. WiFi route bypasses the renderer (see SNR-1) |
| Home link | gated at `home_page_renderer.h:185` (`"🌡️ Sensor do Ambiente"` → `/sensor-config`) |
| Manifests | `test_manifest_handler.cpp:282`, `selftest_handler.cpp:151` — `PAGE("/sensor-config", (ENABLE_AHT20_BMP280 && ENABLE_SENSOR_CONFIG_PAGE))` |

---

## 3. Layout

```
main.page > div.wrap
 ├ div.actions.nav-inicio > a.btn "🏠 Home"   ← always first child of .wrap
 ├ H1 "🌡️ Ambient Sensor"
 ├ (Ethernet only) div.sensor-tab-bar          ← injected by renderer JS (see SNR-1)
 ├ div.section-box  (Main Control)
 │   ├ checkbox #sensorEnabled + "Enable Sensor"
 │   ├ select #readInterval  (60 s … 86400 s)
 │   ├ button #btnMeasure "🔬 Measure Now"
 │   ├ div #lastReadingTime  ("No reading available" / "📅 Last reading: …")
 │   └ div #historyBufferInfo ("📊 Buffer: -- points")
 ├ div.section-box  (Sensor Readings grid: Reading|Active|Last|Offset)
 │   ├ 9 rows: temp_aht20, humidity, temp_bmp280, pressure_hpa, pressure_mmhg,
 │   │   temp_avg, dew_point, heat_index, altitude
 │   │   (each: checkbox #enable_<k>, div .sensor-value #val_<k>, offset input
 │   │    #offset_<k> — EXCEPT dew_point/heat_index show "—", see SNR-3)
 │   └ sea-level row: input #seaLevelPressure + "hPa" (no enable toggle)
 ├ div.section-box  (Output Options)
 │   ├ checkbox #injectBuffer "📊 Monitor Data" (key sensorInjectBuffer)
 │   ├ static text "→ Formato:" (hardcoded PT, no i18n key)
 │   ├ checkbox #outputPretty "Formatted Data" / checkbox #outputCSV "Data for Import"
 │   └ hidden #csvSeparator (value ",")
 ├ div.section-box  (24h Statistics: temp/humidity/pressure min/max/avg cards)
 │   ├ spans #stats_temp_min/max/avg, #stats_hum_*, #stats_press_*, #stats_count, #stats_window
 │   └ button #btnResetStats "🔄 Reset Statistics"
 ├ button #btnSave "💾 Save Configuration"
 └ div #status (legacy fallback box; normally unused — showStatus prefers customAlert)
```

---

## 4. Inputs

| Input | ID | Type / range | Notes |
|---|---|---|---|
| Enable Sensor | `sensorEnabled` | checkbox, default checked | maps to `sensor_enabled` |
| Auto Read Interval | `readInterval` | select 60/120/180/300/600/1800/3600/10800/21600/43200/86400 s | maps to `read_interval` (`SensorInterval`, `sensor_config.h:50`) |
| Enable reading | `enable_<k>` ×9 (`temp_aht20`, `humidity`, `temp_bmp280`, `pressure_hpa`, `pressure_mmhg`, `temp_avg`, `dew_point`, `heat_index`, `altitude`) | checkbox | all checked except `altitude` (unchecked — matches `SensorConfig()` default `altitude.enabled=false`, `sensor_config.h:164`) |
| Calibration offset | `offset_<k>` ×7 (no input for `dew_point`/`heat_index`) | number; temp ±5, humidity ±10, pressure ±50, altitude ±100 | applied in `performReading()` (`sensor_config.cpp:748-763`) |
| Sea level pressure | `seaLevelPressure` | number, default `1013.25`, step 0.1 | altitude barometric reference |
| Monitor Data | `injectBuffer` | checkbox | `inject_to_buffer` → RX-buffer injection |
| Formatted Data | `outputPretty` | checkbox, default checked | mutually exclusive with `outputCSV` (client-side) |
| Data for Import | `outputCSV` | checkbox | mutually exclusive with `outputPretty` |
| CSV separator | `csvSeparator` | hidden, `","` | sent in POST, no visible UI |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 🔬 Measure Now (`#btnMeasure`) | `measureNow()` → `POST /api/sensor-measure` → `updateReadingValues(cfg)` + status; error → status `error` |
| 🔄 Reset Statistics (`#btnResetStats`) | `customConfirm("Reset 24h statistics?…")` → `POST /api/sensor-reset-stats` → clears stats spans; no fallback if `customConfirm` missing (see SNR-4) |
| 💾 Save Configuration (`#btnSave`) | `saveConfig()` → builds JSON → `POST /api/sensor-config` (`application/json`) → `customAlert("✅ Configuration saved!")` + `showSavedCard()` |

Tab buttons (Ethernet only) switch section panels; no server round-trip.

---

## 6. Client-side logic (inline JS in `page_sensor_config_asset.h:337-744`)

- **`loadConfig()`** (`:370`) — `GET /api/sensor-config` (`no-store`); fills `sensorEnabled`, `readInterval`, `outputPretty/CSV`, `injectBuffer`, `csvSeparator`, `seaLevelPressure`, per-reading `enable_/offset_` (`:392`); calls `updateReadingValues` + `updateStatistics`; renders buffer badge (`count/capacity %`, red/amber/green); auto-triggers `measureNow()` after 500 ms when `last_reading_time==0` (`:433`); delayed 150 ms on load so `/api/lang` wins the radio (`:743`).
- **`updateReadingValues(cfg)`** (`:442`) — `last_reading_time==0` → `"No reading available"` + `--` placeholders; else locale timestamp (`en-GB`/`pt-PT`), per-value `sv()` (2 decimals, 1 for altitude) with trend arrows (`trendArrow`, thresholds 0.1/0.5) and comfort color theming (`envTone`: green 18–26 °C / 40–60 % / 980–1030 hPa, lime/amber/orange/red bands, `:494-538`).
- **`updateStatistics(stats)`** (`:562`) — fills min/max/avg + `stats_count` + `Nh Nm` window; `--` when a group has no valid data.
- **`saveConfig()`** (`:604`) — `disableBtn(btnSave)` + `showLoading()`; POSTs `{sensor_enabled, read_interval, output_pretty, output_csv, inject_to_buffer, csv_separator, csv_include_timestamp:false, csv_include_headers:true, sea_level_pressure_hpa, readings:{enabled,offset}}`; success → guarded `customAlert` + `showSavedCard`; inner error path calls **unguarded** `customAlert` (`:652`); `finally` → `hideLoading(); enableBtn(btnSave)`.
- **Output mutual exclusion** (`:720-740`) — checking Pretty unchecks CSV and vice versa; enabling Monitor with neither format re-checks Pretty.
- Uses `t(k, fb)` → `window._t` fallback, `customAlert`/`customConfirm`, no arrow functions in handlers (project rules honoured). Success path checks HTTP status only, not a `d.success` field (POST returns bare `{"success":true}`).

---

## 7. Server API

Handlers in `handlers_unified/sensor_handler.cpp` (decl. `sensor_handler.h:5-13`), all under `#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1`.

### `GET /api/sensor-config` (`:95`)

`res.sendJson(sensorConfigManager.toJsonString())` — full live object built manually in `sensor_config.cpp:564-685`: `{sensor_enabled, read_interval, output_pretty, output_csv, inject_to_buffer, csv_separator, csv_include_timestamp, csv_include_headers, sea_level_pressure_hpa, readings{<9 × {enabled,offset}>}, extra_slots[], alarms{throttle_ms,hysteresis_percent,temperature,humidity,pressure}, last_* (9 values), last_reading_time, stats{window_hours,window_minutes,temperature,humidity,pressure}, history_buffer{count,capacity,save_interval_ms,initialized}}`.

### `POST /api/sensor-config` (`:100`)

`fromJsonString(body) && save()` → 200 `{"success":true}` (plus `buzzerOnSave()` when `ENABLE_BUZZER==1`); parse/save failure → 400 `{"success":false,"error":"Failed to save config"}`. `fromJson` (`sensor_config.cpp:489-562`) applies `|` defaults (`sensor_enabled|true`, `read_interval|INTERVAL_1M`, `output_csv|true`, `sea_level|1013.25`) with **no range/clamp validation** (see SNR-5).

### `POST /api/sensor-measure` (`:112`)

`performReading()` → 200 full config JSON (same shape as GET); sensor disabled/uninitialised/read-fail → 400 `{"success":false,"error":"Failed to read sensor"}`.

### `POST /api/sensor-reset-stats` (`:122`)

`resetStats()` (`sensor_config.cpp:1082`) → 200 `{"success":true}` (unconditional).

### Registered but unused by this page's UI

`GET /api/sensor-slots` (`:128`; 404 when charts page disabled), `POST /api/sensor-flush` (`:150`; replies hardcoded PT `"Dados guardados no SD"` / `"Sem dados novos ou SD não disponível"`, `:153-154`), `GET /api/sensor-flags` (`:74`), `GET /api/sensor-history*` (`:309+`, shared with `/sensor-charts`, MQ135).

---

## 8. Persistence

Independent `SensorConfigManager`, **not** via `ConfigManager` — own file **`/config/sensor_config.json`** (`sensor_config.h:1293`, `sensor_config.cpp:51`):

- `begin()` mounts via `littlefsBegin()` (never `LittleFS.begin()`), `mkdir /config`, loads file (`:57-97`); mount failure → defaults, no persistence (`:81`).
- `save()` → `saveToFile()` (`:233-344`): atomic write (`.tmp` → `serializeJsonPretty` → `flush/close` → bytes>0 → `remove` original → `rename`), `stat()` verification. Persists **settings only** (`sensor_enabled`, `read_interval`, output flags, separator, sea-level, 9 `readings`, `alarms`) — runtime `last_*`, stats, history excluded (`:265-268`); 2048 B JSON pool.
- Background `periodicPersist()` (`:1161-1197`): SD history flush every 60 min, LittleFS config save every 8 h. `loadFromFile()` (`:162+`) tolerates missing file (defaults).

---

## 9. Runtime engine

`chatgpt/src/sensors/sensor_config.{h,cpp}` (`SensorConfigManager sensorConfigManager`, `:37`) over `sensor_aht20_bmp280.{h,cpp}` (`environmentalSensor`):

- **`performReading()`** (`sensor_config.cpp:727-796`) — skips when disabled/uninitialised; `forceUpdate()` → raw `SensorReading` → applies per-reading offsets; `pressure_mmhg = hPa×0.750062 + offset`; `temp_avg`/`dew_point`/`heat_index` computed then offset; altitude via barometric formula `44330·(1−(P/P0)^(1/5.255)) + offset`, `0.0` without BMP280; `last_reading_time` = NTP epoch or `0` when unsynced (`:769-783`); then `updateStats()`, MQ135 extra-slot staging (`:789-796`), history `addPoint`, buffer injection.
- **Scheduler** — `shouldAutoRead()` (`:701`) gates on `sensor_enabled` + `read_interval`; driven from `loop_blocks.h:11`; `markReadingDone()` (`:723`) stamps the tick.
- **Stats/history** — `updateStats()` (`:1065`), `resetStats()` (`:1082`), in-memory ring `history` (count/capacity in GET); `formatPretty()` (`:817`) / `formatCSV()` (`:889`) / `injectToBuffer()` (`:940`); `checkAlarms()` (`:1050`) with throttle/hysteresis email alerts.
- **Packs** — enabled by `pack_environment.h:108-109` (`ENABLE_AHT20_BMP280 1`); forced `0` in radio/psram/cam/waveshare/rambox packs.

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

Page `data-i18n`/`data-i18n-title` + `t()` keys (all present in `lang_en_ui.h`, mirroring `lang_pt_ui.h:1034-1103,1766-1777`; EN/PT sync enforced by build-time i18n validation): `btnHome`, `otaTipHome`, `sensorPageTitle`, `sensorMainTitle`, `sensorEnable`, `sensorInterval`, `sensorInt1m/2m/3m/5m/10m/30m/1h/3h/6h/12h/24h`, `sensorMeasureNow`, `sensorNoReading`, `sensorBuffer`, `sensorBufferPrefix`, `sensorBufferPoints`, `sensorReadingsTitle`, `sensorGridReading/Active/Last/Offset`, `sensorTempAht20`, `sensorHumidity`, `sensorTempBmp280`, `sensorPressureHpa/Mmhg`, `sensorTempAvg`, `sensorDewPoint`, `sensorHeatIndex`, `sensorAltitude`, `sensorSeaLevelRef`, `sensorRefComfort1826/4060`, `sensorRefNormal9801030/735772`, `sensorRefComfort8/1827`, `sensorRefAltitude`, `sensorOutputTitle`, `sensorInjectBuffer`, `sensorDataFormatted`, `sensorDataImport`, `sensorStatsTitle`, `sensorStatTemp/Humidity/Pressure/Min/Max/Avg/Samples/Window`, `sensorResetStats`, `sensorSaveConfig`, `sensorConfirmReset`, `sensorAlertSaved`, `sensorAlertError`, `sensorMeasuring`, `sensorMeasureDone/Err`, `sensorStatsReset/ResetErr`, `sensorConfigLoaded`, `sensorLoadErrorPrefix`, `sensorLastReadingPrefix`, tips `sensorTipEnable/Interval/Measure/Toggle/Offset/SeaLevel/Monitor/Formatted/Csv/ResetStats/Save`. Fallback HTML text is English. Renderer tab labels bypass i18n (see SNR-2); `"→ Formato:"` (`page_sensor_config_asset.h:275`) has no key at all.

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| SNR-1 | **WiFi page has no tab bar; Ethernet does.** The tab UI lives in `SENSOR_CONFIG_TABS_JS` inside `streamSensorConfigPageToPrint` (`sensor_config_page_renderer.cpp:74-86`), but the WiFi route streams `SENSOR_CONFIG_PAGE` directly (`web_routes_config.cpp:312-342`) and never calls the renderer — only the Ethernet path (`ethernet_bootstrap_routes.h:328-331` → `web_shared_pages.cpp:148`) gets tabs. Same URL, different UX per interface. | Medium | Call `streamSensorConfigPageToPrint()` from the WiFi route (or move the tabs JS into the page asset) so both transports render identically. |
| SNR-2 | **Tab labels hardcoded in Portuguese, unaccented.** Renderer pushes literal `'Principal'`, `'Leituras'`, `'Saida'`, `'Estatisticas'` (`sensor_config_page_renderer.cpp:42-45`) — no i18n lookup, and `Saida`/`Estatisticas` miss their accents (`Saída`/`Estatísticas`). | Low | Use `_t()` with new keys (e.g. `sensorTabMain/Readings/Output/Stats`) like the access page does with `tabAccess*`. |
| SNR-3 | **Dew-point / heat-index offsets unreachable from UI.** Both rows render `"—"` instead of an offset input (`page_sensor_config_asset.h:242,247`), yet `saveConfig()` collects `dew_point`/`heat_index` offsets (`:622-623`) and `performReading()` applies them (`sensor_config.cpp:756-757`) — a persisted non-zero offset can never be viewed or cleared. | Low | Add `offset_dew_point` / `offset_heat_index` inputs (or drop those keys from the POST + document them as fixed-0). |
| SNR-4 | **Reset-statistics silently dead without `customConfirm`; fragile `btnSave` global.** The reset handler returns early when `customConfirm` is undefined with no fallback (`page_sensor_config_asset.h:712-717` — button appears to do nothing). `saveConfig()` uses the bare named-element global `btnSave` (`:605,654`) instead of `getElementById`, and the inner error path calls unguarded `customAlert` (`:652`) while the success path guards it (`:645`). | Low | Use `document.getElementById('btnSave')`; guard both `customAlert` calls; fall back to `showStatus()`/native `confirm` when helpers are missing. |
| SNR-5 | **No server-side validation of interval/offsets/sea-level.** `fromJson` casts `read_interval` unchecked (`sensor_config.cpp:496`), accepts any `offset` (`:522`) and any `sea_level_pressure_hpa` (`:513`) — UI min/max and the fixed option list are advisory only; a crafted POST (e.g. `read_interval: 7`, absurd offsets) persists silently. | Low | Whitelist `read_interval` against `SensorInterval` values and clamp offsets/sea-level to the UI ranges (reject or clamp with an error message). |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_sensor_config_asset.h` (gated `:6` on both flags) |
| Renderer (shell + tabs JS) | `chatgpt/src/webpages/renderers/sensor_config_page_renderer.{h,cpp}` |
| WiFi page + API routes | `chatgpt/src/webpages/routes/web_routes_config.cpp:311-343` (page), `:441-516` (APIs) |
| Shared route registry | `chatgpt/src/webpages/routes/route_registry_api.cpp:186,253,375-379` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/sensor_handler.{h,cpp}` (`handleSensorConfigGet/Post` `:95-110`, `handleSensorMeasurePost` `:112`, `handleSensorResetStatsPost` `:122`, slots/flush `:128-157`) |
| Ethernet mirrors | `chatgpt/src/ethernet_bootstrap_routes.h:328-334` (page), `:669-674` (POST); `chatgpt/src/ethernet_bootstrap_handlers.h:1976-2020`; shared page `chatgpt/src/web_shared_pages.cpp:148` |
| Runtime engine + persistence | `chatgpt/src/sensors/sensor_config.{h,cpp}` (`SensorConfigManager`, `/config/sensor_config.json`, `performReading`, stats/history/alarms); driver `chatgpt/src/sensors/sensor_aht20_bmp280.{h,cpp}`; ticker `chatgpt/src/loop_blocks.h:11` |
| Feature flags | `chatgpt/src/config_defaults.h:165-170` (`ENABLE_AHT20_BMP280=0`, `ENABLE_SENSOR_CONFIG_PAGE=1`); packs (`pack_environment.h:108-109` on, radio/psram/cam/waveshare/rambox off) |
| Home link | `chatgpt/src/webpages/renderers/home_page_renderer.h:185` |
| i18n | `chatgpt/src/languages/lang_en_ui.h` + `lang_pt_ui.h` (`sensor*` blocks `:1034-1103`, tips `:1766-1777`) |
| Tests | `tests/test_api_sensors.py:21-35` (GET `/api/sensor-config`, `/api/sensor-slots`); native `test_sensor_config_cycle` + `test_sensor_base_persist` (persistence template/cycle, `tests/native/Makefile:262-285`) — no dedicated page-JS test; manifests `test_manifest_handler.cpp:282`, `selftest_handler.cpp:151` |
