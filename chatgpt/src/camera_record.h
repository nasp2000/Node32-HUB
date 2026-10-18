#pragma once
// camera_record.h — AVI/MJPEG recording to SD and JPEG timelapse.
//
// AVI file layout produced by this module:
//   [0..211]   Fixed RIFF/hdrl/strl header (212 bytes, written as placeholder)
//   [212..223] LIST movi header (12 bytes, written as placeholder)
//   [224..]    '00dc' frame chunks (8 bytes header + JPEG data, padded to WORD)
//
// On cameraRecordFinish(): seek(0) and rewrite header with correct counts/sizes.
// Folder structure on SD: /CAM/YYYY-MM-DD/YYYY-MM-DD_HHMMSS_RES_FPS.avi

#include "feature_flags.h"

#if ENABLE_CAMERA==1

#include <Arduino.h>
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "camera_p4_backend.h"  // P4: no esp_camera; shim + local framesize_t
#else
#include "esp_camera.h"  // for framesize_t
#endif

// Inline helper: map framesize_t enum value to pixel dimensions.
// Used in camera_manager.cpp and camera_handler.cpp.
// Delegates to camera_sensor_map.h (single source of truth, framework-
// independent and covered by native tests). The framework `framesize_t`
// enum (sensor.h) is: 0=96x96 … 10=VGA … 13=HD … 15=UXGA … 19=QXGA …
// 24=5MP (2592x1944). Unknown values fall back to VGA.
#include "camera_sensor_map.h"
#include "camera_record_error.h"  // CameraRecordStartResult + cause messages

inline void cameraFrameSizeToDims(uint8_t fs, uint16_t& w, uint16_t& h) {
    cameraSensorFrameSizeDims(fs, w, h);
}

// Folder roots on the SD card.
#define CAM_REC_ROOT  "/CAM"
#define CAM_TL_FOLDER "/CAM/TIMELAPSE"
// Staging folder for pre-NTP captures (see camera_record_naming.h): files
// started before the RTC sync are written here and migrated to /CAM/YYYY-MM-DD
// once the clock is ready — never into a bogus 1970-01-01 dated folder.

// --- Recording session control ---

// Open a new AVI file on the SD card and write the placeholder header.
// w/h must match the actual output dimensions of the active camera mode.
// tag: short origin label baked into the final filename on finish
// (e.g. "M" manual, "MOT" motion, "DASH" dashcam). nullptr = no tag.
// Returns false if SD is unavailable or the file cannot be created.
bool cameraRecordStart(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h, const char* tag = nullptr);

// Same as cameraRecordStart(), but reports the granular failure cause through
// outCause (touched only on failure; ignored when nullptr). Used by the web
// handler to answer with a precise message instead of the generic "(SD?)".
bool cameraRecordStartEx(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h,
                         const char* tag, CameraRecordStartResult* outCause);

// Append one JPEG frame to the active recording.
void cameraRecordFrame(const uint8_t* jpegData, size_t jpegLen);

// --- Fase 1 (Tarefa 5): H.264 recording (CAM_P4 / P4 MIPI only) ---
// Fase 2: container 0=raw Annex B `.264`, 1=H.264-in-AVI (`.avi` with `H264`
// fcc + SPS/PPS extradata; 480-byte fixed header). Raw stays the fallback.
// MJPEG/AVI stays the default everywhere; H.264 is opt-in.
// Open the file + the HW encoder session (bitrateKbps 1000–8000).
// Returns false with the granular cause (H264_* when the encoder/source
// cannot do it — S3, USB source, or a core without esp_h264).
bool cameraRecordStartH264(uint8_t fps, uint8_t framesizeVal, uint16_t w, uint16_t h,
                           uint32_t bitrateKbps, uint8_t container, const char* tag,
                           CameraRecordStartResult* outCause);

// Append one encoder output blob (one or more Annex B NALs) to the active
// `.264` recording. No-op unless a H.264 session is active.
void cameraRecordFrameH264(const uint8_t* nalData, size_t nalLen);

// True while the active recording is H.264 (false for MJPEG/AVI).
bool cameraRecordIsH264();

// H.264 container of the active session: 0=raw `.264`, 1=H.264-in-AVI
// (Fase 2; 0 when no H.264 session is active).
uint8_t cameraRecordContainer();

// Finalise the recording: AVI seeks to 0 and rewrites the header; `.264`
// just closes (NALs are already verbatim). Returns false if nothing recorded.
bool cameraRecordFinish();

// True while a recording session is active.
bool cameraRecordActive();

// Photo fast path (needs ENABLE_SDCARD): hand an owned JPEG copy to the
// background saver and answer the POST immediately; the tick writes it out.
// False = both slots busy (caller writes synchronously instead).
bool cameraPhotoSubmit(uint8_t* img, size_t len, const String& path);
// Last-photo RAM copy for `/api/camera/last-photo` (viewable even with the
// SD save queued or the card dead). False when empty.
bool cameraPhotoLastJson(uint8_t const** outData, size_t* outLen);
// Stage a photo for the RAM copy (called on every successful grab, both
// fast and synchronous paths). Skips frames above PHOTO_RAM_MAX.
void cameraPhotoRemember(const uint8_t* img, size_t len);
// Background photo saves still queued (0..2), served as `photoPend`.
uint8_t cameraPhotoPending();

// PSRAM ring health (0/0 when the ring is off): bytes currently held and
// lifetime oldest-first drops. Served as `psramBuf`/`psramDrop` in status.
void cameraRecordRingStats(uint32_t* outUsed, uint32_t* outDropped);

// Full SD path of the file being recorded (empty string if inactive).
const String& cameraRecordPath();

// Elapsed recording time in seconds (0 if inactive).
uint32_t cameraRecordElapsedSecs();

// Number of frames appended so far in the active session.
uint32_t cameraRecordFrameCount();

// Save a single JPEG to /CAM/MOTION/YYYYMMDD_HHMMSS.jpg (motion snap action).
// Returns false if SD is unavailable or write fails.
bool cameraMotionSnapSave(const uint8_t* jpegData, size_t jpegLen, String* outPath = nullptr);

// True while the periodic timelapse capture is armed (timelapse_secs > 0).
bool cameraTimelapseActive();

// Seconds remaining until the next timelapse capture (0 when inactive).
// Used by the web UI to show "next capture in Xs".
uint32_t cameraTimelapseNextInSecs();

// Last timelapse capture filename (empty if none yet this session).
const String& cameraTimelapseLastPath();

// Called from cameraLoop(): drives dashcam auto-restart and timelapse interval.
// nowMs: current millis().
void cameraRecordTick(uint32_t nowMs);

#endif // ENABLE_CAMERA==1
