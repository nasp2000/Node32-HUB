#pragma once
// camera_p4_vision.h — Fase 5 (Tarefa 5): Vision foundation for CAM_P4.
//
// Pure header, zero ESP32 deps — fully testable on Linux/MinGW.
// Decision layer for: dataset burst collection, 320x320 PPA preprocess
// geometry, inference-hook state, OK/NOK counters and thresholds.
//
// DESIGN
// ------
// Training always happens off-device; the camera only COLLECTS its own
// training set (dataset burst: N photos + short label → SD/FTP) and runs a
// hook where a trained `.espdl` model will plug in (separate task, never
// blocking capture/record). Modes are placeholders until real models land
// (next task): Off / Count / Defect. First-party MIT.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// --- Modes ---------------------------------------------------------------------
// Persisted in camera.json as `vision_mode`. P4-only (other boards ignore).
#define CAM_VISION_OFF    0
#define CAM_VISION_COUNT  1
#define CAM_VISION_DEFECT 2

static inline uint8_t cameraVisionModeClamp(int v) {
    if (v == CAM_VISION_COUNT) return CAM_VISION_COUNT;
    if (v == CAM_VISION_DEFECT) return CAM_VISION_DEFECT;
    return CAM_VISION_OFF;
}

// --- Thresholds ------------------------------------------------------------------
// Persisted as `vision_thresh` (0–100, default 50): score at/above which a
// detection counts OK (Count) / NOK (Defect). Unit-free until models land.
#define CAM_VISION_THRESH_MIN 0
#define CAM_VISION_THRESH_MAX 100
#define CAM_VISION_THRESH_DEF 50

static inline uint8_t cameraVisionThreshClamp(int v) {
    if (v < CAM_VISION_THRESH_MIN) return CAM_VISION_THRESH_MIN;
    if (v > CAM_VISION_THRESH_MAX) return CAM_VISION_THRESH_MAX;
    return (uint8_t)v;
}

// --- Dataset burst ----------------------------------------------------------------
// One-shot collection: `vision_burst_n` photos (0=off, 1–50) tagged with
// `vision_burst_label` (1–16 chars: letters/digits/_/-) into
// /CAM/VISION/<label>/ for SD/FTP pickup. Label sanitising keeps FAT-safe
// names (anything else becomes '_').
#define CAM_VISION_BURST_MIN 0
#define CAM_VISION_BURST_MAX 50
#define CAM_VISION_LABEL_MAX 16
#define CAM_VISION_ROOT "/CAM/VISION"
#define CAM_VISION_MODEL_PATH "/CAM/VISION/model.espdl"

static inline uint8_t cameraVisionBurstClamp(int v) {
    if (v < CAM_VISION_BURST_MIN) return CAM_VISION_BURST_MIN;
    if (v > CAM_VISION_BURST_MAX) return CAM_VISION_BURST_MAX;
    return (uint8_t)v;
}

// Sanitize a burst label in place (FAT-safe). Returns 1 when non-empty.
static inline int cameraVisionSanitizeLabel(char label[CAM_VISION_LABEL_MAX + 1]) {
    if (!label) return 0;
    label[CAM_VISION_LABEL_MAX] = '\0';
    size_t w = 0;
    for (size_t r = 0; label[r] && w < CAM_VISION_LABEL_MAX; r++) {
        char c = label[r];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        label[w++] = ok ? c : '_';
    }
    label[w] = '\0';
    return w > 0 ? 1 : 0;
}

// Burst item filename into out (>=48 bytes): "<label>_<idx03>.jpg".
static inline void cameraVisionBurstName(const char* label, uint8_t idx, char out[48]) {
    if (out) snprintf(out, 48, "%s_%03u.jpg", label ? label : "burst", (unsigned)idx);
}

// --- Preprocess geometry -------------------------------------------------------------
// Inference input is always 320x320 (HW PPA resize from the live frame).
// Cover-fit scale + center crop offsets for a WxH source (pure math; the PPA
// call itself lives in camera_p4_vision.cpp).
#define CAM_VISION_IN_W 320
#define CAM_VISION_IN_H 320

static inline void cameraVisionCropFor320(uint16_t sw, uint16_t sh,
                                          uint16_t* cropW, uint16_t* cropH,
                                          uint16_t* offX, uint16_t* offY) {
    if (!cropW || !cropH || !offX || !offY || sw == 0 || sh == 0) return;
    // cover-fit: scale so the smaller side hits 320, crop the overflow
    if ((uint32_t)sw * CAM_VISION_IN_H >= (uint32_t)sh * CAM_VISION_IN_W) {
        *cropH = sh; *cropW = (uint16_t)((uint32_t)sh * CAM_VISION_IN_W / CAM_VISION_IN_H);
    } else {
        *cropW = sw; *cropH = (uint16_t)((uint32_t)sw * CAM_VISION_IN_H / CAM_VISION_IN_W);
    }
    if (*cropW == 0) *cropW = sw;
    if (*cropH == 0) *cropH = sh;
    *offX = (sw > *cropW) ? (uint16_t)((sw - *cropW) / 2) : 0;
    *offY = (sh > *cropH) ? (uint16_t)((sh - *cropH) / 2) : 0;
}

// --- Counters --------------------------------------------------------------------
// RAM-resident OK/NOK totals + last annotated path (foundation: persistence
// is a follow-up once real models produce meaningful counts).
typedef struct {
    uint32_t ok;
    uint32_t nok;
    uint32_t inferFpsX10;  // inference fps * 10 (0 = hook idle, no model)
} CamVisionCounters;
