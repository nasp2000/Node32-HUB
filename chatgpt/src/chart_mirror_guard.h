// chart_mirror_guard.h — when the RS232 ".chart" mirror may touch storage.
//
// ARCHITECTURE §6.1 rule 2: persisted chart writes follow the configured flush
// interval (`flashFbWindowMs()`: 10m/30m/60m, default 30m), never a private
// fixed cadence. The SD copy is written with the data chunk (SD has room and is
// the primary sink); the LittleFS mirror is throttled, because unthrottled
// per-line appends would fill the daily file and then trip the 1 MB /config
// reserve gate, stopping every flash write (sd_layout/§6.1 rule 5).
//
// FreeRTOS-free so the throttle can be unit-tested natively with a fake clock.
#pragma once

#include <stdint.h>

// onFlash : the write targets LittleFS (no SD in use)
// lastMs  : millis() of the previous mirror write, 0 = none yet
// windowMs: configured interval (flashFbWindowMs())
inline bool chartMirrorDue(bool onFlash, unsigned long nowMs, unsigned long lastMs, unsigned long windowMs) {
    if (!onFlash) return true;            // SD path: no extra throttle here
    if (lastMs == 0) return true;         // first write since boot
    if (windowMs == 0) return true;       // bad/absent config must not block the record
    return (uint32_t)(nowMs - lastMs) >= (uint32_t)windowMs;
}
