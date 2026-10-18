// camera_p4_vision.cpp — Fase 5 (Tarefa 5): Vision foundation runtime (CAM_P4).
//
// Own FreeRTOS task (never blocks capture/record):
//   - dataset burst: N JPEGs + label → /CAM/VISION/<label>/ (SD/FTP pickup)
//   - model presence scan: /CAM/VISION/model.espdl (FTP upload path)
//   - HW PPA preprocess probe + 320x320 SRM helper (hook for .espdl models)
// Modes are placeholders; training stays off-device. First-party MIT.

#include "camera_p4_vision_rt.h"

#include "feature_flags.h"
// Vision runtime needs the camera stack: camera_manager.h / camera_record.h
// only declare cameraIsReady() / cameraStreamIsActive() / cameraRecordActive()
// when ENABLE_CAMERA==1 (same guard as camera_manager.cpp / camera_handler.cpp).
// On camera-off P4 packs (P4_CNC, P4_USB_SERVER) this TU compiles to empty;
// there are no callers there, so nothing dangles.
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1 && ENABLE_CAMERA == 1

#include <string.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "camera_p4_backend.h"   // raw/JPEG capture, geometry
#include "camera_manager.h"      // cameraIsReady(), cameraStreamIsActive()
#include "camera_config.h"       // CameraConfig (mode/thresh mirror source)
#include "camera_record.h"       // cameraRecordActive() (hook yields to record)
#include "sdcard.h"              // sdcardShouldUseSD(), SD_MMC
#include "runtime_storage.h"     // acquireSDAccess()/releaseSDAccess()
#include "psram_helpers.h"       // ps_malloc()
#include "log.h"

extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static inline void p4VisLog(const String &msg, int level = INFO) {
    sendVirtualMessage(msg, level, false, true);
}

#if __has_include("driver/ppa.h")
#include "driver/ppa.h"
#define P4_HAVE_PPA 1
#else
#define P4_HAVE_PPA 0
#endif

// ---- Module state ---------------------------------------------------------------
static TaskHandle_t s_task = nullptr;
static uint8_t  s_mode = CAM_VISION_OFF;
static uint8_t  s_thresh = CAM_VISION_THRESH_DEF;
// Burst plan (armed by p4VisionBurstStart, executed by the task at ~1/s).
static uint8_t  s_burstTotal = 0;
static uint8_t  s_burstLeft = 0;
static char     s_burstLabel[CAM_VISION_LABEL_MAX + 1] = {0};
// Counters + last annotated (RAM foundation).
static uint32_t s_ok = 0, s_nok = 0;
static String   s_last;
// Model scan (throttled).
static bool     s_model = false;
static uint32_t s_modelSize = 0;
static uint32_t s_lastScanMs = 0;
// Preprocess rate (EMA of successful PPA runs; 0 = hook idle).
static uint32_t s_inferX10 = 0;
static uint32_t s_lastPpaMs = 0;
static uint32_t s_skipLogMs = 0;
#if P4_HAVE_PPA
static ppa_client_handle_t s_ppa = nullptr;
static bool s_ppaOk = false;
#endif

bool p4VisionPpaReady() {
#if P4_HAVE_PPA
    return s_ppaOk;
#else
    return false;
#endif
}

bool p4VisionPpa320(const uint8_t* uyvy, uint16_t w, uint16_t h, uint8_t* out320) {
    if (!uyvy || !out320 || w == 0 || h == 0) return false;
#if !P4_HAVE_PPA
    (void)w; (void)h;
    return false;
#else
    if (!s_ppaOk || !s_ppa) return false;
    if (w > 1920 || w < 80 || h < 80) return false;  // video-class only
    uint16_t cw, ch, ox, oy;
    cameraVisionCropFor320(w, h, &cw, &ch, &ox, &oy);
    ppa_srm_oper_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.in.buffer = uyvy;
    cfg.in.pic_w = w;
    cfg.in.pic_h = h;
    cfg.in.block_w = cw;
    cfg.in.block_h = ch;
    cfg.in.block_offset_x = ox;
    cfg.in.block_offset_y = oy;
    cfg.in.srm_cm = PPA_SRM_COLOR_MODE_YUV422_UYVY;
    cfg.out.buffer = out320;
    cfg.out.buffer_size = (uint32_t)CAM_VISION_IN_W * CAM_VISION_IN_H * 2u;
    cfg.out.pic_w = CAM_VISION_IN_W;
    cfg.out.pic_h = CAM_VISION_IN_H;
    cfg.out.srm_cm = PPA_SRM_COLOR_MODE_YUV422_UYVY;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x = (float)cw / CAM_VISION_IN_W;
    cfg.scale_y = (float)ch / CAM_VISION_IN_H;
    cfg.mirror_x = false;
    cfg.mirror_y = false;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;
    return ppa_do_scale_rotate_mirror(s_ppa, &cfg) == ESP_OK;
#endif
}

// ---- Burst -------------------------------------------------------------------------
bool p4VisionBurstStart(uint8_t n, const char* label) {
    n = cameraVisionBurstClamp(n);
    if (n == 0 || !label || s_burstLeft > 0) return false;
    char tmp[CAM_VISION_LABEL_MAX + 1];
    strncpy(tmp, label, CAM_VISION_LABEL_MAX);
    tmp[CAM_VISION_LABEL_MAX] = '\0';
    if (!cameraVisionSanitizeLabel(tmp)) return false;
    strncpy(s_burstLabel, tmp, sizeof(s_burstLabel));
    s_burstTotal = n;
    s_burstLeft = n;
    p4VisionEnsureTask();
    p4VisLog(String("[CAM-P4/VIS] burst ") + n + "x '" + s_burstLabel + "'", INFO);
    return true;
}

void p4VisionBurstStop() { s_burstLeft = 0; s_burstTotal = 0; }
bool p4VisionBurstActive() { return s_burstLeft > 0; }
uint8_t p4VisionBurstLeft() { return s_burstLeft; }

void p4VisionSetMode(uint8_t mode) {
    s_mode = cameraVisionModeClamp(mode);
    if (s_mode != CAM_VISION_OFF) p4VisionEnsureTask();
}
void p4VisionSetThresh(uint8_t thresh) { s_thresh = cameraVisionThreshClamp(thresh); }
uint8_t p4VisionThresh() { return s_thresh; }
uint32_t p4VisionOk() { return s_ok; }
uint32_t p4VisionNok() { return s_nok; }
void p4VisionResetCounters() { s_ok = 0; s_nok = 0; s_last = ""; }
bool p4VisionModelPresent() { return s_model; }
uint32_t p4VisionModelSize() { return s_modelSize; }
const String& p4VisionLast() { return s_last; }
uint32_t p4VisionInferFpsX10() { return s_inferX10; }

// ---- Task ------------------------------------------------------------------------------
// One JPEG capture → /CAM/VISION/<label>/<label>_NNN.jpg (burst), or one
// UYVY→320x320 PPA pass (hook probe). Never touches an active stream/record
// session's pacing: burst/preprocess yield whenever the pump is busy.
struct VisCapCtx { uint8_t* dst; size_t off; size_t cap; bool ovf; };
static void visWriter(const uint8_t* data, size_t len, void* c) {
    VisCapCtx* s = (VisCapCtx*)c;
    if (!s || !s->dst) return;
    if (s->off + len > s->cap) { s->ovf = true; return; }
    memcpy(s->dst + s->off, data, len);
    s->off += len;
}

static void p4VisionTask(void*) {
    // PPA client (SRM): one registration, reused by every preprocess pass.
#if P4_HAVE_PPA
    {
        ppa_client_config_t cc;
        memset(&cc, 0, sizeof(cc));
        cc.oper_type = PPA_OPERATION_SRM;
        if (ppa_register_client(&cc, &s_ppa) == ESP_OK && s_ppa) {
            s_ppaOk = true;
            p4VisLog("[CAM-P4/VIS] PPA SRM client ready (320x320 HW preprocess)", INFO);
        } else {
            p4VisLog("[CAM-P4/VIS] PPA client failed (preprocess off)", WARN);
        }
    }
#else
    p4VisLog("[CAM-P4/VIS] no PPA headers in this core (preprocess off)", WARN);
#endif
    static uint8_t* s_jpg = nullptr;   // burst JPEG staging (PSRAM, 1 MB)
    static uint8_t* s_raw = nullptr;   // preprocess UYVY staging (PSRAM, ≤4 MB)
    static size_t s_rawCap = 0;
    static uint8_t* s_out = nullptr;   // 320x320 UYVY (aligned PSRAM)
    uint32_t lastTick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = millis();
        if (now - lastTick < 1000) continue;
        lastTick = now;
        if (!cameraIsReady()) { s_inferX10 = 0; continue; }
        // Throttled model scan (5 s): presence + size for the Vision tab.
        if (now - s_lastScanMs > 5000) {
            s_lastScanMs = now;
#if ENABLE_SDCARD==1
            if (sdcardShouldUseSD() && acquireSDAccess("vision-scan", 500)) {
                File f = SD_MMC.open(CAM_VISION_MODEL_PATH, FILE_READ);
                s_model = (bool)f;
                s_modelSize = f ? (uint32_t)f.size() : 0;
                if (f) f.close();
                releaseSDAccess();
            }
#endif
        }
        // Dataset burst first (it owns this second).
        if (s_burstLeft > 0) {
            if (!s_jpg) s_jpg = (uint8_t*)ps_malloc(1024UL * 1024UL);
            if (s_jpg) {
                VisCapCtx ctx{s_jpg, 0, 1024UL * 1024UL, false};
                size_t n = cameraP4Capture(visWriter, &ctx, 1500);
                if (n > 0 && !ctx.ovf && ctx.off > 0) {
                    uint8_t idx = (uint8_t)(s_burstTotal - s_burstLeft + 1);
                    char name[48];
                    cameraVisionBurstName(s_burstLabel, idx, name);
                    String dir = String(CAM_VISION_ROOT) + "/" + s_burstLabel;
                    String path = dir + "/" + name;
#if ENABLE_SDCARD==1
                    if (sdcardShouldUseSD() && acquireSDAccess("vision-burst", 2000)) {
                        if (!SD_MMC.exists(CAM_VISION_ROOT)) SD_MMC.mkdir(CAM_VISION_ROOT);
                        if (!SD_MMC.exists(dir.c_str())) SD_MMC.mkdir(dir.c_str());
                        File f = SD_MMC.open(path.c_str(), FILE_WRITE);
                        if (f) {
                            f.write(ctx.dst, ctx.off);
                            f.close();
                            s_last = path;
                            s_burstLeft--;
                            if (s_burstLeft == 0) {
                                p4VisLog(String("[CAM-P4/VIS] burst done: ") + s_burstTotal
                                         + "x '" + s_burstLabel + "'", INFO);
                            }
                        } else {
                            p4VisLog("[CAM-P4/VIS] burst SD open failed", WARN);
                        }
                        releaseSDAccess();
                    }
#endif
                }
            }
            continue;
        }
        // Preprocess hook probe: mode armed + pump idle. A model is NOT
        // required: without one this pass still proves the PPA path at ~1 Hz
        // and feeds the pill, so the Vision tab shows a live preprocess rate
        // (the pill labels it "preprocess" while no model is loaded) instead
        // of a dead 0. The model run itself is the next task.
        bool armed = (s_mode != CAM_VISION_OFF);
        if (!armed) { s_inferX10 = 0; continue; }
        if (cameraStreamIsActive() || cameraRecordActive()) {
            s_inferX10 = 0;
            if (now - s_skipLogMs > 10000) {
                s_skipLogMs = now;
                p4VisLog("[CAM-P4/VIS] hook yields (stream/record busy)", INFO);
            }
            continue;
        }
        uint16_t aw = 0, ah = 0;
        cameraP4ActualDims(aw, ah);
        if (aw == 0 || aw > 1920) continue;  // video-class only (5MP is stills-only)
        size_t need = (size_t)aw * ah * 2u;
        if (!s_raw || s_rawCap < need) {
            if (s_raw) free(s_raw);
            s_raw = (uint8_t*)ps_malloc(need);
            s_rawCap = s_raw ? need : 0;
        }
        if (!s_out) {
            s_out = (uint8_t*)heap_caps_aligned_alloc(64, (size_t)CAM_VISION_IN_W * CAM_VISION_IN_H * 2u,
                                                      MALLOC_CAP_SPIRAM);
        }
        if (!s_raw || !s_out) continue;
        VisCapCtx rctx{s_raw, 0, s_rawCap, false};
        cameraP4CaptureWithRaw(nullptr, nullptr, visWriter, &rctx, 1500);
        if (rctx.ovf || rctx.off == 0) continue;
        uint32_t t0 = millis();
        if (p4VisionPpa320(rctx.dst, aw, ah, s_out)) {
            uint32_t dt = millis() - t0;
            uint32_t fpsX10 = dt > 0 ? 10000UL / dt : 100;
            if (fpsX10 > 100) fpsX10 = 100;
            s_inferX10 = s_inferX10 ? (s_inferX10 * 3 + fpsX10) / 4 : fpsX10;  // EMA
        }
    }
}

void p4VisionEnsureTask() {
    if (s_task) return;
    xTaskCreatePinnedToCore(p4VisionTask, "p4vision", 4096, nullptr, 2, &s_task, 1);
    if (!s_task) p4VisLog("[CAM-P4/VIS] task create failed", ERROR);
}

#endif  // TARGET_BOARD_P4
