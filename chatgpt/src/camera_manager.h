#pragma once
// camera_manager.h — Public API for the ESP32 camera module.
// Depends on: camera_config.h, camera_pins.h, esp_camera.h
// Called from: setup_impl.h (cameraBegin), loop_impl.h (cameraLoop),
//              camera_handler.cpp (stream, snapshot, status, config endpoints).

#include "feature_flags.h"

#if ENABLE_CAMERA==1

#include <Arduino.h>
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "camera_p4_backend.h"  // P4: ESPVideo backend + fb shim (no esp_camera)
#else
#include "esp_camera.h"
#endif
#include "camera_config.h"

// Initialise the camera hardware and load config from LittleFS.
// Must be called in setup() after LittleFS is mounted.
// Returns true on success.
bool cameraBegin();

// Start camera init in a background task so boot/web remain responsive.
// delayMs allows deferring hardware init until network/services are up.
bool cameraBeginAsync(uint32_t delayMs = 0);

// True while async init task is running.
bool cameraInitInProgress();

// Boot hook (Task 3): called once per boot from loop(). When the persisted
// auto_mode != 0 (photo/video/motion/timelapse), schedules camera init so the
// chosen mode starts automatically after boot settles. No-op when auto_mode
// is off (0) or when the camera is already ready/starting. Does NOT start the
// MJPEG stream (that stays viewer-driven); video/motion/timelapse run their
// continuous engine once the camera is live, photo leaves the camera ready
// for snapshots.
void cameraMaybeAutoStart();

// Called from loop() — reserved for Phase 2 (motion polling, timelapse tick).
void cameraLoop();

// Returns true if esp_camera_init() succeeded.
bool cameraIsReady();

// Current output dimensions for AVI headers/config display. Returns the live
// negotiated P4 geometry when ready, otherwise the effective saved geometry.
void cameraGetOutputDimensions(uint16_t& w, uint16_t& h);

// Returns the max stable FPS for a framesize (network + PSRAM budget).
// Used to clamp user-requested fps before applying or recording.
uint8_t cameraClampFps(framesize_t fs, uint8_t requested);

// Apply new settings to sensor and persist to LittleFS.
bool cameraApplyConfig(const CameraConfig& cfg);

// Returns a reference to the currently active config (read-only).
const CameraConfig& cameraGetConfig();

// Start MJPEG stream session — returns false if already streaming or not ready.
bool cameraStreamStart();

// Signal end of stream session (called when client disconnects).
void cameraStreamStop();

// Returns true if a stream session is currently active.
bool cameraStreamIsActive();

// Semaphore-based frame notification for MJPEG streaming tasks.
// cameraLoop() gives this semaphore after each new frame capture.
// Stream consumers take it to avoid polling.
// Returns the semaphore handle (nullptr if not created).
SemaphoreHandle_t cameraStreamFrameSemaphore();

// Full stop: deactivates camera, stops recording, stops stream.
// Camera sensor stays powered but no frames are captured.
void cameraStop();

// Full start: re-activates camera stream after stop.
bool cameraStart();

// Returns detected sensor model name (e.g. OV2640/OV3660/OV5640/PY260).
// Returns "Not detected" when camera is not ready.
const char* cameraGetDetectedModel();

// Onboard lamp (0–15 brightness, persisted in camera.json).
// No-op when the board has no lamp pin (CAM_PIN_LAMP<0); UI hides the slider.
bool cameraLampAvailable();
uint8_t cameraLampGet();
void cameraLampSet(uint8_t level);
// Apply a brightness level live without persisting (used by motion lamp-auto).
void cameraLampApply(uint8_t level);
// Returns the maximum framesize (framesize_t enum int) the detected sensor
// can output. Returns UXGA before the camera is initialised (safe default
// that matches the historical hard cap), and the PID-based max afterwards
// (e.g. QXGA 2048x1536 for OV3660, 5MP 2592x1944 for OV5640).
int cameraDetectedMaxFrameSize();

// Increment stream frame counter — call once per frame sent.
void cameraStreamFrameSent();

// Returns frames sent in the last second (rolling), 0 if no stream.
uint16_t cameraStreamFps();

// Monotonic MJPEG frames served in the current stream session (0 after stop).
// Diagnostics: proves frames actually flow (curl: read twice, must increase).
uint32_t cameraStreamFramesSentTotal();

// Grab a single JPEG frame from the camera.
// Caller MUST call esp_camera_fb_return(fb) after use to release the DMA buffer.
// Returns nullptr if the camera is not ready.
camera_fb_t* cameraCaptureFrame();

// Single-pump file grab: still/record consumers take frames from the loop
// pre-capture when streaming (waits up to timeoutMs), refuse during motion
// grayscale windows, else one direct capture. PSRAM copy in *outCopy;
// caller MUST free() it. Returns false when no frame could be served.
bool cameraGrabFileFrame(uint8_t** outCopy, size_t* outLen, uint32_t timeoutMs);

// Get the latest pre-captured stream frame (non-blocking, reads from cache).
// Returns pointer to internal buffer (valid until next cameraStreamTick or cameraStreamStop).
// sizeOut receives the JPEG byte count. Returns nullptr if no frame available yet.
// Safe to call from any core — uses mutex.
// NOTE: the pointer is only stable until the next capture. For a zero-copy
// send that spans multiple writes, use cameraStreamFramePin()/Unpin() instead.
const uint8_t* cameraStreamFrame(size_t& sizeOut);

// Pin the current cached frame for zero-copy streaming. While pinned,
// cameraLoop() skips capturing/publishing into the cache, so the returned
// pointer stays valid until the matching cameraStreamFrameUnpin(). Returns
// nullptr when no frame is available. Every successful pin must be matched by
// exactly one unpin. Safe from any core.
const uint8_t* cameraStreamFramePin(size_t& sizeOut);

// Release a pin taken with cameraStreamFramePin().
void cameraStreamFrameUnpin();

// Wait up to timeoutMs for a new stream frame to appear.
// Polls the cache generation counter with yield between checks.
// Returns true when a new frame is available (call cameraStreamFrame() to retrieve it).
// Returns false on timeout or if no stream session is active.
bool cameraStreamWaitFrame(uint32_t timeoutMs);

#endif // ENABLE_CAMERA==1
