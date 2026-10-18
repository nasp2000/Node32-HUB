// camera_manager.cpp — ESP32 camera initialisation and frame management.

#include "camera_manager.h"

#include "feature_flags.h"
#if ENABLE_CAMERA==1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "camera_pins.h"    // validates that CAM_PIN_* are defined by board_pins.h
#include "camera_config.h"
#include "camera_sensor_map.h"
#include "camera_resolution.h"
#include "camera_capture_guard.h"  // stall/photo-fallback pure logic (CAM-62/64)
#include "camera_stream_fps.h"  // rolling-FPS window (pure, rounded)
#include "direct_log.h"
#include "feature_flags.h"
#if ENABLE_SDCARD==1
#include "camera_record.h"  // AVI recording + timelapse tick (Phase 2)
#endif
#include "camera_motion.h"  // Motion detection (Phase 3)
#include "log.h"            // LogLevel (INFO/WARN/ERROR)
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "camera_p4_backend.h"    // Fase 1: raw UYVY tap (WithRaw) + geometry
#include "camera_p4_h264_enc.h"   // Fase 1: HW H.264 encode (stub off-esp32p4cam)
#include "camera_p4_vision_rt.h"  // Fase 5: vision task mirror (stub off-P4)
#endif

// Camera status/error messages MUST reach the Web Serial Monitor (AGENTS
// logging rule): camLog() only hits Serial+Telnet, which left P4
// camera failures invisible remotely. camLog() goes Serial + Telnet + monitor
// and bypasses the identical-message dedup so repeats still show.
extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static void camLog(const String &msg, int level = INFO) {
    sendVirtualMessage(msg, level, false, true);
}

// Module-private state — all static to avoid polluting global scope.
static bool         s_ready        = false;
static bool         s_streamActive = false;
static CameraConfig s_config       = {};
static bool         s_configLoaded = false;
static volatile bool s_initInProgress = false;
static TaskHandle_t  s_initTaskHandle = nullptr;

// Init/stop serialisation. cameraBeginAsync() runs on the loop task
// (boot auto-start) AND the HTTP task (page Start button) on different cores:
// a plain volatile check-then-set allowed TWO CamInit tasks into
// esp_camera_init() concurrently, smashing the driver queues (cam_task
// LoadProhibited in xQueueGenericSend). A spinlock makes check+set atomic.
// Held only for flag flips — never across driver calls.
static portMUX_TYPE s_camMux = portMUX_INITIALIZER_UNLOCKED;

// Forward declaration (defined below cameraBegin; also part of the public API).
void cameraArmAutoMode();
void cameraLampApply(uint8_t level);

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
// Fase 1 (Tarefa 5): file-scope staging ctx so the raw UYVY writer can be a
// plain function pointer (CameraP4ChunkWriter takes no captures).
struct P4LoopStageCtx { uint8_t* dst; size_t off; size_t cap; bool ovf; };
static void cameraP4StageWriter(const uint8_t* data, size_t len, void* c) {
    P4LoopStageCtx* s = (P4LoopStageCtx*)c;
    if (!s || !s->dst) return;
    if (s->off + len > s->cap) { s->ovf = true; return; }
    memcpy(s->dst + s->off, data, len);
    s->off += len;
}
#endif

// Stream frame cache — decouples camera capture (core 1 / loop) from TCP send (core 0 / AsyncTCP).
// cameraLoop() pre-captures frames into s_streamCache; the MJPEG callback reads from it.
// This prevents esp_camera_fb_get() and memcpy from blocking the AsyncTCP task on core 0,
// which would otherwise stall all other HTTP/TCP processing ("everything becomes inaccessible").
static uint8_t*        s_streamCache      = nullptr;
static size_t          s_streamCacheSize  = 0;
static size_t          s_streamCacheLen   = 0;
static volatile bool   s_streamCacheValid = false;
static SemaphoreHandle_t s_streamCacheMux = nullptr;
// Last publish timestamp (millis): photo fallback serves the cached frame
// only while it is younger than PHOTO_CACHE_MAX_AGE_MS (CAM-64).
static volatile uint32_t s_cachePublishMs = 0;

// Zero-copy reader pins. While > 0 a stream consumer is sending straight from
// the shared cache, so cameraLoop() must NOT overwrite it: each reader bumps
// this on cameraStreamFramePin() and drops it on cameraStreamFrameUnpin().
// This removes the old per-viewer PSRAM frame copy (one full JPEG memcpy per
// viewer per frame).
static volatile int    s_streamRefs       = 0;

static uint32_t s_lastStreamCaptureMs = 0;  // throttle pre-capture to configured fps
static uint8_t*  s_p4RawSolo = nullptr;       // H.264-only UYVY stage (no stream)
static size_t    s_p4RawSoloCap = 0;
static uint32_t  s_lastH264PumpMs = 0;

// Binary semaphore — posted by cameraLoop() after each cache update.
// Stream consumers (MJPEG callback, RTSP, etc.) take it to sleep until a new frame arrives.
static SemaphoreHandle_t s_frameSema = nullptr;

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
// P4 idle-capture slot (perf): one persistent PSRAM buffer replaces the two
// ps_malloc (staging + exact copy) and the extra full-frame memcpy the idle
// path used to do per frame — photo, MJPEG recording while idle and the
// motion best-frame grab all come through cameraCaptureFrame(). Single
// occupancy: while the slot is busy a caller falls back to a one-off buffer,
// so a concurrent photo never waits on the recorder's frame.
struct P4CapSlot {
    uint8_t* buf    = nullptr;
    size_t   cap    = 0;
    bool     inUse = false;
};
static P4CapSlot        s_capSlot;
static SemaphoreHandle_t s_capSlotMux = nullptr;

// Recycle a pooled (slot) frame: free the occupancy and the small fb struct.
// Never frees the slot buffer — that stays for the next frame.
static void p4CapSlotReleaseFrame(camera_fb_t* fb) {
    if (s_capSlotMux && xSemaphoreTake(s_capSlotMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (fb && fb->buf == s_capSlot.buf) s_capSlot.inUse = false;
        xSemaphoreGive(s_capSlotMux);
    }
    if (fb) free(fb);
}
#endif // TARGET_BOARD_P4

// Rolling FPS counter — incremented by cameraStreamFrameSent(), read by cameraStreamFps().
// Uses a generation counter to avoid double-counting when the same cached frame is sent twice.
static StreamFpsWindow s_fpsWin = {0, 0, 0};
static uint32_t       s_fpsLastGen      = 0;
static uint32_t       s_cacheGen        = 0;
static uint32_t       s_framesSentTotal = 0;  // monotonic MJPEG frames served

static void ensureConfigLoaded() {
    if (s_configLoaded) return;
    cameraConfigLoad(s_config);
    s_configLoaded = true;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // Fase 5: boot mirror — persisted vision mode arms the task hook.
    p4VisionSetMode(s_config.vision_mode);
    p4VisionSetThresh(s_config.vision_thresh);
#endif
}

static framesize_t clampFrameSize(framesize_t fs) {
    // Values below the smallest supported frame collapse to VGA (safe floor).
    if ((int)fs < (int)FRAMESIZE_96X96) return FRAMESIZE_VGA;
    // Values above the detected sensor's max are clamped DOWN to that max
    // (e.g. picking 5MP on an OV3660 gives QXGA, not a jarring VGA drop).
    int maxFs = cameraDetectedMaxFrameSize();
    if ((int)fs > maxFs) return (framesize_t)maxFs;
    return fs;
}

static uint8_t computeFbCount(framesize_t fs) {
    if (!psramFound()) return 1;

    // Spend PSRAM on DMA buffers to keep internal heap free and the
    // GRAB_LATEST pipeline smooth (MJPEG2SD runs 4). Heaps stay roomy for
    // WiFi/TLS/SMTP stacks, which is where the speed comes from.
    uint32_t freePs = ESP.getFreePsram();
    const uint32_t MB4 = 4UL * 1024UL * 1024UL;
    const uint32_t MB6 = 6UL * 1024UL * 1024UL;
    if ((int)fs >= (int)FRAMESIZE_XGA) return (freePs > MB4) ? 2 : 1;
    if ((int)fs >= (int)FRAMESIZE_VGA) return 3;
    return (freePs > MB6) ? 4 : 3;
}

static uint8_t recommendedMaxFps(framesize_t fs) {
    if ((int)fs >= (int)FRAMESIZE_XGA) return 10;
    if (fs == FRAMESIZE_SVGA) return 15;
    if (fs == FRAMESIZE_VGA) return 20;
    return 25;
}

uint8_t cameraClampFps(framesize_t fs, uint8_t requested) {
    if (requested < 1) return 1;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4: the stored framesize enum is the legacy boot mode, not the live
    // geometry (a 720p session keeps framesize=6), so deriving the ceiling
    // from it caps the wrong thing — a stale FHD enum silently capped 15 fps
    // to 10. The P4 backend owns its own per-source ceiling.
    uint8_t p4Max = cameraP4FpsMax();
    return (requested > p4Max) ? p4Max : requested;
#else
    uint8_t maxFps = recommendedMaxFps(clampFrameSize(fs));
    return (requested > maxFps) ? maxFps : requested;
#endif
}

static uint8_t computeJpegQuality(framesize_t fs, uint8_t requestedQuality) {
    // For large frames, force a slightly lighter JPEG quality to reduce frame size and pressure.
    if ((int)fs >= (int)FRAMESIZE_XGA && requestedQuality < 18) return 18;
    return requestedQuality;
}

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
// setSensorFrameSizeWithFallback: S3-only (needs sensor_t). P4 resolution
// switches go through cameraP4SetResolution() in applyConfigToSensor().
#else
static bool setSensorFrameSizeWithFallback(sensor_t* s, framesize_t requested, framesize_t& applied) {
    if (!s) return false;
    const framesize_t candidates[] = {
        clampFrameSize(requested),
        FRAMESIZE_SXGA,
        FRAMESIZE_XGA,
        FRAMESIZE_SVGA,
        FRAMESIZE_VGA,
        FRAMESIZE_QVGA
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            if (candidates[j] == candidates[i]) { duplicate = true; break; }
        }
        if (duplicate) continue;
        if (s->set_framesize(s, candidates[i]) == 0) {
            applied = candidates[i];
            return true;
        }
    }
    return false;
}
#endif // !TARGET_BOARD_P4

// Apply the in-memory config struct to the sensor registers.
// Safe to call after esp_camera_init(); no-op if sensor unavailable.
static bool applyConfigToSensor(const CameraConfig& cfg, framesize_t* appliedFrameSize = nullptr) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4 (ESPVideo backend): resolution + quality + flip are live-settable.
    // Picture knobs without an ESPVideo equivalent are persisted no-ops.
    framesize_t appliedFs = clampFrameSize(cfg.framesize);
    uint16_t w = 640, h = 480;
    if (cfg.source == 0 && cfg.p4_res_w > 0 && cfg.p4_res_h > 0) {
        // Explicit HD override from the resolution selector (persisted).
        w = cfg.p4_res_w; h = cfg.p4_res_h;
    } else {
        cameraSensorFrameSizeDims((uint8_t)appliedFs, w, h);
    }
    if (!cameraP4SetResolution(w, h)) return false;
    if (appliedFrameSize) *appliedFrameSize = appliedFs;
    cameraP4SetQuality(computeJpegQuality(appliedFs, cfg.quality));
    cameraP4SetFlip(cfg.hmirror, cfg.vflip);
    return true;
#else
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    framesize_t appliedFs = clampFrameSize(cfg.framesize);
    if (!setSensorFrameSizeWithFallback(s, cfg.framesize, appliedFs)) {
        return false;
    }
    if (appliedFrameSize) *appliedFrameSize = appliedFs;
    s->set_quality(s, computeJpegQuality(appliedFs, cfg.quality));
    s->set_brightness(s, cfg.brightness);
    s->set_contrast(s, cfg.contrast);
    s->set_saturation(s, cfg.saturation);
    s->set_hmirror(s, cfg.hmirror ? 1 : 0);
    s->set_vflip(s, cfg.vflip ? 1 : 0);
    s->set_gainceiling(s, (gainceiling_t)cfg.gainceiling);
    s->set_whitebal(s, cfg.awb ? 1 : 0);
    s->set_exposure_ctrl(s, cfg.aec ? 1 : 0);
    s->set_gain_ctrl(s, cfg.agc ? 1 : 0);
    s->set_lenc(s, cfg.lenc ? 1 : 0);
    s->set_special_effect(s, cfg.special_effect);
    s->set_wb_mode(s, cfg.wb_mode);
    s->set_aec_value(s, cfg.aec_value);
    s->set_aec2(s, cfg.aec2 ? 1 : 0);
    s->set_agc_gain(s, cfg.agc_gain);
    s->set_ae_level(s, cfg.ae_level);
    return true;
#endif // !TARGET_BOARD_P4
}

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
// P4 init: ESPVideo MIPI-CSI pipeline (OV5647) + shared pump plumbing.
// Mirrors the S3 post-init tail (mutex/sema, motion begin, auto-mode arm)
// without any esp_camera calls.
static bool cameraBeginP4() {
    ensureConfigLoaded();

    framesize_t requestedFs = clampFrameSize(s_config.framesize);
    uint8_t maxFps = recommendedMaxFps(requestedFs);
    if (s_config.fps > maxFps) s_config.fps = maxFps;

    uint16_t w = 640, h = 480;
    cameraSensorFrameSizeDims((uint8_t)requestedFs, w, h);
    uint8_t quality = computeJpegQuality(requestedFs, s_config.quality);
    // Source fallback: a USB webcam that fails to start (unplugged mid-boot,
    // descriptor quirk, heap) must not leave the board with NO camera when the
    // other source is attached and working. Try the requested source, then the
    // other one, and keep whichever came up — the persisted source is
    // corrected so the next boot (and the UI selector) match reality.
    if (!cameraP4Begin(w, h, quality, s_config.source)) {
        uint8_t other = cameraP4OtherSource(s_config.source);
        if (cameraP4Begin(w, h, quality, other)) {
            camLog(String("[CAM] source ") + s_config.source + " failed to start — fell back to " +
                   other + " (" + cameraP4Model() + ")", WARN);
            s_config.source = other;
        } else {
            camLog("[CAM] P4 pipeline init failed on both sources", ERROR);
            return false;
        }
    }
    s_config.framesize = requestedFs;
    cameraConfigSave(s_config);

    cameraP4SetFlip(s_config.hmirror, s_config.vflip);

    if (!s_streamCacheMux) {
        s_streamCacheMux = xSemaphoreCreateMutex();
    }
    if (!s_frameSema) {
        s_frameSema = xSemaphoreCreateBinary();
    }
    if (!s_capSlotMux) {
        s_capSlotMux = xSemaphoreCreateMutex();
    }
    s_streamCacheValid = false;

    s_ready = true;
    // Stored HD override (Camera panel resolution selector): apply after the
    // probe so only ACKed modes switch; anything else stays at boot geometry.
    if (s_config.source == 0 && s_config.p4_res_w > 0 && s_config.p4_res_h > 0) {
        uint16_t aw = 0, ah = 0;
        cameraP4ActualDims(aw, ah);
        if (aw != s_config.p4_res_w || ah != s_config.p4_res_h) {
            if (!cameraP4SetResolution(s_config.p4_res_w, s_config.p4_res_h)) {
                camLog("[CAM] stored P4 resolution unreachable, clearing override", WARN);
                // Self-healing: don't retry a dead mode on every boot.
                s_config.p4_res_w = 0; s_config.p4_res_h = 0;
                cameraConfigSave(s_config);
            }
        }
    }
    {
        char okmsg[48];
        snprintf(okmsg, sizeof(okmsg), "[CAM] Camera init OK (P4/%s)", cameraP4Model());
        camLog(okmsg);
    }
    char diag[80];
    snprintf(diag, sizeof(diag), "[CAM] heap=%uKB psram=%uKB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
    camLog(diag);
    cameraMotionBegin(s_config.motion);
    cameraArmAutoMode();
    return true;
}
#endif

bool cameraBegin() {
    if (s_ready) return true;

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    return cameraBeginP4();
#else
    ensureConfigLoaded();

    camera_config_t hw = {};
    hw.ledc_channel = LEDC_CHANNEL_0;
    hw.ledc_timer   = LEDC_TIMER_0;
    hw.pin_d0       = CAM_PIN_D0;
    hw.pin_d1       = CAM_PIN_D1;
    hw.pin_d2       = CAM_PIN_D2;
    hw.pin_d3       = CAM_PIN_D3;
    hw.pin_d4       = CAM_PIN_D4;
    hw.pin_d5       = CAM_PIN_D5;
    hw.pin_d6       = CAM_PIN_D6;
    hw.pin_d7       = CAM_PIN_D7;
    hw.pin_xclk     = CAM_PIN_XCLK;
    hw.pin_pclk     = CAM_PIN_PCLK;
    hw.pin_vsync    = CAM_PIN_VSYNC;
    hw.pin_href     = CAM_PIN_HREF;
    hw.pin_sccb_sda = CAM_PIN_SIOD;
    hw.pin_sccb_scl = CAM_PIN_SIOC;
    hw.pin_pwdn     = CAM_PIN_PWDN;
    hw.pin_reset    = CAM_PIN_RESET;
    // S3 freeze workaround: 10 MHz XCLK halves the LCD_CAM/GDMA burst rate
    // into PSRAM (silent dual-core freeze correlated with camera DMA activity
    // while Wi-Fi STA runs; 80 MHz / 10 MHz is an integer divisor).
    hw.xclk_freq_hz = 10000000;
    framesize_t requestedFs = clampFrameSize(s_config.framesize);

    // Low-PSRAM safe mode: cap resolution and FPS to keep camera init stable.
    if (psramFound()) {
        uint32_t freePs = ESP.getFreePsram();
        if (freePs < (3UL * 1024UL * 1024UL)) {
            if ((int)requestedFs > (int)FRAMESIZE_VGA) requestedFs = FRAMESIZE_VGA;
            if (s_config.fps > 15) s_config.fps = 15;
        }
    }

    uint8_t maxFps = recommendedMaxFps(requestedFs);
    if (s_config.fps > maxFps) s_config.fps = maxFps;

    if (requestedFs != s_config.framesize) {
        s_config.framesize = requestedFs;
        cameraConfigSave(s_config);
    }
    hw.frame_size   = requestedFs;
    hw.pixel_format = PIXFORMAT_JPEG;
    // CAMERA_GRAB_LATEST: always returns the most recent frame, minimising latency.
    // Also ensures fb_get() is non-blocking when fb_count >= 2.
    hw.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        // Allocate frame buffers in PSRAM to preserve internal heap.
        hw.fb_location  = CAMERA_FB_IN_PSRAM;
        hw.jpeg_quality = computeJpegQuality(requestedFs, s_config.quality);
        hw.fb_count = computeFbCount(requestedFs);
    } else {
        hw.fb_location  = CAMERA_FB_IN_DRAM;
        hw.jpeg_quality = computeJpegQuality(requestedFs, s_config.quality);
        hw.fb_count     = 1;
        camLog("[CAM] No PSRAM: single frame buffer, streaming may be slow");
    }

    // Single-attempt init (MJPEG2SD model): buffers are sized for the
    // requested framesize, so init is one deterministic pass — not a
    // 6-resolution probe chain (each failed esp_camera_init costs seconds).
    // On hard failure, exactly one retry at the VGA safe floor follows.
    framesize_t tryList[] = {
        requestedFs,
        FRAMESIZE_VGA
    };

    esp_err_t err = ESP_FAIL;
    framesize_t usedFs = requestedFs;
    for (size_t i = 0; i < sizeof(tryList)/sizeof(tryList[0]); i++) {
        framesize_t fs = tryList[i];
        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            if (tryList[j] == fs) { duplicate = true; break; }
        }
        if (duplicate) continue;

        hw.frame_size = fs;
        hw.fb_count = computeFbCount(fs);
        hw.jpeg_quality = computeJpegQuality(fs, s_config.quality);
        err = esp_camera_init(&hw);
        if (err == ESP_OK) {
            usedFs = fs;
            break;
        }

        esp_camera_deinit();
        // Settle: let the driver task and queues fully release before the
        // next attempt — immediate re-init on a half-torn-down driver is the
        // same cam_task queue smash as a double init.
        vTaskDelay(pdMS_TO_TICKS(200));

        char msg[96];
        snprintf(msg, sizeof(msg), "[CAM] Init fail fs=%d err=0x%x — trying lower resolution", (int)fs, (int)err);
        camLog(msg);
    }

    if (err != ESP_OK) {
        char msg[72];
        snprintf(msg, sizeof(msg), "[CAM] Init failed after fallback chain (err=0x%x)", (int)err);
        camLog(msg);
        return false;
    }

    if (usedFs != requestedFs) {
        s_config.framesize = usedFs;
        cameraConfigSave(s_config);
        char msg[88];
        snprintf(msg, sizeof(msg), "[CAM] Fallback applied: framesize %d -> %d", (int)requestedFs, (int)usedFs);
        camLog(msg);
    }

    framesize_t sensorApplied = usedFs;
    if (applyConfigToSensor(s_config, &sensorApplied) && sensorApplied != s_config.framesize) {
        s_config.framesize = sensorApplied;
        cameraConfigSave(s_config);
    }

    // Optional: onboard lamp (PWM brightness 0–15). LEDC channel 1 keeps
    // channel 0 free for the camera XCLK.
    #if defined(CAM_PIN_LAMP) && CAM_PIN_LAMP >= 0
    ledcSetup(1, 5000, 8);
    ledcAttachPin(CAM_PIN_LAMP, 1);
    #endif
    cameraLampApply(s_config.lamp);

    // Create stream cache mutex for cross-core frame sharing.
    if (!s_streamCacheMux) {
        s_streamCacheMux = xSemaphoreCreateMutex();
    }
    // Create binary semaphore for frame-ready notification.
    // Binary, initially empty (0). Posted by cameraLoop() after each cache update.
    if (!s_frameSema) {
        s_frameSema = xSemaphoreCreateBinary();
    }
    s_streamCacheValid = false;

    s_ready = true;
    camLog("[CAM] Camera init OK");
    sensor_t* det = esp_camera_sensor_get();
    if (det) {
        char spid[96];
        snprintf(spid, sizeof(spid), "[CAM] Sensor PID=0x%04X model=%s maxFs=%d",
                 (int)det->id.PID, cameraGetDetectedModel(), cameraDetectedMaxFrameSize());
        camLog(spid);
    }
    char diag[80];
    snprintf(diag, sizeof(diag), "[CAM] heap=%uKB psram=%uKB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
    camLog(diag);
    // Initialise motion detector with config loaded above.
    cameraMotionBegin(s_config.motion);
    // Honor auto-mode (Task 3): arm the selected continuous mode so it runs
    // once the camera is live. Does NOT auto-start the camera — the Start
    // button / scheduled trigger still drives init.
    cameraArmAutoMode();
    return true;
#endif // !TARGET_BOARD_P4
}

// cameraArmAutoMode — derives and enables the continuous subsystem selected by
// auto_mode. Called after a successful init and after config changes that touch
// auto_mode. Only fills in an unset default; never overrides an explicit user
// value. Camera init itself is driven either explicitly (Start button/task) or
// by the cameraMaybeAutoStart() boot hook when auto_mode != 0.
void cameraArmAutoMode() {
    const uint8_t mode = s_config.auto_mode;
    if (mode == 0) return;

    bool changed = false;
    switch (mode) {
        case 1:  // photo — no continuous engine; armed for manual snapshot
            break;
        case 2:  // video (dashcam)
            if (s_config.dashcam_mins == 0) {
                s_config.dashcam_mins = 5;  // arm a sensible rotation default
                changed = true;
            }
            break;
        case 3:  // motion
            if (!s_config.motion.enabled) {
                s_config.motion.enabled = true;
                cameraMotionSetConfig(s_config.motion);
                changed = true;
            }
            break;
        case 4:  // timelapse
            if (s_config.timelapse_secs == 0) {
                s_config.timelapse_secs = 10;  // arm a sensible interval default
                changed = true;
            }
            break;
        default:
            break;
    }

    if (changed) {
        cameraConfigSave(s_config);
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "[CAM] auto-mode=%u armed", (unsigned)mode);
    camLog(msg);
}

// ---------------------------------------------------------------------------
// cameraMaybeAutoStart — boot hook for Task 3 (auto-start modes)
//
// The historical CAM-7 policy (no auto-start from the web page) is preserved:
// the page never auto-starts on load. This hook is the *explicitly configured*
// boot auto-start the user opts into by selecting auto_mode != 0. It only
// schedules `cameraBeginAsync()` (which in turn calls cameraArmAutoMode() at
// the end of a successful init); the continuous engine for video/motion/
// timelapse then runs from cameraLoop() once the camera is live.
// ---------------------------------------------------------------------------
static bool s_autoStartScheduled = false;

void cameraMaybeAutoStart() {
    if (s_autoStartScheduled || s_ready || s_initInProgress) return;

    // Wait for boot to settle (LittleFS mount, web server, network) before the
    // first decision so the persisted config is reliably readable. Until then,
    // return without marking the flag so loop() re-checks on the next pass.
    if (millis() < 3000UL) return;
    s_autoStartScheduled = true;

    ensureConfigLoaded();
    if (s_config.auto_mode == 0) {
        camLog("[CAM] auto-start off");
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "[CAM] auto-start mode=%u — scheduling init",
             (unsigned)s_config.auto_mode);
    camLog(msg);
    cameraBeginAsync(1500);
}

static void cameraInitTask(void* param) {
    uint32_t delayMs = (uint32_t)(uintptr_t)param;
    if (delayMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }

    camLog("[CAM] Async init start");
    bool ok = cameraBegin();
    if (!ok) {
        camLog("[CAM] Async init failed", ERROR);
    }

    taskENTER_CRITICAL(&s_camMux);
    s_initInProgress = false;
    s_initTaskHandle = nullptr;
    taskEXIT_CRITICAL(&s_camMux);
    vTaskDelete(nullptr);
}

bool cameraBeginAsync(uint32_t delayMs) {
    // Atomic check+set: a loop-task boot auto-start racing a page Start
    // (different cores) must never create two CamInit tasks.
    taskENTER_CRITICAL(&s_camMux);
    bool busy = s_ready || s_initInProgress;
    if (!busy) s_initInProgress = true;
    taskEXIT_CRITICAL(&s_camMux);
    if (busy) return true;
    BaseType_t rc = xTaskCreatePinnedToCore(
        cameraInitTask,
        "CamInit",
        6144,
        (void*)(uintptr_t)delayMs,
        1,
        &s_initTaskHandle,
        0);

    if (rc != pdPASS) {
        taskENTER_CRITICAL(&s_camMux);
        s_initInProgress = false;
        s_initTaskHandle = nullptr;
        taskEXIT_CRITICAL(&s_camMux);
        camLog("[CAM] Async init task create failed", ERROR);
        return false;
    }
    return true;
}

bool cameraInitInProgress() {
    return s_initInProgress;
}

// Absolute ceiling for one cached JPEG. P4 uses a 2 MB encoder output slot for
// 1080p; keep the stream cache at least that large so HD frames are not silently
// dropped between capture and MJPEG publication. S3 still allocates by estimate.
// The live allocation is sized per framesize at stream start (w*h/5, the
// MJPEG2SD sizing rule); this cap only guards the MJPEG drop check.
// The buffer is allocated once at stream start and never reallocated while
// readers exist, avoiding a race between core 0's callback reading the
// pointer and core 1's loop freeing it.
#define STREAM_CACHE_MAX_BYTES (2UL * 1024UL * 1024UL)
#define STREAM_CACHE_MIN_BYTES (192UL * 1024UL)

// Cache bytes for a framesize: width*height/5 JPEG estimate, clamped.
static size_t streamCacheBytesFor(framesize_t fs) {
    uint16_t w = 640, h = 480;
    cameraSensorFrameSizeDims((uint8_t)fs, w, h);
    size_t need = (size_t)w * (size_t)h / 5;
    if (need < STREAM_CACHE_MIN_BYTES) need = STREAM_CACHE_MIN_BYTES;
    if (need > STREAM_CACHE_MAX_BYTES) need = STREAM_CACHE_MAX_BYTES;
    return need;
}

void cameraLoop() {
    if (!s_ready) return;

    uint32_t nowMs = millis();

    // The single-pump path below serves BOTH the live MJPEG cache and the
    // H.264 raw tap (one dequeue feeds the MJPEG cache and the `.264` file).
    // A H.264 session therefore has to run it even with no viewer attached:
    // otherwise the encoder is only reached while streaming, and recording
    // without a viewer drops to a handful of frames (observed: 6 in 15 s).
    bool pumpForH264 = false;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1 && ENABLE_SDCARD==1
    pumpForH264 = cameraRecordIsH264();
#endif
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1 && ENABLE_SDCARD==1
    // H.264 with no viewer: skip the MJPEG half of the pump entirely — one
    // dequeue straight into the encoder. Nothing else consumes a JPEG frame
    // in this state, and encoding it was the slow part.
    if (pumpForH264 && !s_streamActive) {
        uint32_t intervalMs = 1000UL / max((uint8_t)1, s_config.fps);
        if (s_lastH264PumpMs == 0 || (nowMs - s_lastH264PumpMs) >= intervalMs) {
            s_lastH264PumpMs = nowMs;
            uint16_t rw = 0, rh = 0;
            cameraP4ActualDims(rw, rh);
            size_t need = (size_t)rw * rh * 2u;
            if (rw && rh && need) {
                if (!s_p4RawSolo || s_p4RawSoloCap < need) {
                    if (s_p4RawSolo) free(s_p4RawSolo);
                    s_p4RawSolo = (uint8_t*)(psramFound() ? ps_malloc(need) : malloc(need));
                    s_p4RawSoloCap = s_p4RawSolo ? need : 0;
                }
                if (s_p4RawSolo) {
                    P4LoopStageCtx rctx{s_p4RawSolo, 0, s_p4RawSoloCap, false};
                    if (cameraP4CaptureRaw(cameraP4StageWriter, &rctx, intervalMs) > 0 &&
                        !rctx.ovf && rctx.off > 0) {
                        const uint8_t* nals = nullptr;
                        size_t nalsLen = 0;
                        if (p4H264EncodeUyvy(rctx.dst, rctx.off, &nals, &nalsLen) &&
                            nals && nalsLen > 0) {
                            cameraRecordFrameH264(nals, nalsLen);
                        }
                    }
                }
            }
        }
        // Motion sampling keeps its tick; the rest of the loop is either
        // stream-only or served by the generic idle pump below.
        cameraMotionTick(nowMs);
        return;
    }
#endif
    if (s_streamActive || pumpForH264) {
        // Pre-capture frames on core 1 (loop task) so the AsyncTCP task on core 0
        // never needs to call esp_camera_fb_get() — that would block WiFi/TCP and
        // make all other web pages inaccessible.
        uint32_t intervalMs = 1000UL / max((uint8_t)1, s_config.fps);
        if (s_lastStreamCaptureMs == 0 || (nowMs - s_lastStreamCaptureMs) >= intervalMs) {
            s_lastStreamCaptureMs = nowMs;

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
            // P4: capture straight into a reusable PSRAM staging buffer, then
            // run the identical cache-commit path as S3 (no per-frame alloc
            // churn, readers never blocked during capture).
            static uint8_t* s_p4Stage = nullptr;
            static size_t s_p4StageCap = 0;
            // Without a viewer the stream cache was never sized, but a H.264
            // session still needs the JPEG staging area (idle pump): give it
            // the same ceiling a stream would have used.
            if (!s_p4Stage && (s_streamCacheSize > 0 || pumpForH264)) {
                size_t cap = s_streamCacheSize ? s_streamCacheSize
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
                                               : (size_t)STREAM_CACHE_MAX_BYTES;
#else
                                               : streamCacheBytesFor(s_config.framesize);
#endif
                s_p4Stage = (uint8_t*)(psramFound() ? ps_malloc(cap) : malloc(cap));
                s_p4StageCap = s_p4Stage ? cap : 0;
            }
            const uint8_t* src = nullptr;
            size_t srcLen = 0;
            // Fase 1 (Tarefa 5): raw UYVY staging for the H.264 encoder —
            // sized from the live geometry, reused across frames (PSRAM).
            static uint8_t* s_p4RawStage = nullptr;
            static size_t s_p4RawStageCap = 0;
#if ENABLE_SDCARD==1
            bool wantH264 = cameraRecordIsH264();
#else
            bool wantH264 = false;
#endif
            if (s_p4Stage) {
                P4LoopStageCtx ctx{s_p4Stage, 0, s_p4StageCap, false};
                P4LoopStageCtx rawCtx{nullptr, 0, 0, false};
                if (wantH264) {
                    uint16_t aw = 0, ah = 0;
                    cameraP4ActualDims(aw, ah);
                    size_t need = (size_t)aw * ah * 2u;  // UYVY bytes
                    if (need > 0) {
                        if (!s_p4RawStage || s_p4RawStageCap < need) {
                            if (s_p4RawStage) free(s_p4RawStage);
                            s_p4RawStage = (uint8_t*)(psramFound() ? ps_malloc(need) : malloc(need));
                            s_p4RawStageCap = s_p4RawStage ? need : 0;
                        }
                        if (s_p4RawStage) rawCtx = P4LoopStageCtx{s_p4RawStage, 0, s_p4RawStageCap, false};
                    }
                }
                size_t total = cameraP4CaptureWithRaw(
                    cameraP4StageWriter, &ctx,
                    rawCtx.dst ? cameraP4StageWriter : nullptr,
                    rawCtx.dst ? &rawCtx : nullptr, intervalMs);
                if (total > 0 && !ctx.ovf && ctx.off > 0) {
                    src = s_p4Stage;
                    srcLen = ctx.off;
                }
                // Encode + append AFTER the JPEG path staging: one dequeue
                // feeds both the live MJPEG cache and the `.264` file.
                if (wantH264 && rawCtx.dst && !rawCtx.ovf && rawCtx.off > 0) {
                    const uint8_t* nals = nullptr;
                    size_t nalsLen = 0;
                    if (p4H264EncodeUyvy(rawCtx.dst, rawCtx.off, &nals, &nalsLen) &&
                        nals && nalsLen > 0) {
                        cameraRecordFrameH264(nals, nalsLen);
                    }
                }
            }
#else
            camera_fb_t* fb = cameraCaptureFrame();
            const uint8_t* src = nullptr;
            size_t srcLen = 0;
            if (fb) {
                src = fb->buf;
                srcLen = fb->len;
            }
#endif
            if (src && s_streamCacheSize > 0 && srcLen <= s_streamCacheSize) {
#if ENABLE_SDCARD==1
                // Recorder is fed from the freshly captured frame, independent
                // of the cache publish below: a pinned cache only defers the
                // publish, it must never starve the recording. Queue-full drops
                // the newest frame. (H.264 sessions take NALs, not JPEG.)
                if (cameraRecordActive() && srcLen > 0 && !cameraRecordIsH264()) {
                    cameraRecordFrame(src, srcLen);
                }
#endif
                bool published = false;
                if (s_streamCacheMux && xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(10)) == pdTRUE) {
                    // Never overwrite a frame a stream viewer is still sending.
                    if (s_streamRefs == 0) {
                        memcpy(s_streamCache, src, srcLen);
                        s_streamCacheLen = srcLen;
                        s_streamCacheValid = true;
                        s_cacheGen++;
                        s_cachePublishMs = nowMs;  // photo fallback freshness (CAM-64)
                        published = true;
                    }
                    xSemaphoreGive(s_streamCacheMux);
                    if (published) {
                        // Notify stream consumers: a new frame is ready in the cache.
                        // Using a binary semaphore: give (post) even if already given — the
                        // next consumer take will succeed. This works because binary semaphores
                        // in FreeRTOS queue a maximum of one notification; excess gives are
                        // no-ops, which is fine — the consumer only needs the latest frame.
                        xSemaphoreGive(s_frameSema);
                    }
                }
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
                // P4 staging buffer is reused, not freed (see above).
#else
                esp_camera_fb_return(fb);
            } else if (fb) {
                esp_camera_fb_return(fb);
#endif
            }
        }
        // When streaming, the recorder is fed from the freshly captured frame
        // above and motion sampling stays paused (it reconfigures the sensor,
        // which would tear the JPEG pipeline). Background ticks below only run
        // when idle.
        return;
    }

    // Background tasks below only run when stream is inactive.

    // Keep camera polling bounded so web routes stay responsive under load.
    static uint32_t s_lastCameraLoopMs = 0;
    if (s_lastCameraLoopMs != 0 && (nowMs - s_lastCameraLoopMs) < 120UL) {
        return;
    }
    s_lastCameraLoopMs = nowMs;

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // MIPI stall re-sync (CAM-62): loop task only, self-throttled to one
    // session restart per 10 s. Recovers a sensor that stops delivering
    // (e.g. marginal mode) instead of wedging every later capture in DQBUF.
    cameraP4StallTick(nowMs);
#endif

#if ENABLE_SDCARD==1
    cameraRecordTick(nowMs);
#endif
    cameraMotionTick(nowMs);
}

bool cameraIsReady()          { return s_ready; }
bool cameraStreamIsActive()   { return s_streamActive; }

const char* cameraGetDetectedModel() {
    if (!s_ready) return "Not detected";

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    return cameraP4Model();  // OV5647 via ESPVideo; no PID probe on P4
#else
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return "Unknown";

    const uint16_t pid = s->id.PID;

    const char* name = cameraSensorModelName(pid);
    if (name) return name;

    static char unknownModel[24];
    snprintf(unknownModel, sizeof(unknownModel), "Unknown (0x%02X)", (pid & 0xFF));
    return unknownModel;
#endif
}

void cameraGetOutputDimensions(uint16_t& w, uint16_t& h) {
    ensureConfigLoaded();
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    if (s_ready) {
        cameraP4ActualDims(w, h);
        if (w > 0 && h > 0) return;
    }
    cameraSensorFrameSizeDims((uint8_t)s_config.framesize, w, h);
    if (s_config.source == CAM_P4_SRC_MIPI) {
        cameraResolveP4OutputDims(s_config.p4_res_w, s_config.p4_res_h, w, h, &w, &h);
    }
#else
    cameraSensorFrameSizeDims((uint8_t)s_config.framesize, w, h);
#endif
}

int cameraDetectedMaxFrameSize() {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    return cameraP4MaxFrameSize();  // OV5647 5MP class
#else
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return (int)FRAMESIZE_UXGA;  // pre-init safe default (matches historical cap)
    return cameraSensorMaxFrameSize(s->id.PID);
#endif
}

bool cameraStreamStart() {
    if (!s_ready || s_streamActive) return false;

    // Size the cache for the live framesize (w*h/5 JPEG estimate). Realloc
    // here is safe: no stream readers exist yet (s_streamActive is false),
    // and the loop pre-capture only runs while active. Reuse the buffer when
    // it already covers the need to avoid repeated PSRAM churn.
    // P4 exception: the MIPI/USB pipelines capture at device-negotiated size
    // (Res select is accepted-but-unverified until HW measures the actual
    // default), so always size at the ceiling there — never drop real frames.
    size_t need =
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        STREAM_CACHE_MAX_BYTES;
#else
        streamCacheBytesFor(s_config.framesize);
#endif
    if (!s_streamCache || s_streamCacheSize < need) {
        if (s_streamCache) free(s_streamCache);
        s_streamCache = (uint8_t*)(psramFound() ? ps_malloc(need) : malloc(need));
        if (!s_streamCache) {
            camLog("[CAM] Failed to allocate stream cache", ERROR);
            s_streamCacheSize = 0;
            return false;
        }
        s_streamCacheSize = need;
    }

    s_streamActive = true;
    s_streamRefs = 0;  // fresh session: no reader can hold a stale pin
    s_streamCacheValid = false;
    s_streamCacheLen = 0;
    s_lastStreamCaptureMs = 0;
    return true;
}

void cameraStreamStop() {
    s_streamActive = false;
    s_streamCacheValid = false;
    s_cachePublishMs = 0;  // cached frame is stale after stop (CAM-64)
    streamFpsWindowReset(&s_fpsWin);
    s_framesSentTotal = 0;
    // Buffer stays allocated for next stream session to avoid repeated malloc/free.
}

void cameraStop() {
    cameraStreamStop();
#if ENABLE_SDCARD==1
    if (cameraRecordActive()) cameraRecordFinish();
#endif
    // If an init is in flight (boot auto-start racing a page Stop), ask a USB
    // probing begin to abort and wait bounded — deinit racing init corrupts
    // the driver task queues. Polling read is safe: the init task is the
    // only writer.
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    cameraP4CancelUsb();
#endif
    for (int i = 0; i < 80 && s_initInProgress; i++) vTaskDelay(pdMS_TO_TICKS(100));
    taskENTER_CRITICAL(&s_camMux);
    bool wasReady = s_ready;
    s_ready = false;  // loop/photo/snapshot/record paths check this first
    taskEXIT_CRITICAL(&s_camMux);
    if (!wasReady) return;
    // Grace period: an in-flight loop fb_get / motion sensor switch /
    // direct capture must finish before the driver is torn down.
    vTaskDelay(pdMS_TO_TICKS(400));
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    cameraP4End();
#else
    // Release the hardware capture engine (DMA + ISR + sensor clocks).
    // Continuous S3 LCD_CAM DMA into PSRAM while Wi-Fi is active is the
    // correlated trigger for the silent dual-core freeze; deinit is the only
    // way to actually park the capture engine between sessions.
    esp_camera_deinit();
#endif
    camLog("[CAM] Stopped");
}

bool cameraStart() {
    if (s_ready) { cameraStreamStart(); return true; }
    return false;
}

void cameraStreamFrameSent() {
    if (!s_streamActive) return;
    uint32_t gen;
    if (s_streamCacheMux && xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(2)) == pdTRUE) {
        gen = s_cacheGen;
        xSemaphoreGive(s_streamCacheMux);
    } else {
        return;
    }
    if (gen == s_fpsLastGen) return;  // same frame already counted
    s_fpsLastGen = gen;
    s_framesSentTotal++;  // monotonic: diagnostics + curl audit (never reset except stop)

    streamFpsWindowTick(&s_fpsWin, millis());
}

uint16_t cameraStreamFps() {
    return s_fpsWin.last;
}

uint32_t cameraStreamFramesSentTotal() {
    return s_framesSentTotal;
}

bool cameraApplyConfig(const CameraConfig& cfg) {
    ensureConfigLoaded();
    const CameraConfig previous = s_config;
    s_config = cfg;
    s_config.framesize = clampFrameSize(s_config.framesize);
    s_config.fps = cameraClampFps(s_config.framesize, s_config.fps);
    if (!s_ready) {
        cameraConfigSave(s_config);  // sensor apply runs on the next init
        return true;
    }
    framesize_t appliedFs = s_config.framesize;
    if (!applyConfigToSensor(s_config, &appliedFs)) {
        // In particular, a failed P4 sensor-mode switch must not be reported
        // as success or persist a mode the camera did not enter. The P4
        // backend reverts the physical sensor before returning false.
        s_config = previous;
        cameraConfigSave(s_config);
        return false;
    }
    if (appliedFs != s_config.framesize) {
        s_config.framesize = appliedFs;
    }
    cameraConfigSave(s_config);
    // Propagate motion config change without full re-init.
    cameraMotionSetConfig(s_config.motion);
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // Fase 5 (Tarefa 5): mirror vision mode/thresholds into the vision task
    // (covers POST apply, boot apply and sensor-switch paths alike).
    p4VisionSetMode(s_config.vision_mode);
    p4VisionSetThresh(s_config.vision_thresh);
#endif
    // Lamp applies live (no sensor reinit needed).
    cameraLampApply(s_config.lamp);
    // Re-arm the continuous mode if auto_mode was (re)selected while live.
    if (s_config.auto_mode != 0) cameraArmAutoMode();
    return true;
}

bool cameraLampAvailable() {
#if defined(CAM_PIN_LAMP) && CAM_PIN_LAMP >= 0
    return true;
#else
    return false;
#endif
}

uint8_t cameraLampGet() {
    ensureConfigLoaded();
    return s_config.lamp > 15 ? 15 : s_config.lamp;
}

void cameraLampApply(uint8_t level) {
#if defined(CAM_PIN_LAMP) && CAM_PIN_LAMP >= 0
    if (level > 15) level = 15;
    // Map 0–15 to 8-bit PWM (0=off). Level 15 ≈ full brightness.
    ledcWrite(1, (uint32_t)level * 17);
#else
    (void)level;
#endif
}

void cameraLampSet(uint8_t level) {
    ensureConfigLoaded();
    if (level > 15) level = 15;
    s_config.lamp = level;
    cameraConfigSave(s_config);
    if (s_ready) cameraLampApply(level);
}

const CameraConfig& cameraGetConfig() {
    ensureConfigLoaded();
    return s_config;
}

camera_fb_t* cameraCaptureFrame() {
    if (!s_ready) return nullptr;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4: single ESPVideo capture into an owned shim fb (freed by
    // esp_camera_fb_return). Serialised by callers via the sampling guard
    // and the stream-active cache path — same single-pump discipline as S3.
    // NOTE: capture lambdas must not capture (plain function pointer type).
    struct CapState { uint8_t* dst; size_t off; size_t cap; bool ovf; };
    // Live geometry sizes the buffer AND is reported back: on P4 the legacy
    // framesize enum is meaningless (a 720p session still stores
    // framesize=6/QVGA), and the old lookup reported 640x480 for every idle
    // capture — which also poisoned the AVI header geometry.
    uint16_t fw = 0, fh = 0;
    cameraP4ActualDims(fw, fh);
    if (fw == 0 || fh == 0) { fw = 640; fh = 480; }
    // P4 HD modes (1080p JPEG worst case ~2 MB): size the staging area to the
    // live geometry so big frames are never truncated (PSRAM-gated).
    // Fase 3 (Tarefa 5): 5MP stills need ~5 MB — cap raised 4→6 MB on P4
    // (PSRAM is roomy there; S3 path untouched, still 4 MB via the same cap).
    size_t need = (size_t)fw * fh;  // ~1 byte/px JPEG upper bound
    if (need < 1024UL * 1024UL) need = 1024UL * 1024UL;
    if (need > 6UL * 1024UL * 1024UL) need = 6UL * 1024UL * 1024UL;

    // Reserve the persistent slot for this frame. The mutex is held only for
    // the reservation, never across the capture, so a slow DQBUF cannot pin
    // another caller out.
    bool slot = false;
    if (s_capSlotMux && xSemaphoreTake(s_capSlotMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (!s_capSlot.inUse && (!s_capSlot.buf || s_capSlot.cap < need)) {
            uint8_t* nb = (uint8_t*)(psramFound() ? ps_malloc(need) : malloc(need));
            if (nb) {
                if (s_capSlot.buf) free(s_capSlot.buf);
                s_capSlot.buf = nb;
                s_capSlot.cap = need;
            }
        }
        if (!s_capSlot.inUse && s_capSlot.buf && s_capSlot.cap >= need) {
            s_capSlot.inUse = true;
            slot = true;
        }
        xSemaphoreGive(s_capSlotMux);
    }

    uint8_t* dst;
    size_t   dcap;
    camera_fb_t* fb = (camera_fb_t*)malloc(sizeof(camera_fb_t));
    if (slot) {
        dst  = s_capSlot.buf;
        dcap = s_capSlot.cap;
    } else {
        // Slot busy or too small (concurrent consumer): one-off buffer keeps
        // the frame flowing instead of dropping it.
        dst  = (uint8_t*)(psramFound() ? ps_malloc(need) : malloc(need));
        dcap = need;
    }
    if (!dst || !fb) {
        if (slot) p4CapSlotReleaseFrame(fb);   // frees fb only, keeps buffer
        else { if (dst) free(dst); if (fb) free(fb); }
        return nullptr;
    }

    CapState cs{dst, 0, dcap, false};
    size_t total = cameraP4Capture(
        [](const uint8_t* data, size_t len, void* ctx) {
            CapState* s = (CapState*)ctx;
            if (s->off + len > s->cap) { s->ovf = true; return; }
            memcpy(s->dst + s->off, data, len);
            s->off += len;
        },
        &cs, 2000);
    if (total == 0 || cs.ovf || cs.off == 0) {
        if (slot) p4CapSlotReleaseFrame(fb);
        else { free(dst); free(fb); }
        return nullptr;
    }
    fb->buf    = dst;
    fb->len    = cs.off;
    fb->width  = fw;
    fb->height = fh;
    if (slot) {
        fb->owned   = false;                 // buffer belongs to the slot
        fb->release = p4CapSlotReleaseFrame;  // recycled on fb_return
    } else {
        fb->owned   = true;
        fb->release = nullptr;
    }
    return fb;
#else
    return esp_camera_fb_get();
#endif
}

// Single-pump file grab (MJPEG2SD model): every still/record consumer takes
// frames from the loop pre-capture — never a concurrent esp_camera_fb_get().
// Policy:
//   stream active → wait up to timeoutMs for a fresh cached frame (copy out);
//   idle + motion sampling → refuse (sensor is in grayscale mode);
//   idle otherwise → one direct fb_get (loop pre-capture is parked).
// Returns true with data/len set; caller frees NOTHING (cache is internal,
// direct fb is returned internally before return — data is a copy in PSRAM).
bool cameraGrabFileFrame(uint8_t** outCopy, size_t* outLen, uint32_t timeoutMs) {
    if (!s_ready || !outCopy || !outLen) return false;
    *outCopy = nullptr;
    *outLen = 0;

    if (s_streamActive) {
        // Prefer a fresh frame, but never fail a photo just because the pump
        // is pin-blocked by a slow viewer (CAM-64: snapshot 503d on every
        // attempt while streaming at a few fps). Fall back to the latest
        // cached frame while it is younger than PHOTO_CACHE_MAX_AGE_MS.
        size_t len = 0;
        const uint8_t* src = nullptr;
        if (cameraStreamWaitFrame(timeoutMs)) {
            src = cameraStreamFrame(len);
        } else if (cameraCachedPhotoOk(s_cachePublishMs, millis(), PHOTO_CACHE_MAX_AGE_MS)) {
            src = cameraStreamFrame(len);
        } else {
            return false;
        }
        if (!src || len == 0) return false;
        uint8_t* copy = (uint8_t*)(psramFound() ? ps_malloc(len) : malloc(len));
        if (!copy) return false;
        memcpy(copy, src, len);
        *outCopy = copy;
        *outLen = len;
        return true;
    }
    if (cameraMotionSamplingActive()) return false;
    // `*outCopy` must stay an OWNED allocation: both callers hand the pointer
    // to the HTTP response, whose freeFn frees it once the socket is done
    // (camera_handler.cpp snapshot freeFn). A pooled/shared buffer here would
    // be freed under the next grab — so the copy-out allocation is required,
    // not churn. The win for the photo path is the capture slot in
    // cameraCaptureFrame() (no staging alloc of its own).
    camera_fb_t* fb =
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        cameraCaptureFrame();
#else
        esp_camera_fb_get();
#endif
    if (!fb) return false;
    uint8_t* copy = (uint8_t*)(psramFound() ? ps_malloc(fb->len) : malloc(fb->len));
    if (!copy) { esp_camera_fb_return(fb); return false; }
    memcpy(copy, fb->buf, fb->len);
    *outLen = fb->len;
    esp_camera_fb_return(fb);
    *outCopy = copy;
    return true;
}

const uint8_t* cameraStreamFrame(size_t& sizeOut) {
    if (!s_ready || !s_streamActive || !s_streamCacheValid) {
        sizeOut = 0;
        return nullptr;
    }
    if (!s_streamCacheMux || xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(5)) != pdTRUE) {
        sizeOut = 0;
        return nullptr;
    }
    sizeOut = s_streamCacheLen;
    const uint8_t* ptr = s_streamCache;
    xSemaphoreGive(s_streamCacheMux);
    return ptr;
}

// Zero-copy variant: pin the cache so cameraLoop() will not overwrite it until
// cameraStreamFrameUnpin(). The returned pointer stays valid for the whole
// send — this removes the per-viewer PSRAM frame copy the stream pump used to
// do. Each successful pin MUST be matched by exactly one unpin.
const uint8_t* cameraStreamFramePin(size_t& sizeOut) {
    sizeOut = 0;
    if (!s_ready || !s_streamActive || !s_streamCacheValid) return nullptr;
    if (!s_streamCacheMux || xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(5)) != pdTRUE) return nullptr;
    const uint8_t* ptr = nullptr;
    if (s_streamCache && s_streamCacheLen > 0) {
        sizeOut = s_streamCacheLen;
        ptr = s_streamCache;
        s_streamRefs++;
    }
    xSemaphoreGive(s_streamCacheMux);
    return ptr;
}

void cameraStreamFrameUnpin() {
    if (s_streamCacheMux && xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_streamRefs > 0) s_streamRefs--;
        xSemaphoreGive(s_streamCacheMux);
    } else if (s_streamRefs > 0) {
        s_streamRefs--;  // best effort if the mutex is momentarily busy
    }
}

bool cameraStreamWaitFrame(uint32_t timeoutMs) {
    if (!s_ready || !s_streamActive) return false;

    uint32_t startGen;
    if (s_streamCacheMux && xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(10)) == pdTRUE) {
        startGen = s_cacheGen;
        xSemaphoreGive(s_streamCacheMux);
    } else {
        startGen = s_cacheGen;
    }

    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        uint32_t gen;
        bool valid;
        if (s_streamCacheMux && xSemaphoreTake(s_streamCacheMux, pdMS_TO_TICKS(5)) == pdTRUE) {
            gen = s_cacheGen;
            valid = s_streamCacheValid && s_streamCacheLen > 0;
            xSemaphoreGive(s_streamCacheMux);
        } else {
            delay(2);
            continue;
        }
        if (gen != startGen && valid) return true;
        delay(2);
    }
    return false;
}

SemaphoreHandle_t cameraStreamFrameSemaphore() {
    return s_frameSema;
}

#endif // ENABLE_CAMERA==1
