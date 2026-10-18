#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Standalone reimpl of the Task 3 boot auto-start hooks from camera_manager.cpp:
//   - cameraMaybeAutoStart(): one-shot boot hook that schedules camera init only
//     when the persisted auto_mode != 0, after the boot-settle delay, and never
//     re-schedules while already scheduled/ready/starting.
//   - cameraArmAutoMode(): fills in an unset default for the selected continuous
//     mode (video->dashcam 5 min, motion->enabled, timelapse->10 s, photo/off no-op)
//     and only ever overrides a *zero/unset* value, never an explicit user choice.
//
// Auto-mode constants mirror camera_config.h: 0=off, 1=photo, 2=video,
// 3=motion, 4=timelapse.

// --- cameraMaybeAutoStart mirror -----------------------------------------
struct AutoStart {
    uint8_t auto_mode = 0;         // persisted config
    bool scheduled = false;        // s_autoStartScheduled latch
    bool ready = false;            // camera already initialised
    bool initInProgress = false;   // async init already running
    uint32_t nowMs = 0;            // millis()
    uint32_t initRequests = 0;     // count of cameraBeginAsync() calls

    static const uint32_t SETTLE_MS = 3000UL;
    static const uint32_t INIT_DELAY_MS = 1500UL;

    // Mirrors cameraMaybeAutoStart() exactly.
    void tick() {
        if (scheduled || ready || initInProgress) return;
        if (nowMs < SETTLE_MS) return;   // boot not settled — do not latch yet
        scheduled = true;
        if (auto_mode == 0) return;      // off: no init scheduled
        initRequests++;
        (void)INIT_DELAY_MS;             // cameraBeginAsync(1500)
    }
};

// --- cameraArmAutoMode mirror ---------------------------------------------
struct ArmConfig {
    uint8_t  auto_mode = 0;
    uint8_t  dashcam_mins = 0;
    bool     motion_enabled = false;
    uint16_t timelapse_secs = 0;
    int      saves = 0;   // cameraConfigSave() count (only when changed)

    // Mirrors cameraArmAutoMode() exactly.
    void arm() {
        const uint8_t mode = auto_mode;
        if (mode == 0) return;
        bool changed = false;
        switch (mode) {
            case 1: break;  // photo — no continuous engine
            case 2: if (dashcam_mins == 0) { dashcam_mins = 5; changed = true; } break;
            case 3: if (!motion_enabled)  { motion_enabled = true; changed = true; } break;
            case 4: if (timelapse_secs == 0) { timelapse_secs = 10; changed = true; } break;
            default: break;
        }
        if (changed) saves++;
    }
};

TEST_CASE("autostart: auto_mode off never schedules init") {
    AutoStart a;
    a.auto_mode = 0;
    a.nowMs = 4000;
    a.tick();
    CHECK(a.scheduled == true);        // latched the decision
    CHECK(a.initRequests == 0);        // but no init scheduled
    a.tick();                          // no-op after latch
    CHECK(a.initRequests == 0);
}

TEST_CASE("autostart: each non-off mode schedules init exactly once") {
    for (uint8_t m = 1; m <= 4; m++) {
        AutoStart a;
        a.auto_mode = m;
        a.nowMs = 4000;
        a.tick();
        CHECK(a.initRequests == 1);
        a.tick();                      // latched — no duplicate schedule
        a.tick();
        CHECK(a.initRequests == 1);
    }
}

TEST_CASE("autostart: waits for boot settle before latching") {
    AutoStart a;
    a.auto_mode = 3;
    a.nowMs = 2999;
    a.tick();
    CHECK(a.scheduled == false);       // not yet decided
    CHECK(a.initRequests == 0);
    a.nowMs = 3000;
    a.tick();
    CHECK(a.scheduled == true);
    CHECK(a.initRequests == 1);
}

TEST_CASE("autostart: ready or in-progress camera never re-schedules") {
    { AutoStart a; a.auto_mode = 2; a.nowMs = 4000; a.ready = true;  a.tick(); CHECK(a.initRequests == 0); CHECK(a.scheduled == false); }
    { AutoStart a; a.auto_mode = 4; a.nowMs = 4000; a.initInProgress = true; a.tick(); CHECK(a.initRequests == 0); CHECK(a.scheduled == false); }
}

TEST_CASE("autostart: config read happens only after settle (auto_mode off default)") {
    AutoStart a;   // default auto_mode 0
    a.nowMs = 1000; a.tick(); CHECK(a.scheduled == false);
    a.nowMs = 5000; a.tick();
    CHECK(a.scheduled == true);
    CHECK(a.initRequests == 0);        // off → no init
}

TEST_CASE("arm: off and photo leave config untouched") {
    ArmConfig c;
    c.auto_mode = 0; c.arm();
    CHECK(c.dashcam_mins == 0); CHECK(c.motion_enabled == false); CHECK(c.timelapse_secs == 0); CHECK(c.saves == 0);
    c.auto_mode = 1; c.arm();
    CHECK(c.dashcam_mins == 0); CHECK(c.motion_enabled == false); CHECK(c.timelapse_secs == 0); CHECK(c.saves == 0);
}

TEST_CASE("arm: video sets a default dashcam rotation when unset, preserves explicit") {
    ArmConfig c;
    c.auto_mode = 2; c.dashcam_mins = 0; c.arm();
    CHECK(c.dashcam_mins == 5); CHECK(c.saves == 1);
    ArmConfig d;
    d.auto_mode = 2; d.dashcam_mins = 20; d.arm();
    CHECK(d.dashcam_mins == 20); CHECK(d.saves == 0);   // never overrides user choice
}

TEST_CASE("arm: motion enables detection, idempotent") {
    ArmConfig c;
    c.auto_mode = 3; c.arm();
    CHECK(c.motion_enabled == true); CHECK(c.saves == 1);
    c.arm();                                            // already enabled → no save
    CHECK(c.saves == 1);
}

TEST_CASE("arm: timelapse sets a default interval when unset, preserves explicit") {
    ArmConfig c;
    c.auto_mode = 4; c.timelapse_secs = 0; c.arm();
    CHECK(c.timelapse_secs == 10); CHECK(c.saves == 1);
    ArmConfig d;
    d.auto_mode = 4; d.timelapse_secs = 60; d.arm();
    CHECK(d.timelapse_secs == 60); CHECK(d.saves == 0);
}

TEST_CASE("arm: unknown mode is a safe no-op") {
    ArmConfig c;
    c.auto_mode = 9; c.arm();
    CHECK(c.dashcam_mins == 0); CHECK(c.motion_enabled == false); CHECK(c.timelapse_secs == 0); CHECK(c.saves == 0);
}

// --- cameraApplyConfig persist-gating mirror (CAM-27) ----------------------
// Mirrors camera_manager.cpp cameraApplyConfig(): the (clamped) config is
// always persisted, and returns true even when the camera is not ready —
// the sensor apply is deferred to the next init. Before the fix it returned
// false when !s_ready, making the handler report a spurious "Config apply
// failed" for every change made while the camera was off.
struct ApplyConfig {
    int  framesize = 5;
    int  maxFs = 19;        // cameraDetectedMaxFrameSize() (e.g. OV3660 QXGA)
    bool ready = false;
    int  persists = 0;
    bool sensorApplied = false;

    bool apply(int requestedFs) {
        // clampFrameSize(): values below the smallest supported frame (96x96=0)
        // collapse to VGA (10); values above the detected sensor max clamp DOWN
        // to that max (e.g. picking 5MP on an OV3660 gives QXGA 19, not a
        // jarring VGA drop).
        framesize = (requestedFs < 0) ? 10 : (requestedFs > maxFs ? maxFs : requestedFs);
        persists++;                    // cameraConfigSave(s_config)
        if (!ready) return true;       // persisted; sensor apply runs on next init
        sensorApplied = true;
        return true;
    }
};

TEST_CASE("apply: config persists even when camera not ready (CAM-27)") {
    ApplyConfig a;
    a.ready = false;
    CHECK(a.apply(24) == true);        // 24 > maxFs 19 → clamped down to QXGA(19), still success
    CHECK(a.framesize == 19);
    CHECK(a.persists == 1);
    CHECK(a.sensorApplied == false);   // deferred to init
}

TEST_CASE("apply: above-max request clamps down to the sensor max, not VGA") {
    ApplyConfig a;
    a.maxFs = 19;                       // OV3660 → QXGA
    CHECK(a.apply(24) == true);         // 5MP request
    CHECK(a.framesize == 19);           // QXGA 2048x1536, not a drop to 640x480
    ApplyConfig b;
    b.maxFs = 10;                       // VGA-only sensor
    CHECK(b.apply(15) == true);
    CHECK(b.framesize == 10);
}

TEST_CASE("apply: below-minimum request falls back to VGA") {
    ApplyConfig a;
    CHECK(a.apply(-1) == true);
    CHECK(a.framesize == 10);
}

TEST_CASE("apply: in-range request passes through unclamped") {
    ApplyConfig a;
    a.ready = false;
    CHECK(a.apply(13) == true);
    CHECK(a.framesize == 13);
    CHECK(a.persists == 1);
}

TEST_CASE("apply: ready camera applies to sensor") {
    ApplyConfig a;
    a.ready = true;
    CHECK(a.apply(13) == true);
    CHECK(a.framesize == 13);
    CHECK(a.sensorApplied == true);
}
