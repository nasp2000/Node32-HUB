# Gestão de Produção e OEE (Proximity Sensors) — `/proximity`

> One-stop reference for the **Gestão de Produção e OEE page** (formerly PNP/NPN Sensors and Relays): purpose, layout,
> tabs, every button/input, client logic, API endpoints, persistence and the counting
> engine. Written for both humans and AI agents.
>
> Note: `/proximity-dashboard` is a **separate page** (own renderer + asset) — gets its own doc.

---

## 1. Objective

Configure and monitor up to **8 digital input channels** (`DI1…DI8`) connected to PNP/NPN
proximity sensors, encoders or relays. Per channel it provides:

- **COUNTER** mode — pulse counting (Count IN / Count OUT), with optional relay pulse output;
- **PRESENCE** mode — live ON/OFF detection only;
- **SPEED** mode — pulses-per-minute + cycle time between pulses;
- **Shift management** — automatic shift transitions, CSV reports to SD, SHIFTDAY email command;
- **OEE** (Elapsed-based — user spec 2026-09) — Availability × Performance × Quality per channel.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /proximity` |
| Related page | `GET /proximity-dashboard` (visual wallboard, separate doc) |
| Feature flag | `ENABLE_PROXIMITY` (page + all API routes compiled out when `0`) |
| WiFi server | `web_routes_proximity.cpp:214` — streams `streamProximityPageToPrint()` |
| Ethernet (W5500) | Same module registers both; API dispatch via `registerApiRoute()` entries |
| Auth | Basic auth + HTML-client check |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |

Renderer assembles: shell (`page_render_common.h`) → tab-bar JS (`PROXIMITY_TABS_JS`,
`proximity_renderer.cpp:6`) → page body (`PROXIMITY_PAGE`, PROGMEM) → `/auto.js` +
`/config-manager.js`.

### Tab system (client)

JS scans `[data-prox-tab]` sections and injects a pill tab bar (`.prox-tab-bar`) before the
first one: **Status · Config · Shifts · Shiftday · OEE**. Fewer than 2 sections ⇒ no bar.
Mobile ≤480px: 2-column grid.

---

## 3. Layout

```
main.page > div.wrap
 ├ [🏠 Home]
 ├ H1 "📍 PNP, NPN Sensors and Relays"
 ├ TAB Status      — live table #sb (11 cols: #, Name, Mode, Count IN, Count OUT, Cycle, Qty/Min, State, Setup, Elapsed, Reset) + poll indicator #ri
 │   ‣ **PRESENCE rows (mode=1)**: `Qty/Min` shows "—" (PPM only makes sense for
 │     COUNTER/SPEED pulse channels); `Cycle (ms)` KEEPS the last ON/OFF interval
 │     (time between the last two input transitions) — it is intentionally not
 │     hidden, as it reflects real signal timing. Column tooltips (`proxTipColPpm`,
 │     `proxTipColState`) document this per-mode behaviour.
 ├ TAB Config      — field-help, cfg table #cb,
 │                   [TCA9554] Relay DO names block (#do-names-block, 8DI8DO only)
 ├ TAB Shifts      — enable/auto-reset checkboxes, shifts/day select, time slots
 ├ TAB Shiftday    — SHIFTDAY command help, destination emails, Send Test Email
 ├ TAB OEE         — status table #oee-sb (A/P/Q/OEE + Running + Stopped + Scrap) + Reset All, formulas ref, params table #oee-cb
  └ Footer          — "Show: N channels" select (#visCh) + [💾 Save]
```

On mobile ≤480px, wide tables (status 11-col/730px, config 14-col/920px, OEE 9-col/700px)
are horizontally scrollable (`overflow-x: auto` on `.table-wrapper`) with the global
"👉 Deslize" indicator; font sizes drop to 10–11px (`!important`, otherwise the shared
`core.css` 13px `!important` would override the mobile sizing and badge/value cells
would overlap) and badges render at 10px so Status cells never collide; Reset buttons
wrap full-width stacked or compact and Reset buttons grow to 8×16px touch targets. The Shifts-tab
"shifts per day" mini table reuses `.cfg-table` (whose class `min-width:1150px` beats
any `max-width` and forced a 1150px-wide table + horizontal scrollbar on desktop), so it
carries inline `min-width:0` — centered via the flex wrapper with no scroll at any width.
On mobile the Shift config table (inline `max-width`) becomes fluid instead of blowing up
(`.cfg-table[style*="max-width"]{min-width:0;width:100%}`), and the OEE tables scroll at a
compact 620px (`#oee-status-tbl,#oee-cfg-tbl{min-width:620px}`). `.table-wrapper` uses
`justify-content:flex-start` so overflowing content is
never clipped behind a centered overflow.

---

## 4. Inputs

### Config tab — per-channel row `i = 0…n-1` (built by `renderCfg()`)

| Column | ID | Type / range | Notes |
|---|---|---|---|
| Name | `cn<i>` | text, max 31 | free label |
| Mode | `cm<i>` | select | `0 COUNTER` / `1 PRESENCE` / `2 SPEED` |
| GPIO IN | `cgi<i>` | number 0–255 | **hidden** on fixed-pin boards (8DI8DO) or TCA9554 boards (`_removeGpioCols()`); requires reboot |
| GPIO IN2 | `cgi2<i>` | number 0–255 | second count pin (Count OUT); 255=off; reboot |
| GPIO OUT | `cgo<i>` | number 0–255 | relay/LED pin; 255=off; reboot; ignored in monitor-only |
| Debounce ms | `cdb<i>` | number 0–65535 | noise filter (typ. 20–100) |
| Goal | `cgl<i>` | number ≥0 | 0 = unlimited; after N pulses GPIO OUT stops firing until Reset |
| DO Delay ms | `cpd<i>` | number 0–65535 | pulse→output delay |
| DO Dur ms | `cpu<i>` | number 0–65535 | must be >0 for output to fire |
| Mult. | `cmul<i>` | number 1…9999 | count multiplier: 1 count = N parts (positive only); manual values accepted (e.g. 200x, 4000x), same math: `delta × mul` added to Count IN/OUT, totals and OEE. **Always applied — even when a divider is set** (spec 2026-09-24, see Div.) |
| Div. | `cdiv<i>` | number 1…9999 | count divider: N counts = 1 part (e.g. 2–3 moves per part); forms the ratio with the multiplier: **parts = `delta × mul ÷ div`** (e.g. Mult.=2 + Div.=5 → 5 counts = 2 parts). Remainder carries across drain ticks so slow rates never lose counts; resets (channel/all) and divider changes clear the carry. Characteristic examples: Mult.=1/Div.=5 → 5 counts = 1 part; Mult.=10/Div.=1 → 1 count = 10 parts |
| Only GPIO IN | `cmo<i>` | checkbox | passive mode: disables + greys out `cgo/cgl/cpd/cpu` (`_lockOutFields`) |
| Gate | `cgc<i>` | select DI list / `--` | presence channel gating Availability (OEE); gate OFF routes pulses to Setup |
| Scrap | `csc<i>` | select DI list / `--` | channel subtracted for Quality (OEE); scrap edges never reset inactivity |
| Relay DO | `ctca<i>` (TCA boards only, injected cell + header) | select None/DO1–8 | TCA9554 relay assignment; labels from the Relay DO Names block |

Column layout: `.cfg-table` uses `table-layout:auto` (overrides the global
`core.css` fixed layout, which forced equal columns and clipped selects like
"COUNTER") with per-column `min-width` (Name/Mode/Gate/Scrap 112px, numbers
72–80px) + `overflow-x:auto` wrapper — full option text always visible.
Status-table Reset buttons use `.ch-table .btn-reset`, which opts out of the global
`core.css` `button`/`[class*="btn"]` rule (`display:block;width:100%;padding:8–12px 24–32px!important;min-height:40–48px;overflow:hidden`) that turned them into oversized
pills whose text was clipped off-center in the narrow fixed-layout Reset column —
small red `inline-block` pills (4×12px, 6px radius, `overflow:visible`), text centered.
Field Description block documents every column (GPIO IN/IN2 rows documented — IN
refers to the live sensor pin; on fixed-pin boards those columns are hidden in the
table but the rows stay in the help; Mult./Gate/Scrap/Relay-DO rows added).

### Other inputs

| Input | ID | Purpose |
|---|---|---|
| Shift enable / auto-reset | `shen` / `shar` | checkboxes |
| Shifts per day | `shnsl` | select 1/2/3 (default 3) → re-renders slot editors |
| Shift start times | `sh_h<j>` / `sh_m<j>` | hour 0–23 : minute 0–59, `j = 0…nSlots-1` |
| Planned stops (use + start/end) | `pst_use<k>` + `pst_sh/sm/eh/em<k>` | 4 fixed rows (`k = 0…3`); only checked rows are sent as `shift.stops:[{sh,sm,eh,em}]`; unchecked rows never reach the server |
| SHIFTDAY emails | `shiftday-emails` | comma-separated recipients for SHIFTDAY CSV email |
| Visible channels | `visCh` | select 1–8, persisted in `localStorage['prox_visCh']`; filters rows in Status + Config tables |
| Relay DO names (TCA9554) | `doName0…doName7` | text max 16, block visible only when expander present |

---

## 5. Buttons

| Button | Handler | Behaviour |
|---|---|---|
| 🏠 Home | link `/` | first child of `.wrap` |
| Reset (per status row) | `resetCh(i)` | `customConfirm("Reset DI%d?")` → `POST /api/proximity/reset?ch=i` → refresh; **restarts the channel's elapsed window** (`_countStartMs[i]=now`) **and its shift baselines** (OEE inputs restart at 0 and grow — no clamping against stale values), so counts, OEE metrics and the Elapsed column reset together; the DAY keeps pre-reset production (carried into day accumulators — Day OEE/donut always span the whole day) |
| ☑ Send Test Email | inline listener | `POST /api/proximity/test-email`; result shown in `#test-email-result` |
| ⟳ Reset All OEE | `oeeResetAll()` | confirm → `POST /api/proximity/oee-reset?ch=i` for each row (`Promise.all`); `proximityResetOee()` captures live counters as new baselines + restarts elapsed (OEE window restarts WITHOUT zeroing counts) |
| Apply Changes (OEE) | `oeeApply()` | copies OEE-tab edits into pending `_oeeCfg` + mirrors gate/scrap into config table; **not persisted** until global Save |
| Defaults (OEE) | `oeeDefaults()` | restores form defaults (tp=0, om=0, gate/scrap=none) |
| 💾 Save | `saveAll()` | saves everything: channels + shift + shiftday emails (+TCA map/names) |

Dead code: `resetAll()` (asset line 638) exists but **no button calls it** (see findings).

---

## 6. Client logic

- **Polling**: `fetchData()` every **2 s** → `GET /api/proximity`; pauses on
  `document.hidden`; consecutive errors shown as `(error n)` near the title.
  Every 3rd tick also refreshes OEE status (`GET /api/proximity/oee`).
- **Load-on-start**: `loadCfg()` (config table), `loadShiftCfg()`, `loadShiftdayEmails()`,
  `loadTca9554Relay()` (silent probe — no-op on boards without `/api/do/status`),
  `_initOeeTab()`.
- **Save flow** (`saveAll()`):
  1. guard: config loaded;
  2. collect channels — on fixed-pin/TCA boards GPIO fields are replaced by board
     defaults (`_proxDgi[i]` / 255);
  3. collect shift slots + emails;
  4. optional `tca` array (relay assignment per channel);
  5. `POST /api/proximity/save-all` (JSON);
  6. on TCA boards, best-effort `POST /api/do/config` with relay names;
  7. `customAlert` + `showSavedCard()`; loading overlay + disabled button throughout.
- **Live hint engine** (`updateNameHint` equivalent for this page is the field-description
  `<details>`).
- Project rules honoured: `customAlert`/`customConfirm`, no arrow functions,
  `t(key, fb)` i18n helper (window._t → localStorage cache → fallback).

---

## 7. Server API

Handlers: `handlers_unified/proximity_handler.cpp`. All JSON; errors use
`{"ok":false,"msg":"…"}`.

| Endpoint | Method | Purpose / notes |
|---|---|---|
| `/api/proximity` | GET | live values: `{channels:[{name,mode,count_in,count_out,total_in,total_out,cycle_ms,ppm,state,goal,setup_count,dir}]}`. `setup_count` = pieces counted while the gate channel is OFF (gate_ch configured). UI shows Setup col only when `state=false` (gate OFF). |
| `/api/proximity/config` | GET | full config incl. `dgi` (board default pin), `fixedPins` (true on 8DI8DO) |
| `/api/proximity/config` | POST | JSON `{channels:[…]}` — legacy single-section save |
| `/api/proximity/shift-config` | GET/POST | `{en,ar,nSlots,slots:[{h,m}],stops:[{sh,sm,eh,em}],emails}` (max 3 slots, max 6 stops). POST validates stops (HH 0–23, MM 0–59, start ≠ end, no pairwise overlap incl. overnight wrap) → 400 otherwise; only present sections are applied |
| `/api/proximity/shiftday-emails` | GET/POST | `{emails}` |
| `/api/proximity/shiftday-emails` | GET/POST | `{emails}` |
| `/api/proximity/tca9554` | GET/POST | relay map per channel (only `BOARD_HAS_TCA9554_DO`) |
| `/api/proximity/save-all` | POST | **unified save**: `{channels, shift:{en,ar,nSlots,slots,stops}, emails[, tca]}` — single atomic write; 400 if neither shift nor emails present; invalid stops → 400 |
| `/api/proximity/reset?ch=` | POST | reset one channel's counters (+persist counters) |
| `/api/proximity/reset-all` | POST | reset all channels |
| `/api/proximity/reset-total?ch=` | POST | reset lifetime totals |
| `/api/proximity/oee` | GET | computed A/P/Q/OEE per channel, shift window + day-so-far window (`day_oee/day_a/day_p/day_q` + `day_run/day_planned/day_elapsed` for the donut, see §9) |
| `/api/proximity/oee-reset?ch=` | POST | clear OEE baselines for channel |
| `/api/proximity/test-email` | POST | send SHIFTDAY-style test email (checks: recipients configured, email+WIFI ok, storage ok, NTP synced, today's SD folder exists) — when the FTP cycle holds the SD exclusivity mutex answers `sd_busy` fast instead of interleaving |
| `/api/proximity/shift-history?days=` | GET | reads SD CSVs (`days` 1–30, default 7), last 20 `shift_end` rows per day — per-day SD access is short-mutex-guarded; answers `{"error":"sd_busy"}` during the FTP cycle |
| `/api/proximity/events` | GET | **stub — returns empty structure** (findings) |
| `/api/proximity-chart-files/-cols/-data` | GET | merged chart data for charts page (needs `getChartsPageEnabled()` + `ENABLE_PROXIMITY_CHARTS`): persisted `.chart` file (SD, or identical-path LittleFS mirror) + volatile 1-min PSRAM ring tail (~30 min, always on, no configuration), one stride budget over the union — always minute-fresh on either backend. The ring tail is bounded by `untilTs` = end of the requested day, so the live samples never leak into a PAST date's series (that leak made the dashboard delta chain jump across the gap and paint ~235k into one hour) |

Chart-data streaming is single-pass with online downsampling (`chart_stream_decimate.h`,
compiled into `chart_history.cpp`): records are read in ~4 KB SD blocks and decimated into a
bounded PSRAM buffer (≤ `maxPoints` samples), JSON `stride` reports the actual power-of-two
interval used (client may re-stride). Replaced the two-pass read-everything-twice algorithm.

Validation style: missing JSON keys fall back to current in-memory values
(`obj["x"] | cur`); only bounds-checked value is the channel index. Invalid JSON →
400 `"JSON invalido"` (**hardcoded PT** — findings); LittleFS failure → 500.

---

## 8. Persistence

| File | Content | Writer |
|---|---|---|
| `/config/sensor_proximity.json` | channels + shift config + planned stops + TCA map (unified, atomic) | `saveProximityConfig()` (`proximity_manager.cpp:1584`) |
| `/config/sensor_prox_counters.json` | live counters / totals / OEE baselines | `saveProximityCounters()` (event-driven: resets, shift-end, day rollover, API + **one sparse 6 h edge snapshot** — the old every-5-min periodic was removed 2026-09-29, see recovery layers below); 2048 JSON pool + `overflowed()` guard (1024 truncated ch8 to `{}` on-device — audited 2026-09-22); parse failures log ERROR instead of silent boot |
| `/config/sensor_prox_daybase.json` | day baselines + carried day production (`dayKey`, per-channel bases + accumulators) | `saveProximityDayBaselines()` (day rollover + throttled 30 min) |
| `/config/sensor_prox_shiftbase.json` | shift baselines (`shift`, `dayKey`, `nSlots`, per-channel in/out/run/down/good/setup) | `saveProximityShiftBaselines()` (every shift-baseline capture, resets, OEE-reset; co-save counters at shift change). Restored on first shift detection after boot ONLY when slot/day/shift still match and live counters ≥ bases — otherwise fresh capture (legacy behavior). Deleted on shift-config change. |
| `/{sys}/{sys}_PROXIMITY_DD-MM-YYYY.chart` (SD) or identical path (LittleFS) + volatile PSRAM ring | 1-min chart records (`prox_ch<i>_in/out/ppm`) | uniform standard: every sample updates the accumulator immediately; PSRAM ring keeps ~30 min @1/min (always on, no configuration); persisted file written on the configured flush interval (default 30m, same rule as the CSV ticks) to SD when mounted, else LittleFS at the identical path. APIs serve file + ring tail merged. Flash mirror covered by auto-cleanup (`/<sys>/` root). |

### Counter recovery layers (no frequent LittleFS writes — 2026-09-29)

Counters survive a reboot through three layers, least-to-most flash wear:

| Layer | Where | Freshness | Used for |
|---|---|---|---|
| RTC RAM mirror | `RTC_NOINIT_ATTR` `s_proxRtc` (RTC_SLOW_MEM = SRAM, **zero flash wear**, magic+version+dayKey guard, refreshed every inject tick) | ≤60 s | soft reboots / OTA / WDT / deep sleep (`_proxRtcRestore()` at boot wins over LittleFS) |
| SD CSV tail | today's aligned-day `PROXIMITY_DD-MM-YYYY.csv` absolute `tick`/`shift_boundary` rows, written at the configured flush interval (30m/20m/...) | ≤ flush interval | **power cut** (RTC SRAM loses power): `_proxRestoreFromSdCsvTail()` at the first NTP-synced inject tick pulls the LAST absolute row (never `shift_end` deltas or `boot`); total/setup are not in the CSV and keep the LittleFS values |
| LittleFS | `/config/sensor_prox_counters.json` | event-driven + sparse 6 h | boot fallback when RTC invalid and, for total/setup, everything a power cut loses from the CSV |

Order at boot: `_proxRtcRestore()` → else `loadProximityCounters()` → (power-cut only) SD CSV tail override before the first boot CSV row so all outputs reflect the restored values.

---

## 9. Runtime engine (firmware side)

`sensors/proximity_manager.cpp`:

- Pulse capture per channel (PCNT on S3, max 4 units, else ISR), debounce filter,
  Count IN (GPIO IN) + Count OUT (GPIO IN2), cycle time & PPM, direction, lifetime totals.
- GPIO OUT pulse train: fires on detected pulses with configured delay/duration,
  suppressed after `goal` pulses (until Reset) or always in monitor-only mode.
- Multiplier scales counted parts (1x..9999; negatives removed — legacy stored values coerce to 1x on load). Values above 20 (e.g. 200x, 4000x) are entered manually; the math is identical (`delta × mul` into Count IN/OUT, totals and OEE). **Always applied, including with a divider (spec 2026-09-24): the ratio is `delta × mul ÷ div`, both on the count math and on the OEE grace window.**
- Sensor activity resets the **inactivity-alert timer** (always on boards without
  RS232/LPT; otherwise only when the alerts-page checkbox allows it) — synthetic rows
  (shift end / OEE summaries / periodic status injection) pass `resetInactivity=false`
  so they can never mask real inactivity (live Waveshare .29: the periodic
  PROXIMITY status injection used the default `resetInactivity=true` and kept
  resetting the timer every 60 s as long as any counter was >0 → alerts that never
  fired). Only a real production pulse (drain block in `proximityTick()`) may reset
  the timer, and it must satisfy the burst filter plus a **minimum inter-pulse
  cycle of `PROX_INACTIVITY_MIN_CYCLE_MS` (60 ms)** — low enough that a fast
  machine (600 ppm = 100 ms/part) still counts as production, high enough to reject
  electrical chatter (5–50 ms).
- **Shift engine**: detects transitions from configured slots; on shift change can
  auto-reset counters, appends `shift_end` CSV rows (per-shift deltas) and
  `shift_boundary` rows (absolute snapshot at the transition) under
  `/SDNAME/PROXIMITY_DD-MM-YYYY/PROXIMITY_DD-MM-YYYY.csv`, sends shift-end email to
  `shiftday_emails` (+ body built by `proximityBuildShiftEmailBody()`). Slots are
  **always normalized to ascending start time** by `proximitySortShiftSlots()`
  (`proximity_manager.h`), applied on load (`applyShiftFromObj`) and on every
  `proximitySetShiftConfig()` — all detection/duration geometry assumes sorted
  order, and an unsorted night config `{22,6}` would otherwise fire the boundary at
  00:00 and never at 06:00 (the missed 6h shift-end email). The shift-end email
  gate logs the specific reason when skipped (no emails / email not configured /
  storage unavailable, `bypassDedup=true`).
  **Email at the day boundary = final CSV of the day**: the shift-end email's
  subject+attachment resolve the day via `shiftDayCtx` (pre-boundary context, ~60 s
  before the transition), so the email fired at 22:00 still points to the folder of
  the day that just ENDED (e.g. `PROXIMITY_DD-MM-YYYY.csv` of day D = its complete
  `[D-1 22:00, D 22:00)` content): it is the day's last/final email. From 22:00 the
  aligned date has stepped to D+1, so a NEW daily CSV begins accumulating ticks and
  its first shift-end email only fires at 06:00 the next morning (end of the
  overnight turn = Turno 1 of day D+1). So between 22:00 and 06:00 there is a gap
  of shifts — by design, not a missing email.
- **Shift email body** (`proximityBuildShiftEmailBody()`): "Registos (Turno XH-YH)" +
  pieces (+ scrap/setup) + OEE line, then a **stacked time bar** (Unicode blocks:
  production ▓ / planned ▒ / unplanned ░, 30 wide — fits one email line — + legend
  with % summing to 100). Production deliberately avoids `█` FULL BLOCK: the owner
  asked for a lighter black in the shift-end email (2026-10-20). Note the two
  denominators (both standard, audited 2026-09-22):
  the bar partitions CLOCK time (`run/elapsed`, `planned/elapsed`, rest) while the OEE
  line's Availability divides by PBT (`run/(elapsed−planned)`) — so `▓%` ≠ `A%` whenever
  stops exist (e.g. 8h shift + 30-min stop + 23328s run: bar `81/6/13`, line `A=86%`),
  then the hourly "Pcs por hora" chart **using the same production-bar blocks**
  (`▓` DARK SHADE filled / `░` LIGHT SHADE track, `proximityBuildHourlyChart()`),
  e.g. `21H  446 pcs  [▓░░░░░░░░░]` — each part separated by one blank
  line. The count is `" pcs"`-suffixed and right-aligned to the widest value of the
  shift (`%02dH %*d pcs`), so every row's bar opens at the same column. The chart bar is **10 cells** (owner pick 2026-10-15: phone-width body,
  one row per shift hour), down from 20. Chart columns cover the ACTUAL shift hours `[start, end)` from
  `proximityGetShiftWindow()` and are **labelled by their END hour** — the hour
  the count is taken — so a shift 06H–14H renders the buckets `07H..14H`
  (07H = production of 06:00–07:00). Only the label is shifted; the data
  attribution is unchanged (deltas credited to the physical shift hour):
  the shift-end flush still lands in the last physical column and no column
  exists outside the shift, so another shift's production can never leak in.
  (History: aged 2026-09-23 the columns were `[start+1, end]` with the +1 ALSO
  shifting which hours were summed — the flush then went to a fictional end-hour
  column that collected the previous day's tail, 22H showed 19 850 instead of
  7 660; that data bug was fixed, and on 2026-09-26 the END-hour label was
  restored as a pure label per the owner, matching the web dashboard.) A negative
  tick-to-tick delta means the counter was reset (shift auto-reset, manual
  reset, reboot without restore): `ch0` is then counted as production since the
  reset (previous charted hour, else current hour) instead of clamped to 0, so
  the first partial hour after a reset is no longer silently dropped. At shift
  end the live counter delta is cross-checked against the CSV hourly sum
  (`_proxWriteWeeklyOee()` out-param): CSV higher by >10%+500 pcs ⇒ WARN on
  Serial/monitor + a `NOTE:` line in the email body (mid-shift reboot with lost
  baselines — header numbers partial, chart already full). **Gap-aware attribution
  (owner report 2026-10-20):** a positive tick-to-tick delta is spread over every
  bucket the pair actually covers, proportional to the minutes spent in each
  (minute read from the row timestamp, midnight wrap handled; a sub-minute pair
  counts as one bucket). Normal 30-min ticks still land in ONE bucket, but when
  rows go missing (FTP holds the SD exclusively for its whole upload cycle, so
  `_proxWriteCSVRow()` skips the row — HISTORIC #57; also reboot/clock jump/OTA)
  a 6 h gap no longer dumps the whole span into one column: live 20-10-2026 data
  (08:00→13:59, 65 640 pcs) used to render `09H 65640` with every other hour at 0
  and now renders ~10 970 per hour with the sum preserved. Each hourly bar additionally shows the **planned-stop share of that hour**
  as `▒` MEDIUM SHADE (e.g. `17H 17340 pcs  [▓▓▓▓▒▒▒▒▒░]` for a 30-min stop):
  the planned fraction is computed over **absolute wall-clock hour windows**
  (last column dated from `refWall` = shift-end/test time, earlier columns walking
  back one day per hour wrap, via `proximityPlannedStopSec()`), so a mid-shift
  reboot can never hide them — unlike the live millis-based snapshot window,
  which restarts at boot. Production fills what is left of the 10 segments;
  output is byte-identical to before when no stops are configured. The render
  mirror (`renderBar()` + planned cases in `test_proximity_chart.cpp`) is natively
  unit-tested.
  The bar inputs (shift-window run/planned/elapsed secs) are snapshot before
  the auto-reset; the manual test-email button computes the same window from the
  shift baselines. Planned/elapsed use the ABSOLUTE shift wall-clock window
  (`proxShiftWindowSec()`: ended/running slot length from the slot table ×
  boundary context — `[start, end)` for the ended shift, `[start, now)` for the
  preview), so a mid-shift reboot can never hide stops from the `▓` share the
  way the millis count-start window did; the OEE line is computed from the same
  inputs so line and bar agree. The pure builder (`proximityBuildTimeBarAscii()`)
  and the window helper (`proxShiftWindowSec()`) are natively unit-tested.
- **OEE** (user spec 2026-09 — ISO 22400-2 decomposition): **Availability** is
  **Elapsed-based** — `runSec / PBT × 100` where `PBT` (planned busy time) =
  `elapsedSec` minus planned-stop overlap, and `elapsedSec` =
  `proximityShiftElapsedSec()`, seconds since the **current shift's wall-clock
  start** (`proxShiftStartWallFor()`: most recent slot start, reboot-immune —
  fix 2026-10-14: the old `proximityCountElapsedSec(ch)` millis window restarted
  at boot while counters/baselines were restored, inflating A to the 100% clip
  after any mid-shift reboot; live .29: shift 82% vs 44% true). Fallback to the
  millis window when the clock is unsynced, shifts are off, or the shift just
  started. Manual Reset / `/api/proximity/reset` / shift-rotation reset still
  restart `_countStartMs` (Elapsed column + fallback window). No
  dependency on shift config: the numbers are "ready to read" at any moment
  ("contas na hora"). **Planned stops** (section `"stops"` in
  `/config/sensor_proximity.json`, max 6 daily `{sh,sm,eh,em}` windows, overnight
  wrap allowed, validated no-overlap on save; empty list ⇒ `PBT == elapsedSec`,
  i.e. legacy behavior) are subtracted from the denominator, so a lunch break no
  longer drags Availability down. Whole window planned ⇒ `A=100` (nothing planned
  = nothing lost). **Performance** is **Running-based per the standard** —
  `countIn / theoretical × 100` with `theoretical = targetPpm × (runSec/60)`:
  stops do NOT move the target, so P=100% whenever the machine holds its cadence
  and P<100% isolates real speed losses (no cycle-time fallback: `targetPpm==0` ⇒
  P=0; `runSec==0` ⇒ P=0). **Quality** = `effectiveGood / countIn × 100`
  (good/total, naturally ≤100), where `effectiveGood` subtracts the **Scrap**
  channel. **OEE** = `A × P × Q` (via `/10000`). **A, P and OEE are clipped to a
  maximum of 100%** inside `proximityComputeOee()`; an `elapsedSec==0` guard yields
  `A=0` (never a divide by zero). Example: 3h at target cadence + 5h stopped in an
  8h window → A=37.5%, P=100%, OEE=37.5% (single penalty, standard). The
  `oee_manual` field is the **Target OEE %** reference: it NEVER enters the A/P/Q
  math — it only colors the OEE status cell (green at/above target, amber within
  25pp below, red further below; target 0 = default 85, preserving the old fixed
  bands) and feeds the gap tooltip (`Target 85%: -41.3pp below target`). The same
  `om` storage key is kept, so existing configs carry over untouched —
  the page tooltip says so (legacy/compat only). Because the window is
  elapsed-based, A keeps degrading while the machine is stopped. The same formula
  is called by **all** consumers: `_proxInjectShiftMarker()`
  (rules-engine `PROX_OEE` line), `proximityGetRunningShiftSummary()`, the shift-end
  DI1 email block in `_proxShiftTick()`, `proximityModbusTick()` (HR +10) and
  `GET /api/proximity/oee`. The OEE window origin is shared by baselines +
  elapsed: `proximityResetChannel()` (manual Reset, both PCNT/SW variants, and
  the automatic shift-rotation `proximityResetAll()`) zeroes counters AND
  baselines AND elapsed, so the baseline-relative inputs (`running_sec`,
  `downtime_sec`, `count_in`, `good_parts` in `/api/proximity/oee`) restart at 0
  and grow — without this they would clamp at 0 against stale pre-reset values.
  `proximityResetOee()` ("Reset All OEE" button) restarts the same window
  WITHOUT zeroing counts (live counters become the new baselines + elapsed
  restarts). `proximityShiftSlotDurationSec()` and
  `proximityOeePlannedSec()` are now **legacy** (kept in header, used only by
  native tests) — planned shift duration no longer feeds A/P/OEE.
- **Day-so-far OEE** (`day_oee/day_a/day_p/day_q` + `day_run/day_planned/day_elapsed`
  in `/api/proximity/oee`): the same formula over `[day boundary, now]` — per-channel
  day baselines captured at the shift-aligned day rollover by `_proxDayTick()` (same
  30 s rate limit as the shift tick; first detection after boot starts the day window
  at boot), elapsed = seconds since the day boundary (the aligned rollover, e.g. 22:00
  for 6/14/22), planned-stop overlap over the same window. Resets never wipe the day:
  `proximityResetChannel()` (both variants, manual + automatic shift-rotation
  `proximityResetAll()`) folds the live day tails into `_dayAccum*[ch]` BEFORE zeroing
  counters/bases, so `dX = accum + max(0, counter − base)` (`proxDayDelta()`, the single
  rule behind `proximityGetDayDeltas()`) always spans the whole day; `proximityResetOee()`
  restarts the day too (accums cleared + bases re-anchored at live). Accums reset only at
  the day rollover (`_proxCaptureDayBaselines()`) and persist in the daybase file
  (`accIn/accRun/accDown/accGood` per channel, old files load with 0). Getters:
  `proximityGetDayDeltas()`, `proximityDayElapsedSec()`, `proximityDayPlannedStopSec()`;
  day-rollover key helper `proximityDayKey()`.
- **OEE status table columns** (TAB OEE `#oee-sb`, from `/api/proximity/oee`):
  A% / P% / Q% / OEE% + **Running** (`running_sec`, H:MM:SS) + **Stopped**
  (`downtime_sec`, H:MM:SS — total stopped time without parts in the window) +
  Scrap. Both times are baseline-relative (since shift start / last reset).
- **Page OEE formula reference** (TAB OEE `<details>` block): Availability =
  Running ÷ Elapsed × 100 (elapsed = since last count reset, NOT the configured
  shift length; clipped 100%); Performance = Actual ÷ (Target PPM × Running Min)
  (clipped 100%); Quality = Good ÷ Total; OEE = A×P×Q÷10000. Rendered client-side
  via the i18n key `proxOeeFormulasHtml` (innerHTML) and must be kept in sync with
  `proximityComputeOee()`.
- **Per-second running/downtime window** (OEE input): every tick the edges drained
  since the last tick are accumulated into `oee_count_window`, and the elapsed ms
  into a sub-second accumulator. Whole seconds are consumed by
  `proximityOeeWindowConsume()` (`proximity_manager.h`, native-tested) — a consumed
  second with ≥1 edge in the window counts as **running**; a second with no edges
  counts as running only inside the **slow-machine grace** (gap since the last
  accepted edge ≤ 2× the ideal EDGE cycle `60000 × multiplier / (target_ppm × divider)` —
  the multiplier matters: at 10 parts/pulse and 300/min the edges come every
  2000ms, so the grace is 4000ms — capped at 10 min; `proximityOeeGraceMs()`,
  standard APT), otherwise **downtime**. The multiplier is **always part of the
  ratio** (spec 2026-09-24): the grace uses `mults × div` in the same way the
  count math uses `delta × mul ÷ div`, so the two always agree. (Gate-present and gate closed → no
  classification; never pulsed since reset → downtime). The window is cleared **only
  when a second was actually consumed** (never per tick): a fast loop must be able
  to accumulate sub-second edges across ticks, otherwise every second misreads as
  downtime and A/OEE collapse to ~0 while the machine is producing (live Waveshare
  .29: running_sec=40 vs downtime_sec=23775 for a 232 720-part line — A=0.14%,
  OEE≈0; the same window-capture fix also keeps a loop stall (elapsed capped at
  2000 ms) from zeroing the window mid-second and counting the 2nd second as
  downtime.
- **Shift-aligned day boundary (Waveshare pack only)**: instead of rolling the
  daily SD file/folder over at 00:00, the boundary follows the last shift hour of
  the day so the shift report is not cut near midnight. The day runs from the
  boundary hour on calendar day D-1 to the SAME boundary hour on day D and is
  **labeled with day D** (the date the day ENDS into — the day the overnight first
  turn belongs to). `proximityShiftDayAlignedDate()` (in `proximity_shift_config.cpp`)
  shifts the date used for the daily folder/file: any slot starting at 00:00
  (e.g. `00-12/12-00`) keeps the 00:00 rollover; otherwise the boundary is the
  latest slot start hour (e.g. **15h** for `03-15/15-03`, **22h** for
  `22-06/06-14/14-22`). Times at/after the boundary already belong to the NEXT
  day's file — the CSV for day D is created at the boundary of day D-1 (at 22:00
  "still on the previous day"). The arithmetic is a single pure helper
  `proxShiftAlignedDate()` (`proximity_manager.h`, native-tested by
  `test_proximity_day_aligned`) which **all** folder consumers share, so the
  written folder and the read folder are always the same date:
  - writing: `_proxWriteCSVRow()` (creates `/SDNAME/PROXIMITY_DD-MM-YYYY/` if missing).
    All SD touches are guarded by the SD exclusivity mutex with a 1.5 s timeout:
    while the FTP cycle owns the SD (see `PAGES/ftp.md`), the row is **skipped**
    (never interleaved with the SDMMC scan — that concurrency was the Equip15
    crash-loop, HISTORIC #57) and appends resume on the next flush after the cycle.
  - row layout (append-only, append-to-the-right): `timestamp,type,shift` +
    per channel `ch<i>_in,ch<i>_out,ch<i>_ppm,ch<i>_run,ch<i>_down,ch<i>_good`.
    Ticks/`shift_boundary` hold ABSOLUTE snapshots; `shift_end` holds per-shift
    DELTAS; `boot` holds the live snapshot at boot with the ESP reset-reason code
    in the shift slot (0 UNKNOWN, 1 POWERON, 2 EXT, 3 SW, 4 PANIC, 5 INT_WDT,
    6 TASK_WDT, 7 WDT, 8 DEEPSLEEP, 9 BROWNOUT, 10 SDIO), emitted once on the first
    synced tick even when idle. Readers parse fixed columns from the LEFT, so old
    3-col files keep working and `boot` rows are skipped like any non-tick row
  - reading: auto shift-end email, the `SHIFTDAY` command (no date arg) and the
    `POST /api/proximity/test-email` button (fixes "Today's proximity folder not
    found on SD" when the aligned date differs from the calendar date)
  - `sdcard.cpp` (`sdcardWriteData`, `sdcardGetTodayFolder`) for the Waveshare
    build only.
- **Weekly OEE CSV (single file per ISO week, additive)**: at every shift-end the
  board appends **one row per hour of the ended shift** to a single per-week file
  `/SDNAME/wk<NN>-oee.csv` (NN = ISO 8601 week number, from `proximityIsoWeek()`
  via `strftime %V/%G`; file name built by `proximityWeeklyOeeFileName()`). Columns (header written on
  first write, `f.size()==0`): `date,shift,hour,count_hour,total,scrap,setup,a,p,q,oee,good,run_sec,plan_sec,unplanned_sec,elapsed_sec,ppm,weekday`.
  `date` is `DD-MM-YYYY` of that hour, `shift` is 1-based turn number **rotated so
  the turn starting at the day boundary is Turno 1** (e.g. 6/14/22: 22h=1, 6h=2,
  14h=3 — via `proximityShiftTurnNumber()`, config-derived; identity numbering when
  a slot starts at 00:00), `hour` is 0..23 and is the **END hour** (the hour the
  count is taken — same convention as the email chart/dashboard: shift 6H–14H
  writes hours 7..14),
  `count_hour` is the pieces attributed to that hour (parsed by the shared
  `_proxReadHourlyCounts()` from the daily CSV — same source as the email chart),
  `total` accumulates across the shift hours and the last row is pinned to the
  real shift-end total (`shiftPiecesDI1`), `scrap`/`setup`/`a`/`p`/`q`/`oee` are the shift-end
  snapshot values (availability/performance/quality/oee) repeated per hour, and the
  trailing summary columns repeat the shift-level auto-reset snapshot per line so
  each row is self-contained for Excel analysis: `good` (good parts = effectiveGood),
  `run_sec` (production seconds), `plan_sec` (planned-stop seconds of the absolute
  shift window), `unplanned_sec` (`elapsed − plan − run`, floor 0), `elapsed_sec`
  (shift length), `ppm` (target PM, the same source the OEE P uses) and `weekday`
  (0=Sunday..6=Saturday from `tm_wday`, per-hour so a midnight-crossing shift reports
  both days). Written by `_proxWriteWeeklyOee()` from the
  shift-end block (after the DI1 snapshot, before auto-reset). Pure helpers
  (`proximityIsoWeek`, `proximityWeeklyOeeFileName`, `proximityFormatOeeRow`) live
  in `proximity_manager.h` and are native-tested by `test_proximity_oee_csv`.
  Additive — never touches the per-day PROXIMITY_*.csv files.
  Before this change the writer used the aligned date but SHIFTDAY read the real
  calendar date (commit d01e5ca regression) → folder not found + no email.

---

## 10. i18n keys

**Added (full PT/EN coverage, `lang_en_ui.h` + `lang_pt_ui.h`):** 8 key(s) — `proxColSetup`, `proxDoNamesTitle`, `proxStatusTitle`, `proxTestEmailBtn`, `proxTipCfgGateCh`, `proxTipCfgScrapCh`, `proxTipColSetup`, `proxTipDoName`. HTML fallback text is English; PT is served from the packs.

`proxTitle`, `btnHome`, `proxTabStatus/Config/Shifts/Shiftday/OEE`, `proxCol*`
(incl. `proxColElapsed` and tooltip `proxTipColElapsed`),
`proxCfgCol*`, `proxTip*` (tooltips), `proxField*` (incl. `proxFieldHelpHtml`), `proxDoNamesTitle`,
`proxStateOn/Off` (live ●ON/○OFF cell), `proxLoading`, `proxNoChannels`,
`proxNoChannelsCfg`, `proxNoOeeData`,
`proxShift*` (incl. `proxStopsTitle/Hint/Use`, `proxTipStops`), `proxShiftday*`, `proxTestEmail*`, `proxOee*` (incl. `proxOeeColRun/Stop`,
`proxOeeColComponent/Formula`, `proxOeeFormulasHtml`, `proxOeeTipRun/Stop`), `proxShow`, `proxCh1…8`,
`proxConfigSaved`, `proxErr*`, `proxConfirmResetCh/All`, `btnSave`.
Firmware-side strings are mostly raw literals (e.g. `Prox: DI%d reset via web`).

---

## 11. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | `resetAll()` defined (asset line 638) but **no button calls it** — "Reset All" affordance missing while the API exists. | Medium | Add Reset All button to Status tab (or remove dead code). |
| 2 | `GET /api/proximity/events` is an **empty stub** — loops channels/events, serialises nothing. | Medium | Implement or remove route (saves flash + confusion). |
| 3 | Server error strings hardcoded PT (`"JSON invalido"`) — violates EN-fallback rule. | Low | English messages (i18n happens client-side anyway). |
| 4 | `[prox:diag] GET config:` INFO log with `bypassDedup=true` on **every** config GET (`proximity_handler.cpp:148`) — debug leftover that spams the Web Monitor. | Medium | Remove or demote behind a debug flag. |
| 5 | `save-all` rejects with 400 when `shift`/`emails` absent even if `channels` valid — all-or-nothing behaviour undocumented client-side. | Low | Document or relax. |
| 6 | Test email fails until today's SD folder exists (`Today's proximity folder not found on SD`) — confusing when validating fresh setup. | Low | Allow empty-folder test (attach nothing / say so). |
| 7 | Config table `colspan="12"` placeholder vs 14 real columns — cosmetic. | Low | Fix colspan. |
| 8 | OEE "Apply Changes" not persisted until global Save (alert warns, still easy to lose). | Info | Consider merging into Save. |

---

## 12. Source map

| Concern | File |
|---|---|
| Renderer + tab system | `chatgpt/src/webpages/renderers/proximity_renderer.{h,cpp}` |
| Page HTML/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_proximity_asset.h` |
| Routes (pages + API) | `chatgpt/src/webpages/routes/web_routes_proximity.{h,cpp}` |
| HTTP handlers | `chatgpt/src/webpages/handlers_unified/proximity_handler.{h,cpp}` |
| Counting / shift / OEE engine + persistence | `chatgpt/src/sensors/proximity_manager.cpp` |
| Dashboard page (separate) | `renderers/proximity_dashboard_renderer.*`, `assets/page_proximity_dashboard_asset.h` |
