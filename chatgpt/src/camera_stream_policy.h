#pragma once
// camera_stream_policy.h — pure decisions shared by the live-stream route and
// the capture pump. Header-only so Linux/MinGW native tests compile it
// directly (no Arduino/FreeRTOS dependency).
//
// Two facts drove these constants (both measured on the P4 CAM, v1.120):
//
// 1) TWO MJPEG viewers were not just slow, they killed the board. v1.120 kept
//    ONE shared frame cache and only published while no viewer held a pin
//    (`s_streamRefs == 0`), so a second viewer widened the pinned window until
//    publishing stopped: measured 5-8 frames in 8-10 s (~0.5-1 fps) with
//    freeHeap 37-43 KB / maxAlloc 2-4 KB, ending in a panic
//    (reset_reason=PANIC, uptime restarted). One viewer: 12 fps, heap flat.
//    CAM-76 replaced that cache with a 3-slot ring (camera_stream_slots.h), so
//    publishing no longer waits on a pin, and the cap went back to 2 after the
//    2-viewer measurement on the .18 (both viewers keep streaming, heap flat,
//    no panic). If that ever regresses, dropping this constant to 1 is the
//    whole rollback.
//
// 2) A manual MJPEG recording with no viewer was fed by a separate idle pump
//    (one full capture + JPEG encode inside the recorder tick): 5.75 fps
//    measured, against 12 fps for the same camera through the streaming
//    staging pump. The staging pump reuses one PSRAM buffer and paces once
//    per frame, so the recorder now rides on it and the idle pump only
//    remains as a fallback when nobody else feeds the recorder.

#include <stdint.h>

// Maximum simultaneous MJPEG viewers. Two is back on the table because the
// 3-slot ring (CAM-76) removed the single-publish-slot stall that made the
// second viewer fatal in v1.120; 3 slots cover writer + 2 pinned viewers + 1
// slack. Verified on the .18 with the ring in place (see HISTORIC/CAM).
#define CAM_MAX_MJPEG_CLIENTS 2

// Admission test for a new /cam/stream viewer. `current` is the number of
// viewers already attached (zombie eviction is handled by the caller).
static inline bool cameraStreamAdmit(int current) {
    return current < CAM_MAX_MJPEG_CLIENTS;
}

// Should cameraLoop() run the capture/staging pump this pass?
//   streaming  : a viewer is attached (frames also feed the shared cache)
//   h264Session: an H.264 recording is active (raw tap / encoder pump)
//   mjpegRec   : a manual MJPEG recording is active (no viewer required)
static inline bool cameraPumpNeeded(bool streaming, bool h264Session, bool mjpegRec) {
    return streaming || h264Session || mjpegRec;
}

// Does the pump feed the MJPEG recorder from the frame it just staged? Only
// when the session is MJPEG — H.264 sessions take NALs from the raw tap.
static inline bool cameraPumpFeedsRecorder(bool mjpegRec) {
    return mjpegRec;
}
