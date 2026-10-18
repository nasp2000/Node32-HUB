#pragma once
// Host-testable core of the cross-task LittleFS lock guard.
//
// The ESP32-specific bits (the semaphore itself, esp_task_wdt_reset) stay in
// littlefs_manager.cpp; the two loops that are easy to get subtly wrong live
// here so tests/native/test_lfs_mutex_guard.cpp can cover the real logic
// without the ESP32 runtime.
//
// Background (live .24, 2026-10-15): callers took lfsMutex with a single long
// blocking take (up to 30 s). A big LittleFS flush in the loop task holding the
// lock could keep a waiter parked for the whole timeout with no watchdog feed.
// Equally, the original lazy init was a plain `if (NULL) create()`, which two
// tasks racing at boot can both pass — one instance leaks and the tasks then
// guard against *different* mutexes (no protection at all).

#include <stdint.h>

// Acquire a lock through `tryTake`, waiting at most `timeoutMs`, but never in
// one long uninterruptible block: the wait is split into `sliceMs` attempts and
// `feedWatchdog()` runs between failed attempts. Returns true when acquired.
//
// `tryTake(ms)` must return true when the lock was taken within `ms`.
template <typename TryTakeFn, typename FeedWatchdogFn>
inline bool lfsMutexWaitSliced(uint32_t timeoutMs, uint32_t sliceMs,
                               TryTakeFn tryTake, FeedWatchdogFn feedWatchdog) {
    if (sliceMs == 0) sliceMs = 1;  // never spin on a zero slice
    if (timeoutMs <= sliceMs) return tryTake(timeoutMs);
    uint32_t waited = 0;
    while (waited < timeoutMs) {
        if (tryTake(sliceMs)) return true;
        feedWatchdog();
        waited += sliceMs;
    }
    return false;
}

// Lazily publish a singleton exactly once. Allocate outside any lock, then let
// an atomic compare-exchange pick a single winner; the loser's instance is
// destroyed so the handle (and therefore the mutual exclusion) is never split
// between two instances. Returns null only if `create()` fails.
template <typename HandleT, typename CreateFn, typename DestroyFn>
inline HandleT lfsMutexPublishOnce(HandleT* slot, CreateFn create, DestroyFn destroy) {
    HandleT m = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (m != nullptr) return m;
    HandleT created = create();
    if (created == nullptr) return nullptr;
    HandleT expected = nullptr;
    if (!__atomic_compare_exchange_n(slot, &expected, created, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // Another task won the race: keep its instance, drop ours.
        destroy(created);
        m = expected;
    } else {
        m = created;
    }
    return m;
}
