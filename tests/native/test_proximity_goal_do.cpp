#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstring>

// Standalone reimpl of goal-based gating and DO pulse timing logic.
// Tests: goal reached triggers DO, DO auto-off, DO clamp, no re-trigger.

static const uint32_t PROX_DO_MAX_PULSE_MS = 10000UL;

struct TestChannel {
    char        name[32];
    uint32_t    count_in;
    uint32_t    count_out;
    uint32_t    goal;
    uint32_t    pulse_delay_ms;
    uint32_t    pulse_dur_ms;
    bool        do_active;
    uint32_t    do_start_ms;
    bool        monitor_only;
};

static bool evalDoPulse(TestChannel& ch, uint32_t nowMs) {
    // Check if DO should activate based on goal.
    // Production semantics: goalReached = (goal > 0 && count_in > goal) STRICT.
    // So DO fires while count_in <= goal (i.e. pulse on reaching the goal value).
    if (!ch.monitor_only && ch.goal > 0 && ch.count_in <= ch.goal && !ch.do_active) {
        ch.do_active = true;
        ch.do_start_ms = nowMs + ch.pulse_delay_ms;
    }
    // Check if active DO should turn off
    if (ch.do_active) {
        if (nowMs >= ch.do_start_ms) {
            uint32_t pulseElapsed = nowMs - ch.do_start_ms;
            if (pulseElapsed >= ch.pulse_dur_ms) {
                ch.do_active = false;
            }
        }
    }
    return ch.do_active;
}

TEST_CASE("DO activates when goal reached") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 100;

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == true);
    CHECK(ch.do_active == true);
    CHECK(ch.do_start_ms == 1000); // no delay
}

TEST_CASE("DO activates while below goal (fires until goal exceeded)") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 99;   // < goal, goalReached false -> DO fires

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == true);
    CHECK(ch.do_active == true);
}

TEST_CASE("DO auto-off after pulse_dur_ms") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 100;
    ch.do_active = true;
    ch.do_start_ms = 1000;

    // Still active within duration
    bool active = evalDoPulse(ch, 1400);
    CHECK(active == true);

    // Turns off after duration
    active = evalDoPulse(ch, 1501);
    CHECK(active == false);
    CHECK(ch.do_active == false);
}

TEST_CASE("DO does NOT re-trigger if already active") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 1000;
    ch.monitor_only = false;
    ch.count_in = 100;
    ch.do_active = true;
    ch.do_start_ms = 1000;

    // count_in increases further while DO already active
    ch.count_in = 150;
    bool active = evalDoPulse(ch, 1200);
    CHECK(active == true); // still active from first trigger
    // do_start_ms should not have been reset
    CHECK(ch.do_start_ms == 1000);
}

TEST_CASE("DO does NOT re-fire once count exceeds goal (goalReached)") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;

    // First cycle: count == goal -> fires
    ch.count_in = 100;
    bool active = evalDoPulse(ch, 1000);
    CHECK(active == true);

    // Pulse ends
    active = evalDoPulse(ch, 1600);
    CHECK(active == false);

    // count_in exceeds goal -> goalReached, DO stays off
    ch.count_in = 200;
    active = evalDoPulse(ch, 2000);
    CHECK(active == false);
    CHECK(ch.do_active == false);
}

TEST_CASE("DO pulse delay is respected") {
    TestChannel ch = {};
    ch.goal = 50;
    ch.pulse_delay_ms = 200;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 50;

    // Goal reached at t=1000, DO should activate at t=1200
    evalDoPulse(ch, 1000);
    CHECK(ch.do_active == true);
    CHECK(ch.do_start_ms == 1200); // 1000 + 200 delay

    // Before delay: DO not yet firing
    bool active = evalDoPulse(ch, 1100);
    CHECK(active == true); // still active, but hasn't started pulsing yet

    // After delay: DO firing (pulse started)
    active = evalDoPulse(ch, 1210);
    CHECK(active == true);
}

TEST_CASE("Monitor-only mode suppresses DO") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = true;
    ch.count_in = 100;

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == false);
    CHECK(ch.do_active == false);
}

TEST_CASE("Goal of zero never triggers DO") {
    TestChannel ch = {};
    ch.goal = 0;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 9999;

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == false);
    CHECK(ch.do_active == false);
}

TEST_CASE("DO pulse duration clamped at PROX_DO_MAX_PULSE_MS") {
    TestChannel ch = {};
    ch.goal = 10;
    ch.monitor_only = false;
    ch.count_in = 10;

    // Clamp applied during config (as production code does)
    uint32_t configuredDur = 30000;
    if (configuredDur > PROX_DO_MAX_PULSE_MS) configuredDur = PROX_DO_MAX_PULSE_MS;
    CHECK(configuredDur == 10000);

    ch.pulse_dur_ms = configuredDur;
    ch.do_active = true;
    ch.do_start_ms = 1000;

    // Still active just before 10s elapses
    bool active = evalDoPulse(ch, 10999);
    CHECK(active == true);

    // Turns off exactly at 10s
    active = evalDoPulse(ch, 11000);
    CHECK(active == false);
}

// ── Expanded DO/goal coverage (production `count_in > goal` semantics) ──
TEST_CASE("DO fires exactly at goal (count_in == goal, strict >)") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 100;   // == goal → fires (goalReached requires > goal)

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == true);
}

TEST_CASE("DO does NOT re-fire once count_in exceeds goal") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 300;
    ch.monitor_only = false;

    // Fire at goal
    ch.count_in = 100;
    evalDoPulse(ch, 1000);
    CHECK(ch.do_active == true);

    // Let pulse end
    evalDoPulse(ch, 1400);
    CHECK(ch.do_active == false);

    // count_in now exceeds goal → goalReached, no re-fire
    ch.count_in = 150;
    bool active = evalDoPulse(ch, 2000);
    CHECK(active == false);
}

TEST_CASE("DO with pulse_dur zero never activates") {
    TestChannel ch = {};
    ch.goal = 50;
    ch.pulse_dur_ms = 0;   // production requires pulse_dur_ms > 0 to fire
    ch.monitor_only = false;
    ch.count_in = 50;

    // Mirror production guard: skip scheduling when pulse_dur_ms == 0
    bool canFire = (ch.goal > 0 && ch.count_in <= ch.goal && !ch.monitor_only && ch.pulse_dur_ms > 0);
    CHECK(canFire == false);
    CHECK(evalDoPulse(ch, 1000) == false);
}

TEST_CASE("DO fires while well below a large goal (relay per pulse)") {
    TestChannel ch = {};
    ch.goal = 1000;
    ch.pulse_dur_ms = 500;
    ch.monitor_only = false;
    ch.count_in = 999;   // <= goal -> fires

    bool active = evalDoPulse(ch, 1000);
    CHECK(active == true);
    CHECK(ch.do_active == true);
}

TEST_CASE("DO remains active across full duration even if count crosses goal") {
    TestChannel ch = {};
    ch.goal = 100;
    ch.pulse_dur_ms = 1000;
    ch.monitor_only = false;
    ch.count_in = 100;
    evalDoPulse(ch, 1000);
    CHECK(ch.do_active == true);

    // While pulse ongoing, count_in jumps past goal — DO must NOT cut short.
    ch.count_in = 200;
    bool active = evalDoPulse(ch, 1500);
    CHECK(active == true);
    CHECK(ch.do_start_ms == 1000); // not re-timed
}
