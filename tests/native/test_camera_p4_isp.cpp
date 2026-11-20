#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cmath>

// Compiles the PRODUCTION header (strategy 3: compile prod source).
// camera_p4_isp.h holds the ISP colour profiles, the wb_mode mapping and the
// gamma curve. The values are calibration data, but the INVARIANTS are what
// keep a typo from destroying the image: every CCM row must sum to 1 (a
// colour matrix that shifts luminance is wrong), the gamma table must satisfy
// the driver's validation rules, and the wb_mode mapping must stay total.
#include "../../chatgpt/src/camera_p4_isp.h"

TEST_CASE("ISP profiles: six calibrations, warmest to coolest") {
    CHECK(CAM_ISP_PROFILE_COUNT == 6);
    CHECK(CAM_ISP_PROFILE_DEFAULT >= 0);
    CHECK(CAM_ISP_PROFILE_DEFAULT < CAM_ISP_PROFILE_COUNT);
    for (int i = 0; i < CAM_ISP_PROFILE_COUNT; i++) {
        CHECK(kCamIspProfiles[i].name != nullptr);
        CHECK(kCamIspProfiles[i].wb_r > 1.0f);   // red needs gain over green
        CHECK(kCamIspProfiles[i].wb_b > 1.0f);   // blue too
        if (i) CHECK(kCamIspProfiles[i].kelvin >= kCamIspProfiles[i - 1].kelvin);
    }
    // Daylight is the default: 6015 K.
    CHECK(kCamIspProfiles[CAM_ISP_PROFILE_DEFAULT].kelvin == 6015);
}

TEST_CASE("ISP profiles: every CCM row sums to 1 (white stays white)") {
    for (int p = 0; p < CAM_ISP_PROFILE_COUNT; p++) {
        for (int r = 0; r < 3; r++) {
            float sum = kCamIspProfiles[p].ccm[r][0] + kCamIspProfiles[p].ccm[r][1] +
                        kCamIspProfiles[p].ccm[r][2];
            CHECK(std::fabs(sum - 1.0f) < 0.01f);
        }
        // Off-diagonals are the colour mixing; a fully diagonal matrix would
        // mean "no correction at all" and would make the profiles pointless.
        CHECK(kCamIspProfiles[p].ccm[0][1] < 0.0f);
        CHECK(kCamIspProfiles[p].ccm[0][2] < 0.0f);
        CHECK(kCamIspProfiles[p].ccm[1][0] < 0.0f);
    }
}

TEST_CASE("wb_mode mapping covers the page selector 0..4") {
    CHECK(cameraIspProfileForWbMode(0) == CAM_ISP_PROFILE_DEFAULT);  // Auto
    CHECK(cameraIspProfileForWbMode(1) == 5);                        // Sun -> shade
    CHECK(cameraIspProfileForWbMode(2) == 4);                        // Cloud
    CHECK(cameraIspProfileForWbMode(3) == -1);                       // Off
    CHECK(cameraIspProfileForWbMode(4) == 0);                        // Home -> tungsten
    CHECK(cameraIspProfileForWbMode(99) == CAM_ISP_PROFILE_DEFAULT); // out of range
    CHECK(cameraIspProfileForWbMode(-3) == CAM_ISP_PROFILE_DEFAULT);
}

TEST_CASE("gamma table satisfies the driver's validation rules") {
    CHECK(CAM_ISP_GAMMA_POINTS == 16);   // the block takes exactly 16 points
    CHECK(cameraIspGammaTableValid(kCamIspGamma, CAM_ISP_GAMMA_POINTS));
    CHECK(kCamIspGamma[0].x > 0);                                  // first x > 0
    CHECK(kCamIspGamma[CAM_ISP_GAMMA_POINTS - 1].x == 255);        // last x = 255
    for (int i = 1; i < CAM_ISP_GAMMA_POINTS; i++) {
        CHECK(kCamIspGamma[i].y >= kCamIspGamma[i - 1].y);          // monotonic
    }
    CHECK(kCamIspGamma[0].y < kCamIspGamma[8].y);                  // real curve, not flat

    // A broken table (x delta not a power of two) must be rejected: that is
    // exactly what the driver refuses at runtime.
    CamIspGammaPoint bad[16];
    for (int i = 0; i < 16; i++) bad[i] = kCamIspGamma[i];
    bad[1].x = 20;   // 20-16 = 4 ok, but 32-20 = 12 -> not a power of two
    CHECK_FALSE(cameraIspGammaTableValid(bad, 16));
    bad[1].x = 32;
    CHECK(cameraIspGammaTableValid(bad, 16));
    bad[15].x = 250;  // last must be 255
    CHECK_FALSE(cameraIspGammaTableValid(bad, 16));
    CHECK_FALSE(cameraIspGammaTableValid(nullptr, 16));
    CHECK_FALSE(cameraIspGammaTableValid(kCamIspGamma, 0));
}
