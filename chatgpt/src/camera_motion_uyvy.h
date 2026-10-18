#pragma once
// camera_motion_uyvy.h — UYVY luma box-downsample for P4 motion sampling.
//
// The P4 ESPVideo backend has no sensor pixformat-switch API (unlike the S3
// esp_camera driver), so motion thumbs come from the live UYVY stream
// instead of a dedicated grayscale capture: in UYVY (U Y0 V Y1) the luma of
// every pixel sits at the odd byte offsets, ready to average.
//
// Header-only, zero ESP32 deps — testable on Linux/MinGW. Used by
// camera_p4_backend.cpp (motion tap) and tests/native/test_camera_motion_uyvy.

#include <stdint.h>
#include <stddef.h>

// Box-downsample the Y (luma) plane of a srcW×srcH UYVY frame into a
// dstW×dstH grayscale thumb. Each output pixel is the average luma of its
// input block (same box-filter rule as downsampleGray in camera_motion.cpp).
// Odd srcW is truncated by one column (UYVY needs an even width). No-ops on
// null/zero arguments.
static inline void cameraDownsampleUyvy(const uint8_t* src, uint16_t srcW, uint16_t srcH,
                                        uint8_t* dst, uint16_t dstW, uint16_t dstH) {
    if (!src || !dst || !srcW || !srcH || !dstW || !dstH) return;
    if (srcW & 1) srcW--;
    if (!srcW) return;
    for (uint16_t ty = 0; ty < dstH; ty++) {
        uint16_t y0 = (uint16_t)((uint32_t)ty * srcH / dstH);
        uint16_t y1 = (uint16_t)((uint32_t)(ty + 1) * srcH / dstH);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > srcH) y1 = srcH;
        for (uint16_t tx = 0; tx < dstW; tx++) {
            uint16_t x0 = (uint16_t)((uint32_t)tx * srcW / dstW);
            uint16_t x1 = (uint16_t)((uint32_t)(tx + 1) * srcW / dstW);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > srcW) x1 = srcW;
            uint32_t sum = 0;
            for (uint16_t y = y0; y < y1; y++) {
                const uint8_t* row = src + (size_t)y * srcW * 2;
                for (uint16_t x = x0; x < x1; x++) sum += row[x * 2 + 1];
            }
            uint32_t count = (uint32_t)(y1 - y0) * (x1 - x0);
            dst[(size_t)ty * dstW + tx] = (uint8_t)(count ? sum / count : 0);
        }
    }
}
