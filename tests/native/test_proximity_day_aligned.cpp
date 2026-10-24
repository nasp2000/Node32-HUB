#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"
#include <ctime>
#include <cstring>

// Compile the REAL production proximity_manager.h so proxShiftAlignedDate —
// the exact day-alignment helper shared by the CSV writer (_proxWriteCSVRow),
// the SHIFTDAY command, the auto shift-end email and the test-email button —
// is the code under test (no reimplementation drift).
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

// Build a wall-clock time (local TZ) via mktime so the test is TZ-independent:
// the helper runs localtime_r/mktime internally on the same local time.
static time_t wallClock(int year, int mon, int day, int hour, int min, int sec) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void readAligned(time_t now, uint8_t boundary, int& day, int& mon, int& year, uint8_t& used) {
    struct tm out;
    memset(&out, 0, sizeof(out));
    used = proxShiftAlignedDate(now, boundary, &out);
    day  = out.tm_mday;
    mon  = out.tm_mon + 1;
    year = out.tm_year + 1900;
}

TEST_CASE("midnight rollover (no shift) keeps the calendar date") {
    int d, m, y; uint8_t u;
    readAligned(wallClock(2026, 8, 30, 12, 0, 0), 0, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 0);
    readAligned(wallClock(2026, 8, 30, 0, 30, 0), 0, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 0);
    readAligned(wallClock(2026, 8, 30, 23, 45, 0), 0, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 0);
}

TEST_CASE("times before the shift boundary keep the CURRENT calendar day (R2)") {
    int d, m, y; uint8_t u;
    // 2026-08-30 02:00 with a 06:00 boundary -> belongs to 2026-08-30 (day starts
    // day before at 06:00; label = the date the day ENDS into)
    readAligned(wallClock(2026, 8, 30, 2, 0, 0), 6, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 6);
    // One minute before the boundary -> same calendar day
    readAligned(wallClock(2026, 8, 30, 5, 59, 0), 6, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 6);
    // Later boundary (22h): 21:00 -> same calendar day (nearly the whole day done)
    readAligned(wallClock(2026, 8, 30, 21, 0, 0), 22, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
}

TEST_CASE("times at or after the boundary belong to the NEXT day (R2)") {
    int d, m, y; uint8_t u;
    // 2026-08-30 10:00 with a 06:00 boundary -> 2026-08-31 (boundary passed)
    readAligned(wallClock(2026, 8, 30, 10, 0, 0), 6, d, m, y, u);
    CHECK(d == 31); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 6);
    // Exactly at the boundary -> NEXT day (the day ending at 06:00 is over)
    readAligned(wallClock(2026, 8, 30, 6, 0, 0), 6, d, m, y, u);
    CHECK(d == 31); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 6);
    // Late-night at/after a 22h boundary -> NEXT day
    readAligned(wallClock(2026, 8, 30, 22, 0, 0), 22, d, m, y, u);
    CHECK(d == 31); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
    readAligned(wallClock(2026, 8, 30, 23, 59, 0), 22, d, m, y, u);
    CHECK(d == 31); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
}

TEST_CASE("month/year rollover across the shift boundary") {
    int d, m, y; uint8_t u;
    // 2026-08-01 02:00 b=06 -> 2026-08-01 (before today's boundary, no rollover)
    readAligned(wallClock(2026, 8, 1, 2, 0, 0), 6, d, m, y, u);
    CHECK(d == 1); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 6);
    // 2026-01-01 02:00 b=06 -> 2026-01-01 (before today's boundary, no rollover)
    readAligned(wallClock(2026, 1, 1, 2, 0, 0), 6, d, m, y, u);
    CHECK(d == 1); CHECK(m == 1); CHECK(y == 2026); CHECK(u == 6);
    // 2026-01-01 07:00 b=06 -> 2026-01-02 (boundary passed -> next day)
    readAligned(wallClock(2026, 1, 1, 7, 0, 0), 6, d, m, y, u);
    CHECK(d == 2); CHECK(m == 1); CHECK(y == 2026); CHECK(u == 6);
    // Cross-month step-forward at a 22h boundary: 2026-08-31 23:00 -> 2026-09-01.
    readAligned(wallClock(2026, 8, 31, 23, 0, 0), 22, d, m, y, u);
    CHECK(d == 1); CHECK(m == 9); CHECK(y == 2026); CHECK(u == 22);
}

TEST_CASE("boundary hour is the LAST shift start hour (SHIFTDAY folder consistency)") {
    // The writer's folder rolls at proximityShiftDayBoundaryHour() = last slot
    // start hour. With 6/14/22 shifts the boundary is 22h, so the file labeled
    // 30-08 holds [29 22:00, 30 22:00) = turno 1 (overnight 22h→6h) + turnos 2/3
    // of day 30. 21:30 must resolve to 30-08 (the day that is about to END).
    int d, m, y; uint8_t u;
    readAligned(wallClock(2026, 8, 30, 21, 30, 0), 22, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
    // 07:00 (between 06:00 shift start and 22:00) is within day 30's file
    readAligned(wallClock(2026, 8, 30, 7, 0, 0), 22, d, m, y, u);
    CHECK(d == 30); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
    // 23:00 -> the NEXT day (31-08) has already begun
    readAligned(wallClock(2026, 8, 30, 23, 0, 0), 22, d, m, y, u);
    CHECK(d == 31); CHECK(m == 8); CHECK(y == 2026); CHECK(u == 22);
}

// Shift-end email context: the boundary block must derive the folder from just
// BEFORE the boundary (the ended shift's ticks), never from the detection time
// (which already belongs to the new day folder at a day-boundary shift end).
// Regression: the 22h email (6/14/22 slots) went out with an empty body/CSV
// because folder + markers resolved to the new day.
static void readCtx(time_t detect, uint8_t slotH, uint8_t slotM, uint8_t boundary,
                    int& day, int& mon, int& year) {
    time_t ctx = proxShiftBoundaryCtxTime(detect, slotH, slotM);
    struct tm out;
    memset(&out, 0, sizeof(out));
    proxShiftAlignedDate(ctx, boundary, &out);
    day  = out.tm_mday;
    mon  = out.tm_mon + 1;
    year = out.tm_year + 1900;
}

TEST_CASE("shift-end ctx stays in the ended shift's folder (22h end, 6/14/22 slots)") {
    int d, m, y;
    // Detected at 22:00:15 on 2026-09-10, new shift starts 22:00, boundary 22h:
    // raw time already resolves to 11-09 (new day), ctx must resolve to 10-09
    // (folder [09 22:00, 10 22:00) where the ended 14h→22h turno's ticks live).
    readCtx(wallClock(2026, 9, 10, 22, 0, 15), 22, 0, 22, d, m, y);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
    uint8_t u;
    readAligned(wallClock(2026, 9, 10, 22, 0, 15), 22, d, m, y, u);
    CHECK(d == 11); // raw detection already belongs to the new day's folder
    // Late detection (tick 30 s cadence) still lands before the boundary.
    readCtx(wallClock(2026, 9, 10, 22, 0, 44), 22, 0, 22, d, m, y);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
}

TEST_CASE("shift-end ctx for 00h / 23h / 18h endings") {
    int d, m, y;
    // Shift ending 00:00 (slot at 00:00 -> midnight rollover, boundary 0):
    // detect 00:00:20 on 2026-09-11 -> ctx 2026-09-10 23:59 -> folder 10-09.
    readCtx(wallClock(2026, 9, 11, 0, 0, 20), 0, 0, 0, d, m, y);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
    // Shift ending 23:00 (boundary 23): detect 23:00:10 -> ctx 22:59 -> folder 10-09
    // (day [09 23:00, 10 23:00) where the ended 14h→23h turno's ticks live).
    readCtx(wallClock(2026, 9, 10, 23, 0, 10), 23, 0, 23, d, m, y);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
    // Shift ending 18:00 (boundary 18): detect 18:00:45 -> ctx 17:59 -> folder 10-09.
    readCtx(wallClock(2026, 9, 10, 18, 0, 45), 18, 0, 18, d, m, y);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
}

TEST_CASE("shift-end ctx is a no-op away from the day boundary") {
    int d, m, y;
    // 14:00 end with 22h boundary: ctx 13:59 resolves to the same folder as raw
    // (both before the 22:00 boundary -> folder 10-09).
    time_t raw = wallClock(2026, 9, 10, 14, 0, 20);
    readCtx(raw, 14, 0, 22, d, m, y);
    int rd, rm, ry; uint8_t u;
    readAligned(raw, 22, rd, rm, ry, u);
    CHECK(d == rd); CHECK(m == rm); CHECK(y == ry);
    CHECK(d == 10); CHECK(m == 9); CHECK(y == 2026);
}

TEST_CASE("turno number rotates so the day-boundary turn is Turno 1 (6/14/22)") {
    ProximityShiftConfig sc;
    memset(&sc, 0, sizeof(sc));
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0].hour = 6;  sc.slots[1].hour = 14; sc.slots[2].hour = 22;
    // Boundary = last slot start (22h): 22h turn is Turno 1, then 6h=2, 14h=3.
    CHECK(proximityShiftTurnNumber(sc, 0) == 2); // 06:00
    CHECK(proximityShiftTurnNumber(sc, 1) == 3); // 14:00
    CHECK(proximityShiftTurnNumber(sc, 2) == 1); // 22:00 (overnight first turn)
}

TEST_CASE("turno rotation for other layouts") {
    ProximityShiftConfig sc;
    memset(&sc, 0, sizeof(sc));
    sc.enabled = true; sc.nSlots = 2;
    sc.slots[0].hour = 6; sc.slots[1].hour = 14; // boundary 14: overnight 14→6 = 1
    CHECK(proximityShiftTurnNumber(sc, 0) == 2);
    CHECK(proximityShiftTurnNumber(sc, 1) == 1);
    // Midnight rollover (slot at 00:00) -> no rotation, identity numbering.
    sc.nSlots = 3;
    sc.slots[0].hour = 0; sc.slots[1].hour = 6; sc.slots[2].hour = 14;
    CHECK(proximityShiftTurnNumber(sc, 0) == 1);
    CHECK(proximityShiftTurnNumber(sc, 1) == 2);
    CHECK(proximityShiftTurnNumber(sc, 2) == 3);
    // Disabled/empty/out-of-range fall back to slotIdx+1.
    sc.enabled = false; sc.nSlots = 0;
    CHECK(proximityShiftTurnNumber(sc, 2) == 3);
    CHECK(proximityShiftTurnNumber(sc, -1) == 0);
}

TEST_CASE("turno rotation: arbitrary last-start hour stays the boundary") {
    ProximityShiftConfig sc;
    memset(&sc, 0, sizeof(sc));
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0].hour = 8; sc.slots[1].hour = 12; sc.slots[2].hour = 20;
    CHECK(proximityShiftTurnNumber(sc, 0) == 2); // 08:00
    CHECK(proximityShiftTurnNumber(sc, 1) == 3); // 12:00
    CHECK(proximityShiftTurnNumber(sc, 2) == 1); // 20:00 (overnight)
}

static ProximityPlannedStop mkStop(uint8_t sh, uint8_t sm, uint8_t eh, uint8_t em) {
    ProximityPlannedStop s;
    s.sh = sh; s.sm = sm; s.eh = eh; s.em = em;
    return s;
}

TEST_CASE("planned-stop overlap: empty list and empty window give zero") {
    time_t t0 = wallClock(2026, 9, 10, 6, 0, 0);
    time_t t1 = wallClock(2026, 9, 10, 14, 0, 0);
    CHECK(proxPlannedStopOverlapSec(t0, t1, nullptr, 0) == 0);
    ProximityPlannedStop s = mkStop(12, 0, 13, 0);
    CHECK(proxPlannedStopOverlapSec(t0, t1, &s, 0) == 0);
    CHECK(proxPlannedStopOverlapSec(t1, t0, &s, 1) == 0);
    CHECK(proxPlannedStopOverlapSec(t0, t0, &s, 1) == 0);
}

TEST_CASE("planned-stop overlap: lunch window inside a shift") {
    // Elapsed 06:00-14:00 (8h = 28800s), lunch 12:00-13:00 → 3600s excluded.
    time_t t0 = wallClock(2026, 9, 10, 6, 0, 0);
    time_t t1 = wallClock(2026, 9, 10, 14, 0, 0);
    ProximityPlannedStop s = mkStop(12, 0, 13, 0);
    CHECK(proxPlannedStopOverlapSec(t0, t1, &s, 1) == 3600);
    // Window fully inside the stop still counts only the window itself.
    time_t a = wallClock(2026, 9, 10, 12, 10, 0);
    time_t b = wallClock(2026, 9, 10, 12, 40, 0);
    CHECK(proxPlannedStopOverlapSec(a, b, &s, 1) == 1800);
    // Window outside the stop → zero.
    time_t c = wallClock(2026, 9, 10, 8, 0, 0);
    time_t d = wallClock(2026, 9, 10, 9, 0, 0);
    CHECK(proxPlannedStopOverlapSec(c, d, &s, 1) == 0);
}

TEST_CASE("planned-stop overlap: overnight wrap and multi-day spans") {
    // Cleaning 23:00-01:00; elapsed 22:00→06:00 next day → 2h overlap.
    time_t t0 = wallClock(2026, 9, 10, 22, 0, 0);
    time_t t1 = wallClock(2026, 9, 11, 6, 0, 0);
    ProximityPlannedStop s = mkStop(23, 0, 1, 0);
    CHECK(proxPlannedStopOverlapSec(t0, t1, &s, 1) == 7200);
    // Two stops accumulate: lunch 1h + cleaning 2h.
    ProximityPlannedStop both[2] = {mkStop(12, 0, 13, 0), mkStop(23, 0, 1, 0)};
    time_t u0 = wallClock(2026, 9, 10, 6, 0, 0);
    time_t u1 = wallClock(2026, 9, 11, 6, 0, 0);
    CHECK(proxPlannedStopOverlapSec(u0, u1, both, 2) == 10800);
    // Zero-length (start == end) and out-of-range entries are ignored.
    ProximityPlannedStop bad[2] = {mkStop(12, 0, 12, 0), mkStop(12, 0, 13, 0)};
    CHECK(proxPlannedStopOverlapSec(u0, u1, bad, 2) == 3600);
}

TEST_CASE("planned-stop capacity: 6 rows (page + firmware contract)") {
    // The /proximity page renders 6 stop rows; the firmware must accept 6.
    CHECK(PROX_MAX_PLANNED_STOPS == 6);
    // Six 30-min stops accumulate over a full-day window: 6x1800 = 10800s.
    ProximityPlannedStop six[6] = {mkStop(1, 0, 1, 30), mkStop(6, 0, 6, 30),
                                   mkStop(9, 0, 9, 30), mkStop(12, 0, 12, 30),
                                   mkStop(15, 0, 15, 30), mkStop(18, 0, 18, 30)};
    time_t w0 = wallClock(2026, 9, 10, 0, 0, 0);
    time_t w1 = wallClock(2026, 9, 11, 0, 0, 0);
    CHECK(proxPlannedStopOverlapSec(w0, w1, six, 6) == 10800);
    CHECK(proxPlannedStopsOverlap(six, 6) == false);
}

TEST_CASE("planned-stop capacity: 6 non-overlapping incl. overnight wrap") {
    // Six stops with one overnight wrap validate clean. Over a 48h window
    // [10th 00:00, 12th 00:00): the five 1h stops hit twice each (36000s);
    // the 23:00-01:00 wrap hits fully on day 10 (7200s) but is clamped to
    // 23:00-00:00 on day 11 (3600s). Total = 46800s.
    ProximityPlannedStop six[6] = {mkStop(1, 0, 2, 0), mkStop(4, 0, 5, 0),
                                   mkStop(8, 0, 9, 0), mkStop(12, 0, 13, 0),
                                   mkStop(16, 0, 17, 0), mkStop(23, 0, 1, 0)};
    CHECK(proxPlannedStopsOverlap(six, 6) == false);
    time_t w0 = wallClock(2026, 9, 10, 0, 0, 0);
    time_t w1 = wallClock(2026, 9, 12, 0, 0, 0);
    CHECK(proxPlannedStopOverlapSec(w0, w1, six, 6) == 46800);
}

TEST_CASE("day delta: carried production plus live tail, stale bases clamp to carry") {
    // Day OEE invariant: dX = accum + max(0, counter - base), so the day always
    // spans [boundary, now] even across counter resets.
    CHECK(proxDayDelta(0, 5000, 1000) == 4000);      // no reset yet: live tail only
    CHECK(proxDayDelta(4000, 200, 0) == 4200);        // post-reset: carry + new tail
    CHECK(proxDayDelta(4000, 0, 0) == 4000);          // fresh reset moment: carry only
    CHECK(proxDayDelta(4000, 100, 9000) == 4000);     // stale base (counter < base): carry only
    CHECK(proxDayDelta(0, 0, 0) == 0);                // fresh day: zero
}

static ProximityShiftConfig mkSc(bool en, uint8_t n, uint8_t h0, uint8_t h1, uint8_t h2) {
    ProximityShiftConfig sc;
    memset(&sc, 0, sizeof(sc));
    sc.enabled = en;
    sc.nSlots = n;
    sc.slots[0].hour = h0; sc.slots[1].hour = h1; sc.slots[2].hour = h2;
    return sc;
}

TEST_CASE("shift window: 14-22 shift resolves [14:00, 22:00) from boundary-60s ctx") {
    ProximityShiftConfig sc = mkSc(true, 3, 6, 14, 22);
    time_t ctx = wallClock(2026, 9, 21, 21, 59, 0); // boundary-60s context
    time_t startW = 0, endW = 0;
    uint32_t len = proxShiftWindowSec(sc, 1, ctx, startW, endW);
    CHECK(len == 28800);
    CHECK(endW == wallClock(2026, 9, 21, 22, 0, 0));
    CHECK(startW == endW - 28800);
    struct tm st;
    localtime_r(&startW, &st);
    CHECK(st.tm_hour == 14);
    // The 17:00-17:30 stop overlaps the absolute window: the bar shows 1800s.
    ProximityPlannedStop s = mkStop(17, 0, 17, 30);
    CHECK(proxPlannedStopOverlapSec(startW, endW, &s, 1) == 1800);
}

TEST_CASE("shift window: overnight 22-06 shift crosses midnight, invalid configs give 0") {
    ProximityShiftConfig sc = mkSc(true, 3, 6, 14, 22);
    time_t ctx = wallClock(2026, 9, 22, 5, 59, 0);
    time_t startW = 0, endW = 0;
    uint32_t len = proxShiftWindowSec(sc, 2, ctx, startW, endW);
    CHECK(len == 28800);
    CHECK(endW == wallClock(2026, 9, 22, 6, 0, 0));
    struct tm st;
    localtime_r(&startW, &st);
    CHECK(st.tm_hour == 22);
    CHECK(st.tm_mday == 21);
    // Invalid: disabled / empty / out-of-range slot → 0 (caller keeps legacy).
    time_t a = 0, b = 0;
    CHECK(proxShiftWindowSec(mkSc(false, 3, 6, 14, 22), 1, ctx, a, b) == 0);
    CHECK(proxShiftWindowSec(mkSc(true, 0, 6, 14, 22), 0, ctx, a, b) == 0);
    CHECK(proxShiftWindowSec(sc, 3, ctx, a, b) == 0);
    CHECK(proxShiftWindowSec(sc, -1, ctx, a, b) == 0);
    // Single slot = full 24h window.
    CHECK(proxShiftWindowSec(mkSc(true, 1, 22, 0, 0), 0, ctx, a, b) == 86400);
}

TEST_CASE("planned-stop list validation rejects overlap and zero length") {
    // NOTE: only inline header helpers are exercised here — this suite links
    // the header alone (no proximity_shift_config.cpp), so the setter itself
    // is covered by build-time compilation, not by these assertions.
    ProximityPlannedStop ok[2] = {mkStop(12, 0, 13, 0), mkStop(23, 0, 1, 0)};
    CHECK(proxPlannedStopsOverlap(ok, 2) == false);
    ProximityPlannedStop ov[2] = {mkStop(12, 0, 13, 0), mkStop(12, 30, 14, 0)};
    CHECK(proxPlannedStopsOverlap(ov, 2) == true);
    ProximityPlannedStop wrap[2] = {mkStop(23, 0, 1, 0), mkStop(0, 30, 2, 0)};
    CHECK(proxPlannedStopsOverlap(wrap, 2) == true);
    ProximityPlannedStop touch[2] = {mkStop(12, 0, 13, 0), mkStop(13, 0, 14, 0)};
    CHECK(proxPlannedStopsOverlap(touch, 2) == false);
    CHECK(proxPlannedStopsOverlap(nullptr, 0) == false);
}

TEST_CASE("computeOee ISO: planned stops raise A, guards hold, legacy default unchanged") {
    // 8h elapsed, 6h running, 300ppm target, 86400 parts, 10% scrap.
    // Legacy (no stops): A = 21600/28800 = 75%.
    ProximityOeeResult legacy = proximityComputeOee(21600, 28800, 86400, 77760, 300.0f);
    CHECK(legacy.availability > 74.99f);
    CHECK(legacy.availability < 75.01f);
    // Same inputs with 1h planned stop: PBT = 25200 → A = 85.71%.
    ProximityOeeResult iso = proximityComputeOee(21600, 28800, 86400, 77760, 300.0f, 3600);
    CHECK(iso.availability > 85.7f);
    CHECK(iso.availability < 85.72f);
    // P, Q, OEE-product inputs unchanged; only A moves.
    CHECK(iso.performance == legacy.performance);
    CHECK(iso.quality == legacy.quality);
    CHECK(iso.oee > legacy.oee);
    // Whole window planned stop → A = 100 (nothing planned = nothing lost).
    ProximityOeeResult full = proximityComputeOee(0, 3600, 0, 0, 300.0f, 3600);
    CHECK(full.availability > 99.99f);
    CHECK(full.availability < 100.01f);
    // Fresh reset still A = 0.
    ProximityOeeResult fresh = proximityComputeOee(0, 0, 0, 0, 300.0f, 0);
    CHECK(fresh.availability == 0.0f);
}
TEST_CASE("chart/API day start: label D starts at D-1 boundary (22h -> -2h)") {
    // .29 evidence: the civil-day chart started at 00:xx while the CSV/totals
    // run [D-1 22:00, D 22:00). The writer and the API share this helper so
    // graph and donut use the SAME day.
    const uint32_t civilMid = (uint32_t)wallClock(2026, 8, 30, 0, 0, 0);
    const uint32_t start = proxShiftDayStartFromCivilMidnight(civilMid, 22);
    CHECK(start == (uint32_t)wallClock(2026, 8, 29, 22, 0, 0));
    // The served window is exactly one production day.
    CHECK(start + 86400UL == (uint32_t)wallClock(2026, 8, 30, 22, 0, 0));
}

TEST_CASE("chart/API day start: boundary 0 keeps the calendar day") {
    const uint32_t civilMid = (uint32_t)wallClock(2026, 8, 30, 0, 0, 0);
    CHECK(proxShiftDayStartFromCivilMidnight(civilMid, 0) == civilMid);
}

TEST_CASE("chart/API day start: 6h boundary pulls back 18h, guards hold") {
    const uint32_t civilMid = (uint32_t)wallClock(2026, 8, 30, 0, 0, 0);
    CHECK(proxShiftDayStartFromCivilMidnight(civilMid, 6) ==
          (uint32_t)wallClock(2026, 8, 29, 6, 0, 0));
    CHECK(proxShiftDayStartFromCivilMidnight(0, 22) == 0);
    CHECK(proxShiftDayStartFromCivilMidnight(civilMid, 24) == civilMid);
}

// ===== OEE timeline stop buckets (?stops=1) =====
// The strip draws 6 squares of 10 min per hour column, in the SAME order as the
// .chart columns (they start at the day-boundary hour). proxStopBinFromClock is
// the pure mapping the RAM bucket writer and the page share, so a block index
// always lands on the column/square the strip paints.
TEST_CASE("stop bins: 22h boundary puts 22:00 in block 0 and 21:50 in block 143") {
    const time_t now2200 = wallClock(2026, 6, 15, 22, 0, 0);
    const time_t now0130 = wallClock(2026, 6, 16, 1, 30, 0);
    const time_t now2155 = wallClock(2026, 6, 15, 21, 55, 0);
    uint32_t ds = 0;

    CHECK(proxStopBinFromClock(22, 0, 0, 22, now2200, &ds) == 0);
    CHECK(ds == (uint32_t)now2200);                    // the day starts here
    CHECK(proxStopBinFromClock(1, 30, 0, 22, now0130, &ds) == 21);   // 22:00+3h30
    CHECK(ds == (uint32_t)wallClock(2026, 6, 15, 22, 0, 0));
    CHECK(proxStopBinFromClock(21, 55, 0, 22, now2155, &ds) == 143); // last square
    CHECK(ds == (uint32_t)wallClock(2026, 6, 14, 22, 0, 0));
    // Every block maps back to its own start once the page adds bin*600 s.
    for (int b = 0; b < 144; b++) {
        CHECK((uint32_t)(ds + (uint32_t)b * 600) ==
              (uint32_t)(wallClock(2026, 6, 14, 22, 0, 0) + b * 600));
    }
}

TEST_CASE("stop bins: boundary 0 = calendar day, sub-minute seconds floor the start") {
    const time_t t = wallClock(2026, 6, 15, 8, 17, 42);
    uint32_t ds = 0;
    CHECK(proxStopBinFromClock(0, 5, 0, 0, wallClock(2026, 6, 15, 0, 5, 0), &ds) == 0);
    CHECK(proxStopBinFromClock(23, 59, 0, 0, wallClock(2026, 6, 15, 23, 59, 0), &ds) == 143);
    // 08:17:42 -> block 49 (08:10) and a day start of 00:00:00 exactly.
    CHECK(proxStopBinFromClock(8, 17, 42, 0, t, &ds) == 49);
    CHECK(ds == (uint32_t)wallClock(2026, 6, 15, 0, 0, 0));
    // NULL day-start pointer is allowed (JSON emitter only needs the index).
    CHECK(proxStopBinFromClock(8, 17, 42, 0, t, nullptr) == 49);
}
