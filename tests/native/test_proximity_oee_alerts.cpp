#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"
#include <ctime>
#include <cstring>

// Compile the REAL production OEE-alert fire helper from proximity_manager.h
// (oeeAlertShouldFire — the exact decision the proximityOeeAlertTick engine uses
// for the alertas OEE tab: fire when the value stayed strictly below threshold
// for at least persistMs). Stubs provide Arduino.h / ArduinoJson.h.
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

// ── Fire decision: value < threshold && belowMs >= persistMs ─────────────
TEST_CASE("oeeAlertShouldFire: below threshold long enough fires") {
    // Shift OEE 54.2% < 60% for 15 min (900000 ms) -> fire.
    CHECK(oeeAlertShouldFire(54.2f, 60.0f, 900000UL, 900000UL));
    // Longer than persist -> still fires (engine cooldown handled by caller).
    CHECK(oeeAlertShouldFire(10.0f, 60.0f, 3600000UL, 900000UL));
}

TEST_CASE("oeeAlertShouldFire: above or equal never fires") {
    // Above threshold, even after a long time below before (recovered).
    CHECK(!oeeAlertShouldFire(60.1f, 60.0f, 3600000UL, 900000UL));
    // Exactly at threshold is NOT below (strict <).
    CHECK(!oeeAlertShouldFire(60.0f, 60.0f, 3600000UL, 900000UL));
    // Healthy OEE never fires.
    CHECK(!oeeAlertShouldFire(95.5f, 60.0f, 0UL, 900000UL));
}

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

// ── Alert-budget day key: production day, not calendar date ───────────────
// The engine resets max/day counters on the shift-aligned production day
// (same R2 model as day OEE/donut), so with a 22h boundary the budget at
// 23:00 already belongs to the next date — never double-spent pre/post
// midnight within one production day.
TEST_CASE("oee-alert day key follows the production day (boundary 22)") {
    struct tm o1, o2;
    memset(&o1, 0, sizeof(o1)); memset(&o2, 0, sizeof(o2));
    proxShiftAlignedDate(wallClock(2026, 8, 21, 23, 0, 0), 22, &o1);
    int kLate = proximityDayKey(o1.tm_year, o1.tm_yday);
    proxShiftAlignedDate(wallClock(2026, 8, 22, 10, 0, 0), 22, &o2);
    int kNext = proximityDayKey(o2.tm_year, o2.tm_yday);
    CHECK(kLate == kNext);   // same production day, same budget
    struct tm cal;
    memset(&cal, 0, sizeof(cal));
    time_t late = wallClock(2026, 8, 21, 23, 0, 0);
    localtime_r(&late, &cal);
    CHECK(cal.tm_mday == 21);  // calendar still says 21st: mday key would split the budget
    CHECK(o1.tm_mday == 22);   // aligned date already stepped forward
}

TEST_CASE("oee-alert day key rolls at the boundary, identity at midnight") {
    struct tm a, b;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    proxShiftAlignedDate(wallClock(2026, 8, 21, 21, 59, 0), 22, &a);
    proxShiftAlignedDate(wallClock(2026, 8, 21, 22, 0, 0), 22, &b);
    CHECK(proximityDayKey(a.tm_year, a.tm_yday) != proximityDayKey(b.tm_year, b.tm_yday));
    struct tm c;
    memset(&c, 0, sizeof(c));
    proxShiftAlignedDate(wallClock(2026, 8, 21, 23, 0, 0), 0, &c);
    CHECK(c.tm_mday == 21);  // boundary 0 (disabled/midnight) = calendar date, legacy
}

TEST_CASE("oeeAlertShouldFire: below but persist not met") {
    // 5 min below with 15 min persist -> wait.
    CHECK(!oeeAlertShouldFire(54.2f, 60.0f, 300000UL, 900000UL));
    // Just under persist -> still waiting.
    CHECK(!oeeAlertShouldFire(54.2f, 60.0f, 899999UL, 900000UL));
    // Zero persist with a below value fires immediately (edge: persist=1 min
    // minimum in the UI, but the helper stays total).
    CHECK(oeeAlertShouldFire(0.0f, 60.0f, 0UL, 0UL));
}
