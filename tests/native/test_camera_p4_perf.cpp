#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>
#include <cstdint>

// Pure helpers behind the CAM_P4 performance work:
//   - resList ordering by pixel count (working modes lead the list)
//   - fps ceiling choice for P4 vs the legacy enum
// Camera-side headers need the ESP32 headers, so the rules under test are
// mirrored here exactly as implemented in camera_p4_backend.cpp /
// camera_manager.cpp — any change there must change this file too.
struct P4CsiMode { uint16_t w, h; };

static const P4CsiMode kModes[] = {
    {1920, 1080}, {1280, 960}, {1280, 720}, {800, 1280}, {800, 800}, {800, 640}, {2592, 1944},
};
#define MODE_COUNT (sizeof(kModes) / sizeof(kModes[0]))

// Mirrors cameraP4CsiResList(): insertion sort on pixel area, largest first.
static void orderByArea(uint8_t* order, size_t n) {
    for (size_t i = 0; i < n; i++) order[i] = (uint8_t)i;
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0) {
            uint32_t a = (uint32_t)kModes[order[j - 1]].w * kModes[order[j - 1]].h;
            uint32_t b = (uint32_t)kModes[order[j]].w * kModes[order[j]].h;
            if (a >= b) break;
            uint8_t t = order[j - 1]; order[j - 1] = order[j]; order[j] = t;
            j--;
        }
    }
}

static uint32_t px(uint8_t i) { return (uint32_t)kModes[i].w * kModes[i].h; }

// Mirrors cameraClampFps() on P4: backend ceiling, not the legacy enum.
#define P4_FPS_MAX_MIPI 15
static uint8_t clampFpsP4(uint8_t requested) {
    if (requested < 1) return 1;
    return (requested > P4_FPS_MAX_MIPI) ? P4_FPS_MAX_MIPI : requested;
}

TEST_CASE("resList ordering: largest first, so working modes lead the list") {
    uint8_t order[MODE_COUNT];
    orderByArea(order, MODE_COUNT);
    // Monotonically non-increasing area.
    for (size_t i = 1; i < MODE_COUNT; i++) {
        CHECK(px(order[i - 1]) >= px(order[i]));
    }
    // 2592x1944 (5 MP) then 1920x1080 (2.07 MP) head the list.
    CHECK(kModes[order[0]].w == 2592);
    CHECK(kModes[order[0]].h == 1944);
    CHECK(kModes[order[1]].w == 1920);
}

TEST_CASE("resList ordering: 1280x720 sorts BELOW the non-stalling modes") {
    uint8_t order[MODE_COUNT];
    orderByArea(order, MODE_COUNT);
    int pos720 = -1, pos960 = -1, pos800x1280 = -1;
    for (size_t i = 0; i < MODE_COUNT; i++) {
        if (kModes[order[i]].w == 1280 && kModes[order[i]].h == 720)  pos720 = (int)i;
        if (kModes[order[i]].w == 1280 && kModes[order[i]].h == 960)  pos960 = (int)i;
        if (kModes[order[i]].w == 800  && kModes[order[i]].h == 1280) pos800x1280 = (int)i;
    }
    CHECK(pos720 > pos960);        // 1.23 MP before 0.92 MP
    CHECK(pos720 > pos800x1280);   // 1.02 MP before 0.92 MP (both RAW8/binning)
}

TEST_CASE("resList ordering: every mode kept exactly once (no loss, no dup)") {
    uint8_t order[MODE_COUNT];
    orderByArea(order, MODE_COUNT);
    bool seen[MODE_COUNT] = {false};
    for (size_t i = 0; i < MODE_COUNT; i++) {
        CHECK(order[i] < MODE_COUNT);
        CHECK_FALSE(seen[order[i]]);
        seen[order[i]] = true;
    }
}

TEST_CASE("fps clamp (P4): backend ceiling 15, independent of the legacy enum") {
    // The old code derived the ceiling from the stored framesize enum, which on
    // P4 is the boot mode (a 720p session keeps 6/QVGA): a stale FHD enum
    // silently capped 15 to 10.
    CHECK(clampFpsP4(0) == 1);     // never zero (interval 1000/0 would spin)
    CHECK(clampFpsP4(1) == 1);
    CHECK(clampFpsP4(6) == 6);     // old ceiling still allowed
    CHECK(clampFpsP4(12) == 12);   // NEW: was capped to 10 by a stale enum
    CHECK(clampFpsP4(15) == 15);   // NEW ceiling reachable
    CHECK(clampFpsP4(30) == 15);   // above ceiling folds back
    CHECK(clampFpsP4(255) == 15);
}
