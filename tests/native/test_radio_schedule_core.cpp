// Native unit tests for radio_schedule_core.h — compiles the production header
// (pure weekly-window logic, no Arduino deps). Run via run_native_tests.py.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "radio_schedule_core.h"

// daysMask: bit0=Sunday .. bit6=Saturday.
static const uint32_t kEveryDay = 0x7F;
static const uint32_t kMonWedFri = (1u << 1) | (1u << 3) | (1u << 5); // Mo,We,Fr
static const uint32_t kSatSun = (1u << 0) | (1u << 6);                // Su,Sa

TEST_CASE("non-masked days are always NONE regardless of time") {
    // Sunday (wday 0) not in mask: kMonWedFri.
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 0, 0) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 0, 9 * 60 + 30) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 0, 17 * 60) == SCHED_WINDOW_NONE);
    // Tuesday (wday 2) not in mask.
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 2, 12 * 60) == SCHED_WINDOW_NONE);
}

TEST_CASE("simple same-day window: START only inside [start, end)") {
    // 09:00-17:00 on Monday (wday 1, in mask).
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 8 * 60 + 59) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 9 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 12 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 16 * 60 + 59) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 17 * 60) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kMonWedFri, 9, 0, 17, 0, 1, 23 * 60 + 59) == SCHED_WINDOW_NONE);
}

TEST_CASE("overnight window 22:00-07:00: START on start day, TAIL next day") {
    // Saturday (wday 6) in mask -> START from 22:00.
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 6, 21 * 60 + 59) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 6, 22 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 6, 23 * 60 + 59) == SCHED_WINDOW_START);
    // Sunday (wday 0) also in mask -> TAIL until 07:00.
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 0, 0) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 0, 6 * 60 + 59) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 0, 7 * 60) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kSatSun, 22, 0, 7, 0, 0, 12 * 60) == SCHED_WINDOW_NONE);
}

TEST_CASE("overnight TAIL requires the next day in the mask") {
    // Saturday only (no Sunday in mask): TAIL day is NONE.
    uint32_t satOnly = (1u << 6);
    CHECK(schedWindowState(satOnly, 22, 0, 7, 0, 6, 22 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(satOnly, 22, 0, 7, 0, 0, 3 * 60) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(satOnly, 22, 0, 7, 0, 0, 6 * 60 + 59) == SCHED_WINDOW_NONE);
}

TEST_CASE("full-day window end==start at midnight is START all day") {
    CHECK(schedWindowState(kEveryDay, 0, 0, 0, 0, 3, 0) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 0, 0, 0, 0, 3, 12 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 0, 0, 0, 0, 3, 23 * 60 + 59) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 0, 0, 0, 0, 5, 1) == SCHED_WINDOW_START);
}

TEST_CASE("full-day window end==start at 07:00 covers both phases") {
    // TAIL 00:00..06:59, START 07:00..23:59 on a masked day.
    CHECK(schedWindowState(kEveryDay, 7, 0, 7, 0, 4, 0) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(kEveryDay, 7, 0, 7, 0, 4, 6 * 60 + 59) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(kEveryDay, 7, 0, 7, 0, 4, 7 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 7, 0, 7, 0, 4, 18 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 7, 0, 7, 0, 4, 23 * 60 + 59) == SCHED_WINDOW_START);
}

TEST_CASE("boundary minutes at end of day and midnight-crossing edges") {
    // Window 23:00-07:00 on Monday+Tuesday.
    uint32_t monTue = (1u << 1) | (1u << 2);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 1, 22 * 60 + 59) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 1, 23 * 60) == SCHED_WINDOW_START);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 1, 23 * 60 + 59) == SCHED_WINDOW_START);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 2, 0) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 2, 6 * 60 + 59) == SCHED_WINDOW_TAIL);
    CHECK(schedWindowState(monTue, 23, 0, 7, 0, 2, 7 * 60) == SCHED_WINDOW_NONE);
}

TEST_CASE("same-day window edge exactly at minute 1439 / 0") {
    // 00:00-06:00 on Friday (wday 5).
    CHECK(schedWindowState(kEveryDay, 0, 0, 6, 0, 5, 0) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 0, 0, 6, 0, 5, 5 * 60 + 59) == SCHED_WINDOW_START);
    CHECK(schedWindowState(kEveryDay, 0, 0, 6, 0, 5, 6 * 60) == SCHED_WINDOW_NONE);
    CHECK(schedWindowState(kEveryDay, 0, 0, 6, 0, 5, 23 * 60 + 59) == SCHED_WINDOW_NONE);
}
