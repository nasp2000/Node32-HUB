# Sound — `/sound`

> One-stop reference for the **Sound level sensor page**: purpose, layout,
> every button/input, client logic, API endpoints, persistence and the
> firmware-side sampling engine. Written for both humans and AI agents.

---

## 1. Objective

Configure and read an analog sound-level module (MAX4466 / MAX9814 / generic
electret, `page_sound_asset.h:10`). The page shows live peak-to-peak (ADC),
amplitude (mV) and dBFS level with a level bar, plus sampling parameters
(window, sample count), read interval, and monitor-injection options.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /sound` |
| Feature flag | `ENABLE_SOUND_SENSOR` (`config_defaults.h:180`, default `0`; `user_config.h:179` commented out). All surveyed packs force `0` (`pack_radio.h:119`, `pack_environment.h:116`, `pack_psram.h:117`, `pack_cam.h:115`, `pack_waveshare.h:127`, `pack_rambox.h:55`) |
| HW pin | `SENSOR_SOUND_PIN` = GPIO2 (`board_pins.h:120`) — **mutually exclusive with `ENABLE_MQ135`**; header fallback is GPIO5 (`sensor_sound.h:32`) |
| WiFi server | `web_routes_config.cpp:266` — streams via `streamSoundPageToPrint()`, gated `#if ENABLE_SOUND_SENSOR==1` (`:265`) |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:381` → `streamSharedSoundPage` (`web_shared_pages.cpp:226`) |
| API (GET) | `GET /api/sound-config` (`registerApiRoute`, `web_routes_config.cpp:503`) |
| API (POST JSON) | `POST /api/sound-config` — manual body-accumulator route (`web_routes_config.cpp:559`; `SensorBodyAccum::soundConfig` at `:425`); Ethernet `ethernet_bootstrap_routes.h:695` |
| API (measure) | `POST /api/sound-measure` (`registerApiRoute`, `web_routes_config.cpp:504`) |
| Auth / caching | Basic auth + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`web_routes_config.cpp:278`) |

Renderer order: shell (`"🔊 Sensor de Som"` title) → `SOUND_CONFIG_PAGE`
(HTML + inline JS) → `/config-manager.js` → auto.js footer
(`sound_page_renderer.cpp:6`).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home] (.actions.nav-inicio, first child of .wrap)
 ├ H1 "🔊 Sound Level Sensor"
 ├ .section-box (Live readings)
 │   ├ .metrics-grid → #peakToPeak (ADC) | #peakMv (mV) | #dbfsVal (dBFS)
 │   ├ #levelBar (0–100% width div)
 │   ├ #lastReadTime ("No reading")
 │   └ [🔬 Measure Now] (#btnMeasure)
 ├ .section-box (Main Control)
 │   ├ checkbox #sensorEnabled ("Enable Sound Sensor")
 │   └ select #readInterval (2/5/10/30/60/300 s, default 30)
 ├ .section-box (Sampling Parameters)
 │   ├ input #sampleWindowMs (number 10–500, default 50)
 │   └ input #sampleCount (number 10–1000, default 200)
 ├ .section-box (Output Options)
 │   ├ checkbox #injectBuffer ("📊 Data in Monitor")
 │   └ checkbox #outputCsv ("Data for Import (CSV)")
 ├ [💾 Save Configuration] (#btnSave)
 └ #status (hidden status div, mostly unused — see SND-5)
```

---

## 4. Inputs

| Input | ID | Type / range | Notes |
|---|---|---|---|
| Enable sensor | `sensorEnabled` | checkbox | maps to `sensor_enabled` |
| Read interval | `readInterval` | select 2/5/10/30/60/300 s | maps to `read_interval_sec`; UI default `30` selected (`page_sound_asset.h:99`) |
| Sampling window | `sampleWindowMs` | number, 10–500 ms | maps to `sample_window_ms`; hint "Capture time per reading. E.g.: 50 ms" |
| ADC sample count | `sampleCount` | number, 10–1000 | maps to `sample_count`; hint "More samples = higher precision. E.g.: 200" |
| Data in Monitor | `injectBuffer` | checkbox | maps to `inject_to_buffer` |
| CSV output | `outputCsv` | checkbox | maps to `output_csv` |

No `adc_pin` or `alert_dbfs` inputs exist in the UI although both are in the
JSON/config struct (see SND-3).

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| 🔬 Measure Now (`#btnMeasure`) | `measureNow()` → `POST /api/sound-measure` → `updateDisplay(d)` + status line |
| 💾 Save Configuration (`#btnSave`) | `saveConfig()` → `disableBtn` + `showLoading()` → `POST /api/sound-config` (JSON) → `customAlert` + `showSavedCard()` on `d.ok` |

---

## 6. Client-side logic (inline JS in `page_sound_asset.h:147`)

- **`t(key, fallback)` / `uiLocale()`** (`:148`) — `window._t` wrapper; locale from `ui_lang_code` or `document.lang`, mapped to `en-GB`/`pt-PT`.
- **`updateDisplay(d)`** (`:158`) — fills `peakToPeak`, `peakMv` (1 decimal), `dbfsVal` (1 decimal, colour red >−20 / orange >−40 / yellow >−60 / green otherwise); level bar maps −90..0 dB to 0..100% (`:177`); timestamp from `last_reading_time` via `sensorLastReadingPrefix`, else `hx711NoReading` fallback `"No reading"`.
- **`loadConfig()`** (`:191`) — `GET /api/sound-config` (`no-store`), fills all inputs (`read_interval_sec || 30`, `sample_window_ms || 50`, `sample_count || 200`), calls `updateDisplay(d)`; if no reading yet → auto `measureNow()` after 600 ms (`:206`).
- **`saveConfig()`** (`:214`) — builds JSON body, `POST /api/sound-config`; `d.ok` → `customAlert(sensorAlertSaved)` + `showSavedCard()`; else `customAlert(sensorAlertError + msg)`. Uses `d.ok`, **not** the project-wide `d.success` convention (see SND-6).
- **`measureNow()`** (`:245`) — status "Measuring…", `POST /api/sound-measure`, `updateDisplay(d)`, always reports success (see SND-4).
- **`showStatus()`** (`:258`) — delegates to `customAlert` when present, so `#status` is effectively dead on the real firmware.
- No arrow functions; `customAlert`, `disableBtn`/`enableBtn`, `showLoading`/`hideLoading` per project rules.

---

## 7. Server API

Handlers in `handlers_unified/sensor_handler.cpp:535` (decl. `sensor_handler.h:57`).

### `GET /api/sound-config` (`:535`)

Returns `soundConfigManager.toJson()` (`sensor_sound_config.cpp:81`):

```json
{
  "sensor_enabled": true, "adc_pin": 2, "sample_window_ms": 50,
  "sample_count": 200, "read_interval_sec": 2, "inject_to_buffer": false,
  "output_csv": true, "alert_dbfs": 0.0,
  "last_peak_to_peak": 0, "last_peak_mv": 0.0, "last_dbfs": -90.0,
  "last_reading_time": 0
}
```

### `POST /api/sound-config` (`:540`)

`updateFromJson(req.body)` → `{"ok":true}` (200), else 400
`{"ok":false,"msg":"Erro ao guardar config sensor de som"}` (PT message, see SND-5).

### `POST /api/sound-measure` (`:549`)

`performReading()` then returns the same JSON as GET. Always HTTP 200 even when
the sensor is disabled/uninitialised (stale values returned — see SND-4).

---

## 8. Persistence

`SoundConfigManager::begin("/config/sensor_sound.json")`
(`sensor_sound_config.h:59`), initialised from `sensor_config.cpp:146`.
Storage via `SensorConfigBase` (`sensor_config_base.h`): atomic write
`.tmp` → bytes>0 → remove original → rename (`:101`); load-on-boot with
defaults fallback (`:29`); slow `periodicPersist()` every 8 h (`:26`,`:65`).
Struct defaults (`sensor_sound_config.h:38`): enabled `true`, pin
`SENSOR_SOUND_PIN`, window 50 ms, count 200, interval **2 s** (UI defaults to
30 s — see SND-6), `output_csv` true, `alert_dbfs` 0 (disabled), `last_dbfs`
−90.0.

---

## 9. Runtime engine

- **Driver** (`sensors/sensor_sound.cpp`): `begin()` sets 12-bit ADC + `ADC_11db` (`:25`); `performReading(windowMs, sampleCount)` tracks min/max, peak-to-peak, and RMS vs mid-rail (`:42`), yielding every 64 samples.
- **Derived values** (`:85`): mV = `p2p/4095*3300`; dBFS = `20*log10(p2p/4095)`, silence floor −90.0 (`:93`).
- **Manager** (`sensor_sound_config.cpp:38`): skips when disabled/uninitialised; caches `last_peak_to_peak`/`last_peak_mv`/`last_dbfs` + timestamp; injects `"🔊 Som: …mV …dBFS"` or `"SOUND,peak_mv,…,dbfs,…"` CSV into the RX/monitor buffer when `inject_to_buffer` (`:65`).
- **Auto-read** (`loop_blocks.h:36`): `shouldAutoRead()` (interval elapsed, `sensor_config_base.h:57`) → `performReading()` + `periodicPersist()`.
- `alert_dbfs` is stored but **never evaluated** anywhere (see SND-3).

---

## 10. i18n keys

**Firmware-side i18n (also added):** the API messages above are no longer hardcoded Portuguese in the handler — they now come from the firmware pack (`LangStrings`, `L(activeLang->…)`) and follow `system_language`. Page `<title>` and tab labels follow the same rule.

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 16 key(s) — `hx711Int2s`, `hx711Int5s`, `hx711MainTitle`, `hx711NoReading`, `ina226LiveTitle`, `soundAmplitude`, `soundDbfs`, `soundEnable`, `soundLevelBar`, `soundPageTitle`, `soundPeakToPeak`, `soundSampleCount`, `soundSampleCountHint`, `soundSamplingTitle`, `soundWindowHint`, `soundWindowMs`. HTML fallback text is English; PT is served from the packs.

Present in `lang_en_ui.h` / `lang_pt_ui.h` (EN shown): `btnHome`, `otaTipHome`,
`sensorMeasureNow`, `sensorTipMeasure`, `sensorTipInterval`, `sensorTipMonitor`,
`sensorTipCsv`, `sensorOutputTitle`, `sensorInjectBuffer`, `sensorSaveConfig`,
`sensorAlertSaved`, `sensorAlertError`, `sensorMeasuring`, `sensorMeasureDone`,
`sensorLoadErrorPrefix`, `sensorLastReadingPrefix`, `mq135IntervalLbl`,
`mq135Int10s`, `mq135Int30s`, `mq135DataImportCsv`, `mq135LoadConfigFailed`,
`mq135SaveFailed`, `mq135MeasureFailed`, `sensorInt1m`, `sensorInt5m`,
`soundTipEnable`, `soundTipSave`, `soundTipWindow`, `soundTipSampleCount`
(`lang_en_ui.h:1807`).

**Missing from both language files** (verified by grep — no match; page falls
back to hardcoded English HTML text; `validate_i18n.py` only checks EN↔PT sync,
not page-key coverage): `soundPageTitle`, `soundPeakToPeak`, `soundAmplitude`,
`soundDbfs`, `soundLevelBar`, `soundEnable`, `soundSamplingTitle`,
`soundWindowMs`, `soundWindowHint`, `soundSampleCount`, `soundSampleCountHint`,
plus borrowed `ina226LiveTitle`, `hx711MainTitle`, `hx711NoReading`,
`hx711Int2s`, `hx711Int5s` (see SND-1).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| SND-1 | **~16 `data-i18n` keys missing from language files.** `soundPageTitle`, `soundPeakToPeak`, `soundAmplitude`, `soundDbfs`, `soundLevelBar`, `soundEnable`, `soundSamplingTitle`, `soundWindowMs/Hint`, `soundSampleCount(Hint)` (`page_sound_asset.h:50`–`119`) and borrowed `ina226LiveTitle` (`:54`), `hx711NoReading` (`:79`), `hx711MainTitle` (`:87`), `hx711Int2s/5s` (`:96`) have zero hits in `lang_en_ui.h`/`lang_pt_ui.h` (only `soundTip*` exist at `:1807`). PT users always see English fallback. | Medium | Add the missing keys to both language files (EN + PT translations). |
| SND-2 | **`updateFromJson` has no range validation.** Any ints are accepted and persisted (`sensor_sound_config.cpp:100`): negative `sample_window_ms`, `sample_count` 0, negative `read_interval_sec`, arbitrary `adc_pin`. UI min/max (`page_sound_asset.h:113`) is client-side only. | Low | Clamp to UI ranges (window 10–500, count 10–1000, interval ≥1) and validate `adc_pin` is ADC-capable before `saveToFile()`. |
| SND-3 | **`alert_dbfs` is dead.** Stored/loaded (`sensor_sound_config.cpp:90,111`, struct `sensor_sound_config.h:30,46`) but no UI input sets it and `performReading()` never compares against it. | Low | Either add a threshold input + alert action, or remove the field from struct/JSON. |
| SND-4 | **Measure reports success with stale data when disabled.** `handleSoundMeasurePost` always returns 200 (`sensor_handler.cpp:549`); `performReading()` returns false when disabled/uninitialised (`sensor_sound_config.cpp:38`), so the response carries the old reading while `measureNow()` shows "Reading complete!" (`page_sound_asset.h:252`). | Low | Return non-200 or an `ok:false` flag when `performReading()` fails, and surface it in `measureNow()`. |
| SND-5 | **EN/PT mix in user-facing strings.** Shell title hardcoded PT `"🔊 Sensor de Som"` (`sound_page_renderer.cpp:9`); home button PT `"Sensor de Nível de Som"` (`home_page_renderer.h:198`); POST error `"Erro ao guardar…"` (`sensor_handler.cpp:545`); `measureNow` fallback `'❌ Erro: '` (`page_sound_asset.h:254`) vs `'❌ Error: '` in `saveConfig` (`:237`). | Low | Use i18n keys / English fallbacks consistently. |
| SND-6 | **Interval default mismatch + `d.ok` vs `d.success`.** Struct default `read_interval_sec` is 2 s (`sensor_sound_config.h:43`) but the UI pre-selects 30 s (`page_sound_asset.h:99`), so first save silently changes the interval; client checks `d.ok` (`:233`) while the project convention (cf. access page) is `d.success`. | Info | Align the default (2 s vs 30 s) and consider standardising on `d.success`. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_sound_asset.h` |
| Renderer | `chatgpt/src/webpages/renderers/sound_page_renderer.{h,cpp}` |
| Routes (WiFi) | `chatgpt/src/webpages/routes/web_routes_config.cpp:265` (page), `:502` + `:558` (APIs) |
| Route registry | `chatgpt/src/webpages/routes/route_registry_api.cpp:196,255,380` |
| Ethernet mirror | `chatgpt/src/ethernet_bootstrap_routes.h:381` (page), `:695` (POST config); handlers `chatgpt/src/ethernet_bootstrap_handlers.h:2169` |
| Shared page stream | `chatgpt/src/web_shared_pages.{h,cpp}` (`:226`) |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/sensor_handler.{cpp:535,h:57}` |
| Sampling driver | `chatgpt/src/sensors/sensor_sound.{h,cpp}` |
| Config manager + persistence | `chatgpt/src/sensors/sensor_sound_config.{h,cpp}` (`/config/sensor_sound.json`); base `chatgpt/src/sensors/sensor_config_base.h` |
| Boot + auto-read loop | `chatgpt/src/sensors/sensor_config.cpp:145`, `chatgpt/src/loop_blocks.h:36` |
| Feature flag / pins | `chatgpt/src/config_defaults.h:180`, `chatgpt/user_config.h:179`, `chatgpt/src/board_pins.h:120` |
| Home button | `chatgpt/src/webpages/renderers/home_page_renderer.h:197` |
| Manifests | `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp:286`, `selftest_handler.cpp:155` |
| Web tests | `tests/web/test_sound.js`, fixture `tests/web/pages/sound_asset.html` |
