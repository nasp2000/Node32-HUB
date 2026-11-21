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
    // CAM-87: 1920x1080 is API-selectable but never restored from a stored
    // override (it failed on the boot path); 1280x720 still stalls, so both
    // fall back to the enum geometry.
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
    // CAM-83: portrait 800x1280 is a host hazard (players/capture stacks) and
    // is no longer offered, even though the sensor mode exists.
    CHECK(cameraP4ModeUsable(800, 1280) == 0);
    CHECK(cameraP4ModePortrait(800, 1280) == 1);
    CHECK(cameraP4ModePortrait(1280, 800) == 0);
    CHECK(cameraP4ModeUsable(1280, 960) == 1);
    // CAM-91: only MEASURED modes are advertised. 2592x1944 is a real sensor
    // mode but has no register profile, so it used to be listed and could
    // never start ("offered but inert" is the same dishonesty CAM-78 fixed).
    CHECK(cameraP4ModeUsable(2592, 1944) == 0);
    CHECK(cameraP4ModeSelectable(2592, 1944) == 0);
    CHECK(cameraP4ModeUsable(1280, 720) == 0);    // hidden + refused
    CHECK(cameraP4ModeUsable(1920, 1080) == 0);
    CHECK(cameraP4ModeUsable(640, 480) == 0);     // not a sensor mode
    CHECK(cameraP4ModeUsable(9999, 9999) == 0);
}

TEST_CASE("CAM-91: the advertised set is exactly the measured video modes") {
    // What a viewer may pick: HD, safe/night and Eco/Fast. Nothing else.
    CHECK(cameraP4ModeMeasured(1280, 960) == 1);
    CHECK(cameraP4ModeMeasured(800, 800) == 1);
    CHECK(cameraP4ModeMeasured(800, 640) == 1);
    CHECK(cameraP4ModeMeasured(2592, 1944) == 0);   // no register profile
    CHECK(cameraP4ModeMeasured(1280, 720) == 0);    // CAM-62 stall
    CHECK(cameraP4ModeMeasured(800, 1280) == 0);    // portrait
    CHECK(cameraP4ModeMeasured(1920, 1080) == 0);   // API-only, not advertised
    for (int i = 0; i < CAM_RES_MODE_COUNT; i++) {
        const int expected = cameraValidResolution(kCamResModes[i][0], kCamResModes[i][1]) &&
                             (cameraP4ModeMeasured(kCamResModes[i][0], kCamResModes[i][1]) ||
                              cameraP4ModeExperimental(kCamResModes[i][0], kCamResModes[i][1]));
        CHECK(cameraP4ModeUsable(kCamResModes[i][0], kCamResModes[i][1]) ==
              cameraP4ModeMeasured(kCamResModes[i][0], kCamResModes[i][1]));
        (void)expected;
    }
    // The legacy enum path must not smuggle the portrait size back in.
    uint16_t w = 0, h = 0;
    CHECK(cameraResolveP4OutputDims(0, 0, 800, 1280, &w, &h) == 0);
    CHECK(w == 800); CHECK(h == 800);   // safe fallback, never 800x1280
    CHECK(cameraResolveP4OutputDims(0, 0, 2592, 1944, &w, &h) == 0);
    CHECK(w == 800); CHECK(h == 800);
}

TEST_CASE("CAM-79/CAM-91: MJPEG fps ceiling is the MEASURED rate per mode") {
    // Measured on the .18 with one viewer: 11.4 fps at 800x640, ~11 at 800x800,
    // 10-12 at 1280x960, 8.01 at 1920x1080. The ceiling is that rate, not the
    // sensor rating (the old 25/15 promised frames this box never produced).
    CHECK(cameraP4MjpegFpsCap(800u * 640u) == 12);
    CHECK(cameraP4MjpegFpsCap(800u * 800u) == 12);    // 0.64 MP boundary
    CHECK(cameraP4MjpegFpsCap(650001u) == 12);        // just above the boundary
    CHECK(cameraP4MjpegFpsCap(1280u * 960u) == 12);
    CHECK(cameraP4MjpegFpsCap(1500000u) == 12);       // below the 1080p class
    CHECK(cameraP4MjpegFpsCap(1920u * 1080u) == 8);   // 1080p: 8.01 measured
    CHECK(cameraP4MjpegFpsCap(2592u * 1944u) == 8);
    CHECK(cameraP4MjpegFpsCap(0) == 12);              // unknown geometry: safe
}

TEST_CASE("CAM-86: 1080p timing comes from the register table (30.0 fps)") {
    // The row used to carry a 2416x1104 pair fitted to 30 fps that matched no
    // register value; the sensor is actually programmed with HTS 2271 / VTS
    // 1199. Assert the constants reproduce 30.0 fps and that the two working
    // modes keep theirs (so the convention is pinned by numbers, not comments).
    CHECK(cameraP4ModeFps(CAM_P4_1080P_PCLK, CAM_P4_1080P_HTS, CAM_P4_1080P_VTS) == 30);
    CHECK(cameraP4ModeFps(88333333u, 1796, 1093) == 45);   // 1280x960 RAW10
    CHECK(cameraP4ModeFps(88333333u, 1796, 1640) == 30);   // 1280x720 RAW10 table
    CHECK(cameraP4ModeFps(0, 100, 100) == 0);
    CHECK(cameraP4ModeFps(1000, 0, 100) == 0);
    // Sanity on the values themselves: HTS must cover the width, VTS the height.
    CHECK(CAM_P4_1080P_HTS >= 1920);
    CHECK(CAM_P4_1080P_VTS >= 1080);
    CHECK(CAM_P4_1080P_VTS < CAM_P4_1080P_HTS);   // blanking stays modest
}

TEST_CASE("CAM-86: 1080p is selectable but never advertised") {
    CHECK(cameraP4ModeExperimental(1920, 1080) == 1);
    CHECK(cameraP4ModeUsable(1920, 1080) == 0);      // never in resList
    CHECK(cameraP4ModeSelectable(1920, 1080) == 1);  // API may try it
    CHECK(cameraP4ModeSelectable(800, 640) == 1);    // advertised modes unchanged
    CHECK(cameraP4ModeSelectable(1280, 720) == 0);   // still refused (stall)
    CHECK(cameraP4ModeSelectable(800, 1280) == 0);   // still refused (portrait)
    CHECK(cameraP4ModeSelectable(640, 480) == 0);    // not a sensor mode
}

TEST_CASE("P4 preset targets resolve to exact output dims") {
    uint16_t w = 0, h = 0;
    // CAM-87: a stored 1920x1080 override is NOT replayed at boot (measured:
    // it made "capture device open failed" and burned the camera until the
    // self-heal cleared it). It stays selectable by an explicit API call, so
    // the advertised enum geometry wins here.
    CHECK(cameraResolveP4OutputDims(1920, 1080, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraP4ModeSelectable(1920, 1080) == 1);   // API can still try it
    // CAM-78 auto-revert still holds for sizes that cannot run at all.
    CHECK(cameraResolveP4OutputDims(1280, 720, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);
    CHECK(cameraResolveP4OutputDims(1280, 960, 800, 640, &w, &h) == 1);
    CHECK(w == 1280); CHECK(h == 960);    // hd (largest working mode)
    CHECK(cameraResolveP4OutputDims(800, 800, 800, 640, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 800);     // safe/night
    CHECK(cameraResolveP4OutputDims(800, 640, 800, 800, &w, &h) == 1);
    CHECK(w == 800); CHECK(h == 640);     // vga/fast/eco
}
