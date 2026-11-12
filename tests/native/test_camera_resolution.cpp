#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Compiles the PRODUCTION units directly (no Arduino dependency):
// camera_resolution.h is pure C/C++ (header table + parser decl).
#include "../../chatgpt/src/camera_resolution.h"
#include "../../chatgpt/src/camera_resolution.cpp"

TEST_CASE("parse: valid WxH strings") {
    uint16_t w = 0, h = 0;
    CHECK(cameraParseResolution("1920x1080", &w, &h) == 1);
    CHECK(w == 1920); CHECK(h == 1080);
    CHECK(cameraParseResolution("800x800", &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraParseResolution("800x1280", &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 1280);
}

TEST_CASE("parse: rejects malformed input") {
    uint16_t w = 0, h = 0;
    CHECK(cameraParseResolution("", &w, &h) == 0);
    CHECK(cameraParseResolution(nullptr, &w, &h) == 0);
    CHECK(cameraParseResolution("1920", &w, &h) == 0);
    CHECK(cameraParseResolution("x1080", &w, &h) == 0);
    CHECK(cameraParseResolution("1920x", &w, &h) == 0);
    CHECK(cameraParseResolution("0x1080", &w, &h) == 0);
    CHECK(cameraParseResolution("1920x0", &w, &h) == 0);
    CHECK(cameraParseResolution("1920X1080", &w, &h) == 0);  // lowercase x only
    CHECK(cameraParseResolution("1920x1080 ", &w, &h) == 0); // no trailing junk
    CHECK(cameraParseResolution(" 1920x1080", &w, &h) == 0); // no leading junk
    CHECK(cameraParseResolution("19x20x1080", &w, &h) == 0);
    CHECK(cameraParseResolution("99999x1080", &w, &h) == 0); // >4 digits / range
    CHECK(cameraParseResolution("abc", &w, &h) == 0);
}

TEST_CASE("mode table: all seven OV5647 sensor modes valid, neighbours not") {
    uint16_t w = 0, h = 0;
    CHECK(cameraValidResolution(2592, 1944) == 1);  // Fase 3: 5MP stills-only
    CHECK(cameraValidResolution(1920, 1080) == 1);
    CHECK(cameraValidResolution(1280, 960) == 1);
    CHECK(cameraValidResolution(1280, 720) == 1);
    CHECK(cameraValidResolution(800, 1280) == 1);
    CHECK(cameraValidResolution(800, 800) == 1);
    CHECK(cameraValidResolution(800, 640) == 1);
    CHECK(cameraValidResolution(640, 480) == 0);   // VGA: not a sensor mode
    CHECK(cameraParseResolution("640x480", &w, &h) == 1); // parses, but invalid mode
    CHECK(cameraValidResolution(1280, 721) == 0);
    CHECK(cameraValidResolution(1920, 1081) == 0); // off-by-one rejected
    CHECK(cameraValidResolution(2592, 1945) == 0); // 5MP off-by-one rejected
    CHECK(CAM_RES_MODE_COUNT == 7);
}

TEST_CASE("Fase 3 stills-only: 5MP class is photo-only, video sizes are not") {
    CHECK(cameraP4StillsOnly(2592, 1944) == 1);  // wider than 1920: stills only
    CHECK(cameraP4StillsOnly(1921, 1080) == 1);  // just over the HW ceiling
    CHECK(cameraP4StillsOnly(1920, 1080) == 0);  // video ceiling: not stills-only
    CHECK(cameraP4StillsOnly(1280, 720) == 0);
    CHECK(cameraP4StillsOnly(800, 800) == 0);
    CHECK(cameraP4StillsOnly(0, 0) == 0);
}

TEST_CASE("effective P4 output dims: explicit supported sensor mode wins") {
    uint16_t w = 0, h = 0;
    // CAM-78: 1920x1080 and 1280x720 are real modes the pipeline cannot run,
    // so a stored override on them is auto-reverted to the enum geometry.
    CHECK(cameraResolveP4OutputDims(1920, 1080, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraResolveP4OutputDims(1280, 720, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    // A working override still wins over the legacy enum.
    CHECK(cameraResolveP4OutputDims(1280, 960, 800, 640, &w, &h) == 1);
    CHECK(w == 1280); CHECK(h == 960);
    CHECK(cameraResolveP4OutputDims(800, 640, 1280, 720, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 640);
}

TEST_CASE("effective P4 output dims: unsupported legacy sizes use safe mode") {
    uint16_t w = 0, h = 0;
    CHECK(cameraResolveP4OutputDims(0, 0, 1280, 1024, &w, &h) == 0);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraResolveP4OutputDims(1280, 1024, 640, 480, &w, &h) == 0);
    CHECK(w == 800); CHECK(h == 800);
}

// CAM-68 contract: the P4 page presets (page_camera_asset.cpp
// P4_OV5647_PRESETS) must target known sensor modes so the backend accepts
// them (cameraP4ValidCsiMode / cameraValidResolution). Safe/Eco/VGA/Fast/
// Night/HD below mirror the JS table; every target must resolve exactly.
TEST_CASE("P4 preset targets are known sensor modes") {
    CHECK(cameraValidResolution(1920, 1080) == 1); // hd
    CHECK(cameraValidResolution(800, 640) == 1);   // vga/fast/eco
    CHECK(cameraValidResolution(800, 800) == 1);   // night/safe (verified)
    CHECK(cameraValidResolution(640, 480) == 0);   // legacy VGA enum: NOT a mode
    CHECK(cameraValidResolution(1280, 720) == 1);  // valid mode, but presets avoid it (CAM-62 stall)
}

TEST_CASE("CAM-78: broken modes are never usable, usable modes are") {
    // Real sensor modes the P4 pipeline cannot run (measured on .18 v1.120).
    CHECK(cameraP4ModeBroken(1280, 720) == 1);    // capture stalls (CAM-62)
    CHECK(cameraP4ModeBroken(1920, 1080) == 1);   // init fails: "Not detected"
    CHECK(cameraP4ModeBroken(1280, 960) == 0);
    CHECK(cameraP4ModeBroken(800, 640) == 0);

    CHECK(cameraP4ModeUsable(800, 640) == 1);
    CHECK(cameraP4ModeUsable(800, 800) == 1);
    CHECK(cameraP4ModeUsable(800, 1280) == 1);
    CHECK(cameraP4ModeUsable(1280, 960) == 1);
    CHECK(cameraP4ModeUsable(2592, 1944) == 1);   // stills-only, still offered
    CHECK(cameraP4ModeUsable(1280, 720) == 0);    // hidden + refused
    CHECK(cameraP4ModeUsable(1920, 1080) == 0);
    CHECK(cameraP4ModeUsable(640, 480) == 0);     // not a sensor mode
    CHECK(cameraP4ModeUsable(9999, 9999) == 0);
}

TEST_CASE("CAM-79: MJPEG fps ceiling follows the live pixel count") {
    CHECK(cameraP4MjpegFpsCap(800u * 640u) == 25);    // 0.51 MP: small RAW8 mode
    CHECK(cameraP4MjpegFpsCap(800u * 800u) == 25);    // 0.64 MP: the boundary
    CHECK(cameraP4MjpegFpsCap(650001u) == 15);        // just above the boundary
    CHECK(cameraP4MjpegFpsCap(1280u * 960u) == 15);
    CHECK(cameraP4MjpegFpsCap(1920u * 1080u) == 15);
    CHECK(cameraP4MjpegFpsCap(0) == 15);              // unknown geometry: safe
}

TEST_CASE("P4 preset targets resolve to exact output dims") {
    uint16_t w = 0, h = 0;
    // CAM-78 auto-revert: a stored override on a broken mode no longer wins —
    // the (working) enum geometry is used instead of leaving the camera dead.
    CHECK(cameraResolveP4OutputDims(1920, 1080, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraResolveP4OutputDims(1280, 960, 800, 640, &w, &h) == 1);
    CHECK(w == 1280); CHECK(h == 960);    // hd (largest working mode)
    CHECK(cameraResolveP4OutputDims(800, 800, 800, 640, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);     // safe/night
    CHECK(cameraResolveP4OutputDims(800, 640, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 640);     // vga/fast/eco
}
