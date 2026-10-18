# Inactivity Alerts — `/alerts-config`

> One-stop reference for the **Inactivity Alerts configuration page**: purpose, layout,
> every button and input, client logic, server API, persistence and the firmware-side
> alert engine. Written for both humans and AI agents.

---

## 1. Objective

Configure up to **6 independent alert slots** that send an **email when the device stops
receiving data** for a configurable amount of time (5 min → 24 h). Each slot has its own
timeout, daily cap and recipient list.

Typical use: watch a machine feeding data over RS232/LPT; if data flow stops (machine off,
cable pulled, crash), the hub emails the responsible people automatically.

On **proximity/OEE builds** (`ENABLE_PROXIMITY==1`) the page gains a second **OEE tab**
(alertas OEE) with up to **3 slots** that email when Shift/Day OEE or Availability stays
below target. The tab, its fields, API and engine are all compiled out on other packs —
those builds see exactly the page described below. Engine (`proximityOeeAlertTick()`,
30 s, DI1): per-slot episode latch (re-arm on recovery), max/day cap keyed by the
shift-aligned production day (same R2 model as day OEE — budget resets at the shift
boundary, never at midnight), email only with valid recipients + wifi.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /alerts-config` |
| Feature flag | `ENABLE_PAGE_ALERTS` (`user_config.h`; page fully compiled out when `0`) |
| WiFi server | `web_routes_config.cpp:142` — streams via `streamAlertsPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:217` — same renderer, chunked response |
| API endpoint | `POST /api/inactivity-alert-config` |
| Entry point | Home page → "🚨 Configurar Alertas" button (`home_page_renderer.h`, shown only when flag on) |
| Auth | Basic auth required (`ensureBasicAuth`); HTML clients only (`allowHtmlClient`) |

---

## 3. Layout

```
<main class="page">
 └ <div class="wrap">
    ├ [🏠 Home]  ← home button is always the first child of .wrap (project rule)
    ├ H1 "🚨 Configure Alerts"
    ├ [tab bar — proximity builds only: Inactivity | OEE]
    ├ <div id='tabInact'>
    │  └ Blue card (#0f172a bg, #3b82f6 border)
    │     ├ H2 "🚨 Inactivity Alert"
    │     ├ Info box (2 lines): what it does + default activity sources
    │     ├ Label "📋 Alert configurations:" + table (3 columns)
    │     │   ⏱️ Time + Enable | 🔢 Alerts/day | 📧 Email     ← 6 rows (slots 0..5)
    │     └ [💾 Save Alert Configuration]  ← centered save button (`#save_inactivity_alert`)
    ├ [<div id='tabOee' hidden> — proximity builds only]
    │  └ Violet card (#0f172a bg, #a78bfa border)
    │     ├ H2 "📉 OEE Alerts"
    │     ├ Info box: emails when Shift/Day OEE or Availability stays below target
    │     ├ Label "📋 Alert configurations:" + table (5 columns)
    │     │   Metric + Enable | Below % | Persist min | 🔢 Alerts/day | 📧 Email  ← 3 rows
    │     └ [💾 Save Alert Configuration]  ← `#save_oee_alert`

> **No global checkbox.** Activity source is per-board, not a page toggle:
> - **Board WITH RS232/LPT** (e.g. N16R8): only the RS232/LPT data line resets the
>   inactivity timer. Generic sensors never reset it.
> - **Board WITHOUT RS232/LPT** (e.g. Waveshare 8DI8DO): only the **proximity** counts
>   (machine pulses) reset the timer. Generic sensors (HX711, AHT20, …) never reset it.
    ├ <script /config-manager.js>
    ├ <script rt()>            ← i18n helper
    ├ inline save-handler JS
    └ <script /auto.js>
```

- Styling via shared `/core.css`; loading overlay + spinner while saving.
- Slot rows are generated **server-side** by `alertsSlotRow()` (`alerts_page_renderer.cpp:38`)
  from live config values — no fetch needed to render state.
- Email values are HTML-escaped by `alertsEscapedHtml()` before injection into `value='…'`.

---

## 4. Inputs

### Per-slot inputs (repeat for `i = 0 … 5`)

| Column | ID pattern | Type | Options / constraints | Config default |
|---|---|---|---|---|
| Enable | `alert_enabled_<i>` | checkbox (18×18) | on/off | disabled |
| Timeout | `alert_timeout_<i>` | select | `5,10,20,30,45,60,120,180,360,720,1440` min → labels *5 min…24 h* | `20` min |
| Alerts/day | `alert_max_<i>` | select | `3,6,9,12,15,20,25,30,45,60` (option text hardcodes PT suffix *"avisos/dia"*) | `12` |
| Emails | `alert_emails_<i>` | text | comma-separated list, placeholder `email1@ex.com, email2@ex.com`; loose validation (must contain `@`) | empty |

### OEE per-slot inputs — proximity builds only (repeat for `i = 0 … 2`, all disabled by default)

| Column | ID pattern | Type | Options / constraints | Config default |
|---|---|---|---|---|
| Enable + Metric | `oee_enabled_<i>` + `oee_metric_<i>` | checkbox + select | metric `0` Shift OEE / `1` Day OEE / `2` Availability (labels via `proxDashOeeShift/proxDashOeeDay/proxDashA`) | slot 0: Shift OEE; slot 1: Day OEE; slot 2: Availability |
| Threshold | `oee_threshold_<i>` | number 1–100 | fire when value < threshold (%) | slot 0/1: `60`; slot 2: `80` |
| Persist | `oee_persist_<i>` | select | `5,10,15,20,30,45,60,120,240` min | `15` min |
| Alerts/day | `oee_max_<i>` | select | `3,6,9,12,15,20,25,30,45,60` | `6` |
| Emails | `oee_emails_<i>` | text | same validation as inactivity | empty |

API accepts wider numeric ranges than the UI offers: timeout **1–1440**, max/day **1–100**
(server-side check in the handler).

---

## 5. Buttons

| Button | ID / target | Behaviour |
|---|---|---|
| 🏠 Home | link to `/` | First child of `.wrap`, navigates back (project convention). |
| 💾 Save Alert Configuration | `#save_inactivity_alert` | Collects all slots → client validation → `POST /api/inactivity-alert-config`. Disabled during request (`disableBtn`) with loading overlay. |
| 💾 Save Alert Configuration | `#save_oee_alert` (OEE tab, proximity builds only) | Collects 3 OEE slots → client validation (threshold 1–100, persist 1–240, emails) → `POST /api/oee-alert-config`. Same loading/error pattern. |

---

## 6. Client-side logic (inline JS in `alerts_page_renderer.cpp:152`)

1. Click handler attached to `#save_inactivity_alert`.
2. Loops slots 0–5 collecting `alert_enabled_i`, `alert_timeout_i`, `alert_max_i`,
   `alert_emails_i` (trimmed).
4. **Client validation** (only for enabled slots):
   - emails empty → `customAlert("Define at least one email when alerts are active!")`, abort;
   - no `@` in emails → `customAlert("Invalid email (missing @)")`, abort.
5. POST via `fetch('/api/inactivity-alert-config', {method:'POST', body:new URLSearchParams(fd), 'Content-Type':'application/x-www-form-urlencoded'})`.
6. Response JSON:
   - `status==='ok'` → `hideLoading(); enableBtn(b)` + `customAlert('Saved!')` + `showSavedCard()`;
   - otherwise → `hideLoading(); enableBtn(b)` + shows `j.message`;
   - network failure → `hideLoading(); enableBtn(b)` + `customAlert('Network error')`.
7. Cleanup (`hideLoading(); enableBtn(b)`) runs in both `.then()` and `.catch()` callbacks
   (not `.finally()`) for ESP32 WebView browser compatibility.

Project rules honoured here: `window.customAlert` (never native `alert`),
no arrow functions (old-browser/ESP32 captive-portal compat),
translations through `rt(key, fallback)` which prefers `window._t` then falls back to the
`ui_lang_cache` in `localStorage`.

### OEE tab JS (proximity builds only, `PAGE_ALERTS_OEE_JS` fragment)

- `alertsShowTab(which)` toggles `#tabInact` / `#tabOee` display + active button styles.
- `#save_oee_alert` handler mirrors the inactivity flow over the 3 OEE slots
  (`oee_enabled/metric/threshold/persist/max/emails_<i>`), with extra range checks:
  threshold 1–100 (`oeeErrThreshold`), persist 1–240 (`oeeErrPersist`).
- POSTs to `/api/oee-alert-config`; same ok/error/network handling.

---

## 7. Server API — `POST /api/inactivity-alert-config`

Handler: `handleInactivityAlertConfigPost()` (`handlers_unified/recipients_handler.cpp:66`).
Route registered for WiFi + Ethernet via `registerApiRoute()` (`web_routes_config.cpp:53`);
also listed in `route_registry_api.cpp:313`. Ethernet mirrors the parsing in
`ethernet_bootstrap_handlers.h:3177`.

### `GET /api/inactivity-alert-config` (load persisted slots, added per WAVESHARE-I)

Handler: `handleInactivityAlertConfigGet()` (`recipients_handler.cpp:132`, route at
`web_routes_config.cpp:53`, listed in `route_registry_api.cpp:142`). Reads the 6 slots back
with `getAlertSlotEnabled/Timeout/MaxPerDay/Emails()` and returns BOTH a `slots` array
(`{enabled,timeout_minutes,max_per_day,emails}` per slot — ConfigManager JSON layout) and
the flat `alert_*_<i>` field names the page form uses. Previously only the POST existed and
any GET to load the config 404'd (`{"status":"error","msg":"not_found"}`), making the page
look like the config did not persist. Persistence itself was fine: the POST does
`configManager.saveSection("alerts")` → atomic `/config/alerts.json`, and boot re-parses it
into `config["inactivity_alerts"]` (`ConfigManager.cpp:1104`) with corruption self-heal.

Params read from **body or query string**:

```
alert_enabled_<i>        = 1|0        (i = 0..5)
alert_timeout_<i>        = minutes    (validated 1..1440 when enabled)
alert_max_<i>            = count      (validated 1..100  when enabled)
alert_emails_<i>         = text       (required + must contain '@' when enabled)
```

Responses:

| Condition | HTTP | Body |
|---|---|---|
| Success | 200 | `{"status":"ok"}` |
| Timeout out of range | 200 | `{"status":"error","message":"Timeout must be 1-1440"}` |
| Max/day out of range | 200 | `{"status":"error","message":"Max per day must be 1-100"}` |
| Enabled slot without emails | 200 | `{"status":"error","message":"Emails required when alert is active"}` |
| Missing `@` | 200 | `{"status":"error","message":"Invalid email (missing @)"}` |
| LittleFS write failure | 500 | `{"status":"error","message":"Save failed"}` |

### `POST /api/inactivity-alert-test` (Send test alert, added per ALERTS-1)

Handler: `handleInactivityAlertTestPost()` (`recipients_handler.cpp:121`). Route registered
via `registerApiRoute("POST", "/api/inactivity-alert-test", …)` in `web_routes_config.cpp:54`.

Params: `i` (slot index, 0-based, `0..ALERT_SLOT_COUNT-1`), `emails` (recipients).

- Validates `i` in range and `emails` non-empty → 200 `{"status":"error","message":"…"}`.
- Under `ENABLE_SMTP`: builds a TEST subject (`🚨 - <system> - TEST Inactivity Alert`) and
  body via `buildEmailInfoTable()`, then calls `startInactivityAlertAsync(emails, …)`.
  Success → 200 `{"status":"ok","message":"Test alert started"}`; already in progress /
  email not configured → `{"status":"error","message":"Email already in progress or not configured"}`.
- SMTP disabled → 200 `{"status":"error","message":"SMTP disabled"}`.

Validation aborts **before any slot is written** (fail-fast loop), so a bad value never
half-saves the set.

Disabled slots are still stored with whatever timeout/max were last sent
(defaults `5` / `6` if absent) but are inert at runtime.

### `GET` + `POST /api/oee-alert-config` (proximity builds only)

Handlers: `handleOeeAlertConfigGet/Post()` (`recipients_handler.cpp`, nested
`#if ENABLE_PROXIMITY==1` inside the `#if ENABLE_PAGE_ALERTS==1` block); routes in
`web_routes_config.cpp` under the same guards; manifest fields/actions in
`test_manifest_handler.cpp` (`oee_alert.*`, also proximity-gated).

Params (POST, body or query, `i = 0..2`):

```
oee_enabled_<i>    = 1|0        (ALL slots ship disabled; user enables explicitly)
oee_metric_<i>     = 0|1|2      (0 shift OEE, 1 day OEE, 2 availability-shift)
oee_threshold_<i>  = percent    (validated 1..100 when enabled)
oee_persist_<i>    = minutes    (validated 1..240 when enabled)
oee_max_<i>        = count      (validated 1..100 when enabled)
oee_emails_<i>     = text       (required + must contain '@' when enabled)
```

GET returns `{"status":"ok","slots":[{enabled,metric,threshold,persist_min,max_per_day,emails}…]`
plus the flat `oee_*_<i>` fields. POST persists via the same
`saveSection("alerts")` → `/config/alerts.json` (the `oee_slots` array lives inside
`inactivity_alerts`, so no new stem/file was needed).

---

## 8. Persistence (ConfigManager)

Stored under the `inactivity_alerts` section of the global config document
(`ConfigManager.h:783-887`):

```json
{
  "inactivity_alerts": {
    "slots": [
      { "enabled": false, "timeout_minutes": 20, "max_per_day": 12, "emails": "" },
      ... × 6 (ALERT_SLOT_COUNT = 6)
    ],
    "oee_slots": [
      { "enabled": false, "metric": 0, "threshold": 60, "persist_min": 15, "max_per_day": 6, "emails": "" },
      { "enabled": false, "metric": 1, "threshold": 60, "persist_min": 15, "max_per_day": 6, "emails": "" },
      { "enabled": false, "metric": 2, "threshold": 80, "persist_min": 15, "max_per_day": 6, "emails": "" }
    ]
  }
}
```

The OEE slots live in the **same** `/config/alerts.json` file (own `oee_slots` array —
no new stem/file needed): `saveSection("alerts")` persists the whole
`inactivity_alerts` object and the loader copies every key back. Metric
`0` = shift OEE, `1` = day OEE, `2` = availability (shift).

- `sensors_reset_timer` was removed (2026-08): activity source is now per-board, not a
  config flag. Boards with RS232/LPT reset on RX/LPT only; boards without (Waveshare)
  reset on proximity only. `getInactivityAlertSensorsResetTimer()` still exists in
  `ConfigManager` for compatibility but is unused by the UI and ignored on Waveshare.

- Defaults lazily created by `_getAlertSlots()` (disabled, 20 min, 12/day).
- OEE defaults self-heal: `_getOeeAlertSlots()` grows the array back to 3 slots
  whenever `size < OEE_ALERT_SLOT_COUNT` (fixed 2026-10-12, ALERTS-11 — a persisted
  `"oee_slots":[]` used to make every set silently no-op and every get return
  hardcoded defaults, so the OEE tab answered ok but never persisted).- Save path: `saveSection("alerts")` in the POST handler
  → atomic LittleFS write (`/config/alerts.json`: `.tmp` write → validate → rename).
  Note: earlier builds used `markDirtyAndScheduleSave()` which is a no-op (loop is
  disabled), so config was never persisted to disk. Fixed 2026-08-25.
- Legacy single-slot getters kept for compatibility:
  `getInactivityAlertEnabled()` (any slot on), `getInactivityAlertTimeoutMinutes()`
  (shortest enabled), `getInactivityAlertMaxPerDay()` (largest enabled),
  `getInactivityAlertEmails()` (comma-joined).

### .29 reboot-loss bug (fixed 2026-08-30)

Two independent defects caused `/config/alerts.json` to never restore the edited
slots after a reboot (POST worked — RAM was correct — but the file was useless):

1. **Save side — `copyJsonCleanControlChars` dropped the `slots` tree.** It passed the
   child destination as a **by-value** `JsonVariant`
   (`copyJsonCleanControlChars(kv.value(), out[kv.key()])`). With the ArduinoJson
   version used by the waveshare libdeps, writes through a copied `JsonVariant` are
   silently discarded, so `alerts.json` was serialized as `{}` (whatever the `maxBytes`
   guard, the content itself was empty). **Fix:** two overloads writing only through
   `JsonObject`/`JsonArray` handles (`createNestedObject`/`createNestedArray`, per-key
   `set`/`add`). The `JsonArray` overload must be forward-declared before the
   `JsonObject` overload body. (`ConfigManager.cpp` helper above `initDefaults()`.)
2. **Load side — pool relocation invalidated the copied tree.** The alerts import ran
   mid-`loadFromLittleFS`; any *later* cross-doc write into `config` (core loop, access,
   usb-server…) relocated the pool and left `config["inactivity_alerts"]["slots"]` not
   `is<JsonArray>()`, so `_getAlertSlots()` rebuilt all slots to disabled defaults right
   after boot. **Fix:** the whole `// Load alerts.json` block now runs as the **final
   config mutation** inside `loadFromLittleFS()` (right before
   `CFG: Loaded config set`); nothing else touches `config` after it.

Proven by `tests/native/test_alerts_persist_real.cpp` (compiles the REAL
`ConfigManager.cpp`): `setAlertSlot(0,1,7,3,…)` → `saveSection("alerts")` → file contains
`slots[0].enabled:true` → fresh instance reloads it → `getAlertSlotEnabled(0)==true`.
29/29 assertions green.

### .29 reboot-loss bug, part 2 (fixed 2026-10-15, BACKLOG ALERTS-12)

Same symptom as above (POST ok → reboot → defaults 20/12) came BACK on .29
(v1.119 WAVESHARE): the "pool relocation" theory of defect 2 was wrong — the
import-last reorder could not help. Real root cause, proven in the compiled
ArduinoJson 6.21.6 sources (`Strings/Adapters/RamString.hpp:87`):
`StringAdapter<const char*>` = **Link policy — keys are NEVER copied**, stored
as raw pointers. Every `config[sec][k] = v` with `const char* k` from a temp
doc (secs/mainDoc/web/system/alerts loops) left a DANGLING key; `alertDoc`
(8KB, freed last at end of load) is reused first by ~8KB response Strings, so
the alerts keys corrupted first: garbage keys in `alerts.json` (`""`, raw MAC
bytes, then whole compact-log text) with bytes CHANGING per save. Second
half: the fixed 16KB `config` pool was nearly full at end-of-load, so the
slots/oee_slots array deep-copies silently OOM-dropped.
**Fix:** `kv.key()` (JsonString/Copy) at all 7 store sites (`recipDoc`/
`schedDoc`/`modbusDoc` loops already used it) + `config` 16KB→24KB
(`ConfigManager.h:283`).
**Proven on the .29 itself** — the native stub cannot model Link-vs-Copy nor
fixed pools: distinctive slot values → reboot → values kept; `alerts.json`
exactly `{sensors_reset_timer,slots,oee_slots}`, 0 non-ASCII, stable across 2
reboots; pack regression `waveshare` 8 OK / 0 FAIL (was 7 OK / 1 FAIL).

---

## 9. Runtime engine (what actually sends the mail)

Loop hook: `checkAndSendInactivityAlert()` called every main-loop pass
(`loop_blocks.h:673`, implemented `wifi_config_impl.h:276`).

### Activity tracking

A single global timestamp `lastDataReceivedForInactivityAlert` (ms) marks the last real
data reception. It is reset by:

| Source | Location | Resets timer? |
|---|---|---|
| RS232 RX lines | `loop_impl.h:720,881` | **only on boards WITH RS232** |
| LPT/DB25 data | `rs232_misc_impl.h:272` | **only on boards WITH LPT** |
| Arduino Bridge frames | `arduino_bridge.cpp:119`, `diagnostics_impl.h:684` | **only on boards WITH bridge** |
| Proximity events (production channels only, scrap excluded) | `proximity_manager.cpp:634` | **only on boards WITHOUT RS232+LPT** (e.g. Waveshare) |
| Sensor buffer injections | `sensor_helpers_impl.h:66` | **never** (was per-checkbox; now always off so generic sensors can't mask a stop) |
| Internally generated rows (shift-end/OEE summaries; periodic PROXIMITY status injection) | callers pass `resetInactivity=false` | never — synthetic rows must not mask inactivity (live Waveshare .29 used the default `true` on the 60 s PROXIMITY injection, resetting the timer as long as any counter was >0 → alerts never fired) |

**Activity-source rule (per board):**
- **Board WITH RS232/LPT** → RX/LPT is the only production signal; proximity and generic
  sensors do NOT reset the timer.
- **Board WITHOUT RS232+LPT (Waveshare 8DI8DO)** → proximity counts are the only production
  signal; scrap channels (DI2) are excluded so their edges cannot hold the timer alive
  on an idle production line. Generic sensors do NOT reset the timer (a
  continuously-injecting sensor would otherwise hold the timer at ~0s and no alert
  would ever fire).

Every reset also clears `inactivityAlertSentSlot[0..5]`, so each new activity period can
produce one fresh alert per slot.

### Evaluation cycle (throttled: runs at most once per 10 s)

1. Quick exit if **no slot enabled**.
2. Day rollover (device local time): resets all per-slot daily counters
   (`inactivityAlertsTodaySlot[]`) and period flags.
3. Wait until first data ever received (`timestamp != 0`) — avoids instant alerts after boot.
4. For each slot, send an alert only if **all** pass:
   - slot enabled;
   - today's counter `< max_per_day`;
   - `now − timestamp ≥ timeout_minutes`;
   - not already sent for this inactivity period;
   - slot has non-empty email list.
5. Send is **asynchronous SMTP** (`startInactivityAlertAsync()` → dedicated FreeRTOS task
   `inactivityAlertTask`, `runtime_email.cpp:1022-1178`, guarded against concurrent sends).
6. On success: increment counters, set flags, log
   `⚠️ [INACTIVITY] Alert sent` (WARN → Serial + Telnet + Web Serial Monitor).

Timer-reset events also surface in the monitor as
`✅ [INACTIVITY] Timer RESET by <source>` (INFO, throttled to one line / 15 s).

### Email produced

- **Subject:** `🚨 - <system name> - <localized "no data received" subject>`
- **Body:** info table built by `buildEmailInfoTable()` with human-readable timeout
  ("45 minutes", "2 hours", …) plus alert counter `(n/max)` and daily-cap note.
- Requires `ENABLE_SMTP==1` (with `ENABLE_GOOGLE_EMAIL==1` contributing to body build);
  otherwise nothing is sent.

Status of every slot is exposed externally via `system_status_payload.h:733-740`
(`alerts_sent_today`, `alert_sent_this_period`) and mirrored in config sync snapshots.

### OEE alert engine (proximity builds only)

`proximityOeeAlertTick()` (`sensors/proximity_manager.cpp`, whole file already gated on
`ENABLE_PROXIMITY==1`), called from the main loop next to the inactivity check
(`loop_blocks.h` → `handleInactivityAlert()`, same guard). Throttled internally to 30 s.

Per enabled slot it reads the live value for **DI1** (same convention as the shift-end
email): shift OEE / day OEE via `proximityGetRunningShiftSummary()` /
`proximityGetRunningDaySummary()`, availability = shift availability. Guards: shift
metrics need `proximityCountElapsedSec(0) > 0` (counters running), day metric needs
`proximityDayElapsedSec() > 0` (RTC synced); email must be configured
(`isEmailConfiguredAndWifiConnected()`).

Fire logic: value must stay **strictly below threshold for ≥ persist_min** (tracked per
slot in `_oeeAlertBelowMs[]`); on fire the slot is marked `_oeeAlertFired[]` and only
re-arms after the value recovers above threshold. Per-slot max/day
(`_oeeAlertSentToday[]`, reset on date change). Delivery reuses
`startInactivityAlertAsync()` (same queue as inactivity/camera alerts); subject
`<system> - OEE Alert - <metric> <value>% < <threshold>%`, plain-text body with metric,
value, threshold, channel and shift/day pieces, closed with the same device
footer as the shift-end emails (`buildEmailSignature()`, audited 2026-09-23).
Every send is logged with
`sendVirtualMessage(..., WARN)` so it reaches Serial + Web Monitor.

The pure decision (`oeeAlertShouldFire()` in `proximity_manager.h`) is unit-tested
natively (`test_proximity_oee_alerts`).

Simple-SMTP encoding: `smtpSendEmail()` (`smtp_core_impl.h`, used by OEE/inactivity/test
alerts) declares `charset=UTF-8` and sends ASCII bodies raw, but base64-encodes
non-ASCII bodies (PT accents — audited 2026-09-23: raw UTF-8 bytes under an implicit
7bit declaration got mangled by MTAs). Same rule as the multipart builder; ASCII
traffic is byte-identical to before. Encoding selection mirrored natively in
`test_email_body_format.cpp`.

All OEE/shift email subjects and bodies follow the active language (PT/EN) via
`L(activeLang->…)` (`lang_types.h` + `lang_active.cpp`): shift body labels
(`emailShiftRecords/Name/Scrap/NoTarget/PcsHour`), time-bar legend
(`emailTimeProd/Plan/Unplan`), OEE alert labels (`emailOeeTag/Prefix/ThresholdLbl/
MetricShift/MetricDay/MetricAvail/DayPieces/ShiftPieces`) and subject tags
(`emailShiftEndTag`, `emailShiftDayTest`).

---

## 10. i18n keys used on this page

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 1 key(s) — `alertsPageTitle`. HTML fallback text is English; PT is served from the packs.

UI (`data-i18n`): `loading`, `btnHome`, `alertTipHome`, `alertsPageTitle`,
`recipInactivityTitle`, `recipInactivityDesc1/2`, `recipTimeoutLbl`, `recipInactivityEnable`,
`recipMaxPerDayLbl`, `recipEmailsLbl`, `btnSaveAlertConfig`, `alertTipSave`,
plus runtime strings `recipSaved`, `recipErrMinEmail`, `recipErrEmail`, `recipErrNetwork`.

OEE tab (proximity builds only): `alertTabInact`, `alertTabOee`, `oeeAlertTitle`,
`oeeAlertDesc`, `oeeAlertMetric`, `oeeAlertThreshold`, `oeeAlertPersist`,
`oeeErrThreshold`, `oeeErrPersist`; metric option labels reuse
`proxDashOeeShift` / `proxDashOeeDay` / `proxDashA`.

Firmware email strings: `emailInactivitySubject`, `emailInactivityPrefix`,
`emailInactivityAlert`, `emailMaxAlertPrefix/Suffix`, `timeMinutes/Hour/Hours`.

Known inconsistency: the alerts/day option labels hardcode the PT word *"avisos/dia"* in
HTML fallback text (repo rule says fallback should be English).

---

## 11. Review findings & improvement ideas

Issues spotted while writing this document (candidates for correction):

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Send test alert button added.** Each slot now has a **📤 Test** button that POSTs `POST /api/inactivity-alert-test` `{i,emails}`; the handler reuses `startInactivityAlertAsync()` with the slot's recipients + a TEST subject/body (SMTP disabled → `"SMTP disabled"`). | High | **fixed** |
| 2 | Email validation is only `indexOf('@') >= 0` on both client and server — `"a@"` passes everywhere. | Medium | open |
| 3 | Alerts/day option text hardcodes PT *"avisos/dia"* — converted to English *"alerts/day"*. | Low | **fixed** |
| 4 | UI select caps alerts/day at **60**, API accepts **1–100** → silent mismatch between what user sees and what API allows. | Low | open |
| 5 | Page shows no live feedback (counters `alerts_sent_today` already exist in `system_status_payload.h`) — user cannot confirm it works from here. | Low | open |
| 6 | Saving overwrites disabled slots with whatever the form holds (defaults 5/6 if untouched) — previously tuned timeout/emails of a disabled slot can be silently lost. | Low | open |
| 7 | **`.finally()` removed from save handler (2026-08).** `Promise.prototype.finally` is not available in all ESP32 WebView browsers, causing `(intermediate value) is not a function` errors. Cleanup now runs inline in both `.then()` and `.catch()` callbacks. Other pages using `.finally()` (CAN, proximity, access, ethernet, etc.) may have the same issue. | High | **fixed** |
| 8 | **Global checkbox + per-slot test buttons removed (2026-08).** Activity source is now per-board: RS232/LPT boards reset on RX/LPT only; Waveshare (no RX) resets on proximity counts only. `sensor_helpers_impl.h` no longer resets the timer for generic sensors on no-RX boards (was holding `seconds_since_last_data` at ~1s, so alerts never fired). Checkbox `sensors_reset_timer` and `#alert_test_<i>` removed from UI/handler; `getInactivityAlertSensorsResetTimer()` kept for compatibility but unused. | High | **fixed** |
| 9 | **OEE alerts tab (2026-09).** New conditional **OEE** tab on this page (proximity/OEE builds only, `#if ENABLE_PROXIMITY==1` on fragments, renderer, API, manifest and engine — other packs unchanged): 3 slots (metric shift/day OEE or availability, threshold %, persist min, max/day, emails), all **disabled by default**, persisted in the same `alerts.json` (`oee_slots`), engine `proximityOeeAlertTick()` in the main loop reusing the shared email queue. | High | **fixed** |
| 9 | **alerts.json written as `{}` on .29 (fixed).** `copyJsonCleanControlChars` passed child destinations as by-value `JsonVariant` → writes silently dropped by the ArduinoJson in the board libdeps. Rewritten with handle-based overloads (see section 8). | High | **fixed** |
| 10 | **Reboot restored slots to defaults on .29 (fixed).** Alerts import ran mid-`loadFromLittleFS`; later config writes relocated the pool and broke the copied tree. Import relocated to the **last** config mutation. | High | **fixed** |
| 11 | **Scrap channel (DI2) held inactivity timer alive (fixed .29).** On Waveshare boards, the inactivity reset loop iterated over ALL channels including scrap — scrap edges on an idle production line kept resetting `lastDataReceivedForInactivityAlert` to ~0 and alerts never fired. Added `!isScrapChannel(i)` guard so only production channels (DI1) can reset the timer. | High | **fixed** |
| 12 | **Alert slots lost every reboot + garbage keys in `alerts.json` (fixed 2026-10-15, BACKLOG ALERTS-12).** `const char*` import keys link-dangle (ArduinoJson Link policy) + 16KB pool pressure at end-of-load; fixed with `kv.key()` copies at all 7 import stores + pool 16KB→24KB; device-proven on .29 (2 reboots, clean file, regression 8/0). | High | **fixed** |

Doc-format notes (for the other PAGES files):
- Keep exactly this section order on every page so AI/humans can scan fast.
- Each doc ends with a findings table like the above → doubles as audit backlog.

---

## 12. Source map

| Concern | File |
|---|---|
| Renderer | `chatgpt/src/webpages/renderers/alerts_page_renderer.{h,cpp}` |
| HTML fragments (PROGMEM) | `chatgpt/src/webpages/assets/page_alerts_asset.{h,cpp}` |
| Save handler | `chatgpt/src/webpages/handlers_unified/recipients_handler.cpp` |
| Test-alert handler + route | `recipients_handler.cpp:121`, `web_routes_config.cpp:54` |
| OEE tab + handlers + routes (proximity-gated) | `page_alerts_asset.{h,cpp}` (`PAGE_ALERTS_OEE_*`), `recipients_handler.{h,cpp}` (`handleOeeAlertConfig*`), `web_routes_config.cpp` (`/api/oee-alert-config`) |
| Routes | `chatgpt/src/webpages/routes/web_routes_config.cpp`, `ethernet_bootstrap_routes.h` |
| Config storage | `chatgpt/src/ConfigManager.{h,cpp}` (`oee_slots` in alerts.json) |
| Alert engine | `chatgpt/src/wifi_config_impl.h` (+ `sd_tasks_impl.h` counters/flags) |
| OEE alert engine | `chatgpt/src/sensors/proximity_manager.{h,cpp}` (`proximityOeeAlertTick`, `proximityGetRunningDaySummary`, `oeeAlertShouldFire`), loop hook in `loop_blocks.h` |
| Async email task | `chatgpt/src/runtime_email.{h,cpp}` |
| Self-test manifest | `test_manifest_handler.cpp` (`alert.slot.*`, `alert.save`, `oee_alert.*`), `selftest_handler.cpp` (`PAGE("/alerts-config", ENABLE_PAGE_ALERTS)`) |
