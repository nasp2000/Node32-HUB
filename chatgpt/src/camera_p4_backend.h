#pragma once
// camera_p4_backend.h — ESP32-P4 MIPI-CSI capture backend (OV5647).
//
// The P4 has no esp_camera driver upstream, so the S3 esp_camera API cannot
// be used here. This backend drives the Arduino-ESP32 ESP_Video stack
// (MIPI CSI + ISP JPEG encoder, same as Waveshare's Camera_Preview sketch)
// and exposes JPEG frames to the shared camera pump (stream cache, AVI
// recorder, snapshot/photo).
//
// Guards: everything here compiles only for TARGET_BOARD_P4. On S3 this
// header is an empty shell so shared callers need no extra #ifs.

#include "feature_flags.h"

#include "camera_capture_guard.h"  // stall fast-fail logic (host-tested)

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Minimal camera_fb_t shim so shared S3-era code (stream cache pump, AVI
// recorder, snapshot/photo, motion snap) compiles unchanged on P4.
// Semantics: buf is an owned PSRAM/malloc copy; the single call that
// consumes it MUST call esp_camera_fb_return() exactly once.
struct camera_fb_t {
    uint8_t* buf = nullptr;
    size_t   len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    // false: buf is owned elsewhere (a pooled slot) — return must not free it.
    bool     owned = true;
    // Custom release for pooled frames (recycles the slot, frees the struct).
    // When set it takes precedence over `owned`.
    void   (*release)(struct camera_fb_t*) = nullptr;
};

static inline void esp_camera_fb_return(camera_fb_t* fb) {
    if (!fb) return;
    if (fb->release) { fb->release(fb); return; }
    if (fb->buf && fb->owned) free(fb->buf);
    free(fb);
}

// Capability probe: returns a short human-readable string describing what
// the toolchain offers (ESP_Video present, MIPI/DVP Kconfig, JPEG format).
// Never touches hardware. Used to validate the P4 build before HW exists.
const char* cameraP4ProbeCaps();

// Capture source on P4: MIPI-CSI (OV5647) or USB-UVC (any MJPEG webcam).
// Persisted in CameraConfig.source; selected in the Camera panel.
#define CAM_P4_SRC_MIPI 0
#define CAM_P4_SRC_USB  1

// The other capture source — used by the manager's start fallback: when the
// requested source fails to open (unplugged webcam, heap, descriptor quirk)
// the other attached source is tried before giving up, so the board never
// ends with no camera while one is physically present.
static inline uint8_t cameraP4OtherSource(uint8_t source) {
    return (source == CAM_P4_SRC_USB) ? CAM_P4_SRC_MIPI : CAM_P4_SRC_USB;
}

// Initialise the selected pipeline at the given resolution.
// w/h: capture size (e.g. 1280x720). quality: JPEG 4..63 (mapped internally).
// Returns true when the first JPEG frame was captured successfully.
bool cameraP4Begin(uint16_t w, uint16_t h, uint8_t quality, uint8_t source);

// Tear down the pipeline (idempotent).
void cameraP4End();

// True after a successful cameraP4Begin(), false after cameraP4End().
// (Manager tracks readiness itself; kept for diagnostics/tests.)
bool cameraP4Ready();

// Capture one JPEG frame. Copies into the loop-owned PSRAM cache area via
// the provided writer: calls write(const uint8_t*, size_t) chunks and
// returns total bytes, or 0 on failure. Never blocks longer than timeoutMs
// plus one driver-bounded dequeue (VIDIOC_S_DQBUF_TIMEOUT when this core
// supports it); while the pipeline is known-stalled it fast-fails with
// only a bounded retry, so no task (HTTP included) can wedge here.
typedef void (*CameraP4ChunkWriter)(const uint8_t* data, size_t len, void* ctx);
size_t cameraP4Capture(CameraP4ChunkWriter write, void* ctx, uint32_t timeoutMs);

// Same as cameraP4Capture(), plus a raw UYVY tap: every dequeued CSI frame is
// also emitted via rawWrite BEFORE the JPEG encode (Fase 1, Tarefa 5 — feeds
// the H.264 HW encoder with a single dequeue). Null rawWrite = old behaviour.
// USB source has no raw UYVY (MJPEG on-device): rawWrite never fires there.
size_t cameraP4CaptureWithRaw(CameraP4ChunkWriter write, void* ctx,
                              CameraP4ChunkWriter rawWrite, void* rawCtx,
                              uint32_t timeoutMs);

// Motion tap (P4 only): dequeue one UYVY frame and downsample its luma into
// a dstW×dstH grayscale thumb — motion sampling without a sensor pixformat
// switch (the S3 esp_camera API does not exist on this backend). No JPEG
// encode is done. Bounded by timeoutMs; the CAM-62 stall fast-fail applies.
// False on USB source (MJPEG on-device, no UYVY), when not capturing, or on
// timeout — callers treat false as "idle this round".
bool cameraP4CaptureGrayThumb(uint8_t* dst, uint16_t dstW, uint16_t dstH,
                              uint32_t timeoutMs);

// Raw UYVY dequeue with NO JPEG encode (perf): for a H.264 session with no
// viewer, where the MJPEG frame would be produced for nobody. Same bounds as
// cameraP4Capture(). 0 = no frame (busy/stall/timeout/USB source).
size_t cameraP4CaptureRaw(CameraP4ChunkWriter rawWrite, void* rawCtx,
                          uint32_t timeoutMs);

// Loop-task stall recovery (CAM-62): re-syncs a stalled MIPI session with
// stopCapture/startCapture, at most once per P4_CAP_RESTART_MS. Must only be
// called from the loop task — session teardown races concurrent dequeues.
// No-op unless ready + MIPI + stalled.
void cameraP4StallTick(uint32_t nowMs);

// True while the MIPI pipeline is known-stalled (no good frame for
// P4_CAP_STALL_MS). Exposed for the status page (capStall).
bool cameraP4CaptureStalledNow();

// Live controls (subset of the sensor API; unsupported ones are no-ops).
// Returns false when not ready or unsupported.
bool cameraP4SetQuality(uint8_t quality);   // JPEG 4..63
bool cameraP4SetFlip(bool hmirror, bool vflip);
bool cameraP4SetResolution(uint16_t w, uint16_t h);  // reopens capture dev

// Detected model string for the status page ("OV5647" / "USB-UVC").
const char* cameraP4Model();

// Maximum framesize_t enum int (5MP class ceiling for both sources).
int cameraP4MaxFrameSize();

// True when the active source is USB (used for status/UI hints).
bool cameraP4IsUsb();

// Ask a runaway USB begin to abort (stop-during-probing). The begin checks
// the flag in its wait/sweep loops and unwinds promptly.
void cameraP4CancelUsb();

// Presence detection (no capture): MIPI via an SCCB ACK at 0x36, USB via a
// quiet bus walk for a UVC VideoStreaming interface. Refreshes at most every
// 10 s and only while idle — never touches a live pipeline; the status page
// reads the cache. Lets the Source selector list attached cameras BEFORE
// any Start.
void cameraP4DetectSources();
bool cameraP4MipiPresent();
bool cameraP4UsbPresent();
uint16_t cameraP4UsbVid();
uint16_t cameraP4UsbPid();

// Live pipeline geometry (actual negotiated size, not the request).
// MIPI: driver fixed mode (800x800 OV5647 here). USB: negotiated size
// (0,0 until the first successful start this boot).
void cameraP4ActualDims(uint16_t &w, uint16_t &h);

// CSI mode offer: "WxH,WxH,..." of the known sensor modes (high to low).
// Driver-refused modes stay listed: selecting one runs the SCCB-direct
// switch with revert-on-failure. Drives the UI resList.
String cameraP4CsiResList();

// Known-table membership (no hardware touched): is WxH a real OV5647 MIPI
// sensor mode? Used to validate the UI resolution selector before persisting.
bool cameraP4ValidCsiMode(uint16_t w, uint16_t h);

// Honest fps ceiling for the UI slider: measured sustained MJPEG rate.
// MIPI: 6 (~5.8fps UYVY+HW-encode @800x800). USB: negotiated, else 30.
uint8_t cameraP4FpsMax();

// Fase 4 (Tarefa 5): H.264 path ceiling (MIPI 30 = sensor rate; MJPEG keeps
// cameraP4FpsMax()). USB: negotiated, else 30 (same either path).
uint8_t cameraP4FpsMaxH264();

#else  // !TARGET_BOARD_P4

static inline const char* cameraP4ProbeCaps() { return "P4 backend not compiled"; }

#endif // TARGET_BOARD_P4
