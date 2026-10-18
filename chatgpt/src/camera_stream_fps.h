#pragma once
// camera_stream_fps.h — pure rolling-FPS window for the MJPEG status pill.
//
// Counts NEW published frames per wall second (duplicate re-sends, emitted
// to keep the TCP ACK clock ticking while the cache is pinned, are not new
// content and must not inflate the number). Rounds to nearest instead of
// truncating (2.9 -> 3, not 2), so the pill tracks what the stream shows.
//
// Header-only, zero ESP32 deps — testable on Linux/MinGW. Used by
// camera_manager.cpp (cameraStreamFrameSent) and
// tests/native/test_camera_stream_fps.

#include <stdint.h>

struct StreamFpsWindow {
    uint32_t startMs;  // millis() of the first frame in the open window (0 = none open)
    uint16_t count;    // frames seen in the open window
    uint16_t last;     // last closed per-second value (what the pill shows)
};

static inline void streamFpsWindowReset(StreamFpsWindow* w) {
    if (!w) return;
    w->startMs = 0;
    w->count = 0;
    w->last = 0;
}

// Feed one newly-sent frame at nowMs. Returns true exactly when a >=1 s
// window closed (w->last updated). Open windows younger than 1 s keep the
// previous value, so a 5 s status poll always reads a fresh-ish number.
static inline bool streamFpsWindowTick(StreamFpsWindow* w, uint32_t nowMs) {
    if (!w) return false;
    if (w->startMs == 0) {
        w->startMs = nowMs;
        w->count = 1;
        return false;
    }
    w->count++;
    uint32_t elapsed = nowMs - w->startMs;
    if (elapsed >= 1000) {
        w->last = (uint16_t)(((uint32_t)w->count * 1000UL + elapsed / 2) / elapsed);
        w->count = 0;
        w->startMs = nowMs;
        return true;
    }
    return false;
}
