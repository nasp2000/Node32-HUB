#pragma once
// camera_sensor_map.h — PID -> model name and PID -> max framesize mapping.
//
// Self-contained and framework-independent so native tests can compile this
// header directly (no Arduino/esp_camera includes). Values mirror the
// esp32-camera framework:
//   - PIDs match `camera_pid_t` in driver/include/sensor.h
//   - max framesizes match the `max_size` column of `camera_sensor[]`
//     in driver/sensor.c (OV5640 is raised from QSXGA to 5MP because its
//     set_framesize() supports 2592x1944 via the sensor-level call).
//
// framesize values are the esp32-camera `framesize_t` enum integers:
//   VGA=10, HD=13, UXGA=15, QXGA=19, 5MP=24.

#include <stdint.h>
#include <cstdio>
#include <stddef.h>

// framesize_t source:
//   - S3 firmware (ARDUINO, not P4): the framework enum from esp_camera.h,
//     pulled by camera_manager.h / camera_record.h / camera_motion.cpp.
//     This header defines NOTHING here (a #ifndef on the enumerator names
//     can never work — #ifndef only sees macros, and enum values would
//     collide on double definition regardless of include order).
//   - P4 firmware: no esp_camera exists — use the mirror below.
//   - Host unit tests (no ARDUINO macro): use the mirror below.
#if (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1) || !defined(ARDUINO)
#define CAM_FRAMESIZE_MIRROR 1
#endif
#ifdef CAM_FRAMESIZE_MIRROR
typedef uint8_t framesize_t;
enum : uint8_t {
    FRAMESIZE_96X96   = 0,   // 96x96
    FRAMESIZE_QQVGA   = 1,   // 160x120
    FRAMESIZE_128X128 = 2,   // 128x128
    FRAMESIZE_QCIF    = 3,   // 176x144
    FRAMESIZE_HQVGA   = 4,   // 240x176
    FRAMESIZE_240X240 = 5,   // 240x240
    FRAMESIZE_QVGA    = 6,   // 320x240
    FRAMESIZE_320X320 = 7,   // 320x320
    FRAMESIZE_CIF     = 8,   // 400x296
    FRAMESIZE_HVGA    = 9,   // 480x320
    FRAMESIZE_VGA     = 10,  // 640x480
    FRAMESIZE_SVGA    = 11,  // 800x600
    FRAMESIZE_XGA     = 12,  // 1024x768
    FRAMESIZE_HD      = 13,  // 1280x720
    FRAMESIZE_SXGA    = 14,  // 1280x1024
    FRAMESIZE_UXGA    = 15,  // 1600x1200
    FRAMESIZE_FHD     = 16,  // 1920x1080
    FRAMESIZE_P_HD    = 17,  // 720x1280
    FRAMESIZE_P_3MP   = 18,  // 864x1536
    FRAMESIZE_QXGA    = 19,  // 2048x1536
    FRAMESIZE_QHD     = 20,  // 2560x1440
    FRAMESIZE_WQXGA   = 21,  // 2560x1600
    FRAMESIZE_P_FHD   = 22,  // 1080x1920
    FRAMESIZE_QSXGA   = 23,  // 2560x1920
    FRAMESIZE_5MP     = 24,  // 2592x1944
    FRAMESIZE_INVALID = 25
};
#endif

// USB VID:PID label for the P4 source selector ("1b3f:1167"). Lowercase hex,
// no padding, no "0x" — short enough for the Source dropdown and the status
// payload. out must hold at least 10 bytes ("ffff:ffff" + NUL).
inline void cameraFormatUsbId(uint16_t vid, uint16_t pid, char* out, size_t n) {
    if (!out || n == 0) return;
    snprintf(out, n, "%x:%x", (unsigned)vid, (unsigned)pid);
}

// PID values from the framework `camera_pid_t` enum.
enum : uint16_t {
    CAM_SENSOR_PID_OV7725   = 0x0077,
    CAM_SENSOR_PID_OV2640   = 0x0026,
    CAM_SENSOR_PID_OV3660   = 0x3660,
    CAM_SENSOR_PID_OV5640   = 0x5640,
    CAM_SENSOR_PID_OV7670   = 0x0076,
    CAM_SENSOR_PID_NT99141  = 0x1410,
    CAM_SENSOR_PID_GC2145   = 0x2145,
    CAM_SENSOR_PID_GC032A   = 0x232a,
    CAM_SENSOR_PID_GC0308   = 0x009b,
    CAM_SENSOR_PID_BF3005   = 0x0030,
    CAM_SENSOR_PID_BF20A6   = 0x20a6,
    CAM_SENSOR_PID_SC101IOT = 0xda4a,
    CAM_SENSOR_PID_SC030IOT = 0x9a46,
    CAM_SENSOR_PID_SC031GS  = 0x0031,
    CAM_SENSOR_PID_MEGA_CCM = 0x039e,
    CAM_SENSOR_PID_HM1055   = 0x0955,
    CAM_SENSOR_PID_HM0360   = 0x0360
};

// Registered sensor model name for a PID, or nullptr when not registered.
inline const char* cameraSensorModelName(uint16_t pid) {
    switch (pid) {
        case CAM_SENSOR_PID_OV7725:   return "OV7725";
        case CAM_SENSOR_PID_OV2640:   return "OV2640";
        case CAM_SENSOR_PID_OV3660:   return "OV3660";
        case CAM_SENSOR_PID_OV5640:   return "OV5640";
        case CAM_SENSOR_PID_OV7670:   return "OV7670";
        case CAM_SENSOR_PID_NT99141:  return "NT99141";
        case CAM_SENSOR_PID_GC2145:   return "GC2145";
        case CAM_SENSOR_PID_GC032A:   return "GC032A";
        case CAM_SENSOR_PID_GC0308:   return "GC0308";
        case CAM_SENSOR_PID_BF3005:   return "BF3005";
        case CAM_SENSOR_PID_BF20A6:   return "BF20A6";
        case CAM_SENSOR_PID_SC101IOT: return "SC101IOT";
        case CAM_SENSOR_PID_SC030IOT: return "SC030IOT";
        case CAM_SENSOR_PID_SC031GS:  return "SC031GS";
        case CAM_SENSOR_PID_MEGA_CCM: return "MEGA_CCM";
        case CAM_SENSOR_PID_HM1055:   return "HM1055";
        case CAM_SENSOR_PID_HM0360:   return "HM0360";
        default: return nullptr;
    }
}

// Pixel dimensions for a framesize_t enum value. Values mirror the framework
// `framesize_t` enum (sensor.h): 0=96x96 … 10=VGA … 13=HD … 15=UXGA …
// 19=QXGA … 24=5MP (2592x1944). Unknown values fall back to VGA (640x480).
inline void cameraSensorFrameSizeDims(uint8_t fs, uint16_t& w, uint16_t& h) {
    switch (fs) {
        case 0:  w = 96;    h = 96;    break;  // FRAMESIZE_96X96
        case 1:  w = 160;   h = 120;   break;  // FRAMESIZE_QQVGA
        case 2:  w = 128;   h = 128;   break;  // FRAMESIZE_128X128
        case 3:  w = 176;   h = 144;   break;  // FRAMESIZE_QCIF
        case 4:  w = 240;   h = 176;   break;  // FRAMESIZE_HQVGA
        case 5:  w = 240;   h = 240;   break;  // FRAMESIZE_240X240
        case 6:  w = 320;   h = 240;   break;  // FRAMESIZE_QVGA
        case 7:  w = 320;   h = 320;   break;  // FRAMESIZE_320X320
        case 8:  w = 400;   h = 296;   break;  // FRAMESIZE_CIF
        case 9:  w = 480;   h = 320;   break;  // FRAMESIZE_HVGA
        case 10: w = 640;   h = 480;   break;  // FRAMESIZE_VGA
        case 11: w = 800;   h = 600;   break;  // FRAMESIZE_SVGA
        case 12: w = 1024;  h = 768;   break;  // FRAMESIZE_XGA
        case 13: w = 1280;  h = 720;   break;  // FRAMESIZE_HD
        case 14: w = 1280;  h = 1024;  break;  // FRAMESIZE_SXGA
        case 15: w = 1600;  h = 1200;  break;  // FRAMESIZE_UXGA
        case 16: w = 1920;  h = 1080;  break;  // FRAMESIZE_FHD
        case 17: w = 720;   h = 1280;  break;  // FRAMESIZE_P_HD
        case 18: w = 864;   h = 1536;  break;  // FRAMESIZE_P_3MP
        case 19: w = 2048;  h = 1536;  break;  // FRAMESIZE_QXGA
        case 20: w = 2560;  h = 1440;  break;  // FRAMESIZE_QHD
        case 21: w = 2560;  h = 1600;  break;  // FRAMESIZE_WQXGA
        case 22: w = 1080;  h = 1920;  break;  // FRAMESIZE_P_FHD
        case 23: w = 2560;  h = 1920;  break;  // FRAMESIZE_QSXGA
        case 24: w = 2592;  h = 1944;  break;  // FRAMESIZE_5MP
        default: w = 640;   h = 480;   break;  // unknown -> VGA
    }
}

// Maximum output framesize the sensor handles (framesize_t enum integer).
inline int cameraSensorMaxFrameSize(uint16_t pid) {
    switch (pid) {
        // 5MP sensors
        case CAM_SENSOR_PID_OV5640:
        case CAM_SENSOR_PID_MEGA_CCM: return 24;       // FRAMESIZE_5MP   2592x1944
        // 3MP sensors
        case CAM_SENSOR_PID_OV3660:   return 19;       // FRAMESIZE_QXGA  2048x1536
        // 2MP sensors
        case CAM_SENSOR_PID_OV2640:
        case CAM_SENSOR_PID_GC2145:   return 15;       // FRAMESIZE_UXGA  1600x1200
        // 1080p/HD sensors
        case CAM_SENSOR_PID_NT99141:
        case CAM_SENSOR_PID_SC101IOT:
        case CAM_SENSOR_PID_HM1055:   return 13;       // FRAMESIZE_HD    1280x720
        // Everything else defaults to VGA-compatible output.
        default: return 10;                            // FRAMESIZE_VGA   640x480
    }
}