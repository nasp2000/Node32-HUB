// sd_mutex_guard.h — pure decision logic for the SD mutex watchdog.
//
// The watchdog used to depend on the optional debug label (sdMutexHolder),
// which only the SD-write pipeline ever set: on packs without that pipeline
// (Cam-M) a leaked lock was never detected and /files stayed 503 for hours.
// This header is FreeRTOS-free so the thresholds can be unit-tested natively;
// sd_locking_impl.h owns the state and loop_blocks.h applies the decision.
#pragma once

#include <stdint.h>

// Held longer than this: worth a log line naming the owner (once per repeat window).
static const unsigned long SD_MUTEX_WARN_AFTER_MS   = 30000UL;
// Held longer than this: by definition a leak (a real SD op never runs 2 min).
static const unsigned long SD_MUTEX_RESET_AFTER_MS  = 120000UL;
// Repeat the warning at most once per window while the state persists.
static const unsigned long SD_MUTEX_WARN_REPEAT_MS  = 15000UL;

enum SdMutexGuardAction : uint8_t {
    SD_MUTEX_GUARD_NONE = 0,  // nothing to do
    SD_MUTEX_GUARD_WARN,      // log the owner, keep waiting
    SD_MUTEX_GUARD_RESET      // force-release (leak): recover without a reboot
};

// Decide what the watchdog should do for a lock held for heldMs with depth.
// ownerKnown is false when the tracking says nobody owns the mutex (then the
// elapsed value is meaningless and nothing is reported).
inline SdMutexGuardAction sdMutexGuardDecide(unsigned long heldMs,
                                             uint32_t depth,
                                             bool ownerKnown) {
    if (!ownerKnown || depth == 0) return SD_MUTEX_GUARD_NONE;
    if (heldMs >= SD_MUTEX_RESET_AFTER_MS) return SD_MUTEX_GUARD_RESET;
    if (heldMs >= SD_MUTEX_WARN_AFTER_MS) return SD_MUTEX_GUARD_WARN;
    return SD_MUTEX_GUARD_NONE;
}

// Throttle for the WARN action: true when lastWarnMs is 0 or the window elapsed.
inline bool sdMutexGuardWarnDue(unsigned long nowMs, unsigned long lastWarnMs) {
    if (lastWarnMs == 0) return true;
    return (unsigned long)(nowMs - lastWarnMs) >= SD_MUTEX_WARN_REPEAT_MS;
}
