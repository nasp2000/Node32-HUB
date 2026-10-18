// Native tests for the cross-task LittleFS lock guard core
// (chatgpt/src/lfs_mutex_guard.h).
//
// These cover the two non-obvious behaviours behind the live .24 crash
// investigation (2026-10-15):
//   * the wait must be sliced + watchdog-fed, bounded by the timeout, and must
//     stop as soon as the lock is acquired;
//   * the lazy mutex publication must be race-free (exactly one instance).
//
// The ESP32 halves (xSemaphoreCreateRecursiveMutex / esp_task_wdt_reset) are
// replaced by recorders; the logic under test is the production header.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "lfs_mutex_guard.h"

using Handle = void*;

TEST_CASE("waitSliced: free lock acquires on the first sliced attempt, no watchdog feed") {
    int attempts = 0;
    int feeds = 0;
    uint32_t requestedTotal = 0;

    bool got = lfsMutexWaitSliced(
        500, 200,
        [&](uint32_t ms) { attempts++; requestedTotal += ms; return true; },  // free: first take wins
        [&]() { feeds++; });

    CHECK(got == true);
    CHECK(attempts == 1);
    CHECK(feeds == 0);            // never fed: we never had to wait a slice
    CHECK(requestedTotal == 200); // first attempt asks for one slice, not the whole budget
}

TEST_CASE("waitSliced: held lock -> bounded wait, sliced attempts, watchdog fed") {
    int attempts = 0;
    int feeds = 0;
    uint32_t requestedTotal = 0;

    bool got = lfsMutexWaitSliced(
        1000, 200,
        [&](uint32_t ms) { attempts++; requestedTotal += ms; return false; },  // always held
        [&]() { feeds++; });

    CHECK(got == false);
    CHECK(attempts == 5);          // 1000 / 200
    CHECK(feeds == 5);             // one feed between/after every failed attempt
    CHECK(requestedTotal == 1000); // total blocking never exceeds the timeout
}

TEST_CASE("waitSliced: lock released during the wait -> acquires and stops feeding") {
    int attempts = 0;
    int feeds = 0;
    int releasesAfter = 2;  // succeed on the 3rd attempt

    bool got = lfsMutexWaitSliced(
        5000, 200,
        [&](uint32_t /*ms*/) {
            attempts++;
            return attempts > releasesAfter;
        },
        [&]() { feeds++; });

    CHECK(got == true);
    CHECK(attempts == 3);
    CHECK(feeds == 2);  // fed only while still blocked
}

TEST_CASE("waitSliced: timeout <= slice stays a single take (no slicing overhead)") {
    int attempts = 0;
    int feeds = 0;
    uint32_t requestedTotal = 0;

    bool got = lfsMutexWaitSliced(
        100, 200,
        [&](uint32_t ms) { attempts++; requestedTotal += ms; return false; },
        [&]() { feeds++; });

    CHECK(got == false);
    CHECK(attempts == 1);
    CHECK(requestedTotal == 100);  // the real timeout is honoured, not rounded up
    CHECK(feeds == 0);
}

TEST_CASE("waitSliced: zero slice is clamped (cannot spin forever)") {
    int attempts = 0;

    bool got = lfsMutexWaitSliced(
        3, 0,
        [&](uint32_t /*ms*/) { attempts++; return false; },
        []() {});

    CHECK(got == false);
    CHECK(attempts == 3);  // slice clamped to 1 => 3 attempts for 3 ms
}

TEST_CASE("waitSliced: timeout that is not a multiple of the slice still terminates") {
    int attempts = 0;

    bool got = lfsMutexWaitSliced(
        900, 200,
        [&](uint32_t /*ms*/) { attempts++; return false; },
        []() {});

    CHECK(got == false);
    CHECK(attempts == 5);  // 0,200,400,600,800
}

TEST_CASE("publishOnce: empty slot -> created once, published, nothing destroyed") {
    Handle slot = nullptr;
    int creates = 0;
    int destroys = 0;
    Handle made = (Handle)0x1000;

    Handle got = lfsMutexPublishOnce<Handle>(
        &slot,
        [&]() -> Handle { creates++; return made; },
        [&](Handle) { destroys++; });

    CHECK(got == made);
    CHECK(slot == made);
    CHECK(creates == 1);
    CHECK(destroys == 0);
}

TEST_CASE("publishOnce: already published -> create never runs") {
    Handle existing = (Handle)0x2000;
    Handle slot = existing;
    int creates = 0;
    int destroys = 0;

    Handle got = lfsMutexPublishOnce<Handle>(
        &slot,
        [&]() -> Handle { creates++; return (Handle)0x9999; },
        [&](Handle) { destroys++; });

    CHECK(got == existing);
    CHECK(slot == existing);
    CHECK(creates == 0);
    CHECK(destroys == 0);
}

TEST_CASE("publishOnce: lost race -> our instance is destroyed, winner kept") {
    Handle slot = nullptr;
    Handle winner = (Handle)0xAAAA;
    Handle mine = (Handle)0xBBBB;
    int destroys = 0;

    Handle got = lfsMutexPublishOnce<Handle>(
        &slot,
        // Simulates another task publishing between our load and our CAS.
        [&]() -> Handle { slot = winner; return mine; },
        [&](Handle h) { if (h == mine) destroys++; });

    CHECK(got == winner);
    CHECK(slot == winner);
    CHECK(destroys == 1);  // the loser's handle is never leaked/split
}

TEST_CASE("publishOnce: create failure -> null, slot untouched") {
    Handle slot = nullptr;
    int destroys = 0;

    Handle got = lfsMutexPublishOnce<Handle>(
        &slot,
        [&]() -> Handle { return nullptr; },
        [&](Handle) { destroys++; });

    CHECK(got == nullptr);
    CHECK(slot == nullptr);
    CHECK(destroys == 0);
}
