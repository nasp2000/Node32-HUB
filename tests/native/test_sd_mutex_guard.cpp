// Native tests for the SD mutex watchdog decision core (chatgpt/src/sd_mutex_guard.h).
//
// Context (live .9 Cam-M, 2026-11-11): /files answered 503 "SD Card Busy" for
// hours because the recursive SD mutex was leaked. The old watchdog gated on an
// optional debug label that only the SD-write pipeline set, so on a camera-only
// pack it never ran at all — and its "force release" used xSemaphoreGiveRecursive
// from a non-owner, which FreeRTOS rejects. These tests pin the pure decision
// table that replaced it: warn while the lock may still be a slow operation,
// recover only once it is by definition a leak, never "reset" a free lock.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "sd_mutex_guard.h"

TEST_CASE("free or untracked lock: never warn, never reset") {
    // No owner (tracking cleared) => the elapsed value is meaningless.
    CHECK(sdMutexGuardDecide(0, 0, false) == SD_MUTEX_GUARD_NONE);
    CHECK(sdMutexGuardDecide(999999, 0, false) == SD_MUTEX_GUARD_NONE);
    // Owner known but depth 0 is a corrupted state: do not act on it.
    CHECK(sdMutexGuardDecide(999999, 0, true) == SD_MUTEX_GUARD_NONE);
}

TEST_CASE("held lock: silent before the warn threshold, warn only in between") {
    CHECK(sdMutexGuardDecide(0, 1, true) == SD_MUTEX_GUARD_NONE);
    CHECK(sdMutexGuardDecide(SD_MUTEX_WARN_AFTER_MS - 1, 1, true) == SD_MUTEX_GUARD_NONE);
    CHECK(sdMutexGuardDecide(SD_MUTEX_WARN_AFTER_MS, 1, true) == SD_MUTEX_GUARD_WARN);
    CHECK(sdMutexGuardDecide(SD_MUTEX_RESET_AFTER_MS - 1, 3, true) == SD_MUTEX_GUARD_WARN);
}

TEST_CASE("held lock past the reset threshold: reset, whatever the depth") {
    CHECK(sdMutexGuardDecide(SD_MUTEX_RESET_AFTER_MS, 1, true) == SD_MUTEX_GUARD_RESET);
    CHECK(sdMutexGuardDecide(SD_MUTEX_RESET_AFTER_MS + 600000UL, 1, true) == SD_MUTEX_GUARD_RESET);
    // Recursive depth (nested callers) is the same leak, not a bigger one.
    CHECK(sdMutexGuardDecide(SD_MUTEX_RESET_AFTER_MS, 9, true) == SD_MUTEX_GUARD_RESET);
}

TEST_CASE("reset threshold is far beyond any real SD operation") {
    // A legitimate write/flush must never reach the reset window.
    CHECK(SD_MUTEX_RESET_AFTER_MS >= 60000UL);
    CHECK(SD_MUTEX_RESET_AFTER_MS > SD_MUTEX_WARN_AFTER_MS);
}

TEST_CASE("warn throttle: first time due, then once per window") {
    const unsigned long t0 = 5000;
    CHECK(sdMutexGuardWarnDue(t0, 0) == true);                    // never warned
    CHECK(sdMutexGuardWarnDue(t0 + 1000, t0) == false);           // same window
    CHECK(sdMutexGuardWarnDue(t0 + SD_MUTEX_WARN_REPEAT_MS - 1, t0) == false);
    CHECK(sdMutexGuardWarnDue(t0 + SD_MUTEX_WARN_REPEAT_MS, t0) == true);
    // millis() wrap must not silence the warning forever.
    CHECK(sdMutexGuardWarnDue(10, 0xFFFFFFFFUL - 5) == true);
}
