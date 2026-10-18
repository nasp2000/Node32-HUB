#pragma once
// camera_p4_h264.h — Fase 1 (Tarefa 5): H.264 raw `.264` helpers for CAM_P4.
//
// Pure header, zero ESP32 deps — fully testable on Linux/MinGW.
// Used by camera_record.cpp / camera_p4_h264_enc.cpp at runtime and by
// tests/native/test_camera_p4_h264 to prove the Annex B / SPS-PPS rules.
//
// DESIGN
// ------
// The ESP32-P4 HW encoder (esp_h264, Apache-2.0, linked from the 3.3.12 P4
// core — headers + libs already in path/link, zero build changes) emits
// NAL units in Annex B form; IDR frames carry SPS/PPS automatically. The
// recorder appends those NALs verbatim to /CAM/.../*.264 files. This header
// owns every decision that can be proven without hardware:
//   - codec select (MJPEG default, H.264 opt-in, P4-only at the call sites)
//   - bitrate clamp (1–8 Mbps) + MB/min estimate for the Rec tab
//   - Annex B start-code scan + NAL type decode (SPS=7, PPS=8, IDR=5)
//   - stream sanity: SPS+PPS present before the first IDR
//   - `.264` filename builder (same RES_FPS tag convention as the AVI path)
//   - HW limits: width <= 1920 (encoder max), 16-px alignment
//   - UYVY -> encoder input repacking (silicon-dependent, see below)
//
// License: first-party MIT. No GPL/ArduCAM content anywhere here.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// --- Codec select -----------------------------------------------------------
// Persisted in camera.json as `rec_codec`. 0 = MJPEG/AVI (default, all
// boards), 1 = H.264 raw .264 (CAM_P4 / TARGET_BOARD_P4 only; other boards
// ignore it and keep MJPEG).
#define CAM_REC_CODEC_MJPEG 0
#define CAM_REC_CODEC_H264  1

static inline uint8_t cameraH264CodecClamp(int v) {
    if (v != CAM_REC_CODEC_H264) return CAM_REC_CODEC_MJPEG;
    return CAM_REC_CODEC_H264;
}

// --- Bitrate ----------------------------------------------------------------
// Persisted in camera.json as `h264_bitrate` (kbps). Range 1000–8000 kbps
// (1–8 Mbps slider on the Rec tab), default 4000 kbps.
#define CAM_H264_BITRATE_MIN_KBPS 1000
#define CAM_H264_BITRATE_MAX_KBPS 8000
#define CAM_H264_BITRATE_DEF_KBPS 4000

static inline uint32_t cameraH264BitrateClampKbps(int v) {
    if (v < CAM_H264_BITRATE_MIN_KBPS) return CAM_H264_BITRATE_MIN_KBPS;
    if (v > CAM_H264_BITRATE_MAX_KBPS) return CAM_H264_BITRATE_MAX_KBPS;
    return (uint32_t)v;
}

// Estimated SD cost in MB per recorded minute at the given bitrate.
// bitrate_kbps * 60 s / 8 bits-per-byte / 1024 KB-per-MB.
static inline float cameraH264MbPerMin(uint32_t bitrateKbps) {
    return (float)bitrateKbps * 60.0f / 8.0f / 1024.0f;
}

// --- HW limits ----------------------------------------------------------------
// HW encoder: max 1920 wide (see esp_h264_types.h), min 80x80. Input needs
// width/height rounded UP to a multiple of 16 (1080p -> 1088).
#define CAM_H264_MAX_W 1920
#define CAM_H264_MAX_H 2032
#define CAM_H264_MIN_W 80
#define CAM_H264_MIN_H 80

static inline uint16_t cameraH264Align16(uint16_t v) {
    return (uint16_t)(((v + 15) / 16) * 16);
}

// UYVY input buffer length for the (possibly padded) encoder geometry.
static inline size_t cameraH264UyvyLen(uint16_t w, uint16_t h) {
    return (size_t)cameraH264Align16(w) * (size_t)cameraH264Align16(h) * 2u;
}

static inline bool cameraH264ResSupported(uint16_t w, uint16_t h) {
    return w >= CAM_H264_MIN_W && h >= CAM_H264_MIN_H &&
           w <= CAM_H264_MAX_W && h <= CAM_H264_MAX_H;
}

// --- Input formats (esp_h264_types.h, hardware-silicon dependent) ------------
// The CSI pipeline hands us UYVY (2 bytes/px). The encoder's accepted raw
// format depends on silicon revision, via ESP_H264_HW_IS_SUPPORTED_PIC_TYPE():
//   rev < 300 : BGR888 / RGB565_LE / VUY / UYVY / O_UYY_E_VYY
//   rev >= 300: O_UYY_E_VYY ONLY
// The SW encoder accepts YUYV and I420 (never UYVY). So one source buffer
// needs two repackings, decided by which encoder was opened.
#define CAM_H264_FMT_HW 0   // O_UYY_E_VYY (YUV420, 1.5 B/px)
#define CAM_H264_FMT_SW 1   // YUYV (YUV422, 2 B/px, byte-swapped UYVY)

// Byte length of the packed input for live geometry w x h at 16-px alignment.
static inline size_t cameraH264InputLen(uint16_t w, uint16_t h, uint8_t fmt) {
    size_t px = (size_t)cameraH264Align16(w) * (size_t)cameraH264Align16(h);
    return (fmt == CAM_H264_FMT_SW) ? px * 2u : px * 3u / 2u;
}

// Repack live UYVY (w x h, 2 B/px, `uyvyLen = aw*ah*2` for aligned aw/ah) into
// the encoder input layout. Rows/cols beyond w/h stay whatever the caller's
// buffer held (zeroed at alloc -> black padding).
//
// O_UYY_E_VYY (esp_h264's "optimized YUV420"): rows are handled in pairs and
// each 2x2 pixel block becomes 6 contiguous bytes [U, Y0, Y1, V, Y2, Y3],
// where (Y0,Y1) is the top row and (Y2,Y3) the bottom row. Width of a
// row-pair run = aw*3 bytes; 6 bytes per macroblock column -> 1.5 B/px.
//
// YUYV (software encoder): same pixels as UYVY with the U/V bytes swapped in
// each pair (Y0,U,Y1,V).
static inline void cameraH264PackUyvy(const uint8_t* src, uint16_t w, uint16_t h,
                                      uint16_t aw, uint8_t* dst, uint8_t fmt) {
    if (!src || !dst || !w || !h || !aw) return;
    if (fmt == CAM_H264_FMT_SW) {
        for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
            dst[i * 2]     = src[i * 2 + 1];  // Y0
            dst[i * 2 + 1] = src[i * 2];      // U
            dst[i * 2 + 2] = src[i * 2 + 3];  // Y1
            dst[i * 2 + 3] = src[i * 2 + 2];  // V
        }
        return;
    }
    // O_UYY_E_VYY: walk 2x2 blocks, writing 6 bytes each.
    for (uint16_t y = 0; y + 1 < h; y += 2) {
        uint8_t* rowPair = dst + (size_t)(y >> 1) * aw * 3u;
        const uint8_t* top = src + (size_t)y * aw * 2u;
        const uint8_t* bot = src + (size_t)(y + 1) * aw * 2u;
        for (uint16_t x = 0; x + 1 < w; x += 2) {
            uint8_t* b = rowPair + (size_t)(x >> 1) * 6u;
            b[0] = top[x * 2];          // U
            b[1] = top[x * 2 + 1];      // Y0 (top, left)
            b[2] = top[x * 2 + 3];      // Y1 (top, right)
            b[3] = bot[x * 2];          // V
            b[4] = bot[x * 2 + 1];      // Y2 (bottom, left)
            b[5] = bot[x * 2 + 3];      // Y3 (bottom, right)
        }
    }
}

// --- Annex B NAL scan ----------------------------------------------------------
// NAL types (ITU-T H.264 Table 7-1, low 5 bits of the first byte after the
// start code): 1 = non-IDR slice, 5 = IDR slice, 6 = SEI, 7 = SPS, 8 = PPS.
#define CAM_H264_NAL_NON_IDR 1
#define CAM_H264_NAL_IDR     5
#define CAM_H264_NAL_SEI     6
#define CAM_H264_NAL_SPS     7
#define CAM_H264_NAL_PPS     8

// Length of the Annex B start code at `p` (0 = not a start code).
// Accepts both 0x000001 (3 bytes) and 0x00000001 (4 bytes).
static inline size_t cameraH264StartCodeLen(const uint8_t* p, size_t left) {
    if (left >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return 4;
    if (left >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) return 3;
    return 0;
}

// NAL type of the unit starting at `nalHdr` (first byte AFTER the start
// code). Returns -1 when no payload byte is available.
static inline int cameraH264NalType(const uint8_t* nalHdr, size_t left) {
    if (!nalHdr || left == 0) return -1;
    return nalHdr[0] & 0x1F;
}

// Scan summary of one encoder output blob (one or more Annex B NALs).
typedef struct {
    uint16_t sps;      // # of SPS NALs (type 7)
    uint16_t pps;      // # of PPS NALs (type 8)
    uint16_t idr;      // # of IDR slices (type 5)
    uint16_t other;    // # of any other NAL (1, 6, ...)
    uint16_t units;    // total NAL units found
} CamH264NalStats;

static inline void cameraH264ScanNals(const uint8_t* data, size_t len,
                                      CamH264NalStats* out) {
    if (out) memset(out, 0, sizeof(*out));
    if (!data || len == 0 || !out) return;
    size_t i = 0;
    while (i < len) {
        size_t sc = cameraH264StartCodeLen(data + i, len - i);
        if (sc == 0) { i++; continue; }
        size_t hdr = i + sc;
        if (hdr >= len) break;  // trailing start code, no payload
        int t = cameraH264NalType(data + hdr, len - hdr);
        out->units++;
        if (t == CAM_H264_NAL_SPS) out->sps++;
        else if (t == CAM_H264_NAL_PPS) out->pps++;
        else if (t == CAM_H264_NAL_IDR) out->idr++;
        else out->other++;
        i = hdr + 1;  // continue past the NAL header byte
    }
}

// True when the blob carries a decodable stream start: at least one SPS and
// one PPS AND at least one IDR, with the parameter sets before the first IDR
// (the HW encoder prepends SPS/PPS to every IDR automatically).
static inline bool cameraH264HasStreamStart(const uint8_t* data, size_t len) {
    if (!data || len == 0) return false;
    bool sps = false, pps = false;
    size_t i = 0;
    while (i < len) {
        size_t sc = cameraH264StartCodeLen(data + i, len - i);
        if (sc == 0) { i++; continue; }
        size_t hdr = i + sc;
        if (hdr >= len) break;
        int t = cameraH264NalType(data + hdr, len - hdr);
        if (t == CAM_H264_NAL_IDR) return sps && pps;
        if (t == CAM_H264_NAL_SPS) sps = true;
        else if (t == CAM_H264_NAL_PPS) pps = true;
        i = hdr + 1;
    }
    return false;
}

// --- `.264` filename ------------------------------------------------------------
// out must be >= 64 bytes. Format: "<base>_<RES>_<FPS>.264"
// (same tag convention as the AVI path, so day folders stay sortable).
static inline void cameraH264FileName(const char* base, const char* resLabel,
                                      uint8_t fps, char out[64]) {
    if (out) snprintf(out, 64, "%s_%s_%u.264", base ? base : "VID",
                      resLabel ? resLabel : "VID", (unsigned)fps);
}

// --- H.264 fps ceiling (Fase 4) ---------------------------------------------------
// The MJPEG path tops at 6 fps (measured: UYVY capture + HW JPEG encode +
// async pump). The H.264 path skips the JPEG encode and the MJPEG pump, so
// the HW encoder can run up to the sensor rate: 30 fps ceiling on MIPI
// (all 5MP-class... video modes offer ≥30 fps at the sensor).
#define CAM_H264_FPS_MAX 30
#define CAM_H264_FPS_MIN 1

static inline uint8_t cameraH264FpsMax(void) { return CAM_H264_FPS_MAX; }

static inline uint8_t cameraH264FpsClamp(int v) {
    if (v < CAM_H264_FPS_MIN) return CAM_H264_FPS_MIN;
    if (v > CAM_H264_FPS_MAX) return CAM_H264_FPS_MAX;
    return (uint8_t)v;
}

// --- H.264 container select (Fase 2) -------------------------------------------
// Persisted in camera.json as `h264_container`. 0 = raw Annex B `.264`
// (Fase 1 default, always available on P4-MIPI), 1 = H.264-in-AVI (`.avi`
// with `H264` fccHandler/biCompression + SPS/PPS extradata in strf).
#define CAM_H264_CONT_RAW 0
#define CAM_H264_CONT_AVI 1

static inline uint8_t cameraH264ContainerClamp(int v) {
    if (v != CAM_H264_CONT_AVI) return CAM_H264_CONT_RAW;
    return CAM_H264_CONT_AVI;
}

// --- H.264-in-AVI layout (Fase 2) ------------------------------------------------
// Same RIFF/hdrl skeleton as the MJPEG path, but strf carries the SPS+PPS
// parameter sets after the 40-byte BITMAPINFOHEADER so players can open the
// file without out-of-band config. The extradata area is RESERVED at a fixed
// 256 bytes: the placeholder header (written at session start, before the
// first IDR exists) and the final header (rewritten on finish) have the same
// size, so frame offsets never move. Layout (all offsets from file start):
//   [0..11]    RIFF header (12)
//   [12..467]  LIST hdrl (8 + 448): hdrl tag [16..19], avih [24..87],
//              LIST strl (8 + 372) at [88..467]: strh [100..163],
//              strf [164..467] = fcc+size [164..171], bmi [172..211],
//              SPS/PPS extradata (reserved 256) [212..467]
//   [468..479] LIST movi header (12)
//   [480..]    '00dc' frame chunks (8 + NAL bytes, WORD-padded)
#define CAM_H264_AVI_FCC "H264"  // fccHandler (strh+112) + biCompression (strf+188)
#define CAM_H264_SPSPPS_MAX  256  // reserved extradata bytes (strf+212 .. strf+467)
#define CAM_H264_AVI_STRF_CONTENT (40 + CAM_H264_SPSPPS_MAX)  // strf chunk content
#define CAM_H264_AVI_STRL_CONTENT (4 + 64 + 8 + CAM_H264_AVI_STRF_CONTENT)
#define CAM_H264_AVI_HDRL_CONTENT (4 + 64 + 8 + CAM_H264_AVI_STRL_CONTENT)
#define CAM_H264_AVI_HDR_BYTES (12 + 8 + CAM_H264_AVI_HDRL_CONTENT + 12)  // 480
#define CAM_H264_AVI_MOVI_OFF CAM_H264_AVI_HDR_BYTES
#define CAM_H264_AVI_EXT_OFF  212  // extradata area start (fixed)

// RIFF size field for the H264-in-AVI header: total file bytes − 8, where
// total = CAM_H264_AVI_HDR_BYTES + moviContentSize.
static inline uint32_t cameraH264AviRiffSize(uint32_t moviContentSize) {
    return (uint32_t)(CAM_H264_AVI_HDR_BYTES - 8) + moviContentSize;
}

// Copy the SPS+PPS parameter sets out of one encoder output blob, in stream
// order, each WITH its 4-byte start code (self-delimiting: the recorder and
// the header builder both re-scan with cameraH264ScanNals()). Writes at most
// outCap bytes; returns bytes written, or 0 when the blob holds no SPS+PPS
// or the cap cannot fit them (caller keeps the session: a later IDR gives
// another chance; the reserved header area is zero-padded meanwhile).
static inline size_t cameraH264ExtractSpsPps(const uint8_t* data, size_t len,
                                             uint8_t* out, size_t outCap) {
    if (!data || len == 0 || !out || outCap == 0) return 0;
    // Two passes: first measure (so a too-small cap fails clean, no halves).
    size_t need = 0;
    size_t i = 0;
    while (i < len) {
        size_t sc = cameraH264StartCodeLen(data + i, len - i);
        if (sc == 0) { i++; continue; }
        size_t hdr = i + sc;
        if (hdr >= len) break;
        int t = cameraH264NalType(data + hdr, len - hdr);
        // NAL payload runs to the next start code (or blob end).
        size_t j = hdr + 1;
        while (j < len && cameraH264StartCodeLen(data + j, len - j) == 0) j++;
        if (t == CAM_H264_NAL_SPS || t == CAM_H264_NAL_PPS) need += 4 + (j - hdr);
        i = j;
    }
    if (need == 0 || need > outCap) return 0;
    size_t w = 0;
    i = 0;
    while (i < len) {
        size_t sc = cameraH264StartCodeLen(data + i, len - i);
        if (sc == 0) { i++; continue; }
        size_t hdr = i + sc;
        if (hdr >= len) break;
        int t = cameraH264NalType(data + hdr, len - hdr);
        size_t j = hdr + 1;
        while (j < len && cameraH264StartCodeLen(data + j, len - j) == 0) j++;
        if (t == CAM_H264_NAL_SPS || t == CAM_H264_NAL_PPS) {
            out[w++] = 0; out[w++] = 0; out[w++] = 0; out[w++] = 1;  // Annex B
            size_t n = j - hdr;
            memcpy(out + w, data + hdr, n);
            w += n;
        }
        i = j;
    }
    return w;
}
