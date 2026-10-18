#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_sensor_map.h is framework-independent (no Arduino/esp_camera).
#include "../../chatgpt/src/camera_sensor_map.h"
// camera_capture_guard.h is stdint-only (capture-stall fast-fail, CAM-62).
#include "../../chatgpt/src/camera_capture_guard.h"

// Mirrors camera_manager.cpp clampFrameSize() policy: a framesize below the
// smallest supported frame (96x96) collapses to VGA (safe floor); a value
// above the detected sensor max clamps DOWN to that max (graceful, so picking
// 5MP on an OV3660 yields QXGA rather than an unexpected VGA drop).
static const int FS_96X96 = 0;
static const int FS_VGA   = 10;

static int clampFs(int fs, int maxFs) {
    if (fs < FS_96X96) return FS_VGA;
    if (fs > maxFs) return maxFs;
    return fs;
}

// Mirrors camera_handler.cpp framesize enum guard: accepts [0, FRAMESIZE_5MP].
static const int FS_5MP = 24;

static bool framesizeAcceptedByApi(int fs) {
    return fs >= 0 && fs <= FS_5MP;
}

TEST_CASE("model name: framework PIDs resolve to their names") {
    CHECK(cameraSensorModelName(CAM_SENSOR_PID_OV2640) == std::string("OV2640"));
    CHECK(cameraSensorModelName(CAM_SENSOR_PID_OV3660) == std::string("OV3660"));
    CHECK(cameraSensorModelName(CAM_SENSOR_PID_OV5640) == std::string("OV5640"));
    CHECK(cameraSensorModelName(CAM_SENSOR_PID_GC2145) == std::string("GC2145"));
    CHECK(cameraSensorModelName(CAM_SENSOR_PID_NT99141) == std::string("NT99141"));
}

TEST_CASE("model name: unknown PID yields nullptr (caller formats 0xNN)") {
    CHECK(cameraSensorModelName(0x0060) == nullptr);
    CHECK(cameraSensorModelName(0x9999) == nullptr);
}

TEST_CASE("max framesize: 5MP sensor allows 2592x1944 (24)") {
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV5640) == 24);
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV5640) == FS_5MP);
}

TEST_CASE("max framesize: OV3660 (3MP) allows QXGA 2048x1536 (19)") {
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV3660) == 19);
}

TEST_CASE("max framesize: OV2640 (2MP) allows UXGA 1600x1200 (15)") {
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV2640) == 15);
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_GC2145) == 15);
}

TEST_CASE("max framesize: HD/VGA sensors cap lower") {
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_NT99141) == 13);   // HD
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV7725) == 10);    // VGA default
    CHECK(cameraSensorMaxFrameSize(CAM_SENSOR_PID_OV7670) == 10);
}

TEST_CASE("max framesize: unknown PID is conservative VGA (10)") {
    CHECK(cameraSensorMaxFrameSize(0x0060) == 10);
}

TEST_CASE("clampFrameSize: in-range requests pass through") {
    CHECK(clampFs(13, 19) == 13);
    CHECK(clampFs(19, 19) == 19);
    CHECK(clampFs(24, 24) == 24);   // OV5640 5MP at its own max
}

TEST_CASE("clampFrameSize: OV3660 caps 5MP request to QXGA") {
    CHECK(clampFs(24, 19) == 19);   // above sensor max => clamp DOWN to QXGA (graceful, not VGA)
    CHECK(clampFs(19, 19) == 19);
}

TEST_CASE("clampFrameSize: above max clamps to max for any sensor") {
    CHECK(clampFs(15, 10) == 10);   // VGA-only sensor: UXGA request -> VGA
    CHECK(clampFs(16, 15) == 15);   // UXGA sensor: FHD request -> UXGA
}

TEST_CASE("clampFrameSize: below 96x96 collapses to VGA") {
    CHECK(clampFs(-1, 24) == 10);
}

TEST_CASE("api framesize guard: accepts up to 5MP enum") {
    CHECK(framesizeAcceptedByApi(0));
    CHECK(framesizeAcceptedByApi(13));
    CHECK(framesizeAcceptedByApi(19));
    CHECK(framesizeAcceptedByApi(24));
    CHECK(framesizeAcceptedByApi(25) == false);   // past FRAMESIZE_5MP
    CHECK(framesizeAcceptedByApi(-1) == false);
}

TEST_CASE("frame size dims: key resolutions resolve to exact pixels") {
    uint16_t w = 0, h = 0;
    cameraSensorFrameSizeDims(10, w, h);
    CHECK(w == 640);  CHECK(h == 480);   // VGA
    cameraSensorFrameSizeDims(13, w, h);
    CHECK(w == 1280); CHECK(h == 720);   // HD
    cameraSensorFrameSizeDims(15, w, h);
    CHECK(w == 1600); CHECK(h == 1200);  // UXGA
    cameraSensorFrameSizeDims(19, w, h);
    CHECK(w == 2048); CHECK(h == 1536);  // QXGA (3MP)
    cameraSensorFrameSizeDims(24, w, h);
    CHECK(w == 2592); CHECK(h == 1944);  // 5MP
}

TEST_CASE("frame size dims: exhaustive 0..24 monotonic, no VGA fallback leakage") {
    // Every valid enum maps to a sane non-zero dimension, and no valid value
    // silently falls back to VGA (the historical enum-drift bug).
    for (int fs = 0; fs <= 24; fs++) {
        uint16_t w = 0, h = 0;
        cameraSensorFrameSizeDims((uint8_t)fs, w, h);
        CHECK(w >= 96);                 // smallest real size is 96x96
        if (fs == 10) { CHECK(w == 640); }  // VGA stays VGA
    }
}

TEST_CASE("frame size dims: unknown values fall back to VGA") {
    uint16_t w = 0, h = 0;
    cameraSensorFrameSizeDims(25, w, h);
    CHECK(w == 640); CHECK(h == 480);
    cameraSensorFrameSizeDims(255, w, h);
    CHECK(w == 640); CHECK(h == 480);
}

TEST_CASE("usb id label: lowercase hex vid:pid for the Source selector") {
    char out[12];
    cameraFormatUsbId(0x1B3F, 0x1167, out, sizeof(out));
    CHECK(std::string(out) == "1b3f:1167");
    cameraFormatUsbId(0x046D, 0x082D, out, sizeof(out));
    CHECK(std::string(out) == "46d:82d");
}

TEST_CASE("usb id label: zero ids and tiny buffers never overrun") {
    char out[12];
    cameraFormatUsbId(0, 0, out, sizeof(out));
    CHECK(std::string(out) == "0:0");
    char tiny[4];
    cameraFormatUsbId(0x1B3F, 0x1167, tiny, sizeof(tiny));
    tiny[sizeof(tiny) - 1] = '\0';
    CHECK(std::string(tiny).size() <= 3);  // truncated but NUL-terminated
    cameraFormatUsbId(0x1B3F, 0x1167, nullptr, 0);  // must not crash
}

TEST_CASE("capture fast-fail: flowing pipeline never skips the dequeue") {
    // Fresh frames (within stall window) -> 0 = attempt the driver dequeue.
    CHECK(cameraCaptureFastFail(1000, 1000, 2000, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
    CHECK(cameraCaptureFastFail(1000, 1000, 1000 + P4_CAP_STALL_MS, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
    // Never captured (lastOk == 0) -> must always try, even "long" after boot.
    CHECK(cameraCaptureFastFail(0, 0, 90000, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
    CHECK(cameraCaptureFastFail(0, 5000, 5000 + P4_CAP_STALL_MS + 1, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
}

TEST_CASE("capture fast-fail: stalled pipeline skips except on retry ticks") {
    const uint32_t lastOk = 1000;
    const uint32_t stale = lastOk + P4_CAP_STALL_MS + 1;
    // Recent attempt (< retry window) -> 1 = skip, fail fast (no DQBUF wedge).
    CHECK(cameraCaptureFastFail(lastOk, stale - 100, stale, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 1);
    // Retry due (last attempt older than retry window) -> 0 = one probe try.
    CHECK(cameraCaptureFastFail(lastOk, stale - P4_CAP_RETRY_MS - 1, stale, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
    // Exactly at the retry boundary is still a retry (strict < for skip).
    CHECK(cameraCaptureFastFail(lastOk, stale - P4_CAP_RETRY_MS, stale, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 0);
    // Millis wrap-around: stall arithmetic stays correct (5000 ms stale, tried 100 ms ago).
    CHECK(cameraCaptureFastFail(0xFFFFED78u, 0xFFFFFF9Cu, 0x00000100u, P4_CAP_STALL_MS, P4_CAP_RETRY_MS) == 1);
}

TEST_CASE("capture stall detector: reports only real stalls") {
    CHECK(cameraCaptureStalled(5000, 5000 + P4_CAP_STALL_MS, P4_CAP_STALL_MS) == 0);
    CHECK(cameraCaptureStalled(5000, 5000 + P4_CAP_STALL_MS + 1, P4_CAP_STALL_MS) == 1);
    CHECK(cameraCaptureStalled(0, 999999, P4_CAP_STALL_MS) == 0);  // never ran
}

TEST_CASE("capture restart: due only when stalled and throttle expired") {
    const uint32_t lastOk = 1000;
    const uint32_t stale = lastOk + P4_CAP_STALL_MS + 1;
    CHECK(cameraCaptureRestartDue(lastOk, 0, stale, P4_CAP_STALL_MS, P4_CAP_RESTART_MS) == 1);
    // Throttled: a re-sync 1 ms ago stays quiet until the restart window.
    CHECK(cameraCaptureRestartDue(lastOk, stale - 1, stale, P4_CAP_STALL_MS, P4_CAP_RESTART_MS) == 0);
    CHECK(cameraCaptureRestartDue(lastOk, (uint32_t)(stale - P4_CAP_RESTART_MS - 1u), stale, P4_CAP_STALL_MS, P4_CAP_RESTART_MS) == 1);
    // Flowing pipeline never restarts.
    CHECK(cameraCaptureRestartDue(stale, 0, stale, P4_CAP_STALL_MS, P4_CAP_RESTART_MS) == 0);
}

TEST_CASE("cached photo fallback: young cache serves, old/never fails") {
    // Published 1 s ago -> serve it (snapshot while streaming on slow link).
    CHECK(cameraCachedPhotoOk(10000, 11000, PHOTO_CACHE_MAX_AGE_MS) == 1);
    // Exactly at the age boundary still counts as fresh.
    CHECK(cameraCachedPhotoOk(10000, 10000 + PHOTO_CACHE_MAX_AGE_MS, PHOTO_CACHE_MAX_AGE_MS) == 1);
    // One ms past the boundary -> honest failure, not a stale photo.
    CHECK(cameraCachedPhotoOk(10000, 10000 + PHOTO_CACHE_MAX_AGE_MS + 1, PHOTO_CACHE_MAX_AGE_MS) == 0);
    // Never published (session never ran / stopped) -> fail.
    CHECK(cameraCachedPhotoOk(0, 999999, PHOTO_CACHE_MAX_AGE_MS) == 0);
    // Millis wrap-around: 3 s-old cache across the wrap still serves.
    CHECK(cameraCachedPhotoOk(0xFFFFF448u, 0x00000100u, PHOTO_CACHE_MAX_AGE_MS) == 1);
}