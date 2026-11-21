#pragma once
// camera_capture_guard.h — pure capture-stall guard logic (CAM-62).
//
// Framework-independent (stdint only) so native host tests compile it
// directly. The P4 backend (camera_p4_backend.h) includes this header and
// feeds it the dequeue heartbeat timestamps.
//
// Context: VIDIOC_DQBUF blocks forever when the sensor stops delivering. A
// stalled 720p mode wedged the HTTP task inside a snapshot and rebooted the
// board via TASK_WDT. The guard has three layers:
//   1. driver DQBUF timeout (backend, when this core's esp_video supports
//      VIDIOC_S_DQBUF_TIMEOUT) bounds every single dequeue;
//   2. fast-fail below skips the dequeue while the pipeline is known-stalled,
//      with a bounded retry so recovery is still detected;
//   3. the loop task re-syncs a stalled session (stop/start capture).
// Stall:  no good frame for P4_CAP_STALL_MS. Retry:  attempt a stalled
// pipeline at most every P4_CAP_RETRY_MS. Restart: session re-sync at most
// every P4_CAP_RESTART_MS (loop task only).

#include <stdint.h>

#define P4_CAP_STALL_MS (4000UL)
#define P4_CAP_RETRY_MS (3000UL)
#define P4_CAP_RESTART_MS (10000UL)

// Idle park (CAM-90): a started session with NO consumer dequeues nothing —
// every capture path is pull-driven (viewer pump, recorder, motion sampling,
// snapshots). CAM-89 fed that state with one discarded frame every 250 ms: it
// took the board from "panics at 68 s / 438 s / 900 s" to runs of 32 and 21
// minutes, but a third run still panicked at ~10 min with min_free_heap at
// 9 128 B. Keeping the ISP/DMA running for nobody is the expensive part, so the
// session is now PARKED (stopCapture) after P4_IDLE_PARK_MS without a
// consumer and resumed on demand by the next capture call.
#define P4_IDLE_PARK_MS (2000UL)

// True = skip the driver dequeue and fail fast: the pipeline delivered no
// frame for stallMs and the last attempt was less than retryMs ago. Returns
// false (attempt the dequeue) when frames are flowing, when nothing was ever
// captured (lastOkMs == 0, must try), or when a recovery retry is due.
// Unsigned subtraction keeps this correct across millis() wrap-around.
static inline int cameraCaptureFastFail(uint32_t lastOkMs, uint32_t lastTryMs,
                                        uint32_t nowMs, uint32_t stallMs,
                                        uint32_t retryMs) {
    if (lastOkMs == 0) return 0;
    if ((uint32_t)(nowMs - lastOkMs) <= stallMs) return 0;
    return ((uint32_t)(nowMs - lastTryMs) < retryMs) ? 1 : 0;
}

// True = the pipeline is stalled right now (no good frame for stallMs).
static inline int cameraCaptureStalled(uint32_t lastOkMs, uint32_t nowMs,
                                       uint32_t stallMs) {
    if (lastOkMs == 0) return 0;
    return ((uint32_t)(nowMs - lastOkMs) > stallMs) ? 1 : 0;
}

// True = a session re-sync is due (loop task only): stalled and no re-sync
// for restartMs.
static inline int cameraCaptureRestartDue(uint32_t lastOkMs, uint32_t lastRestartMs,
                                          uint32_t nowMs, uint32_t stallMs,
                                          uint32_t restartMs) {
    if (!cameraCaptureStalled(lastOkMs, nowMs, stallMs)) return 0;
    if (lastRestartMs != 0 && (uint32_t)(nowMs - lastRestartMs) < restartMs) return 0;
    return 1;
}

// True = the session has been idle long enough to park it (CAM-90): nothing
// consumed a frame for parkMs and the decision was not taken that recently.
// lastOkMs == 0 means "never captured" — never park a session that has not
// proved it can deliver a frame. Unsigned subtraction wraps correctly.
static inline int cameraIdleParkDue(uint32_t lastOkMs, uint32_t lastParkMs,
                                    uint32_t nowMs, uint32_t parkMs) {
    if (lastOkMs == 0) return 0;
    if ((uint32_t)(nowMs - lastOkMs) < parkMs) return 0;
    if (lastParkMs != 0 && (uint32_t)(nowMs - lastParkMs) < parkMs) return 0;
    return 1;
}

// Maximum age of a cached JPEG that may still be served as a photo/video
// frame when no fresh frame arrives in time (slow viewer link, pin-blocked
// pump). Older than this, fail honestly instead of handing out stale data.
#define PHOTO_CACHE_MAX_AGE_MS (5000UL)

// Photo fallback decision (CAM-64): true = serve the latest cached frame
// because it is young enough; false = never published (0) or too old.
// Used when the fresh-frame wait times out while streaming — a seconds-old
// cached JPEG beats "Capture failed" on the photo button.
static inline int cameraCachedPhotoOk(uint32_t publishMs, uint32_t nowMs,
                                      uint32_t maxAgeMs) {
    if (publishMs == 0) return 0;
    return ((uint32_t)(nowMs - publishMs) <= maxAgeMs) ? 1 : 0;
}
