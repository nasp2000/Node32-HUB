#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_p4_vision.h is a pure decision helper — zero ESP32 deps.
#include "../../chatgpt/src/camera_p4_vision.h"

TEST_CASE("vision mode clamp: off default, count/defect opt-in") {
    CHECK(cameraVisionModeClamp(0) == CAM_VISION_OFF);
    CHECK(cameraVisionModeClamp(1) == CAM_VISION_COUNT);
    CHECK(cameraVisionModeClamp(2) == CAM_VISION_DEFECT);
    CHECK(cameraVisionModeClamp(3) == CAM_VISION_OFF);
    CHECK(cameraVisionModeClamp(-1) == CAM_VISION_OFF);
}

TEST_CASE("vision thresh clamp 0-100, default 50") {
    CHECK(cameraVisionThreshClamp(50) == 50);
    CHECK(cameraVisionThreshClamp(0) == 0);
    CHECK(cameraVisionThreshClamp(100) == 100);
    CHECK(cameraVisionThreshClamp(-1) == 0);
    CHECK(cameraVisionThreshClamp(101) == 100);
    CHECK(CAM_VISION_THRESH_DEF == 50);
}

TEST_CASE("burst clamp 0-50") {
    CHECK(cameraVisionBurstClamp(0) == 0);
    CHECK(cameraVisionBurstClamp(10) == 10);
    CHECK(cameraVisionBurstClamp(50) == 50);
    CHECK(cameraVisionBurstClamp(-5) == 0);
    CHECK(cameraVisionBurstClamp(51) == 50);
}

TEST_CASE("label sanitize: FAT-safe, bounded, non-empty check") {
    char a[CAM_VISION_LABEL_MAX + 1];
    strcpy(a, "gear_ok-2");
    CHECK(cameraVisionSanitizeLabel(a) == 1);
    CHECK(std::string(a) == "gear_ok-2");
    char b[CAM_VISION_LABEL_MAX + 1];
    strcpy(b, "a/b c.jpg");
    CHECK(cameraVisionSanitizeLabel(b) == 1);
    CHECK(std::string(b) == "a_b_c_jpg");
    char c[CAM_VISION_LABEL_MAX + 1];
    strcpy(c, "");
    CHECK(cameraVisionSanitizeLabel(c) == 0);
    // truncation at 16 chars
    char d[CAM_VISION_LABEL_MAX + 1];
    strcpy(d, "0123456789abcdef");
    CHECK(cameraVisionSanitizeLabel(d) == 1);
    CHECK(strlen(d) == 16);
    CHECK(cameraVisionSanitizeLabel(nullptr) == 0);
}

TEST_CASE("burst names: label_idx.jpg zero-padded") {
    char out[48];
    cameraVisionBurstName("gear", 3, out);
    CHECK(std::string(out) == "gear_003.jpg");
    cameraVisionBurstName("gear", 50, out);
    CHECK(std::string(out) == "gear_050.jpg");
}

TEST_CASE("320x320 crop math: cover-fit + center") {
    uint16_t cw, ch, ox, oy;
    // 1920x1080 landscape: crop width to 1080x1080, centered
    cameraVisionCropFor320(1920, 1080, &cw, &ch, &ox, &oy);
    CHECK(cw == 1080); CHECK(ch == 1080);
    CHECK(ox == 420); CHECK(oy == 0);
    // 800x1280 portrait: crop height to 800x800, centered
    cameraVisionCropFor320(800, 1280, &cw, &ch, &ox, &oy);
    CHECK(cw == 800); CHECK(ch == 800);
    CHECK(ox == 0); CHECK(oy == 240);
    // square: no crop
    cameraVisionCropFor320(800, 800, &cw, &ch, &ox, &oy);
    CHECK(cw == 800); CHECK(ch == 800);
    CHECK(ox == 0); CHECK(oy == 0);
    // guards
    cameraVisionCropFor320(0, 0, &cw, &ch, &ox, &oy);  // no crash
    cameraVisionCropFor320(640, 480, nullptr, nullptr, nullptr, nullptr);
}
