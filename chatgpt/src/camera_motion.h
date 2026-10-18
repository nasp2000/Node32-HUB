#pragma once
// camera_motion.h — Frame-diff motion detection for the ESP32 camera module.
//
// Algorithm overview:
//   1. Captured JPEG is fed through cameraMotionUpdate().
//   2. The frame is decoded to an internal 40x30 grayscale thumbnail (PSRAM or heap).
//      Downscaling is done with a simple averaging box filter over the JPEG YCbCr luma.
//      NOTE: we operate on the raw JPEG byte stream using a lightweight 1-pass
//      average of every (W/40 * H/30) pixel block — no JPEG library needed at runtime
//      because esp_camera already provides a raw fb in PIXFORMAT_JPEG, but we obtain
//      a separate PIXFORMAT_GRAYSCALE frame only when the motion task fires (see .cpp).
//   3. Absolute-difference sum between current and previous thumbnail is computed.
//      If the sum >= (sensitivity * MOTION_THUMB_PIXELS) the frame is flagged as
//      "changed".
//   4. Adaptive sampling: when idle (no motion), check 1 frame in every
//      MOTION_IDLE_SKIP+1 captures; when motion active, check every other frame.
//   5. A "motion event" starts when MOTION_CONFIRM_FRAMES consecutive changed frames
//      are detected, and ends after MOTION_STOP_FRAMES consecutive unchanged frames.
//   6. ROI (region of interest): optional pixel sub-rectangle applied to the thumbnail.
//      If roi_w==0 the entire 40×30 grid is used.
//
// Visualisation:
//   cameraMotionDiffJpeg() returns a small JPEG (40×30 scaled up or raw) where
//   changed pixels are painted red on a grayscale background.  Served at
//   /cam/motion/diff.jpg for the browser overlay.
//
// Thread safety: all functions must be called from the same task (loop task).

#include "feature_flags.h"

#if ENABLE_CAMERA==1

#include <Arduino.h>

// Thumbnail dimensions — 40×30 gives 1200 pixels, uses 2×1200 = 2.4 KB RAM.
#define MOTION_THUMB_W   40
#define MOTION_THUMB_H   30
#define MOTION_THUMB_PIX (MOTION_THUMB_W * MOTION_THUMB_H)

// Motion action when a motion event is confirmed (Task 5).
enum MotionAction : uint8_t {
    MOTION_ACTION_RECORD_VIDEO = 0,  // record AVI while motion active (current behaviour)
    MOTION_ACTION_SNAP_PHOTO   = 1,  // capture a single JPEG to SD per event
    MOTION_ACTION_EMAIL_ALERT  = 2   // send an email alert (no attachment)
};

// Configuration struct for the motion detector.
// Fields map 1-to-1 to CameraConfig motion sub-fields.
struct MotionConfig {
    bool     enabled;          // Master on/off
    uint8_t  sensitivity;      // 1..100; diff threshold per pixel (default 20)
    uint8_t  confirm_frames;   // Consecutive changed frames needed to trigger (default 3)
    uint8_t  stop_frames;      // Consecutive unchanged frames to end event (default 10)
    uint8_t  pre_record_secs;  // Not used in Phase 3 (reserved for circular buffer)
    uint8_t  post_record_secs; // Extra seconds to keep recording after motion stops (default 5)
    uint8_t  action;           // MotionAction enum (default MOTION_ACTION_RECORD_VIDEO)
    // ROI in thumbnail coordinates (0..39 / 0..29); all zero = full frame
    uint8_t  roi_x, roi_y, roi_w, roi_h;
    uint16_t cooldown_secs;    // Refractory period after an event: new events are
                               // neither counted nor acted upon (default 5, 0=off)
    bool     lamp_auto;        // Full lamp during the event, restore after (default false)
    uint32_t total_events;     // Persistent event counter (product-counter mode)
};

inline MotionConfig motionConfigDefaults() {
    MotionConfig m;
    m.enabled          = false;
    m.sensitivity      = 20;
    m.confirm_frames   = 3;
    m.stop_frames      = 10;
    m.pre_record_secs  = 0;
    m.post_record_secs = 5;
    m.action           = MOTION_ACTION_RECORD_VIDEO;
    m.roi_x = m.roi_y = m.roi_w = m.roi_h = 0;
    m.cooldown_secs    = 5;
    m.lamp_auto        = false;
    m.total_events     = 0;
    return m;
}

// Initialise the motion detector: allocates grayscale thumbnail buffers.
// Must be called once after cameraBegin().
void cameraMotionBegin(const MotionConfig& cfg);

// Reconfigure without restarting the detector.
void cameraMotionSetConfig(const MotionConfig& cfg);

// Returns the active config.
const MotionConfig& cameraMotionGetConfig();

// Feed one raw grayscale (PIXFORMAT_GRAYSCALE) frame buffer into the detector.
// Computes diff against the previous thumbnail.
// Must NOT be called with a JPEG buffer — caller must obtain a GRAYSCALE frame.
// Returns true if the frame was actually evaluated (adaptive sampling may skip it).
bool cameraMotionUpdate(const uint8_t* grayData, uint16_t w, uint16_t h);

// True if a motion event is currently active (enough consecutive changed frames seen).
bool cameraMotionActive();

// Total counted events (persistent product counter). Cooldown-suppressed
// triggers do not increment it.
uint32_t cameraMotionEventCount();

// Reset the persistent event counter to zero (also persists immediately).
void cameraMotionResetCount();

// Diff score of the last evaluated frame (0..100 scaled, 100 = all pixels changed).
// Useful for the web UI indicator bar.
uint8_t cameraMotionScore();

// Returns the raw diff thumbnail (MOTION_THUMB_PIX bytes, grayscale delta per pixel).
// Buffer is valid until the next cameraMotionUpdate() call.
const uint8_t* cameraMotionDiffBuf();

// Build a small visualisation JPEG: changed pixels in red, others in gray.
// Output written to buf (caller-provided, must be >= outBufLen bytes).
// Returns actual JPEG size, or 0 on failure.
// outBufLen: recommended >= 4096 for a 40x30 JPEG.
size_t cameraMotionBuildDiffJpeg(uint8_t* buf, size_t outBufLen);

// Reset state (clear thumbnails and event counters).
void cameraMotionReset();

// True while a grayscale sampling window is open (sensor temporarily
// switched to PIXFORMAT_GRAYSCALE for the motion thumbnail). Direct
// JPEG captures taken in this window would return a 96x96 grayscale frame:
// photo/snapshot/record paths must wait or skip while this is true.
// Safe to call from any task (loop + HTTP).
bool cameraMotionSamplingActive();

// Called from cameraLoop() — drives the adaptive capture cycle and
// triggers cameraRecordStart/cameraRecordFinish when motion events occur.
// nowMs: current millis().
void cameraMotionTick(uint32_t nowMs);

#endif // ENABLE_CAMERA==1
