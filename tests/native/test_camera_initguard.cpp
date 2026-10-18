#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Standalone reimpl of the camera init/stop serialisation protocol from
// camera_manager.cpp (spinlock-guarded flags):
//   - cameraBeginAsync: atomic check+set — a boot auto-start racing a page
//     Start (loop task vs HTTP task, different cores) must create exactly
//     ONE CamInit task. Two concurrent esp_camera_init() calls smash the
//     driver queues (cam_task LoadProhibited in xQueueGenericSend).
//   - cameraStop: waits bounded for an in-flight init, clears ready first,
//     grace period, then deinit only if the camera was live.

struct InitGuard {
    bool ready = false;
    bool initInProgress = false;
    int tasksCreated = 0;
    bool taskCreateOk = true;
    // Mirrors cameraBeginAsync() under the spinlock (modelled as atomic here:
    // check+set is indivisible, which is exactly what the mux guarantees).
    bool beginAsync() {
        bool busy = ready || initInProgress;
        if (!busy) initInProgress = true;
        if (busy) return true;
        if (!taskCreateOk) { initInProgress = false; return false; }
        tasksCreated++;
        return true;
    }
    void initTaskDone() { initInProgress = false; }
};

struct StopCtl {
    bool ready = false;
    bool initInProgress = false;
    int initWaitPolls = 0;   // polls consumed waiting (each = 100 ms)
    int initDrainAt = 0;     // poll index at which the init task finishes (-1: never)
    bool deinitCalled = false;
    // Mirrors cameraStop(): bounded init wait, clear ready, grace, cond deinit.
    // Returns polls waited.
    int stop() {
        int waited = 0;
        while (waited < 50 && initInProgress) {
            waited++;
            initWaitPolls++;
            if (initDrainAt >= 0 && waited >= initDrainAt) initInProgress = false;
        }
        bool wasReady = ready;
        ready = false;
        if (!wasReady) return waited;
        deinitCalled = true;  // after the 400 ms grace (modelled, not waited)
        return waited;
    }
};

TEST_CASE("double start coalesces into a single init task") {
    InitGuard g;
    CHECK(g.beginAsync() == true);
    CHECK(g.beginAsync() == true);  // racing caller: admitted, no new task
    CHECK(g.tasksCreated == 1);
    g.initTaskDone();
    CHECK(g.beginAsync() == true);
    CHECK(g.tasksCreated == 2);  // failed init may be retried with a new task
}

TEST_CASE("start while ready never spawns init") {
    InitGuard g;
    g.ready = true;
    CHECK(g.beginAsync() == true);
    CHECK(g.tasksCreated == 0);
}

TEST_CASE("task create failure clears the flag") {
    InitGuard g;
    g.taskCreateOk = false;
    CHECK(g.beginAsync() == false);
    g.taskCreateOk = true;
    CHECK(g.beginAsync() == true);
    CHECK(g.tasksCreated == 1);
}

TEST_CASE("stop waits for in-flight init then stops") {
    StopCtl s;
    s.ready = false;  // not live yet: init running at boot
    s.initInProgress = true;
    s.initDrainAt = 3;
    int waited = s.stop();
    CHECK(waited == 3);
    CHECK(s.ready == false);
    CHECK(s.deinitCalled == false);  // never live: nothing to tear down
}

TEST_CASE("stop of a live camera requests deinit") {
    StopCtl s;
    s.ready = true;
    s.initInProgress = false;
    int waited = s.stop();
    CHECK(waited == 0);
    CHECK(s.ready == false);  // cleared FIRST so captures stop
    CHECK(s.deinitCalled == true);
}

TEST_CASE("stop wait is bounded when init never drains") {
    StopCtl s;
    s.ready = true;
    s.initInProgress = true;
    s.initDrainAt = -1;  // stuck init
    int waited = s.stop();
    CHECK(waited == 50);  // 50 x 100 ms cap, then proceeds
    CHECK(s.deinitCalled == true);
}
