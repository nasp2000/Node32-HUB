#pragma once
// camera_record_error.h — granular causes for cameraRecordStart() failures.
//
// Header-only, zero ESP32 deps — testable on Linux/MinGW. Used by
// camera_record.cpp to report the real failure (SD vs mkdir vs open vs header)
// and by camera_handler.cpp to map that cause to a distinct API message.
//
// CAM-3: the P4 Cam (.18) always answered the generic
// "Record start failed (SD?)" because the mkdir return values inside
// cameraRecordStart() were ignored. Telling a folder-create failure apart from
// a real open()/header failure is what this enum is for.

#include <stdint.h>

enum CameraRecordStartResult : uint8_t {
    CAM_REC_OK = 0,
    CAM_REC_ALREADY_ACTIVE,
    CAM_REC_SD_UNAVAILABLE,
    CAM_REC_MKDIR_ROOT,
    CAM_REC_MKDIR_FOLDER,
    CAM_REC_OPEN_FAILED,
    CAM_REC_HEADER_FAILED,
    CAM_REC_H264_UNAVAILABLE,  // Fase 1 (Tarefa 5): H.264 asked but unusable
    CAM_REC_H264_ENCODER_FAIL,  // here (no HW encoder / wrong source / open fail)
};

// Short stable token for logs (e.g. "mkdir_folder").
static inline const char* cameraRecordStartCauseTag(CameraRecordStartResult r) {
    switch (r) {
        case CAM_REC_OK:             return "ok";
        case CAM_REC_ALREADY_ACTIVE: return "already_active";
        case CAM_REC_SD_UNAVAILABLE: return "sd_unavailable";
        case CAM_REC_MKDIR_ROOT:     return "mkdir_root";
        case CAM_REC_MKDIR_FOLDER:   return "mkdir_folder";
        case CAM_REC_OPEN_FAILED:    return "open_failed";
        case CAM_REC_HEADER_FAILED:  return "header_failed";
        case CAM_REC_H264_UNAVAILABLE: return "h264_unavailable";
        case CAM_REC_H264_ENCODER_FAIL: return "h264_encoder_fail";
        default:                     return "unknown";
    }
}

// Message for the API body {"success":false,"message":"..."}.
static inline const char* cameraRecordStartCauseMessage(CameraRecordStartResult r) {
    switch (r) {
        case CAM_REC_ALREADY_ACTIVE: return "Already recording";
        case CAM_REC_SD_UNAVAILABLE: return "Record start failed (SD unavailable)";
        case CAM_REC_MKDIR_ROOT:     return "Record start failed (mkdir /CAM)";
        case CAM_REC_MKDIR_FOLDER:   return "Record start failed (mkdir folder)";
        case CAM_REC_OPEN_FAILED:    return "Record start failed (open file)";
        case CAM_REC_HEADER_FAILED:  return "Record start failed (header)";
        case CAM_REC_H264_UNAVAILABLE: return "H.264 needs the P4 MIPI source";
        case CAM_REC_H264_ENCODER_FAIL: return "Record start failed (H.264 encoder)";
        default:                     return "Record start failed";
    }
}
