// camera_p4_h264_enc.cpp — Fase 1 (Tarefa 5): P4 HW H.264 encoder wrapper.
//
// Pipeline: CSI → ISP → UYVY → esp_h264 HW encoder → Annex B NALs → SD .264.
// MJPEG path (UYVY → HW JPEG → AVI) is untouched; this TU only owns the H.264
// session used while a `.264` recording is active.
//
// License: first-party MIT. esp_h264 is Apache-2.0 (Espressif): used via the
// public C API linked from the 3.3.12 P4 core — zero vendored/copied code,
// no GPL/ArduCAM content.

#include "camera_p4_h264_enc.h"

#include "feature_flags.h"
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

#include <Arduino.h>
#include <string.h>
#include <stdio.h>   // snprintf for the diag tag
#include "camera_p4_h264.h"  // clamps, geometry, Annex B helpers
#include "psram_helpers.h"   // ps_malloc()
#include "log.h"             // LogLevel (INFO/WARN/ERROR)

extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static inline void p4H264Log(const String &msg, int level = INFO) {
    sendVirtualMessage(msg, level, false, true);
}

// Only the esp32p4cam core (Arduino 3.3.12) ships the esp_h264 headers+libs.
// Other P4 cores (e.g. 3.3.5 for non-camera packs) compile this TU to the
// stub below so their builds stay green. NOTE: plain -D guard (not
// __has_include) for the TARGET_BOARD_P4 shell — the inner include switch
// uses __has_include, which is compiler-evaluated and needs no LDF help
// because esp_h264 is a prebuilt core lib, not a PlatformIO library.
#if __has_include("esp_h264_enc_single_hw.h") && __has_include("esp_h264_alloc.h")
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_enc_single_sw.h"
// NOTE: esp_h264_alloc.h ships WITHOUT extern "C" guards (unlike the
// interface headers), so include it as C — otherwise C++ mangles
// esp_h264_aligned_calloc and the link fails against the prebuilt lib.
extern "C" {
#include "esp_h264_alloc.h"
}
#define P4_HAVE_H264_HW 1
#else
#define P4_HAVE_H264_HW 0
#endif

#if P4_HAVE_H264_HW

static char s_beginDiag[24] = "";  // last Begin() failure (step or step:err_no)
static void p4H264SetDiag(const char* tag) {
    snprintf(s_beginDiag, sizeof(s_beginDiag), "%s", tag ? tag : "");
}
static esp_h264_enc_handle_t s_enc = nullptr;
static uint8_t* s_inBuf = nullptr;    // 16-B aligned UYVY staging (padded)
static uint32_t s_inCap = 0;
static uint8_t* s_outBuf = nullptr;   // 16-B aligned Annex B output slot
static uint32_t s_outCap = 0;
static uint16_t s_w = 0, s_h = 0;         // requested geometry
static uint16_t s_aw = 0, s_ah = 0;       // 16-px aligned geometry
static uint8_t  s_fmt = CAM_H264_FMT_HW;  // input layout of the opened encoder
static uint32_t s_frames = 0;
static bool s_open = false;
static bool s_isSw = false;   // software encoder session (format differs)

bool p4H264HwAvailable() { return true; }
bool p4H264Active() { return s_open && s_enc != nullptr; }

const char* p4H264BeginDiag() { return s_beginDiag; }

bool p4H264Begin(uint16_t w, uint16_t h, uint8_t fps, uint32_t bitrateKbps) {
    p4H264End();  // idempotent restart
    s_isSw = false;
    if (!cameraH264ResSupported(w, h)) {
        p4H264SetDiag("res");
        p4H264Log(String("[CAM-P4/H264] unsupported res ") + w + "x" + h, WARN);
        return false;
    }
    if (fps == 0) fps = 6;
    if (fps > 30) fps = 30;
    uint32_t kbps = cameraH264BitrateClampKbps((int)bitrateKbps);

    // The CSI pipeline gives us UYVY, but esp_h264's accepted raw format is
    // silicon-dependent (ESP_H264_HW_IS_SUPPORTED_PIC_TYPE): P4 rev >= v3.0
    // takes O_UYY_E_VYY only and rejects UYVY with ESP_H264_ERR_ARG, which is
    // exactly what this board did. Ask for the YUV420 layout and repack.
    esp_h264_enc_cfg_hw_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    cfg.gop = fps;                          // 1 IDR/s (SPS/PPS auto-prepended)
    cfg.fps = fps;
    cfg.res.width = w;
    cfg.res.height = h;
    cfg.rc.bitrate = kbps * 1000UL;
    cfg.rc.qp_min = 10;
    cfg.rc.qp_max = 51;

    s_enc = nullptr;  // the API only writes it on success
    esp_h264_err_t hwErr = esp_h264_enc_hw_new(&cfg, &s_enc);
    if (hwErr != ESP_H264_ERR_OK || !s_enc) {
        s_enc = nullptr;
        // Software fallback: the SW encoder shares the same handle interface
        // (new/open/process/close), so an absent/refused HW block costs one
        // extra call instead of losing H.264 entirely. Slower per frame, but
        // the byte-rate win over MJPEG is the same.
        // The SW encoder takes YUYV (or I420) — never UYVY — so its cfg has
        // to be rebuilt for the swapped layout.
        esp_h264_enc_cfg_t swCfg = cfg;
        swCfg.pic_type = ESP_H264_RAW_FMT_YUYV;
        esp_h264_err_t swErr = esp_h264_enc_sw_new(&swCfg, &s_enc);
        if (swErr == ESP_H264_ERR_OK && s_enc) {
            s_isSw = true;
            p4H264SetDiag("sw");   // honest: runs, just not in hardware
            p4H264Log(String("[CAM-P4/H264] hw_new refused (err ") + (int)hwErr +
                     ") — using SW encoder", WARN);
        } else {
            char d[24];
            snprintf(d, sizeof(d), "hw_new:%d/sw:%d", (int)hwErr, (int)swErr);
            p4H264SetDiag(d);
            p4H264Log(String("[CAM-P4/H264] enc_new failed hw=") + (int)hwErr +
                      " sw=" + (int)swErr, ERROR);
            return false;
        }
    }
    // 16-px alignment (the encoder pads internally per the API note, but the
    // input buffer we hand over must already hold the aligned frame).
    s_w = w; s_h = h;
    s_aw = cameraH264Align16(w);
    s_ah = cameraH264Align16(h);
    s_fmt = s_isSw ? CAM_H264_FMT_SW : CAM_H264_FMT_HW;
    size_t inLen = cameraH264InputLen(w, h, s_fmt);
    uint32_t got = 0;
    s_inBuf = (uint8_t*)esp_h264_aligned_calloc(16, 1, (uint32_t)inLen, &got,
                                                ESP_H264_MEM_SPIRAM);
    if (!s_inBuf || got < inLen) {
        p4H264SetDiag("alloc_in");
        p4H264Log("[CAM-P4/H264] input alloc failed", ERROR);
        esp_h264_enc_del(s_enc);
        s_enc = nullptr; s_inBuf = nullptr;
        return false;
    }
    s_inCap = got;
    // Output slot: ONE compressed frame per process() call, so it only needs a
    // fraction of the raw input. Sizing it like the input (3 MB at 1080p) is
    // what made 1080p fail with alloc_out while 800x640 passed — the input
    // already holds the 1.5 B/px frame and two of those do not fit together.
    // A quarter is still far above any single IDR at these bitrates.
    size_t outLen = inLen / 4;
    if (outLen < (256UL * 1024UL)) outLen = 256UL * 1024UL;
    if (outLen > (2UL * 1024UL * 1024UL)) outLen = 2UL * 1024UL * 1024UL;
    s_outBuf = (uint8_t*)esp_h264_aligned_calloc(16, 1, (uint32_t)outLen, &got,
                                                 ESP_H264_MEM_SPIRAM);
    if (!s_outBuf || got < outLen) {
        p4H264SetDiag("alloc_out");
        p4H264Log(String("[CAM-P4/H264] output alloc failed (") + (uint32_t)outLen + "B)", ERROR);
        esp_h264_enc_del(s_enc);
        s_enc = nullptr;
        esp_h264_free(s_inBuf); s_inBuf = nullptr;
        return false;
    }
    s_outCap = got;
    if (esp_h264_enc_open(s_enc) != ESP_H264_ERR_OK) {
        p4H264SetDiag("open");
        p4H264Log("[CAM-P4/H264] enc open failed", ERROR);
        esp_h264_enc_del(s_enc);
        s_enc = nullptr;
        esp_h264_free(s_inBuf); s_inBuf = nullptr;
        esp_h264_free(s_outBuf); s_outBuf = nullptr;
        return false;
    }
    s_open = true;
    s_frames = 0;
    p4H264SetDiag("ok");
    p4H264Log(String(s_isSw ? "[CAM-P4/H264] SW encoder open " : "[CAM-P4/H264] HW encoder open ") +
              w + "x" + h + " @" + fps + "fps " + kbps + "kbps", INFO);
    return true;
}

bool p4H264EncodeUyvy(const uint8_t* uyvy, size_t uyvyLen,
                      const uint8_t** out, size_t* outLen) {
    if (out) *out = nullptr;
    if (outLen) *outLen = 0;
    if (!s_open || !s_enc || !uyvy) return false;
    size_t expect = (size_t)s_w * s_h * 2u;
    if (uyvyLen < expect) return false;
    // Repack the live UYVY frame into the layout the opened encoder asked for
    // (O_UYY_E_VYY in hardware, YUYV in software). 16-px padding is free: the
    // calloc'd buffer zeroes whatever the repack leaves past the live edge.
    cameraH264PackUyvy(uyvy, s_w, s_h, s_aw, s_inBuf, s_fmt);
    esp_h264_enc_in_frame_t in;
    memset(&in, 0, sizeof(in));
    in.raw_data.buffer = s_inBuf;
    in.raw_data.len = s_inCap;
    in.pts = s_frames * 1000UL;  // ms time base
    esp_h264_enc_out_frame_t encOut;
    memset(&encOut, 0, sizeof(encOut));
    encOut.raw_data.buffer = s_outBuf;
    encOut.raw_data.len = s_outCap;
    esp_h264_err_t e = esp_h264_enc_process(s_enc, &in, &encOut);
    if (e != ESP_H264_ERR_OK || encOut.length == 0) {
        return false;  // session stays open: caller drops the frame, retries
    }
    s_frames++;
    if (out) *out = s_outBuf;
    if (outLen) *outLen = encOut.length;
    return true;
}

void p4H264End() {
    if (s_enc) {
        if (s_open) esp_h264_enc_close(s_enc);
        esp_h264_enc_del(s_enc);
        s_enc = nullptr;
    }
    s_open = false;
    if (s_inBuf) { esp_h264_free(s_inBuf); s_inBuf = nullptr; }
    if (s_outBuf) { esp_h264_free(s_outBuf); s_outBuf = nullptr; }
    s_inCap = 0; s_outCap = 0; s_frames = 0;
    s_fmt = CAM_H264_FMT_HW; s_isSw = false;
}

#else  // !P4_HAVE_H264_HW — core without esp_h264 (e.g. 3.3.5 P4 packs)

bool p4H264HwAvailable() { return false; }
bool p4H264Active() { return false; }
const char* p4H264BeginDiag() { return ""; }
bool p4H264Begin(uint16_t, uint16_t, uint8_t, uint32_t) { return false; }
bool p4H264EncodeUyvy(const uint8_t*, size_t, const uint8_t**, size_t*) { return false; }
void p4H264End() {}

#endif  // P4_HAVE_H264_HW

#endif  // TARGET_BOARD_P4
