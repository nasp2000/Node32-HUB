#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_stream_policy.h holds the two decisions that were wrong in v1.120:
// how many MJPEG viewers the single-slot cache admits, and whether the capture
// pump must run for a recording with no viewer.
#include "../../chatgpt/src/camera_stream_policy.h"
#include "../../chatgpt/src/camera_stream_slots.h"  // ring size vs cap

TEST_CASE("viewer admission: two viewers with the 3-slot ring, third gets 503") {
    CHECK(CAM_MAX_MJPEG_CLIENTS == 2);
    CHECK(CAM_MAX_MJPEG_CLIENTS < CAM_STREAM_SLOTS);  // writer always keeps a slot
    CHECK(cameraStreamAdmit(0));   // first viewer admitted
    CHECK(cameraStreamAdmit(1));   // second one too (ring, CAM-76)
    CHECK_FALSE(cameraStreamAdmit(2));  // third gets 503 "Stream busy"
    CHECK_FALSE(cameraStreamAdmit(9));  // zombie leftovers never over-admit
}

TEST_CASE("pump gate: runs for a viewer, an H.264 session or a MJPEG recording") {
    CHECK_FALSE(cameraPumpNeeded(false, false, false));  // idle camera
    CHECK(cameraPumpNeeded(true, false, false));         // viewer only
    CHECK(cameraPumpNeeded(false, true, false));         // H.264, no viewer
    CHECK(cameraPumpNeeded(false, false, true));         // MJPEG recording, no viewer
    CHECK(cameraPumpNeeded(true, false, true));          // both at once
    CHECK(cameraPumpNeeded(true, true, false));
}

TEST_CASE("feed predicate: only MJPEG sessions take pump frames as JPEG") {
    CHECK(cameraPumpFeedsRecorder(true));
    CHECK_FALSE(cameraPumpFeedsRecorder(false));  // H.264 takes the raw tap
}
