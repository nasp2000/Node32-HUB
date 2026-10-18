// Native unit tests for the streaming "minutes playing" elapsed model in
// radio_audio_runtime.cpp. The firmware tracks a monotonically-increasing
// accumulated stream played time that survives reconnects (previously the
// elapsed reset to ~0 on every reconnect, so the status bar showed "0:00").
//
// This is a standalone reimplementation of the state machine introduced for
// the fix. It reproduces exactly the arithmetic used in
// radioAudioCleanupSource() / radioAudioStatusJson() so the semantics are
// pinned without pulling the whole ESP32 runtime into the test.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// ---- model mirroring the firmware change ----
struct ElapsedModel {
    uint32_t segStartMs = 0;   // s_streamStartMs  — start of current segment
    uint32_t playedMs = 0;     // s_streamPlayedMs — accumulated stream session time
    bool     isStream = false; // s_streamSession  — current source is a stream
    bool     active = false;   // s_active

    // radioAudioStartWithSource success tail (fresh stream / reconnect).
    void beginSegment(uint32_t nowMs, bool freshSession) {
        segStartMs = nowMs;
        if (freshSession) {
            playedMs = 0;
            isStream = true;
        }
    }
    // radioAudioCleanupSource pre-teardown: fold the completed segment in.
    void teardownSegment(uint32_t nowMs) {
        if (isStream && segStartMs) {
            playedMs += (nowMs - segStartMs);
            segStartMs = 0;
        }
        active = false;
    }
    // radioAudioStartWithSource for a local SD file.
    void beginSdSegment(uint32_t nowMs) {
        isStream = false;
        segStartMs = nowMs;
    }
    // radioAudioStatusJson elapse computation, gated by active.
    uint32_t activeMs(uint32_t nowMs) const {
        uint32_t seg = (active && segStartMs) ? (nowMs - segStartMs) : 0;
        return active ? (isStream ? playedMs + seg : seg) : 0;
    }
};

TEST_CASE("idle / no session reports zero") {
    ElapsedModel m;
    CHECK(m.activeMs(1000) == 0);
    m.active = true;          // segment never began
    CHECK(m.activeMs(2000) == 0);
}

TEST_CASE("fresh stream session accumulates from zero and grows") {
    ElapsedModel m;
    m.active = true;
    m.beginSegment(1000, /*freshSession=*/true);
    CHECK(m.activeMs(1000) == 0);
    CHECK(m.activeMs(7000) == 6000);   // +1 min
    CHECK(m.activeMs(67000) == 66000); // ~1:06 elapsed -> 1 minute shown
}

TEST_CASE("stream reconnect keeps the minutes playing counter") {
    ElapsedModel m;
    m.active = true;
    m.beginSegment(500, true);         // station A, session start
    m.activeMs(60500);                 // play 60s
    m.teardownSegment(60500);          // stream drops -> reconnect teardown
    CHECK(m.activeMs(60500) == 0);     // idle teardown; nothing shown
    // Reconnect begins a NEW segment but keeps the SAME session (freshSession=false).
    m.active = true;
    m.beginSegment(70000, /*freshSession=*/false);
    CHECK(m.activeMs(70000) == 60000); // still ~1:00 after the drop
    CHECK(m.activeMs(130000) == 120000);
    // A second drop does not lose the accumulated time either.
    m.teardownSegment(130000);
    m.active = true;
    m.beginSegment(140000, false);
    CHECK(m.activeMs(160000) == 140000);
}

TEST_CASE("fresh station after a reconnect resets the counter") {
    ElapsedModel m;
    m.active = true;
    m.beginSegment(0, true);
    m.teardownSegment(60000);
    m.active = true;
    m.beginSegment(60500, false);                       // reconnect, keep session
    CHECK(m.activeMs(120500) == 60000);
    // User picks a different station -> fresh session resets to 0.
    m.teardownSegment(120500);
    m.active = true;
    m.beginSegment(121000, /*freshSession=*/true);
    CHECK(m.activeMs(127000) == 6000);
}

TEST_CASE("local SD file reports per-track elapsed, not accumulated") {
    ElapsedModel m;
    m.active = true;
    m.beginSegment(1000, true);   // was streaming, now switching to SD
    m.teardownSegment(61000);     // stream played 60s (accumulates internally)
    m.active = true;
    m.beginSdSegment(70000);      // SD track starts -> session is no longer stream
    CHECK(m.activeMs(70000) == 0);
    CHECK(m.activeMs(76000) == 6000);   // per-track elapsed only
    // Even though playedMs still holds 60s internally, SD must not report it.
}

TEST_CASE("unmute prebuffer: clean wall-clock advance is just elapsed") {
    // Non-reconnect case: a single uninterrupted stream segment (the common
    // healthy case) should equal wall-clock elapsed since start.
    ElapsedModel m;
    m.active = true;
    m.beginSegment(2000, true);
    CHECK(m.activeMs(2000) == 0);
    CHECK(m.activeMs(62 * 1000) == 60 * 1000); // exactly 1:00
    CHECK(m.activeMs(62 * 1000 + 999) == 60 * 1000 + 999);
}
