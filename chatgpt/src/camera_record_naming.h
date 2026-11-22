#pragma once
// camera_record_naming.h — Pure folder/name decisions for SD recordings.
//
// Header-only, zero ESP32 deps — fully testable on Linux/MinGW.
// Used by camera_record.cpp at runtime and by tests/native/test_camera_record_naming
// to prove the "never a bogus 1970-01-01 folder" rule.
//
// DESIGN
// ------
// ESP32 boots with the RTC at epoch ≈ 0 (1970-01-01). NTP sync sets the clock
// after WiFi connects (tens of seconds later).  Building a date folder from
// the raw epoch produces the useless 1970-01-01 path that the user will never
// find and that contaminates the SD tree.
//
// Strategy:  pre-sync files are staged under /CAM/PENDING (non-bogus label,
// never a fake date).  Once the clock is ready the active file is migrated on
// finish and any remaining staged files are moved to /CAM/YYYY-MM-DD in a
// single pass by cameraRecordTick().  Synced boots skip the staging path
// entirely.

#include <time.h>
#include <stdint.h>
#include <stdio.h>

// Anything below 2020-09-13 is considered a meaningless / pre-NTP epoch.
// This threshold also guards against factory-reset boards whose RTC starts
// at zero.
#define CAM_CLOCK_SYNC_EPOCH  1600000000L   // 2020-09-13 00:00:00 UTC

// Staging folder name (SD path).  Keep in sync with camera_record.cpp.
#define CAM_REC_STAGE_FOLDER  "/CAM/PENDING"

// --- AVI declared frame rate (CAM-94) --------------------------------------
// The AVI header used to carry the CONFIGURED fps. Measured on the .18
// (2026-11-22): 1280x960 q20 configured 6 really captured ~1.3 fps (a 12 s clip
// played in 2.7 s), while 800x640 q35 stayed at 5.8 of 6. The rate the pipeline
// can sustain depends on resolution/quality, so the file must declare what it
// actually got — otherwise playback speed is wrong in both directions.
//
// Returns the fps to write in dwMicroSecPerFrame / strh.dwRate:
//   - fewer than 2 frames or no elapsed time: keep the configured rate (a
//     one-frame clip has no measurable rate and callers still show the intent);
//   - otherwise the rounded average, clamped to 1..30 (the device's domain:
//     MJPEG modes up to 12, H.264 up to 30).
static inline uint32_t cameraAviDeclaredFps(uint32_t frames, uint32_t elapsedSecs,
                                            uint32_t configuredFps) {
    if (frames < 2 || elapsedSecs == 0) return configuredFps ? configuredFps : 1;
    uint32_t fps = (frames + elapsedSecs / 2) / elapsedSecs;  // rounded
    if (fps < 1) fps = 1;
    if (fps > 30) fps = 30;
    return fps;
}

// --- Clock readiness -------------------------------------------------------

static inline bool cameraClockReady(time_t now) {
    return now >= CAM_CLOCK_SYNC_EPOCH;
}

// --- Date parts ------------------------------------------------------------

// Fill dayStr ("YYYY-MM-DD") and timeStr ("HHMMSS") for a given epoch.
// If the clock is not ready both strings are set to empty C-strings.
static inline void cameraDayTimeParts(time_t now, char dayStr[16], char timeStr[16]) {
    if (dayStr) dayStr[0] = '\0';
    if (timeStr) timeStr[0] = '\0';
    if (!cameraClockReady(now)) return;
    struct tm ti;
    localtime_r(&now, &ti);
    if (dayStr)  strftime(dayStr,  16, "%Y-%m-%d", &ti);
    if (timeStr) strftime(timeStr, 16, "%H%M%S", &ti);
}

// --- Folder selection -------------------------------------------------------

// Pick the SD target folder for a new recording / snapshot.
//   clock not ready  → CAM_REC_STAGE_FOLDER ("/CAM/PENDING")
//   clock ready      → readyFolder unchanged (e.g. "/CAM/2026-09-20", "/CAM/MOTION", ...)
// out must be at least 40 bytes.
static inline void cameraRecordPickFolder(time_t now, const char* readyFolder, char out[40]) {
    if (!cameraClockReady(now)) {
        snprintf(out, 40, "%s", CAM_REC_STAGE_FOLDER);
        return;
    }
    snprintf(out, 40, "%s", readyFolder);
}

// --- Base name components ---------------------------------------------------

// Base name for a new file.
//   pre-NTP  → "NOSYNC_<bootSec>"  (no bogus date embedded)
//   synced   → "YYYY-MM-DD_HHMMSS"
// out must be at least 40 bytes.
static inline void cameraRecordBaseName(time_t now, uint32_t bootSec, char out[40]) {
    if (!cameraClockReady(now)) {
        snprintf(out, 40, "NOSYNC_%lu", (unsigned long)bootSec);
        return;
    }
    char day[16], tim[16];
    cameraDayTimeParts(now, day, tim);
    snprintf(out, 40, "%s_%s", day, tim);
}

// --- Resolution label -------------------------------------------------------
// Filename label from the LIVE geometry. The legacy framesize enum is
// meaningless on P4 (a 720p session still carries framesize=6), so an
// enum-derived label put "QVGA" in the name of every P4 recording. Callers
// that have a trustworthy enum (S3) may keep using frameSizeLabel(); on P4
// the recorded geometry is the only honest source.
static inline const char* cameraRecordResLabel(uint16_t w, uint16_t h) {
    switch (w) {
        case 2592: return "5MP";
        case 1920: return "FHD";
        case 1600: return "UXGA";
        case 1280: return (h <= 720) ? "HD" : "SXGA";
        case 1024: return "XGA";
        case 800:  return "SVGA";
        case 640:  return "VGA";
        case 320:  return "QVGA";
        default:   return "VID";
    }
}

// Dated prefix prepended to a staged filename when it is migrated after sync.
// out must be at least 40 bytes (format: "YYYY-MM-DD_HHMMSS_").
static inline void cameraRecordMigratedPrefix(time_t now, char out[40]) {
    char day[16], tim[16];
    cameraDayTimeParts(now, day, tim);
    snprintf(out, 40, "%s_%s_", day, tim);
}
