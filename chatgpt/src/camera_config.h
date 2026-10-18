#pragma once
// camera_config.h — Camera configuration struct and LittleFS persistence.
// Config is stored in /config/camera.json (not NVS — NVS is reserved for WiFi/auth credentials).

#include "feature_flags.h"

#if ENABLE_CAMERA==1

#include <Arduino.h>
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "camera_sensor_map.h"  // P4: local framesize_t mirror (no esp_camera)
#else
#include "esp_camera.h"  // S3: framework framesize_t (sensor_map adds nothing here)
#endif
#include "camera_motion.h"  // MotionConfig

#define CAM_CFG_PATH      "/config/camera.json"
#define CAM_CFG_TMP_PATH  "/config/camera.json.tmp"

// All persistent camera settings.
struct CameraConfig {
    framesize_t framesize;    // FRAMESIZE_* enum value (e.g. FRAMESIZE_VGA = 8)
    uint8_t     quality;      // JPEG compression (0–63; lower = better quality, larger file)
    uint8_t     fps;          // Target capture FPS (1–30)
    int8_t      brightness;   // Sensor brightness (-2 to +2)
    int8_t      contrast;     // Sensor contrast (-2 to +2)
    int8_t      saturation;   // Sensor saturation (-2 to +2)
    bool        hmirror;      // Horizontal mirror
    bool        vflip;        // Vertical flip
    uint8_t     gainceiling;  // AGC gain ceiling (0–6, maps to gainceiling_t)
    bool        awb;          // Auto white balance
    bool        aec;          // Auto exposure control
    bool        agc;          // Auto gain control
    bool        lenc;         // Lens distortion correction
    // Extended sensor controls (MJPEG2SD parity)
    int8_t      special_effect; // 0=None,1=Negative,2=Grayscale,3=Red,4=Green,5=Blue,6=Sepia
    uint8_t     wb_mode;        // 0=Auto,1=Sunny,2=Cloudy,3=Office,4=Home
    uint16_t    aec_value;      // Manual exposure compensation (0–1200)
    bool        aec2;           // AEC DSP
    uint8_t     agc_gain;       // AGC gain (0–30, maps to 1×–31×)
    int8_t      ae_level;       // Exposure level (-2 to +2)
    // Phase 2 — recording
    uint8_t     dashcam_mins;    // 0=off; N=auto-rotate AVI every N minutes (dashcam mode)
    uint16_t    timelapse_secs;  // 0=off; N=capture one JPEG every N seconds
    // Phase 3 — motion detection
    MotionConfig motion;         // all motion-detection settings (see camera_motion.h)
    // Phase 4 — auto-start mode: which mode to auto-start at boot.
    // 0=off, 1=photo (snapshot-ready), 2=video (dashcam), 3=motion, 4=timelapse
    uint8_t     auto_mode;
    uint8_t     lamp;            // Onboard lamp brightness 0–15 (0=off; no-op when CAM_PIN_LAMP<0)
    uint8_t     source;          // Capture source: 0=native (DVP/S3, CSI/P4), 1=USB UVC (P4 only, ignored on S3)
    uint16_t    p4_res_w;        // P4 MIPI override width (0=auto: framesize dims, lenient)
    uint16_t    p4_res_h;        // P4 MIPI override height (0=auto)
    // Fase 1 (Tarefa 5, CAM_P4 only): 0=MJPEG/AVI (default), 1=H.264 raw .264
    uint8_t     rec_codec;
    uint16_t    h264_bitrate;    // kbps, 1000–8000 (Rec tab slider, 1–8 Mbps)
    // Fase 2 (CAM_P4 only): 0=raw `.264` (default), 1=H.264-in-AVI
    uint8_t     h264_container;
    // Fase 5 (Tarefa 5, CAM_P4 only): vision foundation — 0=Off, 1=Count,
    // 2=Defect (placeholders); thresh 0–100; one-shot dataset burst.
    uint8_t     vision_mode;
    uint8_t     vision_thresh;
    uint8_t     vision_burst_n;                 // 0=off, 1–50 (one-shot)
    char        vision_burst_label[17];         // FAT-safe label (≤16 chars)
};

// Returns a safe set of default values.

// Returns a safe set of default values.
inline CameraConfig cameraConfigDefaults() {
    CameraConfig c;
    c.framesize   = FRAMESIZE_QVGA;
    c.quality     = 20;
    c.fps         = 12;
    c.brightness  = 0;
    c.contrast    = 0;
    c.saturation  = 0;
    c.hmirror     = false;
    c.vflip       = false;
    c.gainceiling = 0;
    c.awb         = true;
    c.aec         = true;
    c.agc         = true;
    c.lenc          = true;
    c.special_effect = 0;
    c.wb_mode        = 0;
    c.aec_value      = 204;
    c.aec2           = true;
    c.agc_gain       = 5;
    c.ae_level       = 0;
    c.dashcam_mins   = 0;
    c.timelapse_secs = 0;
    c.motion         = motionConfigDefaults();
    c.auto_mode      = 0;
    c.lamp           = 0;
    c.source         = 0;
    c.p4_res_w       = 0;
    c.p4_res_h       = 0;
    c.rec_codec      = 0;    // MJPEG default (H.264 is opt-in, P4-only)
    c.h264_bitrate   = 4000;
    c.h264_container = 0;    // raw `.264` default (Fase 2 AVI is opt-in)
    c.vision_mode    = 0;    // Off (Fase 5 placeholders)
    c.vision_thresh  = 50;
    c.vision_burst_n = 0;
    c.vision_burst_label[0] = '\0';
    return c;
}

// Load from LittleFS; fills cfg with defaults on any error.
bool cameraConfigLoad(CameraConfig& cfg);

// Save to LittleFS using mandatory atomic write (.tmp → rename).
bool cameraConfigSave(const CameraConfig& cfg);

#endif // ENABLE_CAMERA==1
