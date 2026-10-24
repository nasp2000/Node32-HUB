# Gráficos e OEE (Proximity Dashboard) — `/proximity-dashboard`

> One-stop reference for the **Proximity Counts wallboard page**: purpose, layout, every
> button/input, client logic, API endpoints and persistence. Written for both humans and AI
> agents.
>
> Note: `/proximity` (the configuration page) has its **own doc** — see
> `PAGES/proximity_sensors.md`.

---

## 1. Objective

A read-only, TV-style **live dashboard** for the proximity/encoder counters: **one 24-hour
per-hour bar (or line) chart per channel in use**, rendered locally on canvas, plus a
**real-time OEE/A/Q/P row with the current-shift indicator**. Charts only — no cards
(user spec 2026-09: the per-channel squares and the day-summary card were removed).
Designed for a big screen in the factory — light theme, auto-polling every 2 s.

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /proximity-dashboard` |
| Feature flag | `ENABLE_PROXIMITY` (same as the config page; `#if` in renderer + asset) |
| WiFi server | `web_routes_proximity.cpp:195` — `streamProximityDashboardPageToPrint()` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h:238` → `streamSharedProximityDashboardPage` |
| Home entry | Home page "📊 Gráficos e OEE" button (`home_page_renderer.h:168`, `target='_blank'`) |
| Auth / caching | `ensureBasicAuth` + `allowHtmlClient`; `ETag` = `FIRMWARE_BUILD`, `private, max-age=15, must-revalidate` |
| Session exemption | ALWAYS exempt (local processing) — unconditional `window._localPagesNeverExpire=true`, never kicked by time |

Renderer: shell → never-expire script → `PROXIMITY_DASHBOARD_PAGE` (PROGMEM) →
auto.js footer.

---

## 3. Layout

```
main.page
 ├ #pdTrigger (12px hover strip, left edge) + #pdPanel (fixed 230px slide-in, compact:
 │   Display options — sections View / Data / Scale, 10 selects persisted in
 │   `localStorage['prox_dash_opts']`; opened ONLY by hovering the left-edge
 │   strip (mouseenter) — no toolbar button since 2026-09 — closed by ✕ or by
 │   moving the mouse away to the right)
 ├ .pd-banner → H1 "📊 Charts and OEE" + "Real-time monitoring"
 ├ .pd-statusbar → centered: DAY + LAST HOUR stat cards (green/amber boxes,
 │   `#liveBar`, hidden until the first live data)
 ├ .pd-oee → real-time KPI bar (`#oeeBar`), one row: day rectangle on the LEFT
 │   (`.pd-donut-box` shared background: "Day OEE" label + value, donut canvas,
 │   Production/Planned/Unplanned legend) + shift rectangle on the RIGHT (`.pd-shift-box`
 │   blue rectangle grouping what belongs to the running shift: `#shiftName` shift box +
 │   "Shift OEE" (violet) + Availability / Performance / Quality boxes; wraps below
 │   on narrow screens)
 └ #rows  → dynamically built, one `.pd-row` per channel in use:
 └ .pd-row-right (full width) → .pd-chart-title "<system|channel> — DD-MM-YYYY"
        (small centered, ~12–16px; date = production-day aligned date, the date
        the day ends into) + canvas#chart<i> (360px tall)

Channel visibility = `isActive()` (counts > 0 or configured `gi` ≠ 255) capped by
the same "Mostrar N canais" selector as the `/proximity` page
(`localStorage['prox_visCh']`) and THEN minus aux-only gate/scrap channels
(`computePrimary()`): visCh slices channels 1..N first, so visCh=3 with
DI2=scrap + DI3=gate of DI1 shows only DI1 — never DI4/DI5 pulled into view.
```

Chart type is selectable in the sidebar (`pdChart`): **bars** (standard) or **line**.
Bars: thin (`slotW = plotW/24`, bars ~35% of slot, ≥2px) production bars (green
`#16a34a` at/above the hourly target, orange `#f59e0b` below; channel blue only when no
target is configured) and **scrap (red) bars side by side** in each hourly slot.
Line: production polyline with a fading area fill (channel color) + dashed red scrap
line, current hour highlighted with a white-ring dot; value labels rotate upward above
non-zero points. The scrap series comes from the channel's configured scrap channel
(`cfg.sc` → that channel's IN; fallback = own OUT sensor) via `scrapMap`, in both live
(`updateHourly`) and historical (`loadHistoricalData`) paths. Nonzero scrap always gets
≥2px height so small scrap stays visible next to large production bars. A dashed target
line (hourly target = `tgtPpm × 60 × targetPct`, caption e.g. `15300/h` at the 85%
default) overlays the plot — it marks the hourly objective at the configured cadence
scaled by the sidebar Target % (T19). No shift-boundary markers
(removed 2026-09 per user spec). Each chart carries a `.pd-legend` chip pinned to its
top-right corner (T19): green "Good ≥ target" + orange "Good < target" (= good pieces
vs the % target), red "Scrap"; single channel-color "Production" entry in solid-colors
mode, no scrap entry in production-only mode.
Y axis: 0 to the hourly target exactly, rising past it (`ceil(dataMax × 1.05)`)
only if some hour exceeds the target; no target configured = data-driven
(`ceil(dataMax × 1.15)`). The 24h chart shows one slot per hour (current column
highlighted with a ▼ marker in bars mode). The 24 columns follow the shift
layout (production day): column 0 is the boundary hour itself on the previous
date (production 22:00–23:00 for boundary 22), column 23 is the hour before the
boundary on the aligned date (production 21:00–22:00) — i.e. production hours
22H..21H. Columns are labelled by their END hour like the shift email chart, so
a 22h boundary reads `23h,24h,1h…22h` left-to-right ("das 23H às 22H"). Boundary 0
(shifts disabled, empty slots, or a 00:00 slot) = legacy calendar 00H..23H layout.
Tick labels thin out (every 2nd/4th, last column kept when it fits) on narrow
screens so they never overlap.

**Ring-tail bound (fixed 2026-10-20).** `loadHistoricalData()` fetches two
production-day segments (previous date from the boundary, plus the aligned date)
and accumulates per-hour buckets from `v − prev`. The backend
(`proxChartServeMergedJson`) merges the live 1-min PSRAM ring tail into every
response; it was filtered only by `fromTs`, so the PREVIOUS-date segment also
carried today's live samples. The last row of segment 1 (yesterday 23:30) and the
first live ring sample (today 19:18) then produced one delta of +234 790 credited
to a single hour — the "one giant bar + empty hours" symptom on the dashboard
(verified on .29: column 19h = 270 230 while every other hour was 8k–17k). The
tail is now bounded by `untilTs` (end of the requested day), so a past date
returns its own file only and the delta chain stays continuous. The shift-email
side of the same symptom is fixed in `_proxReadHourlyCounts()` (gap-aware
attribution, see `proximity_sensors.md`).

**Production-day window (fixed 2026-10-23).** The `.chart` file was named by the
civil date while the CSV/totals use the shift-aligned production day
(label D = `[D-1 b:00, D b:00)`), so a direct `?date=D&mode=day` started at 00:xx
and lost the night-shift head (22:00–24:00) versus the donut — the dashboard only
masked it by merging two requests. The writer now names/flushes the `.chart` by
the aligned date (`proximityShiftDayAlignedDate`, same rule as the CSV) with the
production-day start in the header, and both chart endpoints
(`handleProximityChartDataGet` unified + the legacy `rs232_history_handler` one
that actually serves WiFi, first-registered) default `fromTs` to
`proxShiftDayStartFromCivilMidnight()` and `untilTs` to start + 86400
(`proximity_manager.h`, unit-tested in `test_proximity_day_aligned.cpp`).
`mode=live`/`8h` and explicit `from=` are unchanged; payload format unchanged.

Light theme (`--pd-*` CSS variables, values identical to the sensor-charts **sky**
theme); responsive: charts go full width at ≤700 px.
Typography and canvas colors follow `/sensor-charts` 1:1 (same size relationships):
banner `clamp(17,3.5vw,26)`/`clamp(10,1.4vw,12)`, chart titles `clamp(13,2.2vw,17)`,
values `clamp(11,1.6vw,14)`, labels 9–10px uppercase, sidebar 10/8/9px (T19 compact);
canvas grid `#bfdbfe`, ticks `#0369a1`, donut track `#bfdbfe` (sky `--cgrid`/`--ctick`).
Single-chart pages fit without page scroll at 768px desktop height (banner +
toolbar + 360px chart ≈ 670px) and 844px mobile portrait. Verified with
headless-Chromium screenshots (390px + 1366px) and a CDP layout probe
(`scrollHeight ≤ viewport`, button/box geometry).

---

## 4. Inputs / buttons

**No page buttons.** The top bar has no ⚙️ toggle and no title selector (both removed
2026-09 per user spec). The sidebar (`#pdPanel`) is revealed only by hovering the
12px strip at the left screen edge (`#pdTrigger` mouseenter), and hidden by the ✕
button or by the mouse leaving the panel to the right (mouseleave `clientX > 12`);
no keyboard/button path exists (TV remotes open it from the sensor-charts page pattern
— see backlog item 4).

Statusbar — two centered stat cards for the first charted channel (`primary[0]`):
green DAY card (production-day total since the day boundary + day parts/min ÷
minutes since that boundary) and amber LAST HOUR card (current-hour production
+ current-hour parts/min), via `dayStats()`.

OEE KPI bar — `#oeeBar`, first charted channel only, updated every poll:
- `#shiftName` box (`pd-kpi-shift`, same box style as the OEE boxes) — label
  "Turno/Shift", value `"N (HH:MM–HH:MM)"` for the current shift: the turn number is
  **rotated so the day-boundary turn is Turno 1** (slot index computed client-side
  from the shift-config slots, mirrors the firmware `_detectCurrentShift()`: largest
  slot start ≤ now, before the first slot = the last shift; displayed N via
  `shiftTurnNumber()` — 6/14/22 → 22h=1, 6h=2, 14h=3); `—` when shifts are disabled.
- "Day OEE" (green, `#dayOeeVal`) = OEE since the **shift-aligned day boundary**
  (firmware day baselines captured at the day rollover, e.g. 22:00 for 6/14/22 shifts,
  + elapsed since that boundary), placed left; "Shift OEE" (violet, `#oeeVal`) = OEE
  of the running shift, next to it.
- **Day donut** (`#dayDonut` canvas + HTML legend): production (green) + planned
  stops (amber) + unplanned (red) as fractions of the **day window**
  (`day_run`/`day_planned`/`day_elapsed` from `/api/proximity/oee`), starting at the
  top, clockwise; center empty (no text); legend % forced to sum 100.
- Availability (green), Performance (amber), Quality (blue) — running-shift
  values, one-decimal percentages from the firmware OEE calc.

Display-options sidebar (11 selects in 3 sections, same 280px panel as
sensor-charts but denser content — T19 compact: title 10px, section titles/labels 8px,
selects 9px / 2px 4px padding / full width, tighter vertical gaps — applied to every
chart on change via `onDashOptChange()` → `redrawCharts()`, persisted in
`localStorage['prox_dash_opts']` with sanitized defaults on load):
- **View** — `pdChart` (bars = standard, or line), `pdTitle` (chart title source:
  `sys` system name = **default**, or `ch` channel name), `pdBarMode` (dual
  production+scrap = standard, or production only), `pdColors` (green/orange vs
  target = standard, or single channel color; bars mode only), `pdLabels` (value
  labels on/off), `pdTargetLine` (target line on/off), `pdTargetPct` (T19: hourly
  target as 60–100% of PPM, **default 85%**; out-of-range/stored-garbage falls back
  to 85), `pdHourMark` (▼ marker / dot on/off).
- **Data** — `pdWindow` (24h full production day = standard, or last 12h / last 8h
  columns ending at the current column; Y max over the visible window only).
- **Scale** — `pdYMode` (`target`: 0 → hourly target exactly, growing past it =
  standard; `fit`: 0 → just above visible data; `wide`: 0 → 2× headroom),
  `pdTicks` (`auto` thinning = standard, `all` every visible hour, `sparse` every 4th).

---

## 5. Client-side logic

- **`poll()`** — every **2 s**: `Promise.all` of `GET /api/proximity` (live) +
  `GET /api/proximity/config` (channel names/goals) + `GET /api/proximity/oee`
  (A/P/Q/OEE + running/downtime per channel) + `GET /api/proximity/shift-config`
  (shift slots). Success → `renderRows(live, cfg)` + `updateHourly(live)` +
  stat cards via `dayStats()` + `updateOeeKpi(oeeArr, shiftCfg)`; failure → silent
  retry (no connection indicator since 2026-09).
- **`isActive(ch, cfg, i)`** — a channel is shown if `count_in/count_out > 0` **or** the
  configured `gi` (input GPIO) is not 255.
- **`renderRows(live, cfg)`** — builds chart-only rows for channels in use via
`computePrimary()` (visCh slice, then aux-only gate/scrap removal).
  Keeps per-channel chart state (`hourlyData[key]`, `chartLines[key]` from a fixed
  `PALETTE`), deletes charts for now-inactive channels. Chart title = `chartLabel()`
  (system name in `sys` mode — **default**, channel name in `ch` mode) + date.
- **`loadSysName()`** — fetched once from `/api/config?section=system`; cached in
  `sysName` and used for the default chart title; retried every 30 s on failure.
- **`dayStats(hd, now, boundary)`** — pure, unit-tested: sums the 24 production-day
  columns (day total), day parts/min (÷ minutes since the production-day start via
  `prodDayStartMs()`, 0-guarded; omitted/0 boundary = legacy midnight math),
  current-column production and current-hour parts/min (÷ minutes into the hour,
  0-guarded).
- **Production-day helpers** — pure, unit-tested, mirroring the firmware R2 day
  model (`proximityShiftDayBoundaryHour()` / `proxShiftAlignedDate()`):
  `prodDayBoundary(shiftCfg)` (0 when disabled/empty/00:00 slot, else last slot
  hour), `prodDayStart(boundary)` (first-column production hour: `B`, 0 when B=0),
  `colForHour()` / `hourForCol()` (column↔hour bijection), `prodDayDates(now,
  boundary)` (`{aligned, prev}` Dates), `dayBoundaryTs()` (epoch of prev date at
  `B:00` for the `from=` fetch anchor), `prodDayStartMs()` (day-start epoch ms),
  `currentBoundary()` / `currentDayStart()` (from the `_lastShiftCfg` poll
  payload).
- **`detectShiftIndex(slots, now)`** — pure, unit-tested client mirror of the firmware
  `_detectCurrentShift()` (largest slot start ≤ now; before the first slot = last shift).
  Returns the slot INDEX; the displayed turn number is rotated via
  `shiftTurnNumber(slots, idx)` so the day-boundary turn reads **Turno 1** (6/14/22 →
  22h=1, 6h=2, 14h=3; identity when a slot starts at 00:00).
- **`shiftTurnNumber(slots, idx)`** — pure, unit-tested client mirror of the firmware
  `proximityShiftTurnNumber()` (boundary = last slot start hour; R2 day model).
- **`shiftWindowText(slots, idx)`** — pure, unit-tested `"HH:MM–HH:MM"` slot range,
  wrapping across midnight.
- **`updateOeeKpi(oeeArr, shiftCfg)`** — populates `#oeeBar` for `primary[0]` from the
  `/api/proximity/oee` entry: `#dayOeeVal` from `day_oee`, `#oeeVal`/`avail`/`perf`/`qual`
  from the shift-window fields (one-decimal %), donut legend from
  `day_run`/`day_planned`/`day_elapsed` (via `donutFractions()`, % forced to sum 100),
  `drawDayDonut()` for the canvas, and the `#shiftName` box (Turno N via `shiftTurnNumber`);
  hides the row when no channel is active.
- **`donutFractions(runSec, plannedSec, elapsedSec)`** — pure, unit-tested day-window
  split (production/planned/unplanned, clamped to sum 1; null when empty).
- **`drawDayDonut(canvas, runSec, plannedSec, elapsedSec, centerText)`** — 96px
  DPR-aware canvas donut (green/amber/red arcs from the top, clockwise); center
  intentionally empty (called with `''` — user spec: no percentage inside).
- **`dashOpts()` / `saveDashOpts()` / `onDashOptChange()` / `redrawCharts()`** —
  sidebar prefs with sanitized defaults (`chart:bars, title:sys, bars:dual,
  colors:target, labels/target/hour:on, targetPct:85, window:24, y:target,
  ticks:auto`); any change saves + re-renders all charts from `_lastLive`/`_lastCfg`
  (no refetch — the 24 buckets are re-sliced locally).
- **`targetHourly(tgtPpm, pct)`** (T19) — pure, unit-tested hourly objective
  (`round(tgtPpm × 60 × pct/100)`; pct clamped to 60–100 else default 85;
  `tgtPpm ≤ 0` = 0 = no target). Single source for the dashed line, the
  green/orange bar threshold and the `target` Y mode.
- **`chartLegendHTML(color, vo)`** (T19) — pure (apart from `t()` fallbacks),
  unit-tested per-chart legend HTML for the top-right chip; follows
  Content/Colors modes.
- **`chartYMax(dataMax, hourlyTarget, yMode)`** — pure, unit-tested Y-max math
  (target exact / +5 % growth / fit / 2× wide / data-driven fallback, never < 1).
- **`chartTickStep(slotWpx, tickMode)`** — pure, unit-tested tick thinning
  (auto thresholds, `all` = 1, `sparse` = 4).
- **`updateHourly(live)`** — tracks per-channel production-day columns: on the first
  sight (or when the `boundary|aligned-date` dayKey changes — new production day
  or shift-layout reconfig with the page open) it zeroes the 24 columns and
  re-baselines, then on each poll fills `counts[curCol] = count_in - baseline`;
  on column change it finalises the previous column's bucket and re-baselines.
  Calls `drawChart24(...)` with `dashOpts()` + `vo.dayStart`.
  T23: also feeds the live stop tail (`hd.stopBins`/`hd.stopPrev`/`hd.stopPrevTs` via
  `markStopSec` on every poll — credits the seconds since the previous poll to the
  current 5-min cell when the counter did not advance, covering the minutes past the
  last SD flush; the historical backfill below replaces `hd.stopBins` and the next poll
  re-marks the current cell, since `stopPrev`/`stopPrevTs` track the same counter/clock).
- **`drawChart24(canvas, hourly, scrapHourly, chName, dateStr, curCol, color, tgtPpm, vo?)`** —
  2D canvas chart (equal slots, DPR-aware), current column highlighted (▼ marker in bars,
  white-ring dot in line); Y gridlines 0 → hourly max (`targetHourly(tgtPpm, vo.targetPct)`,
  grows past it only if exceeded); X tick labels by column end-hour (`1h`…`24h` legacy,
  `23h,24h,1h…22h` for a 22h boundary — thinned on narrow screens); dashed hourly-target
  line + `/h` caption; no shift markers. `vo` (from `dashOpts()`, omitted = standard look):
  `targetPct` scales the hourly target (default 85 when omitted),
  `dayStart` selects the production-day first hour (0 = legacy calendar layout),
  `window` slices the last 8/12 columns ending at `curCol` (Y max over the visible window),
  `chart:line` renders the polyline/area instead of bars, `bars:prod` hides scrap,
  `colors:solid` skips target coloring (bars only), `labels/target/hour:off` hide value
  labels / target line / current-hour marker, `y`/`ticks` override via
  `chartYMax()`/`chartTickStep()`. 11th optional arg `stopBins` (T23, omitted = legacy
  solid hour strip): per-column **6×10-min stop squares** in SECONDS without production
  (see `markStopSec` below). Owner revision 2026-10-21: the strip is quantised to
  10 minutes, **6 squares per hour maximum**, and **only stops of at least 10 min are
  shown** — `stopSquaresFromBins()` groups stop-ish squares (≥ 300 s) into runs, drops
  runs below `STOP_CELL_SHOW_SEC` (600 s) and paints `round(run/10 min)` squares from
  the run start, so 10 min = 1 square, 15 min = 2, 20 min = 2, 45 min = 5. No half
  squares and nothing shorter than 10 min ever appears; one separator per square.
- **`stopCellsFromBins(bins, dayStartEpoch, dayStart)`** — pure: folds the server's
  10-min stop buckets (`?stops=1`; index 0 = block starting at `dayStartEpoch`, values
  in SECONDS without production, `-1` = not watched) into the same 24×6 cell grid the
  strip draws (`col = colForHour(hour of the block start)`, `k = minute/10`), so
  `stopSquaresFromBins()` and `drawTimeline()` need no change. `-1` is kept as unknown
  (breaks a run instead of counting as production).
- **`loadStopBins()` / `applyServerStops()`** — fired 6 s after load and re-fired every
  2 min: fetches the compact stops payload for the aligned date and attaches
  `hd.srvBins` per channel; `drawChart24` receives `hd.srvBins || hd.stopBins`, i.e. the
  exact server measurement replaces the counter heuristic for every channel the board
  watched today (a channel missing from `stops` keeps the heuristic). 6 s/2 min keeps it
  off the first paint and well inside the route's rate guard.
- **`loadHistoricalData()`** — fired 3 s after load: fetches the two calendar files
  the production day spans (`GET /api/proximity-chart-data?date=PREV&mode=day&from=<boundaryTs>`
  + `?date=ALIGNED&mode=day`; boundary 0 = single legacy request) and buckets the
  historical per-channel `prox_ch<i>_in` deltas into production-day columns, carrying
  counter baselines across the midnight file boundary; rows outside
  `[boundary, now]` are skipped (but still advance baselines). Backfills every column
  through the current one (`k <= curCol`, `mergeHistoryBackfill()`); future columns stay
  empty. T23: every kept sample pair also feeds `markStopSec()` (same baseline
  discipline, plus the previous timestamp), building the per-channel 24×12 stop cells
  (seconds without production per 5-min cell) attached as `hd.stopBins`. Since
  2026-10-24 the server's 10-min RAM buckets (`?stops=1`, see `loadStopBins()`) take
  precedence for every channel the board watched (`hd.srvBins`); this heuristic stays
  as the fallback (old firmware, request failed, channel never seen today). **Current-hour column full from SD + live re-anchor:** on a page (re)load mid-hour
  the current column is backfilled from SD and each channel's live baseline is re-anchored
  to `liveIn − SD_current_col` (clamped ≥0; scrap counterpart via the scrap-mapped channel,
  else own `count_out`), so the current-hour bar shows the whole hour from start — not just
  the delta since the page opened — and the next live poll adds only the delta past SD.
  The re-anchor applies only when the page's `lastCol` matches the current column: right
  after an hour rollover the live baseline was already set by `updateHourly`, so it stays.
  **Segments are fetched sequentially with a 429 retry** (see `fetchChartSegments()` /
  `fetchChartSegmentOnce()` below): `/api/proximity-chart-data` is rate-limited server-side
  (`HeavyApiRequestGuard`, ~1 request/s per route on the WiFi path), so a parallel
  `Promise.all` previously made one of the two requests fail and the affected segment was
  silently dropped, leaving some hourly columns empty. Each URL is fetched one at a time; on
  HTTP 429/503 (or network/JSON error) it waits ~1.1 s and retries up to 3 times; when a
  segment is still failing after the retries it resolves `null`, which the bucketing loop
  skips (same graceful degradation as before). No arrow functions (ESP32 browser compat).
- i18n via `t(k,fb)` helper (window._t → localStorage cache → fallback): `proxDashNoActive`,
  `proxDashDay`, `proxDashLastHour`, `proxShiftLabel` (shift chip). Several legacy keys
  remain in the lang files for EN/PT parity but are no longer rendered.

---

## 6. Server API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/proximity` | GET | live values `{channels:[{name,mode,count_in,count_out,total_in,total_out,cycle_ms,ppm,state,goal,dir}]}` |
| `/api/proximity/config` | GET | channel config incl. `dgi` board default pin, `fixedPins`, per-channel `gi`/name/goal |
| `/api/proximity/oee` | GET | per-channel real-time OEE result `{ch,name,scrap_count,running_sec,downtime_sec,effective_good,target_ppm,availability,performance,quality,oee,theoretical,day_oee,day_a,day_p,day_q,day_run,day_planned,day_elapsed}` (`day_*` = day-so-far since the shift-aligned day-boundary baselines; `day_run/planned/elapsed` feed the donut) |
| `/api/proximity/shift-config` | GET | shift slots `{en,ar,nSlots,slots:[{h,m}],stops:[…],emails}` (used for the shift chip) |
| `/api/proximity-chart-data?date&mode=day[&from]` | GET | merged per-channel series `{timestamps, stride, prox_ch<i>_in, …}` for the given calendar date (uniform standard: persisted file history — SD or identical-path LittleFS mirror at the configured flush interval, default 30m — PLUS the volatile 1-min PSRAM ring tail, always minute-fresh, one stride budget over the union); the dashboard fetches PREV (`from` = boundary hour) + ALIGNED to cover one production day |
| `/api/proximity-chart-data?…&stops=1` | GET | **OEE timeline squares** — `{day_start, bin_sec:600, bins:144, stops:{"<ch>":[144 × seconds | -1 = not watched]}}`: seconds WITHOUT production per 10-min block of the production day, measured every minute in RAM (`_proxStopSec10`, cleared on day rollover, `-1` = before boot / no sensor). Same route, compact side-channel: the `.chart` series follows the configured cadence (10m/30m/60m) and a whole 10-min stop fits between two records, which is why the strip could not resolve it |
| `/api/config?section=system` | GET | system name (`system_name`) for the default chart title |

All three registered via `registerApiRoute` in `web_routes_proximity.cpp` (see also
`handlers_unified/proximity_handler.cpp`); full details in `PAGES/proximity_sensors.md` §7.

---

## 7. Persistence

**None written** — the dashboard reads live in-memory counters + per-day chart data stored on SD by
the proximity engine (same CSVs documented in `proximity_sensors.md` §8-9). No config is
written by this page. Without SD, history endpoints read the LittleFS mirrors instead:
`/api/proximity/shift-history` scans `/oee/PROXIMITY_<DD-MM-YYYY>.csv`, and the hourly
breakdown reader falls back to the same mirror (`_proxReadHourlyCounts` SD → Flash).

---

## 8. Runtime engine

Reads the same `proximity_manager.cpp` counters (ppm, count_in/out, totals, goal) exposed
through `/api/proximity`, and the per-day per-channel chart series written by the proximity
chart capture (CSV under the day folder) exposed through `/api/proximity-chart-data`.

Day-so-far OEE: `proximityTick()` runs `_proxDayTick()` (same 30 s rate limit as the
shift tick) keyed by the shift-aligned day (`proxShiftAlignedDate()` — the day rolls
at the last shift start hour, e.g. 22:00 for 6/14/22 shifts; first detection after boot
starts the day window at boot). Resets never wipe the day: counter resets (manual or
shift auto-reset) carry pre-reset production into day accumulators, so `day_*` always
spans `[boundary, now]`; only OEE-reset restarts the day window.
`/api/proximity/oee` returns `day_oee/day_a/day_p/day_q` computed with
`proximityComputeOee()` over `[day boundary, now]` (elapsed = seconds since the day
boundary, planned stops overlapping the same window).

Historical backfill follows the shift layout: the dashboard resolves the production-day
window client-side (`prodDayBoundary()` from the shift-config payload) and merges the
previous-date tail (from the boundary hour) with the aligned-date head into the 24
production-day columns; the firmware OEE side (`day_*` in `/api/proximity/oee`) already
uses the same `[boundary, now]` window, so chart, DAY card, Day OEE and donut agree.

Chart streaming (`chartHistoryStreamFloat32JsonImpl`) is single-pass: records are read in
~4 KB SD blocks and downsampled online by `ChartStreamDecimator` (`chart_stream_decimate.h`)
into a bounded PSRAM buffer (≤ `maxPoints` samples), then emitted as JSON. This replaced the
previous two-pass algorithm (read the whole file to count records, then read it again for the
strided samples), which doubled the SD I/O and spent several seconds in per-record
`vTaskDelay(1)` sleeps on large files — the cause of HTTP 000 timeouts on this endpoint.

---

## 9. i18n keys

`proxDashNoActive` ("No active channels"), `proxDashDay` ("day"),
`proxDashPcs` ("parts"), `proxDashLastHour` ("last hour"),
`proxDashConnected` ("connected"), `proxDashDisconnected` ("disconnected"),
`proxDashTitle` ("Charts and OEE"), `proxDashSubtitle` ("Real-time monitoring"),
`proxDashOpts` ("Display options"), `proxDashSecView/SecData/SecScale`,
`proxDashChartType` ("Chart type"), `proxDashChartBars` ("Bars"),
`proxDashChartLine` ("Line"), `proxDashTipChart` ("Chart style"),
`proxDashTitleMode` ("Title"), `proxDashTitleSys` ("System name"),
`proxDashTitleName` ("Channel name"), `proxDashContent` ("Content"),
`proxDashBarsDual/BarsProd`, `proxDashColors`, `proxDashColorsTarget/ColorsSolid`,
`proxDashLabels`, `proxDashOn/Off`, `proxDashTargetLine`, `proxDashTargetPct`
("Target % of PPM"/"Objetivo % do PPM"), `proxDashTipTargetPct`, `proxDashLegendProd`
("Production"/"Produção"), `proxDashLegendGoodHi` ("Good ≥ target"/"Boas ≥ objetivo"),
`proxDashLegendGoodLo` ("Good < target"/"Boas < objetivo"), `proxDashLegendScrap`
("Scrap"/"Refugo"), `proxDashHourMark`,
`proxDashWindow`, `proxDashWin24/Win12/Win8`, `proxDashYMode`,
`proxDashYTarget/YFit/YWide`, `proxDashTicks`, `proxDashTicksAuto/TicksAll/TicksSparse`,
`proxDashOee` ("OEE"), `proxDashOeeDay` ("Day OEE"/"OEE dia"), `proxDashOeeShift`
("Shift OEE"/"OEE turno"), `proxDashA` ("Availability"), `proxDashP` ("Performance"),
`proxDashQ` ("Quality"), `proxDashShift` ("Shift"/"Turno", shift box label),
`proxDashDonutProd` ("Production"/"Produção"), `proxDashDonutPlan` ("Planned"/"Planeadas"),
`proxDashDonutUnplan` ("Unplanned"/"Não planeadas", donut legend),
`proxDashTipOee` ("Overall Equipment Effectiveness"), `proxDashTipA` ("Availability"),
`proxDashTipP` ("Performance"), `proxDashTipQ` ("Quality"),
`proxShiftLabel` ("Shift "/"Turno ", shift chip),
`proxDashTipBarMode/TipColors/TipLabels/TipTargetLine/TipHourMark/TipWindow/TipYMode/TipTicks`.
No remaining hardcoded user-visible strings.

---

## 10. Review findings & improvement ideas

| # | Finding | Severity | Suggested fix |
|---|---|---|---|
| 1 | **`loadHistoricalData` bucketed only the *current calendar day*** and skipped rows whose `dt.getDate() !== curDay`. Fixed 2026-09-22: backfill now merges the previous-date file (from the boundary hour) with the aligned-date file into production-day columns, so e.g. the 23H column fills from yesterday's file for 6/14/22 shifts. | Low | Fixed in `page_proximity_dashboard_asset.h` (`loadHistoricalData` + `prodDay*` helpers). |
| 2 | Hourly buckets are **derived from deltas of `count_in`** only (never `count_out`) — Count OUT pulses are not charted. | Low | Add a `prox_ch<i>_out` series + toggle. |
| 3 | ETA uses **`count_in` vs `goal` and `ppm`** with no tie to speed/out counters — approximate by design; document it. | Info | Label as "approx". |
| 4 | No way to pause polling or select which channels display (auto-active only) — could add a filter on a big screen. | Info | Optional controls. |
| 5 | **Shift-end email showed 22H=0** when a periodic aligned tick fired at the same hour as the flush tick (double-tick at boundary). The boundary delta was attributed to the penultimate hour and the post-loop fix couldn't recover it (delta=0). Fixed 2026-08-25: `proximityBuildHourlyChart` now tracks the boundary delta and re-credits it when the post-loop delta is 0. | High | Fixed in `proximity_manager.cpp:1168-1248`. |
| 6 | **`/api/proximity-chart-data` could time out (HTTP 000)** on slow SD: `chartHistoryStreamFloat32JsonImpl` scanned the .chart file one record per `file.read()` syscall and read it twice (count + strided store). Fixed (shared with RS232/MODBUS charts): the reader now pulls whole fixed-size records in **block-buffered reads** (~4 KB per SD read), cutting SD read syscalls by ~40× while byte-for-byte preserving the strided output (verified by `tests/native/test_chart_history_stream.cpp`). | Medium | Fixed in `chatgpt/src/chart_history.cpp:599-701`. |
| 7 | **Micro-paragens invisíveis na timeline (pedido Nuno 2026-10-14):** a faixa T22 pintava 1 cor sólida por hora, por isso uma paragem de 10 min dentro de uma hora produtiva escondia-se atrás do verde. **Implementado (T23):** cada segmento horário divide-se em blocos de 10 min (`markStopSec()` puro — amostras `prox_ch<i>_in` do histórico + cauda live dos polls; `stopBinIsFuture()`; fallback à cor sólida da hora quando a célula não tem amostras). Paragem = vermelho em turno, âmbar em paragem planeada; futuro = cinzento; legenda com "(bloco = 10 min)". **Revisão 2026-10-15 (pedido Nuno — "20 minutos são dois blocos"):** as células passaram a 5 min (12/hora) contando os SEGUNDOS reais sem produção, pintadas a partir de metade da célula (150 s). **Revisão 2026-10-21 (pedido Nuno — "só coisas mínimo 10 minutos, máximo 6 quadrados por hora"):** voltou a **10 min por quadrado (6/hora)** e o limiar passou a ser o quadrado inteiro: `stopSquaresFromBins()` agrupa quadrados com ≥ 300 s, **descarta corridas < 10 min** (`STOP_CELL_SHOW_SEC`) e pinta `round(corrida/10 min)` quadrados a partir do início da corrida (10 min = 1, 15 = 2, 20 = 2, 45 = 5) — nunca há meios quadrados nem paragens abaixo de 10 min; um separador por quadrado. Buraco de dados > 20 min não credita tudo (evita dia todo vermelho após reboot). Sem mudança de firmware — só JS do asset + i18n. | Feature | `page_proximity_dashboard_asset.h` (`markStopSec`, `stopBinIsFuture`, `drawTimeline`, `timelineLegendHTML`), `lang_pt_ui.h`/`lang_en_ui.h` (`proxDashTlBin`), `test_proximity_dashboard.js` (testes T23 + casos de 10-min/limiar/6 quadrados). |
| 8 | **Letras pequenas (pedido Nuno):** valores nas barras 10/11px e legenda da faixa `.pd-tl` 9px. **Aumentado:** valores 12/13px, legenda 12px com swatches 11px. **Depois:** faixa descida (gap 18→30px dos labels das horas, `padB` 56→68, faixa 10→12px) e legenda em caixa com fundo/borda iguais às caixas OEE do turno (`.pd-shift-box`). | Feature | `page_proximity_dashboard_asset.h` (CSS `.pd-tl`, font dos value labels + geometria `drawTimeline` em `drawChart24`). |
| 9 | **Paragens ≥ 10 min não apareciam na faixa (dono, .29, 2026-10-24):** a faixa mostrava o dia todo verde. **Causa provada:** o `.chart` persistido segue o intervalo configurado (default **30 min**), por isso entre dois registos cabe a paragem inteira — o heurístico `markStopSec()` só vê o que está entre amostras, e a única zona com 1 min de detalhe é a cauda do anel PSRAM (~30 min). Simulação com a forma real do payload (30 min + anel): paragem de 35 min → 0 quadrados; com registos de 1/10 min → 3 quadrados. **Correção:** o firmware passa a medir os quadrados de 10 min em RAM ao minuto (`_proxStopSec10`, 144 blocos × canais, limpos na viragem do dia, `-1` = não vigiado) e serve-os em `?stops=1`; a página mapeia-os com `stopCellsFromBins()` para a grelha 24×6 e passa a preferi-los (`hd.srvBins \|\| hd.stopBins`). Sem ficheiro novo, sem escrita em flash (só RAM), payload ~80 B por canal. | High | Fixed: `data_sd_pipeline_impl.h` (`_proxStopBinsAdd`, `proxChartStopBinsJson`), `proximity_manager.h` (`proxStopBinFromClock`), `proximity_handler.cpp` + `rs232_history_handler.cpp` (`stops=1`), `page_proximity_dashboard_asset.h` (`stopCellsFromBins`, `loadStopBins`), `test_proximity_day_aligned.cpp` + `test_proximity_dashboard.js` (T24). |

---

## 11. Source map

| Concern | File |
|---|---|
| Renderer | `chatgpt/src/webpages/renderers/proximity_dashboard_renderer.{h,cpp}` |
| Page HTML/CSS/JS (PROGMEM) | `chatgpt/src/webpages/assets/page_proximity_dashboard_asset.h` |
| Routes (WiFi + Ethernet) | `web_routes_proximity.cpp:195`, `ethernet_bootstrap_routes.h:238` |
| Live/config/chart data | `handlers_unified/proximity_handler.cpp`, `proximity_manager.cpp` |
| Chart/stop data (files + RAM) | `data_sd_pipeline_impl.h` (ring + `.chart` writer, `_proxStopBinsAdd`/`proxChartStopBinsJson`), `chart_ring_serve.h`, legacy WiFi route `handlers/rs232_history_handler.cpp` |
| Home entry | `home_page_renderer.h:168` |
| Self-test manifest | `test_manifest_handler.cpp:256` (`PAGE("/proximity-dashboard", ENABLE_PROXIMITY)`) |

Related: `PAGES/proximity_sensors.md` (config page `/proximity`).