# Sensor Charts — `/sensor-charts`

> One-stop reference for the **Monitoring Charts page**: purpose, layout,
> every input/button, client logic, chart-data APIs, the 2-slot chart-session
> engine and the sensor-history runtime. Written for both humans and AI agents.

---

## 1. Objective

Render up to 3 Chart.js time-series charts (G1/G2/G3) from live RAM history or
SD daily archives, across 4 data families: ambient sensors (AHT20/BMP280 +
extras), RS232 columns, Modbus registers and proximity channels. Read-only:
the page never writes firmware config; all preferences live in the browser
(`localStorage`). Access is gated by a lightweight 2-slot chart-session
(first viewer = priority 15 min, second = secondary 5 min absolute).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /sensor-charts` |
| Feature flag | Route: `#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) \|\| ENABLE_RS232_CHARTS==1 \|\| ENABLE_MQ135==1` (`web_routes_monitor.cpp:544`; defaults `config_defaults.h:65-66,165-169`: RS232_CHARTS `1`, AHT20 `0`, SENSOR_CONFIG_PAGE `1`, MQ135 `0`) — renderer guard is narrower, see SC-1 |
| WiFi server | `web_routes_monitor.cpp:545` — streams via `sendSensorChartsPageResponse()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:335` → `streamSharedSensorChartsPage` (`web_shared_pages.cpp:152`) |
| Session APIs | `POST /api/sensor-charts-session`, `POST /api/sensor-charts-heartbeat`, `DELETE /api/sensor-charts-session`, `POST /api/sensor-charts-session-end` (`web_routes_config.cpp:468-471`; Ethernet mirrors `ethernet_bootstrap_handlers.h:2023-2050`) |
| Data APIs | `GET /api/sensor-history`, `/api/sensor-history-files`, `/api/sensor-history-file`, `/api/chart-caps`, `/api/sensor-config`, `/api/sensor-slots`, `/api/sensor-flags`, `/api/rs232-cols`, `/api/rs232-history`, `/api/rs232-history-files`, `/api/modbus-chart-cols`, `/api/modbus-chart-data`, `/api/proximity-chart-cols`, `/api/proximity-chart-data` |
| Auth | `ensureBasicAuth` + `getChartsPageEnabled()` (else redirect `/`) + `allowHtmlClient` (counts toward concurrent-session limit) |
| Caching | `Cache-Control: no-cache` (`sensor_charts_page_renderer.cpp:25`); no ETag |

Renderer: `beginResponse_P` streams ~97 KB `SENSOR_CHARTS_HTML` (PROGMEM) without a heap copy (`sensor_charts_page_renderer.cpp:16-27`).

---

## 3. Layout

```
body
 ├ select#period (hidden)                 ← preset-driven 1h/12h/24h
 ├ div#lpTrigger (12px hover strip)
 ├ div#leftPanel
 │   ├ .lp-header → .lp-title "⚙️ Chart Options" + button.lp-close "✕"
 │   └ .lp-body → 3 .lp-section ("Vista" / "Data" / "Scale")
 ├ div.banner → H1 "📊 Monitoring Charts" + p[data-i18n=sensorChartsTitle]
 ├ div.ctrlbar
 │   ├ .cr row 1: Refresh (select#arInterval) + G1/G2/G3 metric selects
 │   │            (+ hidden Y2 selects) + Sensors/RS232/Modbus/Proximity file selects
 │   └ .cr row 2: "🔴 Live" "🕗 8h" "📅 Day" + "🔄 Refresh" "📄 CSV" "💾 PNG"
 ├ div#sts (status line, hidden)
 ├ div.info → #lastUpd | #viewInfo | #ptCount
 ├ div.content → 3 × .cbox#cboxN
 │     ├ .chdr → .ctitle#ctN + S/M/L height btns + "🔍 Reset" + "⛶"
 │     ├ .cwrap → canvas#cvN
 │     └ .sbox → stats rows (Min/Max/Avg/Current + hidden Median/StdDev/P25/P75,
 │                dual-mode B rows) + "📈 Stats+" toggle
 └ div#kickOverlay (hidden; "⏱️" + #kickMsg + "🏠 Back to Home")
```

No `.wrap`, no Home button on the page itself (see SC-6).

---

## 4. Inputs

| Input | ID | Values | Notes |
|---|---|---|---|
| Time period (hidden) | `period` | `1h`/`12h`/`24h` | driven by presets; manual change clears file pickers (`:2481`) |
| Auto-refresh | `arInterval` | Off/30s/1m/2m/5m/10m | live data only; persisted as `sc_ar` (`:2757`) |
| G1/G2/G3 metric | `chart1Type/2/3` | `temp/humidity/pressure/dew/heat/altitude` + injected `extra_N`, `rs232_colN`, `mb_*`, `prox_ch*` | defaults Temp/Hum/Press; Y2 variants `chartNType2` shown only in dual styles |
| Chart style | `chartStyle` | `line`/`dual` (default) /`bar`/`dualbar` | dual modes reveal Y2 selects (`syncDualSelectors`, `:889`) |
| Tension | `lineTension` | `0`/`0.3`/`0.5` | default Very smooth |
| Points | `pointVis` | `0`/`2`/`4`/`7` | default Medium |
| Theme | `themeSelect` | `dark/sky/forest/warm` | `setTheme()` repaints Chart.js (`:1002`) |
| Max points | `maxVisiblePoints` | `0`=All + 24…288 + dynamic current-max | options rebuilt per dataset (`:714`) |
| Stride | `pointStride` | 1…240 | auto-adapted per preset unless user overrides (`:749`) |
| X axis | `timeAxis` | `auto/hhmm/ddhhmm/ddmm` | auto → `DD/MM HH:MM` when span > 23 h |
| Y range | `yRange` | `auto` (default)/`zero1`/`fit`/`zero2` | negatives use padded zero-split scale (`yPadNeg`, `:1089`) |
| Day file | `dailyFile/rs232DailyFile/modbusDailyFile/proxDailyFile` | `--` + date list | anchors axis to 00:00–24:00 of `DD-MM-YYYY` (`:2499`) |

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| `🔴 Live` / `🕗 8h` / `📅 Day` | `applyPreset()` (`:1485`): sets range anchor (1 h / 8 h / midnight→now), point caps 72/96/144, clears file pickers, `refreshCharts()` |
| `🔄 Refresh` / `🛰️ Fresh data` | `forceFreshData()`: bypass localStorage cache once, refetch |
| `📄 CSV` | `exportCSV()` (`:2288`): merges visible series by timestamp → `sensor_<ts>.csv` or `charts_mixed_<ts>.csv` |
| `💾 PNG` | `saveAllCharts()` (`:2366`): composites 3 canvases + title → `graficos_<ts>.png` |
| `🔍 Reset` (×3) | `resetZoom(chartN)` (Chart.js zoom plugin) |
| `⛶` (×3) | `fsChart(n)` fullscreen overlay; `Escape` exits (`:2442`) |
| `S`/`M`/`L` (×3) | `setH(n,size)` chart height compact/normal/tall, persisted |
| `📈 Stats+` (×3) | `togAdv(n)` toggles Median/StdDev/P25/P75 rows |
| `🧹 Clear cache` | clears `scv3_*_cache` keys |
| `✕` | `closeLP()` closes slide panel (hover strip reopens) |

---

## 6. Client-side logic (inline JS in `page_sensor_charts_asset.h`)

- **Init** (`window.load`, `:2768`): Chart.js CDN check (fallback page if offline) → `regSession()` → `loadPref()` (`scv3`) → `loadConfig()` (`sea_level_pressure_hpa`) → `loadSlotsMeta()` (injects `extra_N` options) → `ensureRS232/Modbus/ProximityReady()` (inject `rs232_colN`/`mb_*`/`prox_ch*` + file lists) → `filterMetricsByCapabilities()` (`/api/chart-caps`: hides pickers/options for uncompiled modules) → `mkChart()` ×3 → `refreshCharts()` → restore `sc_ar` auto-refresh. Early pre-Chart.js filter IIFE at `:2597`.
- **Sessions** (`:940`): `POST /api/sensor-charts-session` → 45 s heartbeat (`POST .../heartbeat?sid=`); kick → `#kickOverlay`; `sendBeacon(.../session-end?sid=)` (fallback `DELETE .../session?sid=`) on unload.
- **Fetch**: `loadData()` → `/api/sensor-history?from=<ts>` (live/8h) or `?from=<midnight>&_now=<minBucket>` (day) or `/api/sensor-history-file?date=`; RS232/Modbus/Proximity mirror this (`loadRS232Data`, `loadModbusData`, `loadProximityData`). `fetchChartJson()` retries 429/503 with `Retry-After`/`retry_ms` (`:1634`); stale localStorage fallback on failure.
- **Production-day "day" preset** (RS232/Modbus/Proximity only, `:1660-1770`): when a shift has a nonzero boundary (slot > 0), the day preset is **not** anchored at calendar midnight. `ensureDayBoundary()` fetches `/api/proximity/shift-config` once (memoized in `_dayBoundaryLoading`, degrades to boundary 0 on failure); `dayProdData()` then fetches **prev** (`?date=<prev>&mode=day&from=<dayBoundaryTs>`, tail of the previous day from boundary:00) + **aligned** (`?date=<aligned>&mode=day`) **sequentially** (legacy routes are guarded by `HeavyApiRequestGuard` ~1 req/s — parallel would 429) and merges prev-first (chronological) via `_mergeData`, cached under `keyUrl = baseUrl+'?pd=1&b=<B>&p=<prev>&a=<aligned>&_now=<bucket>'`. Boundary 0 or merge miss → legacy `?from=<midnight>&_now=<bucket>` fallback. Ambient sensors (`loadData`) stay calendar-day. Backends already parse `date`/`mode`/`from` (see §7).
- **Render**: `refreshCharts()` loads only needed families, `updChart()` rebuilds datasets per style (gradient fill, dual-Y, day-break separators), `updStats()` writes Min/Max/Avg/Current (+ trend `↑/↓/→` from 8-point regression, contextual current-value colour). `rerender()` (no fetch) on display-only changes.
- **Auto-refresh**: `_refreshLatest()` (`:2677`) fetches only `?from=<lastTs+1>` per active family and merges in place; skips when a day file is selected.
- **Derived metrics**: prefers server arrays, else client-calculates dew/heat/altitude (`calcDew/calcHeat/calcAlt`, `:1044`).

---

## 7. Server API

Handlers in `handlers_unified/sensor_handler.cpp` (RS232: `handlers/rs232_history_handler.cpp`; Modbus: `handlers_unified/modbus_handler.cpp:587+`; Proximity: `handlers_unified/proximity_handler.cpp:488,527`).

### `POST /api/sensor-charts-session` (`:190`)

Max 2 slots; first occupant = priority. → `{"success":true,"sessionId":"<ms>-<rand>","priority":bool,"timeoutSec":900|300}`; full → 503 `{"error":"Max 2 concurrent chart viewers"}`.

### `POST /api/sensor-charts-heartbeat?sid=` (`:222`)

→ `{"success":true,"priority":bool,"remainingSec":N}`; expired secondary → `{"success":false,"kicked":true,"remainingSec":0}`; unknown → 404; missing sid → 400. `DELETE /api/sensor-charts-session?sid=` removes the slot; `POST .../session-end?sid=` always 204.

### `GET /api/sensor-history[?period=1h|12h|24h][?from=<epoch>]` (`:310`)

Live ring buffer when it covers `from` (PSRAM only). When the ring is **empty** (fresh boot, pre-first-sample) → today's SD archive (`streamDailyWindow`). When the ring has post-boot samples but **does not reach back to `from`** (reboot mid-window — the pre-fix bug that dropped the pre-reboot part once the first sample landed) → **single merged response** via `SensorConfigManager::streamMergedWindow()`: SD `.chart` head `[from, ringOldest)` backfilled from the persisted file + live ring tail, dedup boundary at `ringOldest` (zero overlap), newest `maxPoints` kept. `maxPoints`: 72 (≤1 h) / 144 (≤8 h) / 288. Shape: `{count, labels[], timestamps[], temperature[], humidity[], pressure[], dew_point[], heat_index[], altitude[], extra_N[], ...}`. Source selection + tail budget are the pure helpers in `sensor_history_merge.h` (native-tested).

### `GET /api/sensor-history-files` / `GET /api/sensor-history-file?date=DD-MM-YYYY` (`:366`,`:375`)

File list JSON array; single-day archive streamed as JSON. 400 on missing date.

### `GET /api/chart-caps` (`:160`)

`{"sensors","aht20","gas","rs232","modbus","proximity":bool}` derived from compile flags (`modbus = MODBUS && RS232_CHARTS && SDCARD`). 404 when charts page disabled. RS232/Modbus/Proximity `-cols` endpoints return `{cols, date, col_names}`; `-history`/`-data` return the same time-series shape with family keys.

---

## 8. Persistence

Page is **read-only** toward firmware — no POST writes any config. Browser only:

| Key | Content |
|---|---|
| `scv3` | all selects + heights + preset + stride-auto flag (`savePref`, `:1425`) |
| `scv3_sensor_cache` / `scv3_rs232_cache` / `scv3_modbus_cache` / `scv3_proximity_cache` | `{ts, url, data}` per-URL cache with 2/5/15 min TTL by preset (`:1571`). With a shift boundary active, the day-preset URL carries `?pd=1&b=<B>&p=<prev>&a=<aligned>&_now=<bucket>` so the merged prev+aligned result is cached as-is. |
| `sc_ar` | auto-refresh seconds (`:2761`) |
| `ui_lang_cache` | read-only: banner subtitle translation (`:2835`) |

Firmware-side `system.charts_page_enabled` / `charts_exempt_sessions` (defaults `true`, `ConfigManager.cpp:216-217`, persisted in access/system config) only gate this page; the page itself never edits them.

**History recovery (firmware, 2026-09-29)** — the PSRAM ring persists across a reboot without any frequent flash write:

| Layer | Where | Freshness | Used for |
|---|---|---|---|
| RTC RAM tail | `RTC_NOINIT_ATTR` `s_sensorRtc` (RTC_SLOW_MEM = SRAM, zero flash wear, `SENSOR_RTC_TAIL_SAMPLES=96`, magic+version guard, mirrored by `_sensorRtcMirror()` after every `addPoint()`) | ≤1 sample | soft reboots / OTA / WDT / deep sleep: `_sensorSeedFromRtc()` re-seeds the (post-boot empty) ring at the first NTP-synced reading, clearing the pre-sync millis bogus samples — the merged window then serves real values across the gap |
| SD daily `.chart` head | day `.chart` files flushed at the **configured MicroSD flush interval** (`getSdcardFlushInterval()`, default 30 m, minimum 10 m) | ≤ flush interval | power cut — the existing `streamMergedWindow()` backfills the SD head `[from, ringOldest)` (see §7) |
| LittleFS mirror (no-SD) | same `/sensor_history/<date>/` paths on LittleFS (`.csv` + `.chart`), written by the same 30 min flush when SD is unavailable; read order SD → Flash | ≤ flush interval | power cut without SD — `chartHistoryReadSensorJson()` opens SD first, then the identical LittleFS path |

LittleFS stays sparse otherwise (config every 8 h; sensor history only on the no-SD fallback path).

---

## 9. Runtime engine

- **Chart sessions** (`sensor_handler.cpp:39-70`): 2-slot `_chartSessions[]`; `_kPrioritySessionMs` = 15 min inactivity, `_kSecondarySessionMs` = 5 min absolute from registration; `getChartsExemptSessions()` ("Local pages never expire") disables all cleanup. Client mirror: 45 s heartbeat, secondary countdown (see SC-3).
- **History data** (`sensors/sensor_config.cpp`): PSRAM ring buffer streamed by `streamHistoryJSON()` (`:1399`); SD daily CSVs via `listDailyFiles()` (`sensor_config.h:1010`), `streamDailyFile()` (`:1515`), `streamDailyWindow()` (`:1528`) with midnight-anchored fallback after reboot; post-reboot gap backfill via `streamMergedWindow()` (`:1600`, reads the day `.chart` head through `chartHistoryReadSensorJson` + appends the live ring tail). Recovery layers: RTC RAM tail mirror/seed (`_sensorRtcMirror`/`_sensorSeedFromRtc`, pure math in `sensor_rtc_tail.h`) + SD flush at the configured interval (see §8).
- **Viewer allowlist**: `/sensor-charts` is one of 5 viewer-readable pages (`webpage_state.cpp:242`).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

Single `data-i18n` content key: `sensorChartsTitle` (EN `"Local Processing Page"`, `lang_en_ui.h:940`) applied via `ui_lang_cache`/`window._t` (`:2831`). Title tooltips: `chartTipStyle/Tension/Points/Theme/MaxPts/Stride/TimeAxis/YRange/Type/DailyFile/Period/Close/ClearCache/ForceFresh/Preset/Export/SaveAll/ResetZoom/Adv` — all present in `lang_en_ui.h:2037-2055` (PT mirrors in `lang_pt_ui.h:2037-2055`). All other labels/tips are hardcoded English (with the PT exceptions in SC-4).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| SC-1 | **Renderer guard narrower than route guard.** Route + Ethernet compile with `... \|\| ENABLE_MQ135==1` (`web_routes_monitor.cpp:544`, `ethernet_bootstrap_routes.h:336`) but `streamSensorChartsPageToPrint`/`sendSensorChartsPageResponse` guard is `#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) \|\| ENABLE_RS232_CHARTS==1` (`sensor_charts_page_renderer.cpp:7,17`) → MQ135-only build registers `/sensor-charts` yet serves 404 `"Not available"`. | Medium | Add `\|\| ENABLE_MQ135==1` to both renderer guards. |
| SC-2 | **Modbus day file doesn't lock period select.** `syncPeriodLockFromPickers()` (`:1522`) checks sensor/RS232/proximity pickers but not `modbusDailyFile`, while `syncPresetRange()` (`:1539`) and the `period` change handler (`:2481`) handle all four. | Low | Include `modbusDailyFile` in `syncPeriodLockFromPickers()`. |
| SC-3 | **Secondary countdown never renders.** `updateSessionTimer()` writes to `#sesTimer` (`:950`), but no element with that id exists in the HTML — the lookup always null-returns. | Low | Add a `#sesTimer` span (e.g. in `.info`) or drop the timer code. |
| SC-4 | **Non-English hardcoded strings.** Banner fallback `"Página com Processamento Local"` (`:363`), panel section `"Vista"` (`:250`), kick messages `'A tua sessão terminou.'` (`:946`), `'Sessão terminada.'` (`:947`), `'A tua sessão foi terminada pelo servidor.'` (`:977`) violate the English-only code rule (HTML fallback must be English). | Low | Replace with EN strings and add i18n keys. |
| SC-5 | **Dead center-badge code.** `centerCountBadge.afterDatasetsDraw` returns immediately (`:812`), `setChartCenterBadge()` nulls the badge (`:1186`), `countLabelElementForChart()` returns null (`:1182`) — V7/V8 remnants that still run per refresh. | Info | Remove the three stubs and their call sites. |
| SC-6 | **No Home button / no `.wrap`.** The page has no `.wrap` container and no Home link; the only `/` links are the CDN-failure fallback (`:2772`) and the kick overlay (`:2855`), against the "Home = first child of `.wrap`" rule. | Info | Add a Home link to the banner/control bar. |
| SC-7 | **Day preset missed hours 22–23 of the previous production day.** Day views for RS232/Modbus/Proximity anchored at calendar midnight (`?from=<midnight>`), so with a shift boundary (e.g. 22:00) the 22–23 block of the previous day's file was never fetched (backends correctly ignore `_now` — the handler just treats it as cache-buster). | **Fixed** | Client-side prev+aligned merge via `ensureDayBoundary()`/`dayProdData()` (see §6), sequential fetches to respect `HeavyApiRequestGuard`; sensors stay calendar-day. |
| SC-8 | **Live/8h preset dropped the pre-reboot window after reboot.** `handleSensorHistoryGet` set `psramHasOverlap` from the ring's **newest** sample, so once the first post-boot sample landed the ring falsely "covered" `from` → PSRAM-only response and the pre-reboot data (still on SD) silently vanished until the window slid past boot. | **Fixed** | `SensorHistory::oldestTimestamp()` + `sensorHistorySourceFor()` (overlap = ring's **oldest** retained ≤ `from`); gap → `streamMergedWindow()` merges SD `.chart` head `[from, ringOldest)` with the live tail in one envelope, backfilling like the OEE/proximity dashboard (see §7). |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM, 2862 lines) | `chatgpt/src/webpages/assets/page_sensor_charts_asset.h` (production-day window helpers `:1660-1770`) |
| Renderer (PROGMEM stream) | `chatgpt/src/webpages/renderers/sensor_charts_page_renderer.{h,cpp}` |
| WiFi page route | `chatgpt/src/webpages/routes/web_routes_monitor.cpp:544-565` |
| Session + sensor-history handlers | `chatgpt/src/webpages/handlers_unified/sensor_handler.{h,cpp}` (`:160-396`) |
| Sensor API route registration | `chatgpt/src/webpages/routes/web_routes_config.cpp:441-478` |
| Ethernet page + API mirrors | `chatgpt/src/ethernet_bootstrap_routes.h:335-345`, `chatgpt/src/ethernet_bootstrap_handlers.h:2023-2050`, `chatgpt/src/web_shared_pages.cpp:152` |
| History engine (PSRAM + SD CSV) | `chatgpt/src/sensors/sensor_config.{h,cpp}` (`streamHistoryJSON`, `streamMergedWindow`, `streamDailyFile/Window`, `listDailyFiles`) |
| History recovery layers (RTC tail math) | `chatgpt/src/sensor_rtc_tail.h` (pure), mirror/seed in `chatgpt/src/sensors/sensor_config.cpp` (`_sensorRtcMirror`/`_sensorSeedFromRtc` in `updateHistory()`) |
| History source selection + tail budget | `chatgpt/src/sensor_history_merge.h` (`sensorHistorySourceFor`, `sensorHistoryTailBudget`; pure, native-tested) |
| Day `.chart` read/write refactor | `chatgpt/src/chart_history.{h,cpp}` (`chartHistoryReadSensorJson`, `chartHistoryWriteSensorJson`, `ChartSensorJsonSet`) |
| Native tests | `tests/native/test_sensor_history_sources.cpp` (registered in `tests/native/Makefile` + `chatgpt/scripts/run_native_tests.py`) |
| RS232/Modbus/Proximity chart data | `chatgpt/src/webpages/handlers/rs232_history_handler.cpp`, `handlers_unified/modbus_handler.cpp:587+`, `handlers_unified/proximity_handler.cpp:488,527` |
| Config gating flags | `chatgpt/src/ConfigManager.{h,cpp}` (`charts_page_enabled`, `charts_exempt_sessions`), `chatgpt/src/config_defaults.h:65-76,165-172` |
| i18n dictionaries | `chatgpt/src/languages/lang_en_ui.h:940,2037-2055`, `lang_pt_ui.h:940,2037-2055` |
| Tests (logic only, jsdom fixture) | `tests/web/test_sensor_charts.js`, `tests/web/pages/sensor_charts.html` (no manifest/selftest entry) |
