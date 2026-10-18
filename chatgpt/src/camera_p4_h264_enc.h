#pragma once
// camera_p4_h264_enc.h — Fase 1 (Tarefa 5): ESP32-P4 HW H.264 encoder wrapper.
//
// Feeds UYVY frames (the CSI pipeline's native format) to the esp_h264 HW
// encoder (Apache-2.0, linked from the 3.3.12 P4 core) and returns Annex B
// NAL blobs for the `.264` SD recorder. First-party MIT; the Espressif API
// is only *called*, never copied.
//
// Guards: everything compiles only for TARGET_BOARD_P4. Cores without the
// esp_h264 headers (e.g. the 3.3.5 P4 core used by non-camera packs) get a
// stub that reports unavailable, so those builds stay green.

#include "feature_flags.h"
#include <stdint.h>  // uint*_t no ramo stub (compilado em todos os packs)
#include <stddef.h>  // size_t no ramo stub

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

#include <stdint.h>
#include <stddef.h>

// Open the HW encoder for one stream: UYVY pic_type, res, fps, GOP = fps
// (1 IDR/s), rate-control at bitrateKbps (1000–8000, clamped).-Idempotent:
// closes any previous session first. Returns true when encoding can start.
bool p4H264Begin(uint16_t w, uint16_t h, uint8_t fps, uint32_t bitrateKbps);

// Encode one UYVY frame (uyvyLen must be w*h*2 for the geometry passed to
// p4H264Begin; rows are copied with 16-px padding when needed, e.g. 1080p).
// Writes the Annex B blob (SPS/PPS auto-prepended on IDR) to out (PSRAM,
// owned by the wrapper, valid until the next call or p4H264End) and sets
// outLen. Returns false on any failure (session stays open for retry).
bool p4H264EncodeUyvy(const uint8_t* uyvy, size_t uyvyLen,
                      const uint8_t** out, size_t* outLen);

// Close + free the session (idempotent).
void p4H264End();

// True between a successful p4H264Begin() and p4H264End().
bool p4H264Active();

// Short tag of the LAST p4H264Begin() failure step — surfaced in
// /api/camera/status as `h264Diag` so a device failure is diagnosable
// without a serial console. Values: "ok", "res", "hw_new", "alloc_in",
// "alloc_out", "open". Empty string on cores without the encoder.
const char* p4H264BeginDiag();

// False on cores without the esp_h264 HW encoder (stub builds).
bool p4H264HwAvailable();

#else  // !TARGET_BOARD_P4

static inline bool p4H264Begin(uint16_t, uint16_t, uint8_t, uint32_t) { return false; }
static inline bool p4H264EncodeUyvy(const uint8_t*, size_t, const uint8_t**, size_t*) { return false; }
static inline void p4H264End() {}
static inline bool p4H264Active() { return false; }
static inline bool p4H264HwAvailable() { return false; }
static inline const char* p4H264BeginDiag() { return ""; }

#endif  // TARGET_BOARD_P4
