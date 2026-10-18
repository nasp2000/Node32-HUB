#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/rtc_state_util.h"

// Pure math for the RTC-RAM-backed runtime state: continuous (across warm
// reboots) uptime accumulation + the FTP auto-upload schedule phase anchor.
// Dependency-free header compiled as-is so the exact production arithmetic is
// verified here.

TEST_CASE("uptime sweep: stays quiet until a whole second elapsed") {
    uint32_t acc = 0, last = 1000;
    CHECK_FALSE(uptimeSweep(acc, last, 1000));        // no time passed
    CHECK_FALSE(uptimeSweep(acc, last, 1999));        // 999 ms
    CHECK(acc == 0);
    CHECK(last == 1000);
    CHECK(uptimeSweep(acc, last, 2500));              // 1 whole second
    CHECK(acc == 1);
    CHECK(last == 2000);
    CHECK_FALSE(uptimeSweep(acc, last, 2599));        // leftover 599 ms
    CHECK(uptimeSweep(acc, last, 3000));              // crosses the second
    CHECK(acc == 2);
    CHECK(last == 3000);
}

TEST_CASE("uptime sweep: burst frames accumulate every full second") {
    uint32_t acc = 10, last = 0;
    // 3.5 s elapsed in one call -> exactly 3 whole seconds, last steps by 3 s
    CHECK(uptimeSweep(acc, last, 3500));
    CHECK(acc == 13);
    CHECK(last == 3000);
    // 1.2 s later -> 1 more second, fractional 200 ms kept for the next sweep
    CHECK(uptimeSweep(acc, last, 4200));
    CHECK(acc == 14);
    CHECK(last == 4000);
}

TEST_CASE("uptime now: base + elapsed whole seconds, monotonic") {
    CHECK(uptimeNow(10, 0, 1500) == 11);
    CHECK(uptimeNow(10, 0, 999) == 10);
    CHECK(uptimeNow(100, 4000, 4250) == 100);
    uint32_t a = 12, l = 1000;
    // Instantaneous read does not touch the accumulator
    CHECK(uptimeNow(a, l, 5000) == 16);
    CHECK(a == 12);
    CHECK(l == 1000);
}

TEST_CASE("uptime sweep: millis wrap keeps accumulation monotonic") {
    uint32_t acc = 0, last = 0xFFFFFC18UL;            // 2^32 - 1000 ms
    // nowMs wrapped past 0: 1000+256=1256 ms elapsed -> subtraction borrows
    // correctly (mod 2^32) and last wraps through zero exactly
    CHECK(uptimeSweep(acc, last, 0x00000100UL));
    CHECK(acc == 1);
    CHECK(last == 0x00000000UL);
    // next sweep, now 0x600: 1536 ms past the new 0 -> 1 whole second
    CHECK(uptimeSweep(acc, last, 0x00000600UL));
    CHECK(acc == 2);
    CHECK(last == 0x000003E8UL);                      // 1000
}

TEST_CASE("ftp cycle due: fires only after intervalSec at/after the anchor") {
    CHECK_FALSE(ftpCycleDueEpoch(1000000, 1000000, 300 * 60));
    CHECK_FALSE(ftpCycleDueEpoch(1000000, 1000000 + 300 * 60 - 1, 300 * 60));
    CHECK(ftpCycleDueEpoch(1000000, 1000000 + 300 * 60, 300 * 60));       // exact boundary
    CHECK(ftpCycleDueEpoch(1000000, 1000000 + 300 * 60 + 45, 300 * 60)); // overdue
    CHECK(ftpCycleDueEpoch(0, 1000000, 60));                              // never cycled yet
    CHECK_FALSE(ftpCycleDueEpoch(0, 0, 60));  // both unknown (clock not synced): not due
}

TEST_CASE("ftp cycle due: skewed clock never triggers") {
    // Clock reads before the stored anchor (not NTP-synced / stepped back)
    CHECK_FALSE(ftpCycleDueEpoch(1700000000UL, 1000, 300 * 60));
    CHECK_FALSE(ftpCycleDueEpoch(1700000000UL, 1699999999UL, 60));
    // Backwards clock but over the interval must still be blocked (no burst)
    CHECK_FALSE(ftpCycleDueEpoch(1700000000UL, 1600000000UL, 60));
}