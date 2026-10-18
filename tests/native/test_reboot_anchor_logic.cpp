#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/reboot_anchor_logic.h"

// Pure decision logic for the 3-layer scheduled-reboot anti-loop anchor:
//   RTC warm mirror > LittleFS truth > legacy NVS migration.
// Dependency-free header compiled as-is so the exact production precedence is
// verified here.

TEST_CASE("pick: RTC warm mirror wins when valid and positive") {
    int today = 20260929;
    CHECK(rebootAnchorPickDayKey(true, today, today, today) == today);
    CHECK(rebootAnchorPickDayKey(true, today, 0, 0) == today);
    CHECK(rebootAnchorPickDayKey(true, today, today, 0) == today);
    // Valid RTC but no day recorded -> falls through to the next layers.
    CHECK(rebootAnchorPickDayKey(true, 0, today, 0) == today);
    CHECK(rebootAnchorPickDayKey(true, -1, today, today) == today);
    CHECK(rebootAnchorPickDayKey(true, 0, today, today) == today);
}

TEST_CASE("pick: power cut (RTC invalid) leans on LittleFS truth") {
    int today = 20260929;
    CHECK(rebootAnchorPickDayKey(false, today, today, today) == today);
    CHECK(rebootAnchorPickDayKey(false, 0, today, 0) == today);
    CHECK(rebootAnchorPickDayKey(false, today, today, 0) == today);
    // LittleFS empty -> legacy NVS migration is the fallback.
    CHECK(rebootAnchorPickDayKey(false, 0, 0, today) == today);
    CHECK(rebootAnchorPickDayKey(false, 0, -1, today) == today);
    CHECK(rebootAnchorPickDayKey(false, 0, -1, -1) == 0);
    CHECK(rebootAnchorPickDayKey(false, 0, 0, -1) == 0);
    CHECK(rebootAnchorPickDayKey(false, 0, 0, 0) == 0);
}

TEST_CASE("should-skip: only blocks when the anchor matches today") {
    int today = 20260929;
    CHECK(rebootAnchorShouldSkip(today, today));
    CHECK(rebootAnchorShouldSkip(today, today));
    CHECK_FALSE(rebootAnchorShouldSkip(today - 1, today));  // stale anchor -> fire
    CHECK_FALSE(rebootAnchorShouldSkip(today + 1, today));
    CHECK_FALSE(rebootAnchorShouldSkip(0, today));          // never ran
    CHECK_FALSE(rebootAnchorShouldSkip(-1, today));
    CHECK_FALSE(rebootAnchorShouldSkip(today, 0));          // clock not ready
    CHECK_FALSE(rebootAnchorShouldSkip(today, -1));
}
TEST_CASE("s1-follows-shifts: only with shifts enabled and at least one slot") {
    CHECK(rebootS1FollowsShifts(true, 3));
    CHECK(rebootS1FollowsShifts(true, 1));
    CHECK_FALSE(rebootS1FollowsShifts(false, 3));   // shifts disabled -> stored time
    CHECK_FALSE(rebootS1FollowsShifts(true, 0));    // no slots -> stored time
    CHECK_FALSE(rebootS1FollowsShifts(false, 0));
}

TEST_CASE("effective-time: boundary+grace when following, stored otherwise") {
    // 6/14/22 shifts -> boundary 22:00, grace 5 -> 22:05 (1325 min).
    CHECK(rebootEffectiveTotalMin(true, 22, 5, 0) == 1325);
    CHECK(rebootEffectiveTotalMin(true, 6, 5, 0) == 365);
    CHECK(rebootEffectiveTotalMin(true, 0, 5, 0) == 5);
    // Stored time passes through untouched when not following.
    CHECK(rebootEffectiveTotalMin(false, 22, 5, 0) == 0);     // 00:00 default
    CHECK(rebootEffectiveTotalMin(false, 22, 5, 150) == 150); // custom 02:30
    // Clamps: boundary/grace out of range never wrap past midnight.
    CHECK(rebootEffectiveTotalMin(true, 23, 90, 0) == 1439);
    CHECK(rebootEffectiveTotalMin(true, -1, 5, 0) == 5);
}

TEST_CASE("effective-time: shift-follow grace is 1 min (T19)") {
    // 6/14/22 shifts -> boundary 22:00, grace 1 -> 22:01 (1321 min). The
    // actual reboot is additionally ordered ~1 min after the last shift's
    // email by the post-email quiet window (tested below).
    CHECK(rebootEffectiveTotalMin(true, 22, 1, 0) == 1321);
    CHECK(rebootEffectiveTotalMin(true, 6, 1, 0) == 361);
}

TEST_CASE("quiet-window: reboot only ~1 min after last email activity") {
    CHECK(rebootQuietElapsed(0, 60000));                     // never busy -> ok
    CHECK(rebootQuietElapsed(0, 0));                         // boot, idle -> ok
    CHECK_FALSE(rebootQuietElapsed(100000, 100000));         // busy right now -> wait
    CHECK_FALSE(rebootQuietElapsed(100000, 159999));         // 59.999 s ago -> wait
    CHECK(rebootQuietElapsed(100000, 160000));               // exactly 60 s ago -> fire
    CHECK(rebootQuietElapsed(100000, 160001));               // past the window -> fire
    CHECK(rebootQuietElapsed(5000, 5000 + 120000, 60000));   // explicit window, elapsed
    CHECK_FALSE(rebootQuietElapsed(5000, 5000 + 59999, 60000)); // explicit window, short
}

TEST_CASE("defer-decision: idle fires now, busy waits until deadline") {
    CHECK(rebootShouldFireNow(false, 1000, 2000));          // idle -> now
    CHECK(rebootShouldFireNow(false, 5000, 2000));          // idle, past deadline -> now
    CHECK_FALSE(rebootShouldFireNow(true, 1000, 2000));     // busy, before deadline -> wait
    CHECK(rebootShouldFireNow(true, 2000, 2000));           // busy, at deadline -> fire anyway
    CHECK(rebootShouldFireNow(true, 9000, 2000));           // busy, past deadline -> fire anyway
    CHECK_FALSE(rebootShouldFireNow(true, 0, 900000));      // busy, 15min window -> wait
}
