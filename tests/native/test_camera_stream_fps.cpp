#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_stream_fps.h is the pure rolling window behind the MJPEG status
// pill (streamFps). It counts NEW published frames per wall second and
// rounds to nearest (old code truncated: 2.9 showed as 2, 0.9 as 0).
#include "../../chatgpt/src/camera_stream_fps.h"

TEST_CASE("fps window: no value before the first full second") {
    StreamFpsWindow w = {0, 0, 0};
    CHECK_FALSE(streamFpsWindowTick(&w, 100));
    CHECK(w.last == 0);
    CHECK_FALSE(streamFpsWindowTick(&w, 600));
    CHECK(w.last == 0);
}

TEST_CASE("fps window: rounds to nearest instead of truncating") {
    // 3 new frames in 1050 ms = 2.86 fps -> 3 (truncation gave 2).
    StreamFpsWindow w = {0, 0, 0};
    CHECK_FALSE(streamFpsWindowTick(&w, 100));
    CHECK_FALSE(streamFpsWindowTick(&w, 625));
    CHECK(streamFpsWindowTick(&w, 1150));
    CHECK(w.last == 3);
}

TEST_CASE("fps window: sub-1fps rounds up instead of showing 0") {
    // 2 frames 2100 ms apart ~= 0.95 fps -> 1 (truncation gave 0).
    StreamFpsWindow w = {0, 0, 0};
    CHECK_FALSE(streamFpsWindowTick(&w, 100));
    CHECK(streamFpsWindowTick(&w, 2200));
    CHECK(w.last == 1);
}

TEST_CASE("fps window: steady 6 fps stream reads 6") {
    StreamFpsWindow w = {0, 0, 0};
    uint32_t t = 100;
    bool closed = false;
    for (int i = 0; i < 6; i++) {  // 6 ticks 200 ms apart = 6 fps
        if (streamFpsWindowTick(&w, t)) closed = true;
        t += 200;
    }
    CHECK(closed);
    CHECK(w.last == 6);
}

TEST_CASE("fps window: reset returns to zero (stream stop)") {
    StreamFpsWindow w = {0, 0, 0};
    streamFpsWindowTick(&w, 100);
    streamFpsWindowTick(&w, 1200);
    CHECK(w.last != 0);
    streamFpsWindowReset(&w);
    CHECK(w.last == 0);
    CHECK(w.count == 0);
    CHECK(w.startMs == 0);
    CHECK_FALSE(streamFpsWindowTick(nullptr, 100));
    streamFpsWindowReset(nullptr);  // must not crash
}
