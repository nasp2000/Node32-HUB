#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Standalone reimpl of the proximity shift detection and shift-window logic
// from proximity_manager.cpp (_detectCurrentShift, proximityGetShiftWindow).
// Mirrors production:
//   _detectCurrentShift: select the slot with the largest start <= current time;
//                        before the first slot -> night shift (nSlots-1).
//   proximityGetShiftWindow: chart columns are the ACTUAL shift hours
//                        [start, end); capToNow for test email.

struct Slot { uint8_t hour; uint8_t minute; };
struct ShiftCfg {
    bool enabled;
    uint8_t nSlots;
    Slot slots[3];
};

static int8_t detectCurrentShift(const ShiftCfg& sc, int curHour, int curMin) {
    if (!sc.enabled || sc.nSlots == 0) return -1;
    int cur = curHour * 60 + curMin;
    int best = -1;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        int sm = sc.slots[i].hour * 60 + sc.slots[i].minute;
        if (sm <= cur) best = i;
    }
    if (best == -1) best = sc.nSlots - 1;   // before first daily slot -> night shift
    return best;
}

static bool getShiftWindow(const ShiftCfg& sc, int8_t shiftIdx, int& startHour, int& durHours, bool capToNow, int nowHour = 0, int nowMin = 0) {
    if (!sc.enabled || sc.nSlots == 0 || shiftIdx < 0 || shiftIdx >= sc.nSlots) return false;
    int startMin = sc.slots[shiftIdx].hour * 60 + sc.slots[shiftIdx].minute;
    int nextIdx = (shiftIdx + 1) % sc.nSlots;
    int endMin = sc.slots[nextIdx].hour * 60 + sc.slots[nextIdx].minute;
    if (nextIdx == 0 && endMin <= startMin) endMin += 1440;

    // Chart columns are the actual shift hours [start, end).
    startHour = sc.slots[shiftIdx].hour;
    durHours = (endMin - startMin) / 60;
    if (durHours < 1) durHours = 1;

    if (capToNow) {
        int nowMinAbs = nowHour * 60 + nowMin;
        int elapsed = nowMinAbs - startMin;
        if (elapsed < 0) elapsed = 0;
        int elapsedHours = (elapsed + 59) / 60;
        if (elapsedHours < 1) elapsedHours = 1;
        if (elapsedHours < durHours) durHours = elapsedHours;
    }
    return true;
}

// Default 3-slot config (6h / 14h / 22h) as in production defaults.
static ShiftCfg defaultCfg() {
    ShiftCfg c = {};
    c.enabled = true;
    c.nSlots = 3;
    c.slots[0] = {6, 0};
    c.slots[1] = {14, 0};
    c.slots[2] = {22, 0};
    return c;
}

TEST_CASE("shift: detects first shift (06:00) mid-morning") {
    auto c = defaultCfg();
    CHECK(detectCurrentShift(c, 9, 0) == 0);
}

TEST_CASE("shift: detects second shift (14:00) at 17:30") {
    auto c = defaultCfg();
    CHECK(detectCurrentShift(c, 17, 30) == 1);
}

TEST_CASE("shift: detects third shift (22:00) late evening") {
    auto c = defaultCfg();
    CHECK(detectCurrentShift(c, 23, 0) == 2);
}

TEST_CASE("shift: before first slot returns night shift (nSlots-1)") {
    auto c = defaultCfg();
    CHECK(detectCurrentShift(c, 2, 0) == 2);   // 02:00 before 06:00 -> shift 2
}

TEST_CASE("shift: exactly at slot boundary belongs to that slot") {
    auto c = defaultCfg();
    CHECK(detectCurrentShift(c, 6, 0) == 0);   // 06:00 == slot0 start
    CHECK(detectCurrentShift(c, 14, 0) == 1);
    CHECK(detectCurrentShift(c, 22, 0) == 2);
}

TEST_CASE("shift: disabled returns -1") {
    auto c = defaultCfg();
    c.enabled = false;
    CHECK(detectCurrentShift(c, 9, 0) == -1);
}

TEST_CASE("shift: single-slot config always returns slot 0") {
    ShiftCfg c = {};
    c.enabled = true; c.nSlots = 1; c.slots[0] = {0, 0};
    CHECK(detectCurrentShift(c, 0, 0) == 0);
    CHECK(detectCurrentShift(c, 12, 0) == 0);
    CHECK(detectCurrentShift(c, 23, 59) == 0);
}

TEST_CASE("shift-window: first shift chart covers the actual shift hours") {
    auto c = defaultCfg();
    int sh = 0, dur = 0;
    bool ok = getShiftWindow(c, 0, sh, dur, false);
    CHECK(ok);
    CHECK(sh == 6);    // shift starts 06:00
    CHECK(dur == 8);   // columns 06..13
}

TEST_CASE("shift-window: third shift wraps to next day") {
    auto c = defaultCfg();
    int sh = 0, dur = 0;
    bool ok = getShiftWindow(c, 2, sh, dur, false);   // 22:00 shift
    CHECK(ok);
    CHECK(sh == 22);   // shift starts 22:00
    CHECK(dur == 8);   // 22,23,00..05
}

TEST_CASE("shift-window: capToNow shortens duration") {
    auto c = defaultCfg();
    int sh = 0, dur = 0;
    // Now is 10:00 within shift0 (starts 06:00). Elapsed 10:00-06:00 = 4h.
    bool ok = getShiftWindow(c, 0, sh, dur, true, 10, 0);
    CHECK(ok);
    CHECK(sh == 6);
    CHECK(dur == 4);
}

TEST_CASE("shift-window: capToNow at shift start yields at least 1h") {
    auto c = defaultCfg();
    int sh = 0, dur = 0;
    // Now 06:30, shift starts 06:00 -> elapsed 30 min -> ceil to 1h.
    bool ok = getShiftWindow(c, 0, sh, dur, true, 6, 30);
    CHECK(ok);
    CHECK(dur == 1);
}

TEST_CASE("shift-window: disabled returns false") {
    auto c = defaultCfg();
    c.enabled = false;
    int sh = 0, dur = 0;
    CHECK(getShiftWindow(c, 0, sh, dur, false) == false);
}

// ── Day-boundary hour + aligned-date logic (mirrors production) ──────────
// proximityShiftDayBoundaryHour / proximityShiftDayAlignedDate.
// Rule: any slot at hour 0 -> boundary 0 (keep midnight rollover). Otherwise
// the boundary is the latest slot start hour of the day.

static uint8_t shiftDayBoundaryHour(const ShiftCfg& sc) {
    if (!sc.enabled || sc.nSlots == 0) return 0;
    uint8_t last = 0;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        if (sc.slots[i].hour == 0) return 0;
        if (sc.slots[i].hour > last) last = sc.slots[i].hour;
    }
    return last;
}

static void alignDate(uint8_t boundary, int& day, int& hour) {
    if (boundary == 0) return;                       // midnight rollover — date unchanged
    if ((uint8_t)hour < boundary) day -= 1;          // before today's boundary -> previous day
}

TEST_CASE("boundary: 03-15 / 15-03 shifts -> 15h") {
    ShiftCfg c = {}; c.enabled = true; c.nSlots = 2;
    c.slots[0] = {3, 0}; c.slots[1] = {15, 0};
    CHECK(shiftDayBoundaryHour(c) == 15);
}

TEST_CASE("boundary: 22-06 / 06-14 / 14-22 shifts -> 22h") {
    ShiftCfg c = {}; c.enabled = true; c.nSlots = 3;
    c.slots[0] = {6, 0}; c.slots[1] = {14, 0}; c.slots[2] = {22, 0};
    CHECK(shiftDayBoundaryHour(c) == 22);
}

TEST_CASE("boundary: 00-12 / 12-00 shifts -> keep 00h") {
    ShiftCfg c = {}; c.enabled = true; c.nSlots = 2;
    c.slots[0] = {0, 0}; c.slots[1] = {12, 0};
    CHECK(shiftDayBoundaryHour(c) == 0);
}

TEST_CASE("boundary: disabled or empty -> 00h") {
    ShiftCfg c = {}; c.enabled = false; c.nSlots = 3;
    c.slots[0] = {6, 0}; c.slots[1] = {14, 0}; c.slots[2] = {22, 0};
    CHECK(shiftDayBoundaryHour(c) == 0);
    ShiftCfg e = {}; e.enabled = true; e.nSlots = 0;
    CHECK(shiftDayBoundaryHour(e) == 0);
}

TEST_CASE("aligned-date: before boundary goes to previous day (boundary 15)") {
    int day = 10, hour = 14;
    alignDate(15, day, hour);
    CHECK(day == 9);   // 14:00 < 15:00 -> still in the running previous day
}

TEST_CASE("aligned-date: at/after boundary stays on current day (boundary 15)") {
    int day = 10, hour = 15;
    alignDate(15, day, hour);
    CHECK(day == 10);
}

TEST_CASE("aligned-date: before boundary goes to previous day (boundary 22)") {
    int day = 5, hour = 21;
    alignDate(22, day, hour);
    CHECK(day == 4);
}

TEST_CASE("aligned-date: boundary 0 leaves date unchanged") {
    int day = 1, hour = 3;
    alignDate(0, day, hour);
    CHECK(day == 1);
}
