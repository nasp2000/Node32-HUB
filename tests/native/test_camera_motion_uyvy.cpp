#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>
#include <cstdint>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_motion_uyvy.h is a pure UYVY-luma box-downsample — zero ESP32 deps.
// P4 motion sampling has no sensor pixformat switch, so thumbs come from the
// live UYVY stream (luma at odd byte offsets: U Y0 V Y1).
#include "../../chatgpt/src/camera_motion_uyvy.h"

TEST_CASE("uyvy downsample: uniform luma passes through, chroma ignored") {
    // 8x4 UYVY: Y=100 everywhere, U/V garbage (0xFF) must not leak in.
    uint8_t src[8 * 4 * 2];
    for (int i = 0; i < 8 * 4; i++) {
        src[i * 2] = 0xFF;      // U/V
        src[i * 2 + 1] = 100;   // Y
    }
    uint8_t dst[4 * 2];
    memset(dst, 0, sizeof(dst));
    cameraDownsampleUyvy(src, 8, 4, dst, 4, 2);
    for (int i = 0; i < 4 * 2; i++) CHECK(dst[i] == 100);
}

TEST_CASE("uyvy downsample: left/right step is preserved per block") {
    // 8x2: left half Y=0, right half Y=200 -> 4x1 thumb {0,0,200,200}.
    uint8_t src[8 * 2 * 2];
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 8; x++) {
            src[(y * 8 + x) * 2] = 0x80;
            src[(y * 8 + x) * 2 + 1] = (x < 4) ? 0 : 200;
        }
    uint8_t dst[4];
    cameraDownsampleUyvy(src, 8, 2, dst, 4, 1);
    CHECK(dst[0] == 0);
    CHECK(dst[1] == 0);
    CHECK(dst[2] == 200);
    CHECK(dst[3] == 200);
}

TEST_CASE("uyvy downsample: vertical gradient averages correctly") {
    // 4x4: rows Y = 0, 64, 128, 192 -> 2x2 thumb rows {32, 160}.
    uint8_t src[4 * 4 * 2];
    const uint8_t rows[4] = {0, 64, 128, 192};
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            src[(y * 4 + x) * 2] = 0x80;
            src[(y * 4 + x) * 2 + 1] = rows[y];
        }
    uint8_t dst[2 * 2];
    cameraDownsampleUyvy(src, 4, 4, dst, 2, 2);
    CHECK(dst[0] == 32);
    CHECK(dst[1] == 32);
    CHECK(dst[2] == 160);
    CHECK(dst[3] == 160);
}

TEST_CASE("uyvy downsample: guards never crash, odd width truncated") {
    uint8_t dst[4] = {9, 9, 9, 9};
    uint8_t src[4 * 2 * 2];
    memset(src, 0, sizeof(src));
    for (unsigned i = 0; i < sizeof(src) / 2; i++) src[i * 2 + 1] = 50;
    cameraDownsampleUyvy(nullptr, 4, 2, dst, 2, 2);
    cameraDownsampleUyvy(src, 0, 2, dst, 2, 2);
    cameraDownsampleUyvy(src, 4, 2, nullptr, 2, 2);
    cameraDownsampleUyvy(src, 4, 2, dst, 0, 2);
    CHECK(dst[0] == 9);  // untouched by the no-op calls
    // Odd width 5 -> truncated to 4, still averages the Y plane.
    uint8_t src5[5 * 2 * 2];
    for (unsigned i = 0; i < 5 * 2; i++) { src5[i * 2] = 0xFF; src5[i * 2 + 1] = 60; }
    uint8_t dst2[2] = {0, 0};
    cameraDownsampleUyvy(src5, 5, 2, dst2, 2, 1);
    CHECK(dst2[0] == 60);
    CHECK(dst2[1] == 60);
}
