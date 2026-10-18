#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Standalone reimpl of the motion-detector integration guards fixed in
// camera_motion.cpp / camera_record.cpp / camera_handler.cpp:
//   1. Sampling guard: while the sensor is switched to PIXFORMAT_GRAYSCALE
//      for the motion thumbnail, direct JPEG captures must wait (photo/
//      snapshot, bounded) or skip (record tick) — never persist a 96x96
//      grayscale frame as JPEG.
//   2. Recording ownership: the post-motion timeout only finalises sessions
//      the motion module itself started; manual/dashcam sessions are immune.
//   3. Sample cadence: idle skip 4 (was 9) at the 120 ms loop throttle.

// --- 1. Sampling guard protocol ---------------------------------------
struct SamplingGuard {
    bool sampling = false;
    // Photo/snapshot path: bounded wait, then capture. Returns true when the
    // capture that follows is guaranteed a clean JPEG sensor mode.
    // Mirrors: for (w = 0; w < 20 && sampling; w++) delay(20).
    bool waitClean(int& waitedMs) {
        waitedMs = 0;
        for (int w = 0; w < 20 && sampling; w++) waitedMs += 20;
        return !sampling;
    }
    // Record-tick path: skip the frame while sampling (mirrors the
    // `if (!cameraMotionSamplingActive())` capture guards).
    bool shouldCapture() { return !sampling; }
};

// --- 2. Recording ownership --------------------------------------------
struct MotionRec {
    bool recordActive = false;
    bool ownRecording = false;
    bool motionActive = false;
    uint32_t motionEndMs = 0;
    uint32_t nowMs = 0;
    uint8_t postSecs = 5;
    bool finished = false;
    // Mirrors the ENABLE_SDCARD block of cameraMotionTick().
    void tick() {
        finished = false;
        if (motionActive) {
            if (!recordActive) {
                recordActive = true;  // cameraRecordStart ok
                ownRecording = true;
            }
        } else {
            if (!recordActive) {
                ownRecording = false;
            } else if (ownRecording && motionEndMs > 0) {
                uint32_t postMs = (uint32_t)postSecs * 1000UL;
                if ((nowMs - motionEndMs) >= postMs) {
                    recordActive = false;  // cameraRecordFinish()
                    ownRecording = false;
                    finished = true;
                }
            }
        }
    }
};

// --- 3. Sample cadence ---------------------------------------------------
static uint32_t samplePeriodMs(uint8_t skip, uint32_t loopThrottleMs) {
    return (uint32_t)(skip + 1) * loopThrottleMs;
}

TEST_CASE("guard: photo waits while sampling, then captures clean") {
    SamplingGuard g;
    g.sampling = true;
    int waited = 0;
    // Guard still set after budget: capture would proceed unclean.
    CHECK(g.waitClean(waited) == false);
    CHECK(waited == 400);
    // Guard cleared mid-window (sensor restored): clean capture.
    g.sampling = false;
    CHECK(g.waitClean(waited) == true);
    CHECK(waited == 0);
}

TEST_CASE("guard: record tick skips frames only inside the window") {
    SamplingGuard g;
    CHECK(g.shouldCapture() == true);
    g.sampling = true;
    CHECK(g.shouldCapture() == false);
    g.sampling = false;
    CHECK(g.shouldCapture() == true);
}

TEST_CASE("ownership: manual recording survives motion timeout") {
    MotionRec m;
    m.recordActive = true;   // user pressed Rec (not motion-owned)
    m.ownRecording = false;
    m.motionActive = false;
    m.motionEndMs = 1000;
    m.nowMs = 1000 + 30000;  // long after any post window
    m.tick();
    CHECK(m.recordActive == true);
    CHECK(m.finished == false);
}

TEST_CASE("ownership: motion-owned recording finalises after post secs") {
    MotionRec m;
    m.motionActive = true;
    m.tick();  // motion starts a session
    CHECK(m.recordActive == true);
    CHECK(m.ownRecording == true);
    m.motionActive = false;
    m.motionEndMs = 5000;
    m.nowMs = 5000 + 4999;  // just inside the window
    m.tick();
    CHECK(m.recordActive == true);
    m.nowMs = 5000 + 5000;  // window elapsed
    m.tick();
    CHECK(m.recordActive == false);
    CHECK(m.finished == true);
    CHECK(m.ownRecording == false);
}

TEST_CASE("ownership: external stop clears ownership, no double finish") {
    MotionRec m;
    m.recordActive = true;
    m.ownRecording = true;  // motion started it, user stopped it externally
    m.motionActive = false;
    m.motionEndMs = 1000;
    m.recordActive = false;  // external cameraRecordFinish()
    m.nowMs = 1000 + 99999;
    m.tick();
    CHECK(m.finished == false);
    CHECK(m.ownRecording == false);
}

TEST_CASE("cadence: idle ~600ms, active ~240ms at 120ms throttle") {
    CHECK(samplePeriodMs(4, 120) == 600);
    CHECK(samplePeriodMs(1, 120) == 240);
    CHECK(samplePeriodMs(9, 120) == 1200);  // old idle skip for comparison
}

TEST_CASE("cadence: trigger latency = confirm_frames * idle period") {
    uint32_t idle = samplePeriodMs(4, 120);
    CHECK(idle * 3 == 1800);  // new: ~1.8 s with confirm=3 (was ~3.6 s)
    CHECK(idle * 1 == 600);   // confirm=1: single sample
}

// --- 4. Motion action dispatch (Task 5) ---------------------------------
// Mirrors the switch in cameraMotionTick(): record_video | snap_photo |
// email_alert, each fired once per event (rising edge via s_actionFired).
static const int ACT_RECORD_VIDEO = 0;
static const int ACT_SNAP_PHOTO   = 1;
static const int ACT_EMAIL_ALERT  = 2;

struct MotionActionDispatch {
    int action = ACT_RECORD_VIDEO;
    bool motionActive = false;
    bool actionFired = false;
    bool ownRecording = false;
    bool recordActive = false;
    bool snapSaved = false;    // SNAP_PHOTO side-effect
    bool emailSent = false;    // EMAIL_ALERT side-effect (guarded, no SMTP in test)

    // Mirrors the rising-edge reset in cameraMotionUpdate(): false->true
    // transition arms the once-per-event action.
    void onMotionRise() { motionActive = true; actionFired = false; }
    void onMotionStop() { motionActive = false; }

    // Mirrors the action block of cameraMotionTick() (SMTP compiled out: the
    // EMAIL case marks fired to avoid re-entry; side-effect tracked only).
    void tick() {
        if (!motionActive) { actionFired = false; return; }
        if (actionFired) return;
        actionFired = true;
        switch (action) {
            case ACT_RECORD_VIDEO:
                if (!recordActive) { recordActive = true; ownRecording = true; }
                break;
            case ACT_SNAP_PHOTO:
                snapSaved = true;
                break;
            case ACT_EMAIL_ALERT:
                emailSent = true;
                break;
            default: break;
        }
    }
};

TEST_CASE("action dispatch: record_video starts a session once per event") {
    MotionActionDispatch m;
    m.action = ACT_RECORD_VIDEO;
    m.onMotionRise();
    m.tick();               // first sample after rise fires
    CHECK(m.recordActive == true);
    CHECK(m.ownRecording == true);
    m.tick();               // subsequent samples do nothing (actionFired)
    m.tick();
    CHECK(m.recordActive == true);
    m.onMotionStop();
    m.tick();               // re-arms for the next event
    m.onMotionRise();
    m.tick();
    CHECK(m.recordActive == true);   // stays active, no duplicate start churn
}

TEST_CASE("action dispatch: snap_photo fires exactly once per event") {
    MotionActionDispatch m;
    m.action = ACT_SNAP_PHOTO;
    m.onMotionRise();
    m.tick();
    CHECK(m.snapSaved == true);
    CHECK(m.recordActive == false);  // never opens a recording
    m.snapSaved = false;
    m.tick();                       // still in the same event → not re-fired
    CHECK(m.snapSaved == false);
    m.onMotionStop();
    m.onMotionRise();               // new event → fires again
    m.tick();
    CHECK(m.snapSaved == true);
}

TEST_CASE("action dispatch: email_alert fires once per event, no recording") {
    MotionActionDispatch m;
    m.action = ACT_EMAIL_ALERT;
    m.onMotionRise();
    m.tick();
    CHECK(m.emailSent == true);
    CHECK(m.recordActive == false);
    m.emailSent = false;
    m.tick();
    CHECK(m.emailSent == false);    // once per event only
}

// --- 5. Timelapse next-capture countdown (Task 4) ------------------------
// Mirrors cameraTimelapseNextInSecs(): ceil((lastTlMs + interval - now)/1000),
// 0 when inactive or due-now.
static uint32_t timelapseNextInSecs(uint16_t intervalSecs, uint32_t lastTlMs,
                                    uint32_t nowMs) {
    if (intervalSecs == 0 || lastTlMs == 0) return 0;
    uint32_t intervalMs = (uint32_t)intervalSecs * 1000UL;
    uint32_t nextMs = lastTlMs + intervalMs;
    if (nextMs <= nowMs) return 0;               // due now
    return (nextMs - nowMs + 999) / 1000;        // ceil to whole seconds
}

TEST_CASE("timelapse next: inactive when interval or last capture absent") {
    CHECK(timelapseNextInSecs(0, 5000, 5000) == 0);    // interval off
    CHECK(timelapseNextInSecs(10, 0, 5000) == 0);      // never captured yet
}

TEST_CASE("timelapse next: mid-interval counts down to next capture") {
    // interval 10s, captured at t=10000; at t=13000 → 7s remain.
    CHECK(timelapseNextInSecs(10, 10000, 13000) == 7);
    // just captured (t=10000, now 10000) → full interval.
    CHECK(timelapseNextInSecs(10, 10000, 10000) == 10);
}

TEST_CASE("timelapse next: due-now collapses to 0 (loop tick captures shortly)") {
    CHECK(timelapseNextInSecs(10, 10000, 20000) == 0);  // exactly at next
    CHECK(timelapseNextInSecs(10, 10000, 25000) == 0);  // overdue
}

// --- 6. Event counter + cooldown (product-counter mode) ------------------
// Mirrors the rising-edge block in cameraMotionUpdate(): first event always
// counts; later ones only outside the refractory window.
struct EventCounter {
    uint32_t total = 0;
    uint32_t lastEndMs = 0;
    uint16_t cooldownSecs = 5;
    // Returns true when the new event counts (rising edge at nowMs).
    bool onRisingEdge(uint32_t nowMs) {
        bool counted = (cooldownSecs == 0) || (lastEndMs == 0) ||
            ((nowMs - lastEndMs) >= (uint32_t)cooldownSecs * 1000UL);
        if (counted) total++;
        return counted;
    }
    void onFallingEdge(uint32_t nowMs) { lastEndMs = nowMs; }
};

TEST_CASE("counter: first event always counts, cooldown gates the rest") {
    EventCounter e;
    CHECK(e.onRisingEdge(1000) == true);
    CHECK(e.total == 1);
    e.onFallingEdge(2000);
    CHECK(e.onRisingEdge(2000 + 4999) == false);  // inside 5 s window
    CHECK(e.total == 1);
    CHECK(e.onRisingEdge(2000 + 5000) == true);   // window elapsed
    CHECK(e.total == 2);
}

TEST_CASE("counter: cooldown 0 disables gating") {
    EventCounter e;
    e.cooldownSecs = 0;
    CHECK(e.onRisingEdge(1000) == true);
    e.onFallingEdge(1100);
    CHECK(e.onRisingEdge(1150) == true);
    CHECK(e.total == 2);
}

// --- 7. Best-frame tracking ----------------------------------------------
// Mirrors the tick block: keep the highest-score JPEG (cap 400KB), replace
// only on strictly higher score.
struct BestFrame {
    size_t len = 0;
    uint8_t score = 0;
    static const size_t MAXB = 400UL * 1024UL;
    // Returns true when the candidate becomes the new best.
    bool consider(uint8_t s, size_t l) {
        if (l == 0 || l > MAXB) return false;
        if (s > score) { score = s; len = l; return true; }
        return false;
    }
    void reset() { len = 0; score = 0; }
};

TEST_CASE("best-frame: strictly increasing scores replace, oversize refused") {
    BestFrame b;
    CHECK(b.consider(10, 20000) == true);
    CHECK(b.consider(10, 25000) == false);  // equal score: keep first
    CHECK(b.consider(9, 20000) == false);
    CHECK(b.consider(42, 30000) == true);
    CHECK(b.score == 42); CHECK(b.len == 30000);
    CHECK(b.consider(99, BestFrame::MAXB + 1) == false);  // oversize
    CHECK(b.consider(50, 0) == false);                   // empty
    b.reset();
    CHECK(b.score == 0); CHECK(b.len == 0);
}
