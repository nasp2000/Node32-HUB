# Falas + Ecra + Touch7 — `/falas`, `/ecra`

> One-stop reference for the **TouchMeteo-7 local-phrases page** and the
> **LCD web twin**: purpose, layout, every button/input, client logic, API
> endpoints, persistence and the PIR/speaker engine. Written for both humans
> and AI agents.

---

## 1. Objective

Manage local WAV phrases in LittleFS `/falas` (list / play / delete via the
1-pin PWM speaker → PAM8403) plus quick peers JSON config (viewer 2+2) and
one-tap peer refresh. Upload of new WAVs goes through `/files` (existing
uploader). Home page of the TouchMeteo-7 pack shows Meteo + Peers + Monitor.

`/ecra` is a **single-source pixel mirror** of the live 800×480 LCD: the browser
polls `GET /api/touch7-frame` (RGB565, 768,000 bytes) and paints the framebuffer
into one 800×480 canvas. There is no separately-rendered HTML screen, so data,
themes, selected tabs, font colours, peer photos and overlays cannot drift from
the physical display. Browser scaling uses pixel-preserving rendering; pointer
down/move/up map to LCD coordinates through `/api/touch7-tap`, so taps, button
presses and slider drags use the same LVGL controls as the physical touch panel.
Refresh can be paused or set to 2, 1, 0.5 or 0.25 fps. Weather-location tools
are kept in a separate collapsed panel and are explicitly outside the mirror.
Search or auto-detect a location there; saving it triggers a fresh Open-Meteo
request, and the real AGORA/PREVISÃO tabs show current conditions and five days.
**Exception to the home-first rule: `/ecra` has NO home button** (owner request
17-09-2026).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URLs | `GET /falas`, `GET /ecra` (physical LCD framebuffer mirror) |
| Pack gate | `SELECTED_PACK == PACK_S3_TOUCH7` (pages + routes + handlers compiled out elsewhere) |
| Home | `/ecra` button is **first** in the Módulos section (TouchMeteo-7 only) |
| WiFi server | `web_routes_touch7.cpp:registerTouch7Routes` — streamed (`AsyncResponseStream`) |
| Ethernet (W5500) | API via `registerApiRoute()` (shared); page is WiFi-only like `/sound` |
| Auth | Basic auth + HTML-client check + ETag (`FIRMWARE_BUILD`) |
| Session exemption | `/ecra` ALWAYS exempt (local processing, screen mirror) — never kicked by time; `/falas` is static, governed by the Access option |

Renderer assembles: shell (`page_render_common.h` `streamPageShellStart`) → body
(`FALAS_PAGE`, PROGMEM, `page_falas_asset.h`) → `/config-manager.js` + auto.js footer.

---

## 3. Layout

```
div.wrap (column)
 ├ a.btn home (FIRST child of .wrap) + h1 "🔊 Falas locais"
 ├ div.card: [Atualizar lista] + #falasStatus + table#falasTable (Ficheiro|Tamanho|Ouvir|Apagar)
 └ div.card: peers JSON (textarea#peersJson + [Ler config][Guardar][Testar agora])
```

Physical TouchMeteo-7 LCD (800×480):
```
y=0..31    Status bar: WiFi/IP (left), clock (middle), temperature (right)
y=32..415  Active content tab:
           AGORA: weather icon, temperature, humidity/pressure and system data
           PREVISÃO: current day + five forecast entries
           PEERS: four 2×2 snapshot tiles; tap a tile for fullscreen
           CFG: left = 6 themes, 5 clock styles, brightness slider;
               right = WiFi/system status, dim/saver timers, restart
y=416..479 Fixed four-button navigation; active tab has a white outline
```

`/ecra` page:
```
div.wrap#ecraWrap
 ├ h1 + short help
 ├ section#ecraMirrorCard
 │  ├ .ecra-mirror-wrap > canvas#ecraCanvas (800×480 actual LCD pixels)
 │  └ [Pause][refresh rate][live status]
 └ details#ecraGeoTools (weather location/status/refresh; outside the LCD image)
```

Rules: `/falas` keeps 🏠 as the first child of `.wrap`; `/ecra` is the documented
no-home exception. PROGMEM JS has no arrow functions; feedback uses
`customAlert`/`customConfirm`; JSON success checks use `d.success` (never `d.ok`).

---

## 4. Inputs

| ID | Type | Backend key |
|---|---|---|
| `peersJson` | textarea (full `/api/touch7-config` JSON: peers[4] + thresholds + theme + clock + volume + PIR) | `POST /api/touch7-config` (JSON body, manual accumulator) |
| `/ecra` `ecraCanvas` | 800×480 canvas | Raw framebuffer from `GET /api/touch7-frame`; click/touch sends `/api/touch7-tap` |
| `/ecra` `ecGeoQ` | text (max 60 chars) | `GET /api/touch7-geo?name=...`; selected candidate saved through `/api/touch7-config` |
| `/ecra` `ecWeatherRefresh` | button | `POST /api/touch7-weather-refresh`; status/result via `GET /api/touch7-forecast` |

---

## 5. Buttons

| Button | Calls | Effect |
|---|---|---|
| `bFalasRef` | `falasLoad()` → `GET /api/falas` | Rebuilds the table |
| `Ouvir` (per row) | `falasPlay()` → `POST /api/falas-play?file=<name>` | Plays via PWM task (503 if busy) |
| `Apagar` (per row) | `customConfirm` → `falasDel()` → `POST /api/falas-delete?file=<name>` | Stops audio + removes file |
| `bPeersLoad` | `peersLoad()` → `GET /api/touch7-config` | Fills the textarea |
| `bPeersSave` | `peersSave()` → `POST /api/touch7-config` | Persists + applies theme/clock live |
| `bPeersTest` | `peersTest()` → `POST /api/touch7-peer-refresh` | Forces poll of all enabled peers |
| LCD tab buttons | `evtTab()` | Select AGORA/PREVISÃO/PEERS/CFG; active tab is outlined |
| LCD theme buttons | `evtTheme()` | Select one of six persisted colour themes; selected theme is outlined |
| LCD clock buttons | `evtClock()` | Select one of three digital or two analogue screensaver clocks |
| LCD brightness slider | `evtBright()` | Directly adjusts backlight and displays the selected percentage; AUTO is omitted when BH1750 is disabled in this pack |
| LCD dim/saver buttons | `evtDimStep()` / `evtSaverStep()` | Cycle persisted OFF/1/2/5/10/30-minute timers |
| LCD peer tile / fullscreen close | `evtPeerOpen()` / `evtFsClose()` | Opens the peer fullscreen view and returns to the 2×2 grid |
| `/ecra` `ecMirrorPlay` | `ecraMirrorToggle()` | Pauses/resumes the actual framebuffer polling |
| `/ecra` `ecMirrorRate` | `ecraMirrorStart()` | Selects 2/1/0.5/0.25 frames per second |
| `/ecra` canvas | `ecraCanvasPoint()` → `POST /api/touch7-tap` | Forwards pointer down/move/up at LCD coordinates; supports taps and drags |
| `/ecra` `ecGeoGo` / `ecGeoAuto` | `ecraGeoSearch()` / `ecraGeoAuto()` | Searches or suggests a forecast location; selection is persisted |
| `/ecra` `ecWeatherRefresh` | `ecraWeatherRefresh()` | Queues a background forecast refresh; status panel reports loading, age, stale data or failure |

---

## 6. Client logic

- `falasLoad()` renders rows with `textContent` (no innerHTML injection from
  filenames); play/delete use `encodeURIComponent(name)`.
- `__test__` hooks (`falasRow`, `falasSafe`) mirror the server-side guards so
  `tests/web/test_touch7.js` covers them (6 cases).
- Runtime wiring (`$` bindings + initial `falasLoad()`) runs only when `$`
  exists, so the page also loads under JSDOM/extract without the shell JS.
- `/ecra` renders no display content independently: it validates the exact
  frame size, expands RGB565 to RGBA, and paints the sole canvas. It sends the
  last 64-bit frame hash back; unchanged frames return 304 instead of another
  768 KB. Weather status is fetched on load and polled only while refresh is
  pending. Location search/autodetect saves coordinates in `/config/touch7.json`,
  invalidates the cache and starts an asynchronous Open-Meteo request.

---

## 7. Server API

| Method | Endpoint | Handler | Notes |
|---|---|---|---|
| `GET` | `/api/falas` | `handleFalasListGet` | `{"files":[{"name","size"}]}` (LittleFS `/falas/*.wav`) |
| `POST` | `/api/falas-play?file=<name>` | `handleFalasPlayPost` | Guards: `..`, `/`, length, `.wav`; 404 missing, 503 busy |
| `POST` | `/api/falas-delete?file=<name>` | `handleFalasDeletePost` | Same guards; stops playback first |
| `GET` / `POST` | `/api/touch7-config` | `handleTouch7ConfigGet/Post` | Full touch7.json (peer passwords are not returned). Accepts `wx_lat` (-90..90), `wx_lon` (-180..180), `wx_place`; changing location bypasses weather cache/backoff for a next-loop fetch |
| `GET` | `/api/touch7-geo?name=X` | `handleTouch7GeoGet` | Open-Meteo geocoding (keyless, `count=5&language=pt`) trimmed to `{name,country,admin1,lat,lon}` (<1 KB) |
| `GET` | `/api/touch7-autoloc` | `handleTouch7AutoLocGet` | ip-api.com city suggestion `{city,country,lat,lon}`; user confirms in UI before saving |
| `GET` | `/api/touch7-forecast` | `handleTouch7ForecastGet` | `{has,code,isDay,today,days[5],refreshing,stale,age_sec,last_error}`; cached result/status for the weather tools, not an HTML screen renderer |
| `POST` | `/api/touch7-weather-refresh` | `handleTouch7WeatherRefreshPost` | Queues a background fetch; loop stays responsive |
| `POST` | `/api/touch7-tap?x=0..799&y=0..479` | `handleTouch7TapPost` | Legacy one-shot LVGL press; `/ecra` adds `state=down|move|up` for a held virtual pointer, auto-released after 500 ms without updates |
| `GET` | `/api/touch7-frame?hash=<last>` | `handleTouch7FrameGet` | Last VSYNC-presented RGB565 framebuffer (768,000 B), copied under the flip lock; 64-bit `X-Frame-Hash` identifies pixels and unchanged content returns 304 |
| `POST` | `/api/touch7-peer-refresh` | `handleTouch7PeerRefreshPost` | `?idx=N` one peer, else all (paced 1.5 s in tick) |
| `GET` / `POST` | `/api/bh1750-config` | `handleBh1750ConfigGet/Post` | Addr clamped to `0x23`/`0x5C`. **Removed from pack 17-09-2026: routes 404** |
| `POST` | `/api/bh1750-measure` | `handleBh1750MeasurePost` | Immediate read + JSON. **Removed from pack 17-09-2026** |

JSON APIs use `registerApiRoute()` (WiFi + Ethernet), except the two manual
JSON-body POST accumulators (`Touch7BodyAccum`). JSON responses stay under 4 KB;
`/api/touch7-frame` is the deliberate 768 KB binary framebuffer response.

---

## 8. Persistence

- `/config/touch7.json` via `Touch7ConfigManager` (SensorConfigBase reuse:
  atomic `.tmp` → `bytes>0` → `remove` + `rename`; `pages.reg` budget 4 KB).
  Physical LCD controls update RAM and defer the write until dim/screensaver
  (3 s quiet) or a 60 s fallback; flush runs under `Touch7PanelHold` after
  `lv_timer_handler()` has returned. Web config POSTs remain synchronous under
  the same panel hold. Clamps: theme 0-5, clock 0-4, brightness `backlight_pct` 5-100% (default 85; added field defaults safely for older JSON), volume 0-10, cadence 5-60 s, PIR/screensaver 0-3600 s,
  `wx_lat` -90..90, `wx_lon` -180..180, `wx_place` 63 chars (default Lisboa 38.72,-9.14).
- `/config/sensor_bh1750.json` via `BH1750ConfigManager` (same machinery; pack-off 17-09-2026).
- `/falas/*.wav` in LittleFS (16-bit PCM mono 16 kHz preferred; header
  validated by `touch7ParseWavHeader`, covered in `test_touch7_logic`).

---

## 9. Runtime engine

- **Peers:** `touch7PeersTick()` (loop) — round-robin one peer per window,
  cadence 10 s (≤2 peers) / 15 s (3) / 20 s (4), backoff 10/20/40/80→120 s +
  slot jitter; 256 KB PSRAM snapshot slot; JPEGDecoder → 320×180 RGB565 tiles;
  fullscreen fetches straight into a transient 640×480 PSRAM buffer.
- **Weather:** `touch7WeatherTick()` schedules an Open-Meteo worker every 10 min
  (one retry/min after failure). Network/TLS and JSON parsing run off the LVGL
  loop; fixed-size results are applied to the display on the loop task. Status
  tracks refresh, stale data, last-success age and errors. Location changes and
  manual refresh bypass the regular cache interval.
- **Audio:** `touch7AudioPlay()` — FreeRTOS task streams file → LEDC PWM
  (GPIO6, 8-bit, volume-scaled); WAV header validated; mid-rail idle.
- **Alarms:** `touch7AlarmsTick()` (30 s) — `touch7AlarmCheck()` bitmask over
  AHT readings vs `t_max/t_min/h_max` (degenerate config disables that bit);
  rising edge + 15 min cooldown → toast + phrase (`quente/frio/humidade.wav`,
  skipped silently when missing).
- **PIR:** `touch7PirTick()` — rising edge → F3b snapshot + toast + phrase
  (60 s cooldown); idle ≥ `pir_dim_sec` → dim 15% + pause icon anims; idle ≥
  `screensaver_sec` → giant clock (any touch wakes).
- **Display:** `Touch7Display::tick()` every loop — `lv_tick_inc` +
  `lv_timer_handler` (≤25 Hz, skipped while flash-hold is set); 1 s clock/agora;
  5 s BH1750 auto-brightness only when that feature is enabled. Deferred Touch7
  settings are flushed by `touch7SystemTick()` outside the LVGL event/render stack.
- **PSRAM-first:** draw bufs 64 KB, icons 3×51 KB, tiles 4×115 KB, snapshot
  256 KB, JSON docs — all PSRAM. DRAM exception: JPEGDecoder internal
  workspace (lib-owned, small) + 1-byte PIR state.

---

## 10. i18n keys

`touch7Falas/touch7Peers/touch7Theme/touch7ThemeLight/touch7ThemeMedium/touch7ThemeDark/touch7Clock/touch7Brightness/touch7Forecast/touch7Now/touch7Stale/touch7Lux/touch7Power/touch7NoPeers/touch7Test/touch7Saved`.
EN + PT kept in sync (`lang_en_ui.h` / `lang_pt_ui.h`, 2310 keys each).

---

## 11. Review findings table

| ID | Finding | Status |
|---|---|---|
| T7-1 | First `pio run -e s3_touch7` (14-09-2026): **SUCCESS** in 20m30s — panel/LVGL/JPEGDecoder API names verified (board macro, `drawBitmap`, `readPoints`, `setBrightness`, `pImage` RGB565). RAM 36.4%, Flash 59.6%. Binary: `build/esp32s3/S3_TOUCH7/v1.112/...` (signed). Fixed 2 compile rounds: lib tag `v1.0.5`→`v1.0.4`, `fetchStatus` arg order, `psram_helpers.h` path, missing `display_profiles`/weather/peers includes, latent AHT-without-SMTP link (guarded `sendAlarmEmail`). | fixed |
| T7-1b | **Boot abort root cause (14-09-2026, flashed 2314):** `E i2c: CONFLICT!` + abort in `check_i2c_driver_conflict` during `do_global_ctors` (addr2line vs build `.elf`, ELF SHA `9198bca69…` matched via `esptool image_info`; IDF v5.5 source confirms the check is a link-time `__attribute__((constructor))`: it aborts whenever BOTH I2C drivers are linked, regardless of ports/pins). Arduino ≥3.2 (IDF ≥5.4) links Wire=new driver; panel v1.0.4 links legacy. **Fix:** `[env:s3_touch7]` pinned to pioarduino **53.03.13-1** (Arduino 3.1.x/IDF 5.3, legacy Wire). Owner rebuilds + reflashes. | fixed (code) / open (HW verify) |
| T7-2 | I2C sharing (Arduino Wire1 sensors + panel I2C0 touch/expander, same pins 8/9) needs scope/HW validation. **Validated 17-09-2026 → see T7-24: local I2C sensors unsupported (deadlock), GT911 errors intrinsic (T7-24b). Pack composition updated same day: `ENABLE_AHT20_BMP280=0`, `ENABLE_BH1750=0` (were already hardware-absent + runtime-neutered); `/sensor-charts` route and home link compile out via the existing `kHasChartsRoute` constexpr; BH1750 API routes return 404.** | fixed (HW verify) |
| T7-3 | `JpegDec.pImage` assumed RGB565 `uint16_t*` (Bodmer lib convention). | fixed (build) |
| T7-4 | Supported-board macro `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7` per panel docs; confirm at first build. | fixed (build) |
| T7-5 | TF slot is SPI+EXIO4 while SD stack expects SDMMC: Falas/audio read LittleFS for now; TF move is F2c refinement. | open |
| T7-6 | PIR GPIO unassigned (`TOUCH7_PIR_PIN=-1`): PIR present in code but inert until schematic review. | open (HW) |
| T7-7 | Native logic caught 2 real bugs pre-merge (backoff cap, weathercode ≥95 fallback). | fixed |
| T7-8 | Handlers return `success` (never `ok`); page checks `d.success`. | fixed |
| T7-9 | LVGL line is v8.4 (not v9): board PIO stack pins LVGL 8.3–8.4. Doc corrected. | fixed |
| T7-10 | Peer HTTP timeouts shortened (status 2.5 s, snapshot 4 s) so a dead peer never stalls the LVGL pump; backoff keeps it rare. | fixed |
| T7-11 | i18n `touch7*` keys wired into `/falas` (`data-i18n`); handlers return `success`. | fixed |
| T7-12 | **Boot cache-panic root cause (addr2line vs 2314 `.elf`):** RGB GDMA ISR (`lcd_rgb_panel_fill_bounce_buffer`, bounce mode) memcpy's PSRAM while a concurrent SPI1 flash op (LittleFS `stat` in `SensorConfigManager::begin`) suspends the cache → `Cache disabled but cached memory region accessed`. S3 SPI1 is shared flash+PSRAM. | fixed |
| T7-13 | ISR-side hazard removed: single-FB streaming (driver default 1 FB, LVGL renders into it, DMA streams it, NOTHING ever switches) — the VSYNC restart path (`try_restart_transmission` → `gdma_hal_append`, flash-hosted because `CONFIG_GDMA_CTRL_FUNC_IN_IRAM` is off in Arduino) never runs. Bounce forced to 0 via pre-script (else the ISR would still memcpy PSRAM→bounce). Tearing accepted (HMI). | fixed (code) |
| T7-14 | Panel starts LAST in setup (after all boot flash I/O); WAVs preloaded to PSRAM at boot (reboot to index new files). | fixed (code) |
| T7-15 | Render/PSRAM pause (`touch7FlashHold`) around LittleFS writes from other tasks (config POSTs, falas list/play/delete); peers/weather decode + audio streaming honor it; loop-task saves are sequential (safe by construction). | fixed (code) |
| T7-16 | OTA dels the panel before `Update.begin` (reboot follows). NVS/WiFi small ops: residual risk, documented. | fixed (code) |
| T7-19 | **ISR still memcpy'd (FB-direct incomplete):** `configFrameBufferNumber(2)` selects framebuffers but the board conf keeps `bounce W*10`, so the ISR keeps copying PSRAM→bounce on every chunk. Fix: `scripts/patch_touch7_bounce.py` (pre-script, idempotent, pinned-lib exact-line replace) forces bounce size 0 → GDMA streams FBs directly. Refined by T7-20 (below): even switching is gone. | fixed (code) |
| T7-20 | **Restart path also flash-hosted:** the Arduino core builds the RGB driver with `RESTART_IN_VSYNC=y`, so EVERY VSYNC ISR runs `try_restart` → `gdma_reset/start/append`, which lives in FLASH (`CONFIG_GDMA_CTRL_FUNC_IN_IRAM` off) — panics on any concurrent flash op even with bounce=0 (proven: INA226 `periodicPersist` save → `esp_flash_write` vs restart-ISR). Fix: single FB, LVGL renders into it, flush = msync + ready, never switches; saves quiesce the engine (`esp_lcd_panel_reset` → write → `esp_lcd_panel_init`, refcounted, render/audio quiet-wait). | fixed (code) |
| T7-22 | **Acceptance: zero automatic flash writes.** Any LittleFS erase/write with RGB streaming panics (60 Hz restart ISR in flash code, unstoppable from product code; `panel_reset` quiesce proven useless). So on TOUCH7 every `periodicPersist()` is compiled out (loop_blocks.h); meteo caches rebuild live; thresholds/peers persist on explicit POSTs only (atomic `.tmp`+rename: usually survive, occasionally reboot, never corrupt). NVS/WiFi internals: residual. | fixed (code+docs) |
| T7-23 | **Acceptance: zero automatic flash reads too.** Sensor retry at 60 s calls `begin()` → `initFilesystem` → `loadFromFile`, which reads flash; the VSYNC restart ISR is flash-hosted → same cache panic. Fix: `initFilesystem()` in sensor_config_base.h returns early (config already loaded at boot) once `touch7RgbStreaming()` is true (T7-23 guard, touches7_i2c.h + touch7_display.cpp). | fixed (code+docs) |
| T7-21 | **Files coverage:** `/files` listing + `/api/sd/download` (LittleFS branch, whole-transfer hold + release on done/disconnect) + create/rename/delete + falas + all config POSTs quiesce via the shared `Touch7FlashGuard`/save paths. SDMMC/PSRAM branches need no guard (no SPI1). Upload path compiled out (`DUALFILE=0`); OTA dels the panel. | fixed (code) |
| T7-17 | SD hot-plug auto-mount (30 s retries) disabled on TOUCH7: no SDMMC card can ever appear (TF is SPI-only) and the retry window coincided with the ~136 s reboot cycle. | fixed (code) |
| T7-18 | **Boot hang root cause (TWDT 120 s, UART trace):** loopTask stuck in `environmentalSensor.begin()` — GT911 holds SDA pre-panel-reset, any Wire1 transaction blocks in the IDF driver; Adafruit begin would also spin forever on 0xFF status. Fix: `touch7I2CBusFree()` SDA gate on all sensor begins + presence probes before Adafruit calls + `touch7SensorsRetryTick()` post-panel (60 s, hot-plug friendly). | fixed (code) |
| T7-24 | **I2C sharing validated: local sensors UNSUPPORTED (17-09-2026, flashed 1708).** Wire1 (port 1) shares pins 8/9 with panel I2C0 (GT911+CH422G @~30 Hz); the two controllers have no mutual arbitration and ANY live Wire1 transaction can deadlock both to the 120 s TWDT — proven twice: (a) boot probe hang (build 1421/1535, `pinMode`-first reorder exposed it; 1133 was immune by accident — its early `Wire1.begin` + guard `pinMode` detached I2C1 so probes failed fast on dead pins), (b) 60 s post-panel retry hang (build 1655, TWDT backtrace right after `sensors: retry AHT/BMP`). Idle-level guards (SDA-only, then SDA+SCL) cannot see a stretch-held bus. Fix (T7-24c): all TOUCH7 sensor begins (AHT/BMP280, BH1750, INA226, DS3231) report absent with zero I2C traffic; retry tick is a no-op; `/api/i2c-scan` scans nothing on TOUCH7. Display data comes from network APIs (weather/peers). | fixed (code+HW verify) |
| T7-24b | **GT911 ~1/s `ESP_ERR_TIMEOUT` is intrinsic to the panel, not Wire1 contention.** Proven: build 1708 (Wire1 never initialized — zero I2C1 traffic) runs 310 s with **0** GT911 errors and `touch polls=2561 slow=0` (new 60 s T7LOG instrumentation in `touch7TouchCb`: times every poll, slow=>200 ms counts I2C-timeout blocks). Same binary family with Wire1 configured-but-idle (1133/1655) showed ~0.8/s. Touch polling verified healthy (97%+ fast polls even in cranky boots). Physical press-to-coordinate check still needs hands on the screen. | fixed (HW verify) |
| T7-25 | **Fast full-screen flicker (user report 17-09-2026):** infinite LVGL icon animation re-rendered 800x480 continuously on single-FB streaming (every redraw tears). Fix: animation removed (static icon); 1 s jobs (`refreshClock`/`refreshAgora`/`setAgora`) change-gated so labels/hands only update on new values; `render flushes=/60s` T7LOG counter added to prove idle redraws stop. | fixed (code, HW verify pending) |
| T7-26 | **Dim/screensaver never engage + ~13 Hz redraws in idle:** `press=0` over 60 s rules out phantom touches; `full_refresh` fuses every invalidation to full-screen by design (area buckets useless). Hunting the invalidator via press/bbox instrumentation. | open (investigation) |
| T7-27 | **Weather position configurable (owner request 17-09-2026):** `wx_lat`/`wx_lon`/`wx_place` in touch7.json (default Lisboa); forecast URL built from config. Place search via Open-Meteo geocoding (`GET /api/touch7-geo?name=`, keyless, trimmed <1 KB) + IP auto-detect (`GET /api/touch7-autoloc`, ip-api.com, user confirms before saving). Parsing pure in `touch7_geo.h`, covered by `test_touch7_geo` (8 cases/36 asserts). UI on `/ecra` CFG tab (search box + results + autodetect); save via existing `POST /api/touch7-config`. On-screen dim/saver stepper buttons on tabCfg (OFF/1/2/5/10/30 min presets, persisted via guarded `updateFromJson`). | fixed (code, HW verify pending) |
| T7-29 | **Anti-tearing double-buffer + cross-task flash lock:** panel configured `configFrameBufferNumber(2)` (2 FBs) + LVGL FULL mode; flush switches FB and waits VSYNC (task-notify). Cross-task `s_flashMutex` (recursive): flush try-locks around `switchFrameBufferTo` (flash-hosted GDMA), all async flash writers take it (central route dispatcher + `Touch7FlashGuard`/`Touch7PanelHold`). PanelHold no longer reboots on rebuild failure (falls back to single-FB / display-off). | fixed (code, HW verify pending) |
| T7-30 | **LVGL v9.6 migration + MyAuraPT look (owner request 19-09-2026):** `lvgl.git#v8.4.0` → `#v9.6.0`, `lv_conf.h` rewritten for v9; display/indev moved to `lv_display_create`/`lv_display_set_buffers`/`lv_indev_create`; images `lv_image_dsc_t`; transform `transform_scale_x/y`; Aura palette (gradient `0x1a4a9f→0x4a8cd8`, panels `0x5e9bc8`, gold `0xffd700`) + persisted theme applied at boot + explicit label text colours (fixes invisible background text); icon pulse (zoom 1.56→1.88x, 1.2 s ping-pong). Weather icons from MyAuraPT (`chatgpt/src/touch7_icons/*.c`, LVGL v9 RGB565A8, WMO mapping in `touch7_icons.h`). | fixed (code, build/HW verify pending) |
| T7-31 | **Exact `/ecra` pixel mirror:** one 800×480 canvas from the last VSYNC-presented RGB565 framebuffer; copy guarded against framebuffer flips; RGB565→RGBA conversion, pixel-preserving scaling and down/move/up touch forwarding. Separate HTML twin removed to prevent visual drift; unchanged frame hashes return 304. Weather/location/status controls stay outside the mirror. JSDOM coverage: 22/22. | fixed (code, device verify pending) |
| T7-35 | **LCD UI/readability cleanup:** every LVGL label receives an explicit foreground colour; CFG tab uses separated two-column groups, selected theme/clock/tab borders, and a real brightness slider (AUTO hidden when BH1750 is off). Touch changes update RAM and defer flash writes until dim/saver or 60 s fallback, outside `lv_timer_handler`; reboot flushes pending settings first. LittleFS download suspends before any `exists/open/size` read and releases on error/final chunk/disconnect; delete-multiple and `/api/nvs-stats` are guarded/cached on Touch7. | fixed (code, firmware/device verify pending) |
| T7-36 | **Weather/brightness/mirror optimization:** Open-Meteo TLS/JSON runs in a background task, results are applied on the LVGL loop, and status exposes stale/age/error + manual refresh. `backlight_pct` persists manual brightness with default/clamp for old config files. `/ecra` returns 304 for unchanged 64-bit RGB565 hashes. | fixed (code; firmware/OTA/device verification pending) |
| T7-37 | **TF mount fix (code only):** `board_pins.h` selects SPI with Touch7 pins and external CS; `touch7SdAssertCs()` holds EXIO4 LOW via the Board's expander (re-asserted after panel rebuilds); boot mounts LittleFS first, then `sdcardBeginAsync()` after the panel; `sdcardBegin()` defers while the expander is down; SPI auto-mount re-enabled. Pending owner build/flash + HW verify (mount/read/write/remove, `/files`, LittleFS intact). | in-progress (code; firmware/device verify pending) |
| T7-37 | **TF slot mount failure (confirmed source audit):** board wiring is SPI GPIO11/12/13 + active-low CH422G EXIO4, while `SDCARD_USE_SPI_MODE` defaults to 0 so boot calls SDMMC with SPI signals. SPI mode defaults are also wrong (GPIO5 CS collides LCD; MISO19 collides USB), and SD init starts before the Board-created expander exists. Planned fix: LittleFS first; mount TF at `POST_EXPANDER_BEGIN` reusing `board->getIO_Expander()->getBase()`, with an external-CS-capable SPI backend; no second CH422G instance. | open (implementation + HW verify) |

---

## 12. Source map

| Layer | File |
|---|---|
| Pack flags | `chatgpt/packs/pack_s3_touch7.h` (TouchMeteo-7, ID 12) |
| Board pins | `chatgpt/src/board_pins.h` (`TARGET_BOARD_S3_TOUCH7`) |
| Env | `chatgpt/platformio.ini` (`[env:s3_touch7]`) |
| Display | `chatgpt/src/touch7_display.{h,cpp}`, `screen/display_profiles.h` (ID 3), `screen/view_touch7.h`, `screen/view_selector.h`, `screen.h`, `screen.cpp`, `src/lv_conf.h` (LVGL v9.6) |
| Icons | `chatgpt/src/touch7_icons/*.c` (MyAuraPT RGB565A8 weather assets) + `touch7_icons.h` (WMO → asset map) |
| Sensors | `chatgpt/src/sensors/sensor_bh1750.{h,cpp}`, `sensor_bh1750_config.{h,cpp}`, `sensor_ina226_config.cpp` (Wire1), `rtc_ds3231.cpp` (RTC_WIRE) |
| Logic | `chatgpt/src/touch7_peers_logic.h` (host-tested), `touch7_config.{h,cpp}`, `touch7_peers.{h,cpp}`, `touch7_weather.{h,cpp}`, `touch7_audio.{h,cpp}`, `touch7_pir.{h,cpp}` |
| Page | `chatgpt/src/webpages/assets/page_falas_asset.h` (`FALAS_PAGE`) |
| Page (pixel mirror) | `chatgpt/src/webpages/assets/page_ecra_asset.h` (`ECRA_PAGE`) |
| Renderer | `chatgpt/src/webpages/renderers/falas_page_renderer.{h,cpp}` |
| Renderer (pixel mirror) | `chatgpt/src/webpages/renderers/ecra_page_renderer.{h,cpp}` |
| API handlers | `chatgpt/src/webpages/handlers_unified/touch7_handler.{h,cpp}` |
| Alarms | `chatgpt/src/touch7_alarms.{h,cpp}` (+ `touch7AlarmCheck` in logic header) |
| Routes | `chatgpt/src/webpages/routes/web_routes_touch7.{h,cpp}` |
| Loop wiring | `chatgpt/src/loop_blocks.h` (`touch7SystemTick`), `chatgpt/src/rs232_config_impl.h` (`touch7SystemBegin`) |
| i18n | `chatgpt/src/languages/lang_en_ui.h`, `lang_pt_ui.h` (`touch7*`) |
| Tests | `tests/native/test_touch7_logic.cpp` (15 cases), `tests/web/test_touch7.js` + `test_ecra.js` (auto-discovered) |
| Plan | `docs/waveshareLCDTouch7.md` |
