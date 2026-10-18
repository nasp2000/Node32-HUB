# Buzzer — `/buzzer`

> One-stop reference for the **Buzzer configuration page**: purpose, layout,
> every button/input, client logic, API endpoints, persistence and the
> firmware-side non-blocking buzzer engine. Written for both humans and AI agents.

---

## 1. Objective

Configure the onboard/external **active buzzer** (single GPIO, HIGH/LOW, no PWM):
master enable, GPIO pin, and which firmware events beep (boot complete, config
saved, proximity alarm, sensor threshold, network change, critical error), plus
live pattern testing (OK / Warning / Error / Long / Alarm / Stop). Applies
**without reboot** (`buzzerApplyConfig` reinitialises the pin if changed).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /buzzer` |
| Feature flag | `ENABLE_BUZZER` (`config_defaults.h:226-227`, default `0`; `user_config.h:332-333` sets `1` for the Waveshare 8DI8DO board block; `packs/pack_waveshare.h:66-68` forces `0` — "not needed for industrial pack") |
| WiFi server | `web_routes_buzzer.cpp:71` — streams via `streamBuzzerPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:353-359` → `streamSharedBuzzerPage` (`web_shared_pages.cpp:179-181`) |
| API (GET) | `GET /api/buzzer-config` via `registerApiRoute` (`web_routes_buzzer.cpp:45`); on Ethernet served through the registry fallback (`ethernet_bootstrap_routes.h:807-808`) |
| API (POST) | `POST /api/buzzer-config` (body-accum `server.on`, `:49`), `POST /api/buzzer-test` (body-accum, `:57`), `POST /api/buzzer-stop` via `registerApiRoute` (`:46`); Ethernet explicit branches for config/test (`ethernet_bootstrap_routes.h:708-711`), stop via registry fallback |
| Auth | Page: `ensureBasicAuth` + `allowHtmlClient` (`web_routes_buzzer.cpp:73-74`); POST accum handlers re-check `ensureBasicAuth` (`:53`, `:61`) |
| Caching | `ETag` = `FIRMWARE_BUILD`, `Cache-Control: private, max-age=15, must-revalidate` (`:81-82`) |

Renderer order: shell → `PAGE_BUZZER` (HTML + inline JS) →
`/config-manager.js` → auto.js footer (`buzzer_page_renderer.cpp:12-17`).

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home] (.actions.nav-inicio, data-i18n='btnHome')  ← first child of .wrap
 ├ H1 "🔔 Configure Buzzer"
 ├ Status & Test card (blue border)
 │   ├ H2 "🎷 Status and Test"
 │   ├ #buzzerStatus > #statusText ("Checking...")
 │   └ 6 buttons: [✓ OK (1 beep)] [⚠ Warning (2 beeps)] [✖ Error (3 beeps)]
 │                 [● Long (1 beep)] [⚠ Alarm (cont.)] [■ Stop]
 └ Configuration card (blue border)
     └ form#buzzerForm
        ├ checkbox #enabled ("Buzzer active (master)")
        ├ input #pin (number 0–48) + Waveshare/N16R8 hint
        ├ H3 "Beep on:"
        ├ checkbox #on_boot ("Boot complete")
        ├ checkbox #on_save ("Configuration saved")
        ├ checkbox #on_alarm ("Proximity alarm active")
        ├ checkbox #on_sensor_alert ("Sensor threshold exceeded")
        ├ checkbox #on_network ("Network connected / lost")
        ├ checkbox #on_error ("Critical error")
        └ [💾 Save] (type=button, onclick saveBuzzer())
```

---

## 4. Inputs

| Input | ID / name | Type / range | Notes |
|---|---|---|---|
| Buzzer active (master) | `enabled` | checkbox | `false` disables all patterns in `startPattern()` |
| Buzzer GPIO pin | `pin` | number, 0–48 (UI) | default 46 (Waveshare onboard); no server-side range check (see BZR-2) |
| Boot complete | `on_boot` | checkbox | 1 beep (OK) |
| Configuration saved | `on_save` | checkbox | 1 beep (OK); the only event with wired callers |
| Proximity alarm active | `on_alarm` | checkbox | continuous beeps while DIN alarm active |
| Sensor threshold exceeded | `on_sensor_alert` | checkbox | 2 beeps (Warning), MQ135 / AHT20-BMP280 |
| Network connected / lost | `on_network` | checkbox | 1 long beep online, 2 long beeps on loss (label only — see BZR-6) |
| Critical error | `on_error` | checkbox | 3 beeps (Error), SD / I2C faults |

`#buzzerStatus > #statusText` is read-only: `"Buzzer active — GPIO <pin>"` (green)
or `"Buzzer disabled (master off)"` (grey), set after the initial GET.

---

## 5. Buttons

| Button | Behaviour |
|---|---|
| 🏠 Home | link `/` — first child of `.wrap` |
| ✓ OK (1 beep) | `testBeep(1)` → `POST /api/buzzer-test {"pattern":1}`, fire-and-forget |
| ⚠ Warning (2 beeps) | `testBeep(2)` |
| ✖ Error (3 beeps) | `testBeep(3)` |
| ● Long (1 beep) | `testBeep(5)` (`BEEP_LONG = 5`) |
| ⚠ Alarm (cont.) | `testBeep(4)` (`BEEP_ALARM = 4`, loops until stopped) |
| ■ Stop | `stopBeep()` → `POST /api/buzzer-stop`, fire-and-forget, no UI feedback |
| 💾 Save | `saveBuzzer()` → `disableBtn` + `showLoading()` → `POST /api/buzzer-config` (JSON) → `customAlert('✅ Configuration saved!')` + `showSavedCard()`; else `customAlert('❌ Error: ' + ...)`; `finally` → `hideLoading()` + `enableBtn` |

---

## 6. Client-side logic (inline JS in `page_buzzer_asset.h:98-159`)

- **IIFE on load** — `GET /api/buzzer-config` (`cache:'no-store'`), fills all 8
  fields (`pin` defaults to 46 when absent) and sets `#statusText`; failures
  silently ignored (`.catch(function() {})`).
- **`saveBuzzer()`** — builds JSON from all fields (`parseInt(pin) || 46`),
  `POST /api/buzzer-config` with `Content-Type: application/json`, checks
  `d.success` (matches the `d.success` rule).
- **`testBeep(pattern)` / `stopBeep()`** — one-shot POSTs with only a
  `.catch()`; no success/error feedback in the UI.
- Uses `function()` (no arrow functions), `customAlert`, `disableBtn`,
  `showLoading`/`hideLoading` — project rules honoured.

---

## 7. Server API

Handlers in `handlers_unified/buzzer_handler.cpp` (gated `#if ENABLE_BUZZER==1`).

### `GET /api/buzzer-config` (`:8-27`)

```json
{"enabled":true,"pin":46,"on_boot":true,"on_save":true,
 "on_alarm":true,"on_sensor_alert":false,"on_network":false,"on_error":true}
```

Serialised with `snprintf` from `buzzerGetConfig()` (256-byte buffer).

### `POST /api/buzzer-config` (`:29-52`)

JSON body; each present key is applied (`containsKey` → set, `anyChanged=true`).
Empty body → 400 `{"ok":false,"msg":"Missing buzzer config fields"}`;
bad JSON → 400 `{"ok":false,"msg":"JSON inválido"}`; saved →
`{"success":true,"message":"Configuração guardada"}` + `buzzerOnSave()` beep.
Note the `ok`/`msg` vs `success`/`message` schema split and PT strings (BZR-1).

### `POST /api/buzzer-test` (`:54-60`)

Body `{"pattern":1..6}` → `buzzerBeep((BuzzerPattern)pattern)` →
`{"ok":true}`. Missing pattern defaults to 1; no range check (BZR-3).

### `POST /api/buzzer-stop` (`:62-66`)

No body → `buzzerSilence()` → `{"ok":true}`.

Python integration test: `tests/test_api_modules.py:35-40` (GET only, skips when
the endpoint 404s). No `tests/web/test_buzzer*` page test exists.

---

## 8. Persistence

Direct LittleFS JSON at **`/config/buzzer.json`** (`buzzer.cpp:15,44-82`) —
bypasses `ConfigManager` (no section/stem). Atomic write: open
`/config/buzzer.json.tmp` → `bytes>0` → remove original → rename; else remove
`.tmp`. Defaults live in the `BuzzerConfig` struct (`buzzer.h:29-38`):
`enabled/on_boot/on_save/on_alarm/on_error = true`, `pin = 46`,
`on_sensor_alert/on_network = false` (compile-time `BUZZER_PIN` can preset the
pin, `buzzer.cpp:118-120`). Because `buzzerBegin()` runs before LittleFS is
mounted, `buzzerReloadConfig()` re-applies the file later from
`taskInitFlash` (`sd_tasks_impl.h:394-398`).

---

## 9. Runtime engine (non-blocking driver, `buzzer.cpp`)

- **`buzzerBegin()`** (`:116-124`) — preset pin, `loadConfig()`, `pinMode OUTPUT`.
  Called from `setup_impl.h:280`.
- **`buzzerLoop()`** (`:126-172`) — advances the `{on_ms, off_ms}` step tables
  (`PATTERN_OK/WARN/ERROR/ALARM_CYCLE/LONG/DOUBLE_LONG`, `:22-27`) from `loop()`
  (`loop_impl.h:21`); alarm pattern loops until `buzzerSilence()`. Zero blocking.
- **`buzzerBeep(p)` / `buzzerSilence()` / `buzzerIsBusy()`** (`:174-195`) —
  enqueue/stop/query; `startPattern()` is a no-op when master `enabled` is off.
- **Event wrappers** (`:234-258`): `buzzerOnBoot/Save/Alarm/SensorAlert/Network/Error`.
  Only **`on_save` is wired**: `sensor_handler.cpp:103-105`, three
  `rs232_template_handler.cpp:367,415,440` call sites, `notifyScreenConfigSaved`
  (`config_api_common.h:25-30`), and the buzzer POST handler itself (`:50`).
  The other five wrappers have no call sites in `src` (see BZR-6).
- `BUZZER_PIN` default 46 documented in `config_defaults.h:229-231` and
  `board_pins.h:61`.

---

## 10. i18n keys

Page body labels are hardcoded English; only **tooltips** are i18n'd
(`data-i18n-title` in `page_buzzer_asset.h:9-93`) plus `btnHome` (`:9`).
All used keys exist in `lang_en_ui.h:1708-1724` (PT mirrors in
`lang_pt_ui.h:1708-1724`):
`btnHome`, `buzzerTipEnable`, `buzzerTipPin`, `buzzerTipTestOk`,
`buzzerTipTestWarn`, `buzzerTipTestErr`, `buzzerTipTestLong`,
`buzzerTipTestAlarm`, `buzzerTipStop`, `buzzerTipOnBoot`, `buzzerTipOnSave`,
`buzzerTipOnAlarm`, `buzzerTipOnSensor`, `buzzerTipOnNetwork`,
`buzzerTipOnError`, `buzzerTipSave`.
`buzzerTipFreq` / `buzzerTipDuration` (`lang_en_ui.h:1709-1710`) exist but are
unused by the page (see BZR-5).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| BZR-1 | **Mixed response schema + PT strings.** Errors return `{"ok":false,"msg":...}` while success returns `{"success":true,"message":...}` (`buzzer_handler.cpp:32,46,51`); strings are PT (`"JSON inválido"`, `"Configuração guardada"`, vs English `"Missing buzzer config fields"`). Client success ignores the server message, and the error path reads `d.message` (undefined for `msg`) → always falls back to `'Failed to save'` (`page_buzzer_asset.h:143`). | Medium | Unify on `{"success":bool,"message":...}` with English messages; fix client to read the same key. |
| BZR-2 | **No pin validation; GPIO 0 unreachable.** Handler casts unchecked to `uint8_t` (`buzzer_handler.cpp:38`) — negative/huge values wrap. Client `parseInt(...) \|\| 46` (`page_buzzer_asset.h:127`) maps pin `0` to 46, so GPIO 0 can never be set despite UI `min='0'`. | Low | Validate 0–48 server-side; client: `isNaN(v) ? 46 : v`. |
| BZR-3 | **No pattern validation on test endpoint.** `handleBuzzerTestPost` casts any int to `BuzzerPattern` (`buzzer_handler.cpp:57-58`); unknown values silently no-op, missing field masked by `\| 1`; `BEEP_DOUBLE_LONG (6)` is unreachable from the UI. | Low | Return 400 for out-of-range patterns; add a 6th test button or drop the enum value. |
| BZR-4 | **Dead Ethernet wrapper functions.** `ethernetHandleBuzzerConfigGet` (`ethernet_bootstrap_handlers.h:2487`) and `ethernetHandleBuzzerStopPost` (`:2512`) are defined but never called — Ethernet serves those endpoints via the registry fallback (`ethernet_bootstrap_routes.h:807-808`). Harmless but misleading. | Info | Delete the two wrappers or route the fallback through them. |
| BZR-5 | **Orphan i18n keys + non-i18n'd labels.** `buzzerTipFreq`/`buzzerTipDuration` (`lang_en_ui.h:1709-1710`, PT mirrors) have no matching inputs in the asset — leftover from a removed frequency/duration UI. Conversely, all headings, labels and status texts are hardcoded English with no `data-i18n`. | Info | Remove the two orphan keys; add `data-i18n` for headings/status or accept EN-only and document it. |
| BZR-6 | **5 of 6 event checkboxes have no firmware trigger.** `buzzerOnBoot/OnAlarm/OnSensorAlert/OnNetwork/OnError` are defined (`buzzer.cpp:234-258`) but have no call sites in `src`; only `on_save` is wired (sensor/RS232 handlers, `notifyScreenConfigSaved`). E.g. the network label promises "2 long beeps when connection is lost" but `buzzerOnNetwork()` only plays one `BEEP_LONG` and is never called. | Medium | Either wire the triggers (setup boot, proximity manager, network events, error paths) or mark the checkboxes roadmap-only so users are not misled. |

---

## 12. Source map

| Concern | File |
|---|---|
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_buzzer_asset.h` |
| Renderer (shell + asset) | `chatgpt/src/webpages/renderers/buzzer_page_renderer.{h,cpp}` |
| Routes (WiFi page + API) | `chatgpt/src/webpages/routes/web_routes_buzzer.{h,cpp}` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/buzzer_handler.{h,cpp}` |
| Ethernet page mirror | `chatgpt/src/ethernet_bootstrap_routes.h:353-359`, explicit POST branches `:707-712`, registry fallback `:807-808` |
| Ethernet wrappers (partly dead) | `chatgpt/src/ethernet_bootstrap_handlers.h:2480-2519` |
| Runtime engine + persistence | `chatgpt/src/buzzer.{h,cpp}` (`/config/buzzer.json`, atomic `.tmp` write) |
| Lifecycle wiring | `chatgpt/src/setup_impl.h:280` (`buzzerBegin`), `chatgpt/src/loop_impl.h:21` (`buzzerLoop`), `chatgpt/src/sd_tasks_impl.h:394-398` (`buzzerReloadConfig`) |
| Save-beep callers | `handlers_unified/sensor_handler.cpp:103-105`, `handlers/rs232_template_handler.cpp:367,415,440`, `handlers/config_api_common.h:25-30` |
| Feature flag | `chatgpt/src/config_defaults.h:226-231`, `chatgpt/user_config.h:332-334`, `chatgpt/packs/pack_waveshare.h:66-68` |
| i18n | `chatgpt/src/languages/lang_en_ui.h:1708-1724`, `lang_pt_ui.h:1708-1724` |
| Home link | `chatgpt/src/webpages/renderers/home_page_renderer.h:170-172` |
| Route registry / manifests | `chatgpt/src/webpages/routes/route_registry_api.cpp:104,216,265-267`, `handlers_unified/test_manifest_handler.cpp:276`, `handlers_unified/selftest_handler.cpp:75,145` |
| Tests | `tests/test_api_modules.py:35-40` (GET only); native `tests/native/test_config_defaults.cpp:61,122` (flag defaults) |
