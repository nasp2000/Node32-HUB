#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>
#include "../feature_flags.h"

// Maximum channels supported at compile time (ISR table size).
#define PROXIMITY_MAX_CHANNELS  8

// Ring buffer depth for recent events (anomaly detection feed).
#define PROX_RECENT_EVENTS   10
// Moving-average window for anomaly detector (number of rate samples).
#define PROX_ANOMALY_WINDOW  10

// ---------------------------------------------------------------------------
// Counter recovery layers (no frequent LittleFS writes):
//   1. RTC RAM mirror — refreshed every inject tick; survives esp_restart/OTA/
//      WDT/deep sleep with ~0 loss and zero flash wear (RTC_NOINIT_ATTR).
//   2. SD CSV tail — the daily PROXIMITY_<date>.csv absolute snapshots written
//      at the configured flush interval (30m/20m/...), restore point for a real
//      power cut (RTC SRAM loses power). Boot rows carry the reset reason.
//   3. LittleFS — event-driven only (shift-end, resets, day rollover, API) plus
//      one sparse 6 h edge snapshot; no periodic 5 min writes.
// ---------------------------------------------------------------------------

// Magic guards the RTC snapshot against garbage on a power-on (RTC_NOINIT).
#define PROX_RTC_MAGIC      0x50524958UL   // "PRIX"
#define PROX_RTC_VERSION    1U

// One channel's persistent counters (same fields as the LittleFS counter JSON).
struct ProxRtcChannel {
    uint32_t in;
    uint32_t out;
    uint32_t total_in;
    uint32_t total_out;
    uint32_t run;
    uint32_t down;
    uint32_t good;
    uint32_t setup;
};

struct ProxRtcSnapshot {
    uint32_t      magic;     // PROX_RTC_MAGIC (written LAST, after the data)
    uint32_t      version;   // PROX_RTC_VERSION
    uint16_t      channels;  // PROXIMITY_CHANNELS at mirror time
    uint16_t      reserved;
    uint32_t      dayKey;    // shift-aligned day key (cold-stale guard when clock synced)
    uint32_t      updBootMs; // millis() at last mirror (diagnostic)
    ProxRtcChannel ch[PROXIMITY_MAX_CHANNELS];
};

// True for CSV row types that hold ABSOLUTE counter snapshots (usable as a
// reboot restore point). shift_end rows hold per-shift DELTAS and boot rows the
// post-restore snapshot — neither is a valid baseline.
static inline bool proxCsvRowIsAbsolute(const char* type) {
    return type != nullptr &&
           (strcmp(type, "tick") == 0 || strcmp(type, "shift_boundary") == 0);
}

// Parse one mutable CSV data row into per-channel counters. Layout:
// timestamp,type,shift,ch<i>_in,ch<i>_out,ch<i>_ppm,ch<i>_run,ch<i>_down,
// ch<i>_good — 3 header tokens + 6 per channel. The exact channel count is
// derived from the token count, so files written with fewer channels (older
// builds) still parse; returns false for header/boot/shift_end rows and any
// malformed line. Mutates line (strtok) and is pure (native-testable).
static inline bool proxCsvParseAbsRow(char* line, uint32_t chCount,
                                      uint32_t* outsIn, uint32_t* outsOut,
                                      uint32_t* outsRun, uint32_t* outsDown,
                                      uint32_t* outsGood) {
    if (!line) return false;
    uint32_t tokens = 1;
    for (const char* p = line; *p; p++) tokens += (*p == ',');
    if (tokens < 3) return false;
    uint32_t nch = (tokens - 3) / 6;
    if (nch == 0 || (tokens - 3) % 6 != 0) return false;
    if (nch > chCount) nch = chCount;
    if (nch > PROXIMITY_MAX_CHANNELS) nch = PROXIMITY_MAX_CHANNELS;
    char* t0 = strtok(line, ",");           // timestamp
    char* t1 = strtok(nullptr, ",");        // type
    if (!t0 || !t1 || !proxCsvRowIsAbsolute(t1)) return false;
    if (!strtok(nullptr, ",")) return false; // shift (unused)
    for (uint32_t i = 0; i < nch; i++) {
        char* tin   = strtok(nullptr, ",");
        char* tout  = strtok(nullptr, ",");
        char* tppm  = strtok(nullptr, ",");
        char* trun  = strtok(nullptr, ",");
        char* tdown = strtok(nullptr, ",");
        char* tgood = strtok(nullptr, ",");
        if (!tin || !tout || !tppm || !trun || !tdown || !tgood) return false;
        outsIn[i]   = (uint32_t)strtoul(tin,  nullptr, 10);
        outsOut[i]  = (uint32_t)strtoul(tout, nullptr, 10);
        outsRun[i]  = (uint32_t)strtoul(trun, nullptr, 10);
        outsDown[i] = (uint32_t)strtoul(tdown,nullptr, 10);
        outsGood[i] = (uint32_t)strtoul(tgood,nullptr, 10);
    }
    return true;
}

// Channel operating modes.
#define PROX_MODE_COUNTER   0   // Count rising edges; supports IN/OUT counting and goal tracking.
#define PROX_MODE_PRESENCE  1   // Report ON/OFF state only; no counting.
#define PROX_MODE_SPEED     2   // Compute rate (ppm) and cycle time from pulse period.

// One daily shift time slot.
struct ProximityShiftSlot {
    uint8_t hour;   // Shift start hour (0-23).
    uint8_t minute; // Shift start minute (0-59).
};

// One daily planned-stop window (recurring every day). Stored as wall-clock
// minutes; end <= start wraps past midnight (e.g. 23:00-01:00). end == start
// is zero-length and ignored everywhere (never persisted by validated paths).
struct ProximityPlannedStop {
    uint8_t sh;   // Window start hour (0-23).
    uint8_t sm;   // Window start minute (0-59).
    uint8_t eh;   // Window end hour (0-23).
    uint8_t em;   // Window end minute (0-59).
};
#define PROX_MAX_PLANNED_STOPS 6

// Shift management configuration (persisted inside /config/sensor_proximity.json, section "shift").
struct ProximityShiftConfig {
    bool               enabled;               // Enable shift tracking and end-of-shift actions.
    bool               auto_reset_on_shift;   // Reset all channel counters on every shift transition.
    uint8_t            nSlots;                // Number of shift slots (1-3).
    ProximityShiftSlot slots[3];              // Shift start times (sorted ascending by caller).
    char               shiftday_emails[512];  // Extra recipients for SHIFTDAY command (comma-separated).
};

// Standard OEE result (Nuno's "Calculo TRS" Excel reference — A/P/Q/OEE formulas).
struct ProximityOeeResult {
    float    availability;   // A % = runSec / elapsedSec (clipped to 100; elapsed = shift/day wall time)
    float    performance;    // P % = cIn / theoretical (unbounded — faster cycle pushes >100)
    float    quality;        // Q % = effectiveGood / cIn (0..100 naturally, good/total)
    float    oee;            // OEE % = A * P * Q (product of the three)
    uint32_t theoretical;    // tgtPpm parts/min * running minutes (0 when no target configured)
};

// Overlap in seconds between [t0, t1) and daily planned-stop windows. Pure
// (testable): day iteration uses mktime-normalized local midnights, so TZ/DST
// handling matches the shift-day code. Empty list, t1 <= t0 → 0. Zero-length
// windows (end == start) and out-of-range values are ignored (validated paths
// never persist them; this is the last line of defense).
static inline uint32_t proxPlannedStopOverlapSec(time_t t0, time_t t1,
                                                 const ProximityPlannedStop* stops,
                                                 uint8_t nStops) {
    if (t1 <= t0 || stops == nullptr || nStops == 0) return 0;
    struct tm day;
    localtime_r(&t0, &day);
    day.tm_hour = 0; day.tm_min = 0; day.tm_sec = 0; day.tm_isdst = -1;
    uint32_t maxDays = (uint32_t)((t1 - t0) / 86400) + 3;
    uint32_t total = 0;
    for (uint32_t d = 0; d < maxDays; d++) {
        time_t dayStart = mktime(&day);
        if (dayStart == (time_t)-1 || dayStart >= t1) break;
        for (uint8_t i = 0; i < nStops; i++) {
            int s = (int)stops[i].sh * 60 + (int)stops[i].sm;
            int e = (int)stops[i].eh * 60 + (int)stops[i].em;
            if (s < 0 || s > 1439 || e < 0 || e > 1439 || e == s) continue;
            time_t ws = dayStart + (time_t)s * 60;
            time_t we = dayStart + (time_t)e * 60;
            if (we <= ws) we += 86400;   // overnight wrap (e.g. 23:00-01:00)
            time_t lo = (t0 > ws) ? t0 : ws;
            time_t hi = (t1 < we) ? t1 : we;
            if (hi > lo) total += (uint32_t)(hi - lo);
        }
        day.tm_mday += 1;
    }
    return total;
}

// Pairwise overlap check for a candidate stop list (POST validation).
// Zero-length (end == start) entries are skipped here; the caller rejects
// them explicitly so an "unused" UI row can never sneak into storage.
static inline bool proxPlannedStopsOverlap(const ProximityPlannedStop* stops, uint8_t nStops) {
    if (stops == nullptr) return false;
    for (int m = 0; m < 1440; m++) {
        uint8_t cover = 0;
        for (uint8_t i = 0; i < nStops; i++) {
            int s = (int)stops[i].sh * 60 + (int)stops[i].sm;
            int e = (int)stops[i].eh * 60 + (int)stops[i].em;
            if (s < 0 || s > 1439 || e < 0 || e > 1439 || e == s) continue;
            bool inside = (e > s) ? (m >= s && m < e) : (m >= s || m < e);
            if (inside && ++cover > 1) return true;
        }
    }
    return false;
}

// Standard OEE computation (ISO 22400-2 decomposition, user spec 2026-09) —
// the ONLY board formula. Availability is Elapsed-based (wall-clock time since
// the last count reset, manual or automatic shift rotation) so the numbers are
// "ready to read" at ANY moment of the shift without depending on shift config;
// Performance is Running-based per the standard (cadence while producing):
//   A = runSec / PBT * 100           PBT (planned busy time) = elapsedSec minus
//                                      planned-stop overlap; empty stop list →
//                                      PBT == elapsedSec (legacy behavior).
//                                      When the machine stops at 3h, at 8h the
//                                      availability already reflects 3h/8h.
//   P = cIn / theoretical * 100       theoretical = tgtPpm * RUNNING minutes —
//                                      stops do NOT move the target, so P=100%
//                                      whenever the machine holds its cadence,
//                                      and P<100% isolates real speed losses;
//                                      NO cycle-time fallback (tgtPpm==0 → P=0)
//   Q = effectiveGood / cIn * 100     good/total; never exceeds 100 naturally
//   OEE = A * P * Q
// Guards: elapsedSec==0 (fresh reset) → A=0; PBT<=0 (whole window planned
// stop) → A=100 (nothing planned = nothing lost); runSec==0 → theoretical=0 →
// P=0 (a stopped machine has no cadence to measure); A/P clipped to 100.
static inline ProximityOeeResult proximityComputeOee(uint32_t runSec, uint32_t elapsedSec,
                                                     uint32_t cIn, uint32_t effectiveGood,
                                                     float tgtPpm, uint32_t plannedStopSec = 0) {
    ProximityOeeResult r = {};
    uint32_t pbt = (elapsedSec > plannedStopSec) ? elapsedSec - plannedStopSec : 0;
    r.availability = (pbt > 0) ? (float)runSec * 100.0f / (float)pbt
                               : ((elapsedSec > 0) ? 100.0f : 0.0f);
    r.theoretical  = (tgtPpm > 0) ? (uint32_t)(tgtPpm * ((float)runSec / 60.0f)) : 0;
    r.performance  = (r.theoretical > 0) ? (float)cIn * 100.0f / (float)r.theoretical : 0.0f;
    r.quality      = (cIn > 0) ? (float)effectiveGood * 100.0f / (float)cIn : 100.0f;
    // Clip A, P and the final OEE to 100% — utilisation and cadence above target
    // are physically impossible to exceed 100% of the measured time.
    if (r.availability > 100.0f) r.availability = 100.0f;
    if (r.performance  > 100.0f) r.performance  = 100.0f;
    r.oee          = (r.availability * r.performance * r.quality) / 10000.0f;
    if (r.oee > 100.0f) r.oee = 100.0f;
    return r;
}

// ── Legacy planned-time helpers (no longer used by proximityComputeOee since
// ── the 2026-09 Elapsed-based change; kept for tests and diagnostics only) ──
// Planned duration (seconds) of a shift slot: the span between this slot's start
// and the next slot's start (wrapping across midnight). A single-slot config is
// a 24h shift (86400 s). Returns 0 when shifts are disabled or no slots are
// configured.
static inline uint32_t proximityShiftSlotDurationSec(const ProximityShiftConfig& sc,
                                                     uint8_t shiftIdx) {
    if (!sc.enabled || sc.nSlots == 0) return 0;
    if (shiftIdx >= sc.nSlots) shiftIdx = 0;
    if (sc.nSlots == 1) return 86400UL;
    int startMin = (int)sc.slots[shiftIdx].hour * 60 + sc.slots[shiftIdx].minute;
    int nextIdx  = (shiftIdx + 1) % (int)sc.nSlots;
    int endMin   = (int)sc.slots[nextIdx].hour * 60 + sc.slots[nextIdx].minute;
    if (nextIdx == 0 && endMin <= startMin) endMin += 1440;
    int durMin = endMin - startMin;
    if (durMin < 1) durMin = 1;
    return (uint32_t)durMin * 60UL;
}

// OEE planned seconds with fallback: when shifts are disabled or shiftIdx is
// invalid, falls back to 86400 (full day). LEGACY — Elapsed-based OEE no longer
// reads this; a disabled shift no longer affects A or OEE (1h of work vs 1h of
// elapsed time is 100% regardless of shift config).
static inline uint32_t proximityOeePlannedSec(const ProximityShiftConfig& sc,
                                               int8_t shiftIdx) {
    if (shiftIdx >= 0) {
        uint32_t ps = proximityShiftSlotDurationSec(sc, (uint8_t)shiftIdx);
        if (ps > 0) return ps;
    }
    return 86400UL;
}

// Drain math shared by the IN/OUT paths: whole parts for this drain tick,
// carrying the remainder into *rem so slow pulse rates never lose counts
// (e.g. divider=3 with 1 pulse/tick still advances every 3rd tick).
// Pure helper (native-tested). mult/div < 1 behave as 1 (legacy behavior).
//
// MULTIPLICATIVE MODEL (Nuno spec 2026-09-24): the multiplier ALWAYS applies —
// divider sets how many (multiplied) counts make one part. parts = pulses*ml/div,
// so mult=2,div=5 with 5 counts = 2 parts; mult=10,div=1 with 1 count = 10 parts.
// (Characteristic examples: ml=1,div=5,5 counts->1 part; ml=2,div=5,5 counts->2.)
static inline uint32_t proxApplyDivider(uint32_t delta, int32_t mult, int32_t div, uint32_t* rem) {
    if (mult < 1) mult = 1;
    if (div < 1) div = 1;
    uint64_t total = (uint64_t)delta * (uint64_t)mult + (uint64_t)(rem ? *rem : 0);
    uint32_t parts = (uint32_t)(total / (uint64_t)div);
    if (rem) *rem = (uint32_t)(total % (uint64_t)div);
    return parts;
}

// Slow-machine grace for the running/downtime decision (standard APT,
// ISO 22400-2): a second with no edges still counts as RUNNING while the gap
// since the last accepted edge is within GRACE_MULT x the ideal EDGE cycle.
// The ideal edge cycle accounts for multiplier AND divider (N pulses = 1 part):
// 60000 * multiplier / (target_ppm * divider) ms. Only a gap beyond grace
// declares a stop — so a slow but cycling machine accrues running time (its
// slowness shows in P, per the standard) instead of collapsing A. Capped at
// GRACE_MAX_MS against absurd targets. Fast lines (edges every second) never
// touch this path.
#define PROX_OEE_RUN_GRACE_MULT   2
#define PROX_OEE_RUN_GRACE_MAX_MS 600000UL   // 10 min

// Grace window (ms) for the running/downtime decision, from the configured
// target cadence, count multiplier and divider. Pure helper (native-tested).
// divider defaults to 1 so legacy 2-arg callers keep exact behavior.
// MULTIPLICATIVE MODEL (Nuno spec 2026-09-24): the multiplier always applies —
// ideal EDGE cycle = 60000*ml/(target_ppm*div) ms — so the grace always matches
// what the counting does: parts = pulses*ml/div (see proxApplyDivider).
static inline uint32_t proximityOeeGraceMs(float target_ppm, int32_t multiplier, int32_t divider = 1) {
    if (!(target_ppm > 0.0f)) return 0;
    if (divider < 1) divider = 1;
    if (multiplier < 1) return 0;
    float g = (60000.0f * (float)multiplier / (target_ppm * (float)divider)) * (float)PROX_OEE_RUN_GRACE_MULT;
    if (g > (float)PROX_OEE_RUN_GRACE_MAX_MS) g = (float)PROX_OEE_RUN_GRACE_MAX_MS;
    return (uint32_t)g;
}

// Consume whole seconds from the OEE sub-second accumulator, classifying each
// as running (>=1 edge accumulated in the window during that second, OR inside
// the slow-machine grace window) or downtime (no edges AND beyond grace),
// unless the gate is closed (no classification).
//
// IMPORTANT (issues #3/#7 — OEE): the edge window is cleared ONLY when a
// second was actually consumed. Clearing it on every tick wipes the sub-second
// accumulation (the caller's loop runs at tens of ms, far faster than 1s), so
// when the 1000ms boundary finally fires the window only holds the last tick's
// delta (usually 0) — nearly every second misreads as downtime and
// availability/OEE collapse to ~0 even while the machine is producing.
static inline void proximityOeeWindowConsume(uint32_t& msAccum, uint32_t& edgeWindow,
                                             bool gateOpen, uint32_t& runningSec,
                                             uint32_t& downtimeSec,
                                             uint32_t lastEdgeMs, uint32_t nowMs,
                                             uint32_t graceMs) {
    uint32_t windowCount = edgeWindow;
    bool consumedSecond = false;
    // lastEdgeMs==0 (never pulsed since reset/boot) forces downtime — without
    // this guard the fresh-boot clock (nowMs small) would sit inside grace.
    bool withinGrace = (graceMs > 0) && (lastEdgeMs != 0) &&
                       ((nowMs - lastEdgeMs) <= graceMs);
    while (msAccum >= 1000) {
        msAccum -= 1000;
        consumedSecond = true;
        if (gateOpen && (windowCount > 0 || withinGrace)) {
            runningSec++;
        } else if (gateOpen) {
            downtimeSec++;
        }
    }
    if (consumedSecond) {
        edgeWindow = 0;
    }
}

// Normalize shift slots to ascending order by start minute (hour*60+minute) in place.
// ALL consumers (_detectCurrentShift, proximityShiftSlotDurationSec,
// proximityGetShiftWindow) assume the slots array is sorted ascending — the struct
// documents "sorted ascending by caller" but web/JSON writers never enforced it.
// With an unsorted night config {22,6} the boundary lands at 00:00 instead of 06:00
// (wrong/absent shift-end email). Sort here so loading and saving always heal the order.
static inline void proximitySortShiftSlots(ProximityShiftConfig& sc) {
    if (sc.nSlots < 2) return;
    for (uint8_t i = 0; i < sc.nSlots - 1; i++) {
        int a = sc.slots[i].hour * 60 + sc.slots[i].minute;
        for (uint8_t j = i + 1; j < sc.nSlots; j++) {
            int b = sc.slots[j].hour * 60 + sc.slots[j].minute;
            if (b < a) {
                ProximityShiftSlot t = sc.slots[i];
                sc.slots[i] = sc.slots[j];
                sc.slots[j] = t;
                a = b;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Weekly OEE CSV pure helpers (additive, independent of SD/state; tested natively).
// ---------------------------------------------------------------------------

// ISO 8601 week number (01-53) and ISO week-year (%G) for a given local time.
// Week belongs to the year containing the week's Thursday. Uses newlib strftime
// (%V / %G) available on the ESP32 firmware and the host libc in native tests.
static inline int proximityIsoWeek(const struct tm& ti, int& isoYear) {
    struct tm copy = ti;
    char wbuf[8] = {0}, ybuf[8] = {0};
    strftime(wbuf, sizeof(wbuf), "%V", &copy);
    strftime(ybuf, sizeof(ybuf), "%G", &copy);
    int w = atoi(wbuf);
    if (w < 1) w = 1;
    if (w > 53) w = 53;
    isoYear = atoi(ybuf) > 0 ? atoi(ybuf) : (ti.tm_year + 1900);
    return w;
}

// Builds the weekly OEE CSV file name for an ISO week: "wk<NN>-oee.csv" (NN zero-padded).
static inline void proximityWeeklyOeeFileName(int week, char* out, size_t cap) {
    snprintf(out, cap, "wk%02d-oee.csv", week);
}

// Formats one weekly OEE CSV data row.
// Fixed left-side columns (date..oee) keep reading compat with older files;
// the summary columns (good, run_sec, plan_sec, unplanned_sec, elapsed_sec,
// ppm, weekday) are appended at the right and repeat the shift-level values
// (weekday is per-hour and can change at midnight). `hour` is the END hour
// (the hour the count is taken), same convention as the shift email chart
// and the web dashboard: shift 6H-14H writes hours 7..14.
static inline int proximityFormatOeeRow(char* out, size_t cap, const struct tm& ti,
                                        int shift, int hour, uint32_t countHour,
                                        uint32_t total, uint32_t scrap, uint32_t setup,
                                        float avail, float perf, float qual,
                                        float oee,
                                        uint32_t good, uint32_t runSec, uint32_t planSec,
                                        uint32_t unplannedSec, uint32_t elapsedSec,
                                        float ppm) {
    return snprintf(out, cap, "%02d-%02d-%04d,%d,%d,%lu,%lu,%lu,%lu,%.2f,%.2f,%.2f,%.2f,%lu,%lu,%lu,%lu,%lu,%.2f,%d",
                    ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                    shift, hour,
                    (unsigned long)countHour, (unsigned long)total,
                    (unsigned long)scrap, (unsigned long)setup,
                    (double)avail, (double)perf, (double)qual, (double)oee,
                    (unsigned long)good, (unsigned long)runSec,
                    (unsigned long)planSec, (unsigned long)unplannedSec,
                    (unsigned long)elapsedSec, (double)ppm,
                    (ti.tm_wday < 0 ? 0 : ti.tm_wday));
}

// Per-channel runtime state.
// WARNING: members accessed from ISR — this struct must remain in DRAM, never PSRAM.
struct ProximityChannel {
    char        name[32];       // Human-readable label, editable at runtime via web config.
    uint8_t     gpio_in;        // GPIO connected to optocoupler output; 255 = disabled.
    uint8_t     gpio_out;       // GPIO for optional timed DO response; 255 = none.
    uint8_t     gpio_in2;       // Second input GPIO for count_out (e.g. exit sensor); 255 = disabled.
    uint8_t     mode;           // PROX_MODE_* constant.
    bool        monitor_only;   // When true: GPIO OUT pulse is never fired; channel still counts and measures rate.
    int32_t     multiplier;     // Count multiplier: 1 pulse = multiplier parts (1..9999; manual entry allowed).
    int32_t     divider;        // Count divider: divider pulses = 1 part (1..9999; e.g. 2-3 moves per part).
    volatile uint32_t debounce_ms;  // Minimum ms between accepted edges (software debounce). Read from ISR.
    uint32_t    count_in;       // Accepted edge count (COUNTER / SPEED modes).
    uint32_t    count_out;      // Secondary counter (e.g. OUT sensor on the same line).
    uint32_t    total_in;       // Non-resettable total input counter (factory reset only).
    uint32_t    total_out;      // Non-resettable total output counter.
    uint32_t    goal;           // Shift target count (0 = disabled).
    uint32_t    last_pulse_ms;  // millis() of the last accepted edge.
    uint32_t    cycle_time_ms;  // Duration between the last two accepted edges (ms).
    float       rate_ppm;       // Pulses per minute, updated every 5 s.
    bool        state;          // Current digital state (PRESENCE mode).
    uint32_t    pulse_delay_ms; // DO: delay from edge to output activation (ms).
    uint32_t    pulse_dur_ms;   // DO: active duration of the output pulse (ms).
    bool        do_active;      // True while a DO pulse is in progress.
    uint32_t    do_start_ms;    // millis() when the DO pulse activation was scheduled.

    uint8_t     direction;      // 0=unknown, 1=IN, 2=OUT (direction detection via gpio_in/gpio_in2 timing).
    uint32_t    dir_edge_ms_in; // Timestamp of last gpio_in edge for direction detection.

    // OEE accumulators (standard "Calculo TRS" A/P/Q/OEE)
    uint32_t    running_seconds;    // Seconds with active production (edges seen in window).
    uint32_t    downtime_seconds;   // Seconds stopped (no edges seen in window).
    uint32_t    oee_count_window;   // Edge count accumulated since last 1s OEE sample.
    uint32_t    good_parts;         // Good parts = count_in - count_out (since last OEE reset).
    float       target_ppm;         // Ideal cadence (parts/minute) — configurable.
    float       oee_manual;         // Retained for config-compat; no longer overrides OEE (standard formula always used).

    // Flexible channel roles: gate (presence) and scrap
    uint8_t     gate_ch;    // 0-7: PRESENCE channel that gates OEE for this channel. 255 = no gate.
    uint8_t     scrap_ch;   // 0-7: COUNTER channel whose count_in is subtracted for Quality OEE. 255 = no scrap.
    uint32_t    setup_count; // Parts counted while the gate channel is OFF (gate_ch configured). Setup pieces.
};

#if ENABLE_PROXIMITY==1

// Configure GPIOs and attach ISRs for all active channels.
// Must be called once from setup() after GPIO and ConfigManager are ready.
void proximityBegin();

// Drive DO timers, update rate estimates, refresh PRESENCE state.
// Call every iteration of loop() — lightweight, no blocking operations.
void proximityTick();

// Reset counters and rate for a single channel (0-based index).
// Safe to call from a web handler or serial command.
void proximityResetChannel(uint8_t ch);

// Reset all channels (e.g. on shift start).
void proximityResetAll();

// Reset total_in / total_out for a single channel (admin only).
void proximityResetTotal(uint8_t ch);

// Reset OEE accumulators for a single channel.
void proximityResetOee(uint8_t ch);

// Return a snapshot copy of one channel's state (copy taken under interrupt lock).
ProximityChannel proximityGetChannel(uint8_t ch);

// Seconds elapsed since the current count window started (reset with the channel).
// Unsynchronised with shift baselines; reads the live wall-clock delta.
uint32_t proximityCountElapsedSec(uint8_t ch);

// Return the number of active channels (equals the PROXIMITY_CHANNELS macro).
uint8_t proximityChannelCount();

// Board default GPIO for a channel (DI1=GPIO4 ... DI8=GPIO11 on 8DI8DO).
// Lets the web config pre-fill the GPIO field so the user only wires the terminal.
uint8_t proximityDefaultGpioIn(uint8_t ch);

// Update one channel's configuration at runtime.
// GPIO changes take effect after restart; all other fields apply immediately.
void proximityConfigureChannel(uint8_t ch, const char* name, uint8_t mode,
                                uint8_t gpio_in, uint8_t gpio_out, uint8_t gpio_in2,
                                uint32_t debounce_ms, uint32_t goal,
                                uint32_t pulse_delay_ms, uint32_t pulse_dur_ms,
                                bool monitor_only, int32_t multiplier = 1,
                                float target_ppm = 0.0f, float oee_manual = 0.0f,
                                uint8_t gate_ch = 255, uint8_t scrap_ch = 255,
                                int32_t divider = 1);

// Persist all channel configs to LittleFS (/config/sensor_proximity.json).
// Returns true if the file was written successfully.
bool saveProximityConfig();

// Load channel configs from NVS and apply to the channel array.
// Called automatically by proximityBegin() on boot.
void loadProximityConfig();

// Persist live count_in / count_out values to NVS so they survive reboot.
// Called periodically (on CSV write) and after manual reset.
void saveProximityCounters();

// Restore count_in / count_out from NVS. Called automatically by proximityBegin().
void loadProximityCounters();

// --- Shift management (Phase 4) ---

// Apply a new shift configuration; resets shift detection so next tick re-baselines.
void proximitySetShiftConfig(const ProximityShiftConfig& cfg);

// Return a copy of the current shift configuration.
ProximityShiftConfig proximityGetShiftConfig();

// --- Planned stops (ISO 22400-2 planned busy time) ---
// Daily recurring stop windows persisted inside /config/sensor_proximity.json,
// section "stops". Empty list = no exclusion (legacy elapsed-based A).

// Copy the in-memory stop list into `out` (up to `maxOut` entries). Returns count.
uint8_t proximityGetPlannedStops(ProximityPlannedStop* out, uint8_t maxOut);

// Replace the in-memory stop list after validating it (ranges, no zero-length,
// no pairwise overlap). Returns false and applies NOTHING when invalid.
bool proximitySetPlannedStops(const ProximityPlannedStop* stops, uint8_t nStops);

// Overlap in seconds between [resetWall, nowWall) and the in-memory stops.
// Guards (unsynced clock, empty window) return 0 → legacy behavior.
uint32_t proximityPlannedStopSec(time_t resetWall, time_t nowWall);

// Overlap for one channel's current elapsed window: resetWall derives from
// proximityCountElapsedSec(ch). Single call site helper for the 5 OEE callers.
uint32_t proximityChannelPlannedStopSec(uint8_t ch);

// Day-boundary hour (0-23) that the daily SD file/folder should roll over at,
// derived from the shift config so shift reports are not cut near midnight.
// Returns 0 when a shift starts at 00:00, or when shifts are disabled/empty.
// Otherwise returns the latest (last) shift start hour of the day.
uint8_t proximityShiftDayBoundaryHour();

// The calendar date (day/month/year) a given time belongs to once the
// day-boundary offset is applied. Fills the passed tm with the aligned date.
// Returns the boundary hour used (0 = no shift, calendar date unchanged).
uint8_t proximityShiftDayAlignedDate(time_t now, struct tm* out);

// Pure day-alignment arithmetic shared by the CSV writer, the SHIFTDAY reads and
// the auto shift-end email, so they all open the SAME shift-aligned daily folder.
// Breaks down `now` in local time and fills it into `out`, applying
// `boundaryHour` (0 = midnight rollover, calendar date unchanged). The day runs
// from boundary hour on calendar day D-1 to the SAME boundary hour on day D and
// is labeled with day D — the date the day ENDS into, where the overnight first
// turn (e.g. 22h→6h for 6/14/22 shifts) belongs. Times at/after today's boundary
// already belong to the NEXT day's file (step forward); the CSV for day D is
// therefore created at the boundary of day D-1. Returns the boundary hour used.
static inline uint8_t proxShiftAlignedDate(time_t now, uint8_t boundaryHour, struct tm* out) {
    localtime_r(&now, out);
    if (boundaryHour == 0) return 0;   // midnight rollover — calendar date unchanged
    if ((uint8_t)out->tm_hour >= boundaryHour) {
        now += (time_t)(24 - boundaryHour) * 3600;   // step into the NEXT day (boundary passed)
        localtime_r(&now, out);
    }
    return boundaryHour;
}

// Wall-clock START (epoch) of the production day labeled with the calendar
// date whose local midnight is `civilMidnightTs`. Label D covers
// [D-1 b:00, D b:00), so the start is civil midnight pulled back (24 - b)h.
// Boundary 0 keeps the calendar day. Pure — shared by the .chart writer and
// the proximity chart API so the axis and the totals use the SAME day
// (tests/native/test_proximity_chart.cpp).
static inline uint32_t proxShiftDayStartFromCivilMidnight(uint32_t civilMidnightTs, uint8_t boundaryHour) {
    if (civilMidnightTs == 0 || boundaryHour == 0 || boundaryHour > 23) return civilMidnightTs;
    const uint32_t backSec = (uint32_t)(24 - boundaryHour) * 3600UL;
    return (civilMidnightTs > backSec) ? (civilMidnightTs - backSec) : 0;
}

// 10-min block of the production day that holds a given local clock time, plus
// the epoch of that day's start. The block index runs 0..143 from the
// day-boundary hour (same ordering as the .chart columns and the OEE timeline
// strip), so block 0 is [b:00,b:10) and block 143 is [b-1:50,b:00). Pure —
// shared by the RAM stop buckets and tests/native/test_proximity_day_aligned.cpp.
// boundaryHour 0 = calendar day. `sec` only rounds the day start down.
static inline int proxStopBinFromClock(int hour, int minute, int sec, uint8_t boundaryHour,
                                       time_t now, uint32_t* dayStartOut) {
    int mins  = (hour * 60) + minute;
    int since = mins - ((int)boundaryHour * 60);
    if (since < 0) since += 1440;
    if (since >= 1440) since -= 1440;
    if (dayStartOut) {
        time_t start = now - (time_t)since * 60 - (time_t)(sec < 0 ? 0 : sec);
        *dayStartOut = (start > 0) ? (uint32_t)start : 0;
    }
    return since / 10;
}

// 1-based turno number for a shift slot, rotated so the turn that STARTS at the
// day-boundary hour is Turno 1 (e.g. {6,14,22} with boundary 22h: 22h=Turno 1,
// 6h=Turno 2, 14h=Turno 3 — the day's first turn is the overnight one). Derived
// from the shift config (nothing hardcoded). Falls back to slotIdx+1 when there
// is no day boundary (midnight rollover or shifts disabled/empty).
static inline int proximityShiftTurnNumber(const ProximityShiftConfig& sc, int8_t slotIdx) {
    int n = (int)sc.nSlots;
    if (!sc.enabled || n == 0 || slotIdx < 0 || slotIdx >= n) return (int)slotIdx + 1;
    uint8_t boundaryHour = 0;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        if (sc.slots[i].hour == 0) { boundaryHour = 0; break; }
        if (sc.slots[i].hour > boundaryHour) boundaryHour = sc.slots[i].hour;
    }
    if (boundaryHour == 0) return (int)slotIdx + 1;
    int boundaryIdx = -1;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        if (sc.slots[i].hour == boundaryHour) { boundaryIdx = (int)i; break; }
    }
    if (boundaryIdx < 0) return (int)slotIdx + 1;
    return ((int)slotIdx - boundaryIdx + n) % n + 1;
}

// Context time for every shift-day-aligned derivation inside a shift-boundary
// block (marker rows, weekly hourly read, email folder/subject). Must point
// just BEFORE the boundary: at a day-boundary shift end (e.g. 22h with 6/14/22
// slots, or 00h/23h/18h endings) the detection time already belongs to the NEW
// day folder while the ended shift's ticks live in the PREVIOUS one — the 22h
// email body/CSV went out empty because of this. Derived from the new shift's
// slot time (minus 60 s) so detection lag can never push it past the boundary;
// no hour is hardcoded, so any shift layout works. Falls back to detectTime-120
// when mktime fails.
static inline time_t proxShiftBoundaryCtxTime(time_t detectTime, uint8_t slotHour, uint8_t slotMinute) {
    struct tm bt;
    localtime_r(&detectTime, &bt);
    bt.tm_hour = slotHour;
    bt.tm_min  = (slotMinute > 59) ? 59 : slotMinute;
    bt.tm_sec  = 0;
    time_t bnd = mktime(&bt);
    if (bnd <= 0) {
        if (detectTime > 120) return detectTime - 120;
        return detectTime;
    }
    return bnd - 60;
}

// Absolute wall-clock window of a shift slot: [startW, endW).
// shiftEndCtx = the boundary-60s context captured at shift-end detection
// (proxShiftBoundaryCtxTime), so endW reconstructs the true shift-end
// boundary within ~1 min. startW = endW minus the exact slot length from
// proximityShiftSlotDurationSec() (single source of truth, no date math so
// overnight shifts need no special case). Reboot-immune: unlike the millis
// count-start window (which restarts at boot and hides stops between shift
// start and boot), this window always covers the whole shift. Returns the
// window length in seconds, 0 when the config/slot is invalid or the clock
// is unsynced (caller keeps legacy).
static inline uint32_t proxShiftWindowSec(const ProximityShiftConfig& sc, int8_t slotIdx,
                                          time_t shiftEndCtx, time_t& outStartW, time_t& outEndW) {
    if (!sc.enabled || sc.nSlots == 0 || slotIdx < 0 || slotIdx >= (int)sc.nSlots) return 0;
    uint32_t lenSec = proximityShiftSlotDurationSec(sc, (uint8_t)slotIdx);
    if (lenSec == 0) return 0;
    time_t endW = shiftEndCtx + 60;
    if (endW < 1000000000L) return 0;
    time_t startW = endW - (time_t)lenSec;
    if (startW <= 0) return 0;
    outStartW = startW;
    outEndW = endW;
    return lenSec;
}

// Persist shift config to LittleFS (/config/sensor_proximity.json, section "shift").
// Returns true if the file was written successfully.
bool saveProximityShiftConfig();

// Load shift config from LittleFS and apply. Called automatically from proximityBegin().
void loadProximityShiftConfig();

// Current shift slot index (0..nSlots-1) from the RTC time, or -1 when disabled/unsynced.
int8_t proximityGetCurrentShiftIndex();

// Hour window of a shift slot. Returns false when shifts disabled or index invalid.
// capToNow truncates the duration to the elapsed time since the shift started (test email).
bool proximityGetShiftWindow(int8_t shiftIdx, int& startHour, int& durHours, bool capToNow);

// Running shift summary (pieces since shift start + OEE + A/P/Q) for channel 0.
void proximityGetRunningShiftSummary(uint32_t& pieces, float& oee, float& avail, float& perf, float& qual);

// Running day summary (pieces since the shift-aligned day boundary + OEE + A/P/Q)
// for channel 0. Same shape as the shift summary, over the day deltas / [boundary, now].
void proximityGetRunningDaySummary(uint32_t& pieces, float& oee, float& avail, float& perf, float& qual);

// Pure OEE-alert fire decision (testable): fire when the value stayed strictly
// below the threshold for at least persistMs. The caller tracks the below-since
// timestamp, the fired-per-episode flag, cooldown and max/day.
static inline bool oeeAlertShouldFire(float value, float threshold, uint32_t belowMs, uint32_t persistMs) {
    return (value < threshold) && (belowMs >= persistMs);
}

// OEE alert engine: evaluates the configured OEE alert slots (shift OEE, day OEE,
// availability) for channel 0 and sends email when a value stays below threshold.
// Called from the main loop next to the inactivity check; throttled internally.
// No-op unless ENABLE_PROXIMITY==1 (whole .cpp is gated).
void proximityOeeAlertTick();

// Shift baselines captured at shift start for computing delta values.
// Returns false if channel index is out of range.
bool proximityGetShiftBaselines(uint8_t ch, uint32_t& countInBase, uint32_t& runSecBase,
                                uint32_t& downtSecBase, uint32_t& goodPartsBase);

// One day-so-far delta: carried pre-reset production plus the live
// (counter − base, 0-guarded) tail. Pure (testable): the single rule every
// day reader uses, so Day OEE stays continuous across counter resets.
static inline uint32_t proxDayDelta(uint32_t accum, uint32_t counter, uint32_t base) {
    return accum + ((counter >= base) ? counter - base : 0U);
}

// Day-so-far deltas for a channel (in/run/downtime/good), continuous across
// shift auto-resets: pre-reset production is carried in the day accumulators
// (see proximityResetChannel), which reset only at the day rollover, on OEE
// reset, or when the daybase file fails to restore. Returns false if the
// channel index is out of range.
bool proximityGetDayDeltas(uint8_t ch, uint32_t& dIn, uint32_t& dRunSec,
                           uint32_t& dDowntSec, uint32_t& dGood);

// Persist/restore the day baselines (+ day key) across reboots so the day-so-far
// OEE and donut continue since the day boundary like the charts do.
void saveProximityDayBaselines();
void loadProximityDayBaselines();

// Persist the shift baselines (+ shift index, day key, slot count) across
// reboots so a mid-shift reboot keeps the shift window (shift-end totals/OEE
// continue instead of restarting at boot). Restore is internal to the shift
// engine (first detection after boot).
void saveProximityShiftBaselines();

// Seconds since the current day's START — the last shift day-boundary rollover
// (e.g. 22:00 for 6/14/22 shifts; local midnight when no boundary is configured).
// 0 when the RTC is not synced yet.
uint32_t proximityDayElapsedSec();

// Planned-stop overlap for the day-so-far window [day boundary, now].
// Unsynced clock or empty window → 0.
uint32_t proximityDayPlannedStopSec();

// Wall-clock epoch of the CURRENT shift's start (most recent slot start at or
// before `now`; before the first daily slot the overnight shift started
// yesterday). Pure (testable): mirrors _detectCurrentShift() without reading
// live config — callers pass the shift slots explicitly. Returns 0 when there
// are no slots. Unlike the millis-based _countStartMs (RAM-only, restarts at
// boot), this survives reboots, so the live shift OEE no longer inflates
// Availability after a mid-shift OTA/reboot (live .29 2026-10-14: 82% vs 44%).
static inline time_t proxShiftStartWallFor(uint8_t nSlots, const ProximityShiftSlot* slots, time_t now) {
    if (nSlots == 0 || slots == nullptr || now <= 0) return 0;
    struct tm t;
    localtime_r(&now, &t);
    int cur = t.tm_hour * 60 + t.tm_min;
    int best = -1, bestMin = -1;
    for (uint8_t i = 0; i < nSlots; i++) {
        int sm = (int)slots[i].hour * 60 + (int)slots[i].minute;
        if (sm <= cur && sm > bestMin) { bestMin = sm; best = i; }
    }
    if (best < 0) best = nSlots - 1; // before the first slot → night shift
    struct tm st = t;
    st.tm_hour = slots[best].hour;
    st.tm_min  = slots[best].minute;
    st.tm_sec  = 0;
    st.tm_isdst = -1;
    time_t start = mktime(&st);
    if (start <= 0) return 0;
    if (start > now) start -= 86400; // overnight: started yesterday
    if (start > now) return 0;
    return start;
}

// Seconds since the current shift START (wall clock, reboot-immune).
// 0 when the clock is unsynced, shifts are disabled/empty, or the shift just
// started — callers fall back to the millis-based proximityCountElapsedSec.
uint32_t proximityShiftElapsedSec();

// Planned-stop overlap for the running-shift window [shift start, now].
// 0 when unavailable — callers fall back to proximityChannelPlannedStopSec.
uint32_t proximityShiftPlannedStopSec();

// Pure day-key for the shift-aligned day rollover detector (testable):
// tm_year*1000 + tm_yday, so any calendar day maps to a unique key.
static inline int proximityDayKey(int year, int yday) { return year * 1000 + yday; }

// Stacked time bar for the shift email (testable): production (U+2593 DARK
// SHADE) + planned stops (U+2592 MEDIUM SHADE) + unplanned (U+2591 LIGHT
// SHADE) as fractions of the window, plus a legend line in the caller's
// language. Widths/percentages are adjusted so bars fill `width` and
// percentages sum to 100. Empty when the window is empty (caller skips the
// block). Production deliberately avoids U+2588 FULL BLOCK so the black is
// lighter in the shift-end email.
static inline String proximityBuildTimeBarAscii(uint32_t runSec, uint32_t plannedSec,
                                                uint32_t elapsedSec, const String& prodLbl,
                                                const String& planLbl, const String& unplanLbl,
                                                uint8_t width = 30) {
    String out;
    if (elapsedSec == 0 || width == 0) return out;
    float prod = (float)runSec / (float)elapsedSec;
    if (prod < 0.0f) prod = 0.0f;
    if (prod > 1.0f) prod = 1.0f;
    float plan = (float)plannedSec / (float)elapsedSec;
    if (plan < 0.0f) plan = 0.0f;
    if (plan > 1.0f - prod) plan = 1.0f - prod;
    int nProd = (int)(prod * (float)width + 0.5f);
    int nPlan = (int)(plan * (float)width + 0.5f);
    if (nProd + nPlan > (int)width) {
        nPlan = (int)width - nProd;
        if (nPlan < 0) { nPlan = 0; nProd = (int)width; }
    }
    int nUnplan = (int)width - nProd - nPlan;
    int pp = (int)(prod * 100.0f + 0.5f);
    int pl = (int)(plan * 100.0f + 0.5f);
    int un = 100 - pp - pl;
    for (int i = 0; i < nProd; i++) out += "\xE2\x96\x93";
    for (int i = 0; i < nPlan; i++) out += "\xE2\x96\x92";
    for (int i = 0; i < nUnplan; i++) out += "\xE2\x96\x91";
    out += "\n";
    out += "\xE2\x96\x93"; out += " "; out += prodLbl; out += " "; out += String(pp); out += "%  ";
    out += "\xE2\x96\x92"; out += " "; out += planLbl; out += " "; out += String(pl); out += "%  ";
    out += "\xE2\x96\x91"; out += " "; out += unplanLbl; out += " "; out += String(un); out += "%";
    return out;
}

// Builds the full shift email body: summary + time bar + hourly ASCII chart + device footer.
// Shared by the auto shift-end email and the manual test-email button. The time
// bar (run/planned/elapsed secs of the shift window) is skipped when elapsed is 0.
// refWall dates the hourly columns for the planned-stop overlay (0 = time(nullptr)).
String proximityBuildShiftEmailBody(const String& csvPath, int startHour, int durHours,
                                    uint32_t pieces, float oee, float avail, float perf, float qual,
                                    uint32_t scrapCount = 0, uint32_t setupCount = 0,
                                    uint32_t runSec = 0, uint32_t plannedSec = 0, uint32_t elapsedSec = 0,
                                    time_t refWall = 0);

// T8 — turno_data.txt: path + sink decision for the shift-end snapshot file.
// The file holds EXACTLY the shift-end email body, written every time a shift
// closes (even when the email is skipped or fails to start — the .txt is the
// local record, the email is best-effort delivery).
//   latest: "/{sys}/turno_data.txt" — overwritten each shift, always the LAST
//     ended shift (stable pointer for dashboards/downloads).
//   dated (T8b): "/{sys}/turno{N}_{YYYY-MM-DD}_{HH-MM}.txt" — one file per
//     shift close, stamped with the shift END wall time (history). ISO date on
//     purpose: the shift-end email lists /{sys}/ filtered by DD-MM-YYYY, so the
//     dated turno files NEVER match that filter and are never attached (the
//     send path stays untouched, per brief T8).
//   sink: SD when (sdMode && mounted), else the SAME relative path on LittleFS
//     (parity: readers find it at the identical path on either backend).
static inline String proximityShiftDataTxtPath(const String& sys) {
    String s = sys;
    if (s.isEmpty()) s = "SD_LOG";
    return String("/") + s + String("/turno_data.txt");
}
// T8b (Nuno): nome por turno+data+hora, DENTRO da pasta do sistema:
// /{sys}/turno{N}_{YYYY-MM-DD}_{HH-MM}.txt
static inline String proximityShiftDataTxtPathDated(const String& sys, int turnNum,
                                                    int year, int mon, int mday,
                                                    int hour, int minute) {
    String s = sys;
    if (s.isEmpty()) s = "SD_LOG";
    char f[64];
    snprintf(f, sizeof(f), "/%s/turno%d_%04d-%02d-%02d_%02d-%02d.txt",
             s.c_str(), turnNum, year, mon, mday, hour, minute);
    return String(f);
}
static inline bool proximityShiftDataTxtUseSD(bool sdMode, bool mounted) {
    return sdMode && mounted;
}

// Inject the current shift config into a JSON object as the "shift" member.
// Used by the unified proximity config save so channels + shift live in ONE file.
void proximityShiftToJson(JsonObject& out);

// Apply the "shift" section of a unified config doc to the in-memory shift state.
// Used by the unified proximity config load. Returns true if a shift section existed.
bool proximityShiftFromJson(JsonVariantConst doc);

// Inject the current planned stops as the "stops" member.
// Used by the unified proximity config save so stops live in ONE file.
void proximityStopsToJson(JsonObject& out);

// Apply the "stops" section of a unified config doc (lenient load path).
// Returns true if a stops section existed.
bool proximityStopsFromJson(JsonVariantConst doc);

// Reset current shift detection state so the next tick forces a re-baseline.
// Called by proximitySetShiftConfig() in proximity_shift_config.cpp.
void proximityResetShiftDetection();

// Update Modbus slave holding registers 100..175 with live channel counters.
// Registers follow the map: base = 100 + ch*10
//   base+0 count_in, base+1 count_out, base+2 net (INT16),
//   base+3 goal, base+4 rate_ppm*10, base+5 state (0/1).
// No-op unless both ENABLE_MODBUS and ENABLE_MODBUS_SLAVE are 1.
void proximityModbusTick();

// --- TCA9554 DO relay mapping (8DI8DO POE board only) ---
// Returns the TCA9554 output channel linked to proximity channel ch (255 = disabled).
// Set/get functions are no-ops on boards without the expander.
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
uint8_t proximityGetTCA9554Ch(uint8_t ch);
void    proximitySetTCA9554Ch(uint8_t ch, uint8_t tca_ch);
#endif

#endif // ENABLE_PROXIMITY==1
