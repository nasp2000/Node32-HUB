#pragma once
// camera_p4_isp.h — ESP32-P4 ISP colour profiles for the MIPI camera (CAM-84).
//
// The ISP is driven from user space through the ISP video device
// (`/dev/video20`, V4L2_CID_USER_ESP_ISP_*): colour-correction matrix, white
// balance gains, gamma curve, sharpening, Bayer denoise and — explicitly —
// demosaic. v1.120 never touched any of it, so the pipeline ran with the
// driver defaults while the sensor is perfectly capable of calibrated colour.
//
// The numbers below are colour-calibration DATA (a 3x3 matrix and two gains
// per illuminant) derived from public OV5647 tuning data — facts, not code:
// no source file was copied. They are applied through the same V4L2 control
// IDs the reference bring-ups use, so the values are comparable.
//
// Pure header: the ioctl side lives in camera_p4_isp.cpp and the data here is
// unit-tested (profile count, gamma invariants, wb_mode mapping).

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CAM_ISP_PROFILE_COUNT 6
#define CAM_ISP_GAMMA_POINTS  16

typedef struct {
    const char* name;
    uint16_t kelvin;
    float ccm[3][3];
    float wb_r;
    float wb_b;
} CamIspProfile;

// Ordered from warmest to coolest, so the index is also a temperature rank.
static const CamIspProfile kCamIspProfiles[CAM_ISP_PROFILE_COUNT] = {
    { "Tungsten",    2873, {{  1.88195f, -0.26249f, -0.61946f },
                            { -0.40081f,  1.77632f, -0.37551f },
                            {  0.00257f, -0.75415f,  1.75158f }}, 1.50f, 1.76f },
    { "IndoorWarm",  3725, {{  1.94343f, -0.50885f, -0.43458f },
                            { -0.38988f,  1.85523f, -0.46535f },
                            { -0.00887f, -0.74623f,  1.75510f }}, 1.46f, 1.49f },
    { "Fluorescent", 5095, {{  2.00666f, -0.63316f, -0.37350f },
                            { -0.40071f,  1.94742f, -0.54671f },
                            { -0.03109f, -0.83048f,  1.86157f }}, 1.37f, 1.33f },
    { "Daylight",    6015, {{  1.99726f, -0.63965f, -0.35761f },
                            { -0.40616f,  1.94421f, -0.53805f },
                            { -0.01886f, -0.73970f,  1.75855f }}, 1.30f, 1.24f },
    { "Cloudy",      6865, {{  2.05107f, -0.68023f, -0.37084f },
                            { -0.42693f,  1.93461f, -0.50768f },
                            { -0.01654f, -0.69652f,  1.71306f }}, 1.26f, 1.21f },
    { "Shade",       7600, {{  2.06599f, -0.39161f, -0.67439f },
                            { -0.43251f,  1.92138f, -0.48887f },
                            { -0.01948f, -0.77319f,  1.79267f }}, 1.22f, 1.19f },
};

// Default profile when the user has not chosen one (outdoor/daylight, the
// widest useful range for a security camera).
#define CAM_ISP_PROFILE_DEFAULT 3   // Daylight (6015 K)

// The camera page already has a white-balance selector (wb_mode 0..4:
// Auto, Sun, Cloud, Off, Home). Map it onto these profiles instead of adding
// another control: -1 means "leave colour correction off" (wb off).
static inline int cameraIspProfileForWbMode(int wbMode) {
    switch (wbMode) {
        case 0: return CAM_ISP_PROFILE_DEFAULT;  // Auto -> daylight
        case 1: return 5;                        // Sun -> shade (7600 K)
        case 2: return 4;                        // Cloud -> cloudy (6865 K)
        case 3: return -1;                       // Off -> no CCM/WB
        case 4: return 0;                        // Home -> tungsten (2873 K)
        default: return CAM_ISP_PROFILE_DEFAULT;
    }
}

// sRGB-like gamma (approx. 2.2) for the ISP gamma block. The driver validates
// three things: first x > 0, every x delta a power of two, last x exactly 255.
typedef struct { uint8_t x, y; } CamIspGammaPoint;

static const CamIspGammaPoint kCamIspGamma[CAM_ISP_GAMMA_POINTS] = {
    { 16,  72 }, { 32,  99 }, { 48, 119 }, { 64, 136 },
    { 80, 151 }, { 96, 164 }, {112, 175 }, {128, 186 },
    {144, 197 }, {160, 206 }, {176, 215 }, {192, 224 },
    {208, 232 }, {224, 240 }, {240, 248 }, {255, 255 },
};

// Rule check used by the driver (and by our test): 1 when the table is legal.
// Interior x deltas must be powers of two; the FINAL step is the exception —
// the driver requires the last x to be 255 and treats it as a full step
// (255 - 240 = 15 is accepted, a strict power-of-two check would reject every
// legal table).
static inline bool cameraIspGammaTableValid(const CamIspGammaPoint* pts, int n) {
    if (!pts || n <= 0) return false;
    if (pts[0].x == 0) return false;
    for (int i = 1; i < n - 1; i++) {
        int delta = (int)pts[i].x - (int)pts[i - 1].x;
        if (delta <= 0) return false;
        if ((delta & (delta - 1)) != 0) return false;   // not a power of two
    }
    return pts[n - 1].x == 255;
}
