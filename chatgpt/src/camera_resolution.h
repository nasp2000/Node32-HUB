#pragma once
// camera_resolution.h — pure-C "WxH" resolution parsing + OV5647 MIPI mode
// table. Zero Arduino/ESP dependencies so native (doctest) tests can include
// it directly; the P4 firmware uses the same table as camera_p4_backend.cpp.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parse "WIDTHxHEIGHT" (e.g. "1920x1080"). Returns 1 on success, 0 otherwise.
// Rejects empty parts, non-digits, zeros and oversized values (>4096).
int cameraParseResolution(const char* s, uint16_t* w, uint16_t* h);

// OV5647 MIPI sensor modes (must match kP4CsiModes in camera_p4_backend.cpp).
// Fase 3 (Tarefa 5): 2592x1944 leads (high to low) as a STILLS-ONLY mode —
// the sensor register profile for full-res is still HW homework (see the
// kP4SensorFmts entry: null regs = clean refuse + revert), and video paths
// (stream/record/H.264 ≤1920 wide) never use it.
static const uint16_t kCamResModes[][2] = {
    {2592, 1944}, {1920, 1080}, {1280, 960}, {1280, 720}, {800, 1280}, {800, 800}, {800, 640},
};
#define CAM_RES_MODE_COUNT 7

// 1 when WxH is a known sensor mode, 0 otherwise.
static inline int cameraValidResolution(uint16_t w, uint16_t h) {
    for (int i = 0; i < CAM_RES_MODE_COUNT; i++) {
        if (kCamResModes[i][0] == w && kCamResModes[i][1] == h) return 1;
    }
    return 0;
}

// 1 when WxH is a stills-only size (wider than the video/H.264 ceiling of
// 1920): stream/record/H.264 must not run there; Photo (idle direct capture)
// may. 0 for all video-safe sizes.
static inline int cameraP4StillsOnly(uint16_t w, uint16_t h) {
    (void)h;
    return w > 1920 ? 1 : 0;
}

// Resolve the effective P4 MIPI output size. An explicit persisted mode wins;
// otherwise use the legacy framesize-derived size only if it is a real OV5647
// mode. Unsupported enum sizes fall back to safe 800x800.
// Returns 1 when the preferred/enum mode is supported, 0 when fallback was used.
static inline int cameraResolveP4OutputDims(uint16_t override_w, uint16_t override_h,
                                            uint16_t enum_w, uint16_t enum_h,
                                            uint16_t* out_w, uint16_t* out_h) {
    if (!out_w || !out_h) return 0;
    if (override_w && override_h && cameraValidResolution(override_w, override_h)) {
        *out_w = override_w;
        *out_h = override_h;
        return 1;
    }
    if (cameraValidResolution(enum_w, enum_h)) {
        *out_w = enum_w;
        *out_h = enum_h;
        return 1;
    }
    *out_w = 800;
    *out_h = 800;
    return 0;
}

#ifdef __cplusplus
}
#endif
