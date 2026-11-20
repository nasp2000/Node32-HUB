#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header (strategy 3: compile prod source).
// camera_p4_uvc.h decides the UVC control-transfer sizes (26/34/48 bytes) and
// which USB modes may be offered. Both were wrong in the field: a UVC 1.1
// camera got a 26-byte probe (its structure is 34), UVC 1.5 got 34 (it is 48),
// and every advertised mode was drowned by a legacy 640x480 guess.
#include "../../chatgpt/src/camera_p4_uvc.h"

#define CAM_USB_MODES_MAX_TEST 12

TEST_CASE("probe/commit length follows the UVC revision (26/34/48)") {
    CHECK(cameraUvcProbeLen(0x0100) == 26);   // UVC 1.0
    CHECK(cameraUvcProbeLen(0x0101) == 26);
    CHECK(cameraUvcProbeLen(0x0110) == 34);   // UVC 1.1: framing info + version range
    CHECK(cameraUvcProbeLen(0x0140) == 34);
    CHECK(cameraUvcProbeLen(0x0150) == 48);   // UVC 1.5: usage/bitdepth/rate-control/layout
    CHECK(cameraUvcProbeLen(0x0151) == 48);

    // Selectors move to 0x02xx only at 1.5.
    CHECK(cameraUvcProbeSelector(0x0110) == 0x0101);
    CHECK(cameraUvcCommitSelector(0x0110) == 0x0102);
    CHECK(cameraUvcProbeSelector(0x0150) == 0x0200);
    CHECK(cameraUvcCommitSelector(0x0150) == 0x0201);

    // Commit what the device returned, never beyond its revision's structure.
    CHECK(cameraUvcCommitLen(0x0150, 48) == 48);
    CHECK(cameraUvcCommitLen(0x0150, 34) == 34);   // short read: 1.1-sized reply
    CHECK(cameraUvcCommitLen(0x0150, 64) == 48);   // device padded: clamp
    CHECK(cameraUvcCommitLen(0x0110, 26) == 26);
    CHECK(cameraUvcCommitLen(0x0100, 48) == 26);
    CHECK(cameraUvcCommitLen(0x0150, 0) == 0);
    CHECK(cameraUvcCommitLen(0x0150, -5) == 0);
}

TEST_CASE("USB modes: landscape only, capped, largest wins") {
    const uint16_t modes[8][2] = {
        {640, 480}, {1920, 1080}, {800, 600}, {800, 1280},   // 800x1280 portrait
        {2560, 1440}, {1280, 720}, {320, 240}, {1280, 960},
    };
    CHECK(cameraUsbModeKeep(1920, 1080));
    CHECK(cameraUsbModeKeep(800, 800));      // square is fine
    CHECK_FALSE(cameraUsbModeKeep(800, 1280));  // portrait: host hazard
    CHECK_FALSE(cameraUsbModeKeep(2560, 1440)); // above our pipeline cap
    CHECK_FALSE(cameraUsbModeKeep(0, 480));

    uint16_t w = 0, h = 0;
    CHECK(cameraUsbPickBest(modes, 8, &w, &h) == 1);
    CHECK(w == 1920); CHECK(h == 1080);      // 2560x1440 capped out, portrait skipped

    // Nothing usable: caller keeps its default.
    const uint16_t onlyBad[2][2] = {{800, 1280}, {4000, 3000}};
    CHECK(cameraUsbPickBest(onlyBad, 2, &w, &h) == 0);
    CHECK(cameraUsbPickBest(nullptr, 3, &w, &h) == 0);
    CHECK(cameraUsbPickBest(modes, 0, &w, &h) == 0);
}

TEST_CASE("USB mode order is largest-first and filtered") {
    const uint16_t modes[6][2] = {
        {640, 480}, {1280, 720}, {800, 600}, {1920, 1080}, {320, 240}, {800, 1280},
    };
    int idx[CAM_USB_MODES_MAX_TEST];
    int n = cameraUsbOrderModes(modes, 6, idx, CAM_USB_MODES_MAX_TEST);
    CHECK(n == 5);                                    // portrait dropped
    CHECK(modes[idx[0]][0] == 1920);
    CHECK(modes[idx[0]][1] == 1080);
    CHECK(modes[idx[1]][0] == 1280);
    CHECK(modes[idx[1]][1] == 720);
    CHECK(modes[idx[2]][0] == 800);
    CHECK(modes[idx[2]][1] == 600);
    CHECK(modes[idx[3]][0] == 640);
    CHECK(modes[idx[3]][1] == 480);
    CHECK(modes[idx[4]][0] == 320);
    CHECK(modes[idx[4]][1] == 240);

    // Capacity is honoured (caller's array size).
    int two[2];
    CHECK(cameraUsbOrderModes(modes, 6, two, 2) == 2);
    CHECK(modes[two[0]][0] == 1920);
    CHECK(modes[two[1]][0] == 1280);
    CHECK(cameraUsbOrderModes(nullptr, 6, two, 2) == 0);
    CHECK(cameraUsbOrderModes(modes, 6, nullptr, 2) == 0);
}
