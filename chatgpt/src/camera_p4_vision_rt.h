#pragma once
// camera_p4_vision_rt.h — Fase 5 (Tarefa 5): Vision foundation runtime (CAM_P4).
//
// Dataset burst collection (N photos + label → SD/FTP), HW PPA preprocess
// (full-res UYVY → 320x320), and the `.espdl` inference hook in its own task
// (never blocks capture/record). Modes are placeholders until trained models
// land (next task): training always happens off-device.
//
// Guards: full API only for TARGET_BOARD_P4; elsewhere (and on cores without
// the PPA headers) everything stubs out so other packs stay green.
// First-party MIT; Espressif APIs (PPA, ESP-DL later) only *called*.

#include "feature_flags.h"
#include "camera_p4_vision.h"  // pure decisions (modes, clamps, geometry)

#include <Arduino.h>
#include <stdint.h>

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

// --- Burst control --------------------------------------------------------------
// Arm a one-shot collection (copies label). The vision task captures up to n
// JPEGs into /CAM/VISION/<label>/ at ~1/s while the camera is ready. n=0
// disarms. Returns false when busy or invalid.
bool p4VisionBurstStart(uint8_t n, const char* label);
void p4VisionBurstStop();
bool p4VisionBurstActive();
uint8_t p4VisionBurstLeft();

// --- Mode / thresholds (mirrors camera.json, set by the config handler) ---------
void p4VisionSetMode(uint8_t mode);
void p4VisionSetThresh(uint8_t thresh);
uint8_t p4VisionThresh();

// --- Inference hook state (for /api/camera/status) --------------------------------
// Model presence (scanned throttled): /CAM/VISION/model.espdl via FTP/SD.
bool p4VisionModelPresent();
uint32_t p4VisionModelSize();
// RAM counters (foundation: persistence follows with real models).
uint32_t p4VisionOk();
uint32_t p4VisionNok();
void p4VisionResetCounters();
// Last annotated frame path (empty = none yet).
const String& p4VisionLast();
// Preprocess rate x10 (0 = hook idle: mode OFF, no model, or stream busy).
uint32_t p4VisionInferFpsX10();
// PPA client ready (HW preprocess available).
bool p4VisionPpaReady();

// Full-res UYVY → 320x320 UYVY via HW PPA SRM (cover-fit crop + scale).
// out320 must hold 320*320*2 bytes (L1+L2 aligned when in PSRAM).
// Returns false when PPA is unavailable or dims are out of range.
bool p4VisionPpa320(const uint8_t* uyvy, uint16_t w, uint16_t h, uint8_t* out320);

// Ensure the vision task exists (idempotent; called from the camera path).
void p4VisionEnsureTask();

#else  // !TARGET_BOARD_P4

static inline bool p4VisionBurstStart(uint8_t, const char*) { return false; }
static inline void p4VisionBurstStop() {}
static inline bool p4VisionBurstActive() { return false; }
static inline uint8_t p4VisionBurstLeft() { return 0; }
static inline void p4VisionSetMode(uint8_t) {}
static inline void p4VisionSetThresh(uint8_t) {}
static inline uint8_t p4VisionThresh() { return 50; }
static inline bool p4VisionModelPresent() { return false; }
static inline uint32_t p4VisionModelSize() { return 0; }
static inline uint32_t p4VisionOk() { return 0; }
static inline uint32_t p4VisionNok() { return 0; }
static inline void p4VisionResetCounters() {}
static inline const String& p4VisionLast() { static String s; return s; }
static inline uint32_t p4VisionInferFpsX10() { return 0; }
static inline bool p4VisionPpaReady() { return false; }
static inline bool p4VisionPpa320(const uint8_t*, uint16_t, uint16_t, uint8_t*) { return false; }
static inline void p4VisionEnsureTask() {}

#endif  // TARGET_BOARD_P4
