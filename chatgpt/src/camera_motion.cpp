// camera_motion.cpp — Grayscale-diff motion detection for the ESP32 camera.
//
// Strategy to avoid a JPEG decoder:
//   esp_camera supports PIXFORMAT_GRAYSCALE natively.  However the live stream
//   and recording both need PIXFORMAT_JPEG.  Switching pixel formats requires
//   calling esp_camera_deinit / esp_camera_init which is too slow for loop().
//
//   Instead we use a *second capture* approach:
//     - The sensor is reconfigured to a small thumbnail resolution (FRAMESIZE_96X96)
//       + PIXFORMAT_GRAYSCALE only for the motion sample.
//     - After grabbing and processing the grayscale frame, the sensor is switched
//       back to the user's configured format and resolution.
//     - This takes ~50 ms total and happens at most every N loop iterations
//       (adaptive sampling), so the overhead on the main frame pipeline is minimal.
//
//   If the sensor does not support dual-format mode (some OV2640 firmware revisions),
//   we fall back to averaging every 8th row/column of the JPEG bitstream to
//   estimate luma variance, which is cheaper but less accurate.

#include "camera_motion.h"

#include "feature_flags.h"
#if ENABLE_CAMERA==1

#include <Arduino.h>
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "camera_p4_backend.h"  // P4: fb shim (no esp_camera); sensor switch unavailable
#else
#include "esp_camera.h"
#endif
#include "camera_manager.h"   // cameraIsReady(), cameraGetConfig()
#include "camera_config.h"
#include "direct_log.h"
#include "psram_helpers.h"    // psramFound(), ps_malloc()
#if ENABLE_SDCARD==1
#include "camera_record.h"    // cameraRecordStart/Frame/Finish/Active, cameraMotionSnapSave
#endif
#if ENABLE_SMTP==1
#include "config_manager_runtime.h"  // configManager.getEmailDestination()
#include "runtime_email.h"           // startInactivityAlertAsync() — motion email alert (no attachment)
#endif

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static MotionConfig s_cfg;
static bool         s_enabled        = false;

// Two grayscale thumbnail buffers (current + previous).
// Allocated in PSRAM at cameraMotionBegin() — frees 3.6 KB of SRAM.
static uint8_t* s_thumbA   = nullptr;
static uint8_t* s_thumbB   = nullptr;
static uint8_t* s_current  = nullptr;  // points to s_thumbA or s_thumbB (ping-pong)
static uint8_t* s_previous = nullptr;  // points to the other buffer

// Scratch grayscale capture buffer (one MOTION_THUMB_PIX block). Moved out of
// static DRAM (was a `static uint8_t thumb[]` local in cameraMotionTick) into
// PSRAM to keep the internal heap free — Task 2 PSRAM optimisation.
static uint8_t* s_thumbScratch = nullptr;

// Per-pixel absolute difference of the last comparison.
static uint8_t* s_diffBuf = nullptr;

// Detection counters
static uint8_t  s_changedRun  = 0;  // consecutive changed frames
static uint8_t  s_unchangedRun= 0;  // consecutive unchanged frames
static bool     s_motionActive= false;

// Adaptive sampling counters
static uint8_t  s_frameCounter   = 0;   // counts raw cameraMotionTick() calls
static uint8_t  s_idleSkip       = 4;   // check 1 in (skip+1) when idle (~600 ms at 120 ms loop throttle)
static uint8_t  s_activeSkip     = 1;   // check every other frame when motion active

// Sampling guard: true while the sensor is switched to PIXFORMAT_GRAYSCALE
// for the motion thumbnail (captureGrayscaleThumb window, ~50-100 ms).
// Direct JPEG captures (photo/snapshot/record tick) must wait or skip here.
static volatile bool s_sampling = false;

// Recording ownership: true only when THIS module started the session.
// Prevents the post-motion timeout from finalising a manual/dashcam recording.
static bool s_ownRecording = false;

// Score (0..100)
static uint8_t  s_score = 0;

// Action-fired guard: snap/email actions fire once per motion event (on the
// false->true transition), reset when the event ends — prevents repeated
// captures/emails during a long continuous motion window.
static bool s_actionFired = false;

// Post-motion recording: keep recording N seconds after motion stops.
static uint32_t s_motionEndMs = 0;  // millis() when motion last ended

// Event counter (product-counter mode): incremented on the false->true edge
// unless inside the cooldown refractory window. Persisted throttled.
static uint32_t s_totalEvents    = 0;
static uint32_t s_lastEventEndMs = 0;  // millis() of the previous event end
static uint32_t s_lastTotalSaveMs = 0;
static bool     s_eventCounted   = false;  // current event passed cooldown

// Best-frame of the event: highest-score JPEG held in PSRAM, saved on the
// falling edge for SNAP/EMAIL actions (NVR-style keyframe).
static uint8_t* s_bestBuf   = nullptr;
static size_t   s_bestLen   = 0;
static size_t   s_bestCap   = 0;
static uint8_t  s_bestScore = 0;
#define MOTION_BEST_MAX_BYTES (400UL * 1024UL)

// Lamp-auto: saved level restored when the event ends.
static uint8_t s_lampPrev = 0;
static bool    s_lampHeld = false;

// Persist the counter without wearing flash: every 10 events or 60 s.
static void persistTotalEvents() {
    uint32_t now = millis();
    if ((s_totalEvents % 10u) != 0 && (now - s_lastTotalSaveMs) < 60000UL) return;
    s_lastTotalSaveMs = now;
    CameraConfig cfg = cameraGetConfig();
    cfg.motion.total_events = s_totalEvents;
    cameraConfigSave(cfg);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Downsample a PIXFORMAT_GRAYSCALE frame (w×h) into the MOTION_THUMB_W×MOTION_THUMB_H
// thumbnail buf[] using a box filter.  Each output pixel is the average of the
// corresponding input block.
static void downsampleGray(const uint8_t* src, uint16_t srcW, uint16_t srcH,
                            uint8_t* dst) {
    // Block size (may be non-integer — we use fixed-point 8-bit fractional scale).
    // MOTION_THUMB_W / srcW and MOTION_THUMB_H / srcH are typically < 1, so
    // we iterate over the *output* thumbnail and map each cell to an input region.
    for (int ty = 0; ty < MOTION_THUMB_H; ty++) {
        int y0 = (int)((uint32_t)ty * srcH / MOTION_THUMB_H);
        int y1 = (int)((uint32_t)(ty + 1) * srcH / MOTION_THUMB_H);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > (int)srcH) y1 = srcH;
        for (int tx = 0; tx < MOTION_THUMB_W; tx++) {
            int x0 = (int)((uint32_t)tx * srcW / MOTION_THUMB_W);
            int x1 = (int)((uint32_t)(tx + 1) * srcW / MOTION_THUMB_W);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > (int)srcW) x1 = srcW;
            uint32_t sum = 0;
            int count = (y1 - y0) * (x1 - x0);
            for (int y = y0; y < y1; y++) {
                const uint8_t* row = src + (size_t)y * srcW;
                for (int x = x0; x < x1; x++) sum += row[x];
            }
            dst[ty * MOTION_THUMB_W + tx] = (uint8_t)(sum / count);
        }
    }
}

// Compute per-pixel absolute difference between a and b, write to diff[].
// Returns the sum of all differences (used to calculate score).
static uint32_t computeDiff(const uint8_t* a, const uint8_t* b, uint8_t* diff,
                             const MotionConfig& cfg) {
    uint32_t total = 0;
    int xStart = 0, yStart = 0;
    int xEnd   = MOTION_THUMB_W, yEnd = MOTION_THUMB_H;

    // Apply ROI if configured.
    if (cfg.roi_w > 0 && cfg.roi_h > 0) {
        xStart = cfg.roi_x;
        yStart = cfg.roi_y;
        xEnd   = xStart + cfg.roi_w;
        yEnd   = yStart + cfg.roi_h;
        if (xEnd > MOTION_THUMB_W) xEnd = MOTION_THUMB_W;
        if (yEnd > MOTION_THUMB_H) yEnd = MOTION_THUMB_H;
    }

    memset(diff, 0, MOTION_THUMB_PIX);

    for (int y = yStart; y < yEnd; y++) {
        for (int x = xStart; x < xEnd; x++) {
            int idx  = y * MOTION_THUMB_W + x;
            int d    = (int)a[idx] - (int)b[idx];
            uint8_t ad = (uint8_t)(d < 0 ? -d : d);
            diff[idx] = ad;
            total += ad;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Grayscale capture
// ---------------------------------------------------------------------------

// Capture a single FRAMESIZE_96X96 PIXFORMAT_GRAYSCALE frame.
// Temporarily switches sensor configuration; restores afterwards.
// Returns true and fills thumbBuf[MOTION_THUMB_PIX] on success.
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
// P4 (ESPVideo backend) has no sensor pixformat switch API, so the S3-style
// dedicated grayscale capture is impossible. Instead the live UYVY stream
// already carries luma: one frame is dequeued and downsampled to the thumb
// (no format switch, no stream disruption). Bounded wait (~one frame time);
// false keeps the tick idle this round (pre-start, USB source, stall).
static bool captureGrayscaleThumb(uint8_t* thumbBuf) {
    return cameraP4CaptureGrayThumb(thumbBuf, MOTION_THUMB_W, MOTION_THUMB_H, 1000);
}
#else
static bool captureGrayscaleThumb(uint8_t* thumbBuf) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    // Open the sampling window BEFORE touching sensor registers so concurrent
    // direct captures (photo/snapshot/record tick on the HTTP task) can wait.
    s_sampling = true;

    // Save current format and size so we can restore them.
    const CameraConfig& cfg = cameraGetConfig();

    s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
    s->set_framesize(s, FRAMESIZE_96X96);  // smallest square — fast grab

    // Discard one frame to let the sensor adjust.
    camera_fb_t* discard = esp_camera_fb_get();
    if (discard) esp_camera_fb_return(discard);

    // Grab the actual grayscale frame.
    camera_fb_t* fb = esp_camera_fb_get();
    bool ok = false;
    if (fb && fb->format == PIXFORMAT_GRAYSCALE && fb->len >= 96u * 96u) {
        downsampleGray(fb->buf, fb->width, fb->height, thumbBuf);
        ok = true;
    }
    if (fb) esp_camera_fb_return(fb);

    // Restore JPEG + original framesize.
    s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, cfg.framesize);

    s_sampling = false;
    return ok;
}
#endif // !TARGET_BOARD_P4

bool cameraMotionSamplingActive() {
    return s_sampling;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void cameraMotionBegin(const MotionConfig& cfg) {
    // Allocate pixel buffers in PSRAM on first call; fall back to heap if unavailable.
    bool hasPsram = psramFound();
    if (!s_thumbA)  s_thumbA  = (uint8_t*)(hasPsram ? ps_malloc(MOTION_THUMB_PIX) : malloc(MOTION_THUMB_PIX));
    if (!s_thumbB)  s_thumbB  = (uint8_t*)(hasPsram ? ps_malloc(MOTION_THUMB_PIX) : malloc(MOTION_THUMB_PIX));
    if (!s_diffBuf) s_diffBuf = (uint8_t*)(hasPsram ? ps_malloc(MOTION_THUMB_PIX) : malloc(MOTION_THUMB_PIX));
    if (!s_thumbScratch) s_thumbScratch = (uint8_t*)(hasPsram ? ps_malloc(MOTION_THUMB_PIX) : malloc(MOTION_THUMB_PIX));

    s_cfg     = cfg;
    s_enabled = cfg.enabled;
#if ENABLE_SMTP==0
    // Email alerts are compiled out in this pack: a legacy stored action=2
    // (Email alert) would sit in a dead state, so fold it to Snap photo (1).
    if (s_cfg.action == MOTION_ACTION_EMAIL_ALERT) s_cfg.action = MOTION_ACTION_SNAP_PHOTO;
#endif
    if (s_thumbA)  memset(s_thumbA, 128, MOTION_THUMB_PIX);
    if (s_thumbB)  memset(s_thumbB, 128, MOTION_THUMB_PIX);
    if (s_diffBuf) memset(s_diffBuf, 0,  MOTION_THUMB_PIX);
    s_current      = s_thumbA;
    s_previous     = s_thumbB;
    s_changedRun   = 0;
    s_unchangedRun = 0;
    s_motionActive = false;
    s_frameCounter = 0;
    s_score        = 0;
    s_motionEndMs  = 0;
    s_sampling     = false;
    s_ownRecording = false;
    s_actionFired  = false;
    s_totalEvents  = cfg.total_events;  // restore persistent counter
    s_lastEventEndMs = 0;
    s_lastTotalSaveMs = millis();
    s_eventCounted = false;
    s_bestLen = 0;
    s_bestScore = 0;
    s_lampHeld = false;
}

void cameraMotionSetConfig(const MotionConfig& cfg) {
    s_cfg     = cfg;
    s_enabled = cfg.enabled;
#if ENABLE_SMTP==0
    if (s_cfg.action == MOTION_ACTION_EMAIL_ALERT) s_cfg.action = MOTION_ACTION_SNAP_PHOTO;
#endif
}

const MotionConfig& cameraMotionGetConfig() {
    return s_cfg;
}

bool cameraMotionUpdate(const uint8_t* grayData, uint16_t w, uint16_t h) {
    if (!s_enabled) return false;
    downsampleGray(grayData, w, h, s_current);
    uint32_t diffSum = computeDiff(s_current, s_previous, s_diffBuf, s_cfg);

    // Score: average diff per pixel, clamped to 0..100.
    uint32_t avgDiff = diffSum / MOTION_THUMB_PIX;
    s_score = (uint8_t)(avgDiff > 100 ? 100 : avgDiff);

    // A pixel is "changed" if its diff exceeds the sensitivity threshold.
    uint32_t changedPixels = 0;
    for (int i = 0; i < MOTION_THUMB_PIX; i++) {
        if (s_diffBuf[i] >= s_cfg.sensitivity) changedPixels++;
    }
    // Trigger threshold: >= 2% of pixels changed (avoids single-pixel noise).
    bool frameChanged = (changedPixels * 100u / MOTION_THUMB_PIX) >= 2u;

    if (frameChanged) {
        s_changedRun++;
        s_unchangedRun = 0;
    } else {
        s_unchangedRun++;
        if (s_unchangedRun > 2) s_changedRun = 0;
    }

    // Event state machine.
    if (!s_motionActive && s_changedRun >= s_cfg.confirm_frames) {
        s_motionActive = true;
        s_motionEndMs  = 0;
        s_actionFired  = false;  // (re)arm once-per-event action
        // Cooldown gate: events inside the refractory window are tracked
        // (score/badges still live) but neither counted nor acted upon.
        uint32_t nowEv = millis();
        s_eventCounted = (s_cfg.cooldown_secs == 0) ||
            (s_lastEventEndMs == 0) ||
            ((nowEv - s_lastEventEndMs) >= (uint32_t)s_cfg.cooldown_secs * 1000UL);
        if (s_eventCounted) {
            s_totalEvents++;
            persistTotalEvents();
        }
        // Best-frame + lamp arm for the new event.
        s_bestLen = 0;
        s_bestScore = 0;
        if (s_cfg.lamp_auto && cameraLampAvailable()) {
            s_lampPrev = cameraLampGet();
            cameraLampApply(15);
            s_lampHeld = true;
        }
        char evmsg[64];
        snprintf(evmsg, sizeof(evmsg), "[CAM] Motion detected (event %lu%s)",
                 (unsigned long)s_totalEvents, s_eventCounted ? "" : ", cooldown");
        directLogPrintln(evmsg);
    } else if (s_motionActive && !frameChanged &&
               s_unchangedRun >= s_cfg.stop_frames) {
        s_motionActive = false;
        s_motionEndMs  = millis();
        s_lastEventEndMs = s_motionEndMs;
        if (s_lampHeld) {
            cameraLampApply(s_lampPrev);
            s_lampHeld = false;
        }
#if ENABLE_SDCARD==1
        // Best-frame save for SNAP/EMAIL actions (keyframe of the event).
        if (s_eventCounted && s_bestBuf && s_bestLen > 0 &&
            (s_cfg.action == MOTION_ACTION_SNAP_PHOTO ||
             s_cfg.action == MOTION_ACTION_EMAIL_ALERT)) {
            String bestPath;
            if (cameraMotionSnapSave(s_bestBuf, s_bestLen, &bestPath)) {
                char bmsg[160];
                snprintf(bmsg, sizeof(bmsg), "[CAM] Motion best frame (score %u) → %s",
                         (unsigned)s_bestScore, bestPath.c_str());
                directLogPrintln(bmsg);
            }
        }
#endif
        s_bestLen = 0;
        s_bestScore = 0;
        directLogPrintln("[CAM] Motion stopped");
    }

    // Swap buffers: current becomes the new previous for the next call.
    uint8_t* tmp = s_previous;
    s_previous   = s_current;
    s_current    = tmp;

    return true;
}

bool    cameraMotionActive()   { return s_motionActive; }
uint8_t cameraMotionScore()    { return s_score; }
const uint8_t* cameraMotionDiffBuf() { return s_diffBuf; }
uint32_t cameraMotionEventCount() { return s_totalEvents; }

void cameraMotionResetCount() {
    s_totalEvents = 0;
    s_lastTotalSaveMs = millis();
    CameraConfig cfg = cameraGetConfig();
    cfg.motion.total_events = 0;
    cameraConfigSave(cfg);
    directLogPrintln("[CAM] Motion event counter reset");
}

void cameraMotionReset() {
    if (s_thumbA)  memset(s_thumbA, 128, MOTION_THUMB_PIX);
    if (s_thumbB)  memset(s_thumbB, 128, MOTION_THUMB_PIX);
    if (s_diffBuf) memset(s_diffBuf, 0,  MOTION_THUMB_PIX);
    s_changedRun = s_unchangedRun = 0;
    s_motionActive = false;
    s_frameCounter = 0;
    s_score = 0;
    s_motionEndMs = 0;
    s_sampling = false;
    s_ownRecording = false;
    s_actionFired = false;
    s_eventCounted = false;
    s_bestLen = 0;
    s_bestScore = 0;
    if (s_lampHeld) {
        cameraLampApply(s_lampPrev);
        s_lampHeld = false;
    }
}

// ---------------------------------------------------------------------------
// Diff JPEG visualisation
//
// Builds a minimal 40x30 JPEG by hand using the esp32-camera JPEG encoder
// (writejpeg / fmt2jpg_cb from esp32-camera's img_convertor.h).
// If the encoder is unavailable we fall back to a raw BMP-like structure
// that modern browsers can display via canvas (returned as image/bmp).
//
// We use fmt2jpg() which is available in arduino-esp32 >= 3.1.1.
// Signature: bool fmt2jpg(const uint8_t* src, size_t srcLen,
//                         uint16_t w, uint16_t h, pixformat_t format,
//                         uint8_t quality, uint8_t** out, size_t* outLen);
// ---------------------------------------------------------------------------

// Build an RGB888 visualisation: gray with red overlay on motion pixels.
static void buildDiffRgb(uint8_t* rgb, const uint8_t* gray,
                          const uint8_t* diff, uint8_t threshold) {
    for (int i = 0; i < MOTION_THUMB_PIX; i++) {
        if (diff[i] >= threshold) {
            // Red pixel: high R, low G/B.
            rgb[i * 3 + 0] = 220;
            rgb[i * 3 + 1] = 0;
            rgb[i * 3 + 2] = 0;
        } else {
            // Grayscale pixel.
            rgb[i * 3 + 0] = gray[i];
            rgb[i * 3 + 1] = gray[i];
            rgb[i * 3 + 2] = gray[i];
        }
    }
}

size_t cameraMotionBuildDiffJpeg(uint8_t* buf, size_t outBufLen) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4 v1: no fmt2jpg (no esp_camera conversions) — diff endpoint 500s.
    (void)buf; (void)outBufLen;
    return 0;
#else
    // RGB888 source: 40*30*3 = 3600 bytes — allocate from PSRAM/heap to avoid stack pressure.
    uint8_t* rgb = (uint8_t*)(psramFound() ? ps_malloc(MOTION_THUMB_PIX * 3)
                                           : malloc(MOTION_THUMB_PIX * 3));
    if (!rgb) return 0;
    buildDiffRgb(rgb, s_previous, s_diffBuf, s_cfg.sensitivity);

    uint8_t* jpegOut  = nullptr;
    size_t   jpegSize = 0;

    // fmt2jpg allocates its output buffer via heap; we then copy to caller's buf.
    bool ok = fmt2jpg(rgb, (size_t)MOTION_THUMB_PIX * 3, MOTION_THUMB_W, MOTION_THUMB_H,
                      PIXFORMAT_RGB888, 80, &jpegOut, &jpegSize);
    free(rgb);  // release PSRAM/heap rgb buffer regardless of fmt2jpg result
    if (!ok || jpegOut == nullptr || jpegSize == 0) return 0;

    size_t copySize = (jpegSize < outBufLen) ? jpegSize : outBufLen;
    memcpy(buf, jpegOut, copySize);
    free(jpegOut);
    return copySize;
#endif // !TARGET_BOARD_P4
}

// ---------------------------------------------------------------------------
// cameraMotionTick — called from cameraLoop()
// ---------------------------------------------------------------------------
void cameraMotionTick(uint32_t nowMs) {
    if (!s_enabled || !cameraIsReady()) return;

    // Adaptive skip: check less often when idle.
    uint8_t skip = s_motionActive ? s_activeSkip : s_idleSkip;
    s_frameCounter++;
    if (s_frameCounter <= skip) return;
    s_frameCounter = 0;

    // Capture a grayscale thumbnail and run the diff.
    // Static: only one tick runs at a time (loop task), so this is safe.
    // Scratch buffer lives in PSRAM (allocated by cameraMotionBegin) to keep
    // the internal heap free.
    if (!s_thumbScratch) return;
    if (!captureGrayscaleThumb(s_thumbScratch)) return;
    cameraMotionUpdate(s_thumbScratch, MOTION_THUMB_W, MOTION_THUMB_H);

    // Best-frame tracking: while the event runs, keep the highest-score
    // JPEG in PSRAM (sensor is back in JPEG mode here — the grayscale
    // window closed inside captureGrayscaleThumb). Saved on the falling edge.
    if (s_motionActive && s_eventCounted && s_score > s_bestScore &&
        (s_cfg.action == MOTION_ACTION_SNAP_PHOTO ||
         s_cfg.action == MOTION_ACTION_EMAIL_ALERT)) {
        camera_fb_t* bfb = cameraCaptureFrame();
        if (bfb && bfb->len > 0 && bfb->len <= MOTION_BEST_MAX_BYTES) {
            if (bfb->len > s_bestCap) {
                if (s_bestBuf) free(s_bestBuf);
                s_bestBuf = (uint8_t*)(psramFound() ? ps_malloc(bfb->len) : malloc(bfb->len));
                s_bestCap = s_bestBuf ? bfb->len : 0;
            }
            if (s_bestBuf) {
                memcpy(s_bestBuf, bfb->buf, bfb->len);
                s_bestLen = bfb->len;
                s_bestScore = s_score;
            }
        }
        if (bfb) esp_camera_fb_return(bfb);
    }

    // Action dispatch (Task 5): record_video | snap_photo | email_alert.
    // record_video keeps the existing event-driven AVI flow; snap_photo now
    // saves the best frame on the falling edge (see above); email_alert fires
    // once per event (rising edge). Cooldown-suppressed events are tracked
    // but dispatch nothing.
    if (s_motionActive && !s_eventCounted) return;
    if (s_motionActive) {
        switch (s_cfg.action) {
            case MOTION_ACTION_RECORD_VIDEO:
#if ENABLE_SDCARD==1
                // Start when motion detected; ownership rule guards the post-timeout.
                if (!cameraRecordActive()) {
                    const CameraConfig& cfg = cameraGetConfig();
                    uint16_t w, h;
                    cameraGetOutputDimensions(w, h);
                    if (cameraRecordStart(cfg.fps, (uint8_t)cfg.framesize, w, h, "MOT")) {
                        s_ownRecording = true;
                    }
                }
#endif
                break;

            case MOTION_ACTION_SNAP_PHOTO:
                // Best frame is saved on the falling edge — nothing to do here.
                // Mark fired so the guard never re-enters mid-event.
                s_actionFired = true;
                break;

            case MOTION_ACTION_EMAIL_ALERT:
#if ENABLE_SMTP==1
                if (!s_actionFired) {
                    s_actionFired = true;
                    // No attachment; text-only alert. startInactivityAlertAsync()
                    // is single-flight + priority-gated, so repeated motion events
                    // converge instead of queuing a storm. Recipients come from the
                    // configured email destination.
                    startInactivityAlertAsync(
                        configManager.getEmailDestination(),
                        String("[") + PACK_SYSTEM_NAME + "] Motion detected",
                        String("Motion detected on ") + PACK_SYSTEM_NAME + " at " +
                            String(millis() / 1000UL) + "s uptime.");
                }
#else
                // SMTP not compiled in: mark fired to keep the guard from re-entering.
                s_actionFired = true;
#endif
                break;

            default:
                break;
        }
    } else {
        // Motion ended (or never started): settle recording ownership.
#if ENABLE_SDCARD==1
        // If OUR recording is active but motion has stopped, check post-record timeout.
        if (!cameraRecordActive()) {
            s_ownRecording = false;
        } else if (s_ownRecording && s_motionEndMs > 0) {
            uint32_t postMs = (uint32_t)s_cfg.post_record_secs * 1000UL;
            if ((nowMs - s_motionEndMs) >= postMs) {
                cameraRecordFinish();
                s_ownRecording = false;
            }
        }
#endif
    }
}

#endif // ENABLE_CAMERA==1
