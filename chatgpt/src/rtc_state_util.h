// rtc_state_util.h
// Pure, dependency-free helpers for RTC-RAM-backed runtime state (monotonic
// uptime accumulation + FTP auto-upload schedule phase). Kept header-only so
// the exact arithmetic is reusable in native (doctest) tests and in firmware.
// See also: proximity_manager.h (ProxRtcSnapshot), sensor_rtc_tail.h.
#ifndef RTC_STATE_UTIL_H
#define RTC_STATE_UTIL_H

#include <stdint.h>

// Advance a monotonic whole-second accumulator by the elapsed whole seconds
// since the last sweep. `lastMs` is stepped forward in whole-second chunks so
// millis() wraps and bursty callers never double-count a second. Returns true
// when the accumulator grew (caller may persist it to RTC RAM then).
static inline bool uptimeSweep(uint32_t &accumSec, uint32_t &lastMs, uint32_t nowMs) {
    uint32_t deltaSec = (nowMs - lastMs) / 1000UL;
    if (deltaSec == 0) return false;
    accumSec += deltaSec;
    lastMs   += deltaSec * 1000UL;
    return true;
}

// Instantaneous uptime in whole seconds (accumulated + elapsed since last sweep).
static inline uint32_t uptimeNow(uint32_t accumSec, uint32_t lastMs, uint32_t nowMs) {
    return accumSec + (nowMs - lastMs) / 1000UL;
}

// True when `intervalSec` seconds have elapsed since `lastEpoch` (UTC epoch
// seconds). False when the clock reads before `lastEpoch` (not yet NTP-synced
// or stepped back) so a skewed clock never triggers a burst of cycles.
static inline bool ftpCycleDueEpoch(uint32_t lastEpoch, uint32_t nowEpoch, uint32_t intervalSec) {
    if (nowEpoch < lastEpoch) return false;
    return (nowEpoch - lastEpoch) >= intervalSec;
}

#endif // RTC_STATE_UTIL_H