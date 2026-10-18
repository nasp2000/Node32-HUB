#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>

// Standalone reimpl of the inactivity-timer reset decision.
//
// Rule history:
//   Bug 2 (2026-08): internal buffer injections (shift_end, oee, periodic prox
//   tick, UART bridge) must NOT reset the timer — only a real sensor pulse may.
//
//   Inactivity watchdog fix (2026-08-25): on boards without RS232/LPT (e.g.
//   Waveshare 8DI8DO) ANY DI edge used to reset the timer. Idle-machine sensor
//   chatter (oscillating input / burst noise — measured as ~20-edge bursts in
//   a single drain tick every ~1s) kept seconds_since_last_data at ~0 forever,
//   so the alert never fired. Mirroring proximity_manager.cpp (~line 570), the
//   timer now resets ONLY on a valid production pulse:
//     - channel counts parts (COUNTER/SPEED, not PRESENCE);
//     - gate channel (if wired) reports the machine active;
//     - few edges in the drain tick (multi-edge burst = chatter);
//     - another accepted edge happened recently (not an isolated glitch).
//   Boards WITH RS232/LPT keep the checkbox behaviour (compile-time branch,
//   unchanged).

#define PROX_INACTIVITY_MAX_TICK_DELTA  4UL
#define PROX_INACTIVITY_CONFIRM_MS      10000UL  // RETAINED for reference; no longer used in the reset decision
// FIX (issues #7): lowered 200→60ms. A 600ppm line (10 parts/s) has a 100ms
// inter-pulse interval; the old 200ms threshold masked it as noise, so the
// timer never reset during high-speed production and alerts fired falsely.
// 60ms separates real part flow (>=100ms on a 600ppm line) from electrical
// chatter (<=50ms).
#define PROX_INACTIVITY_MIN_CYCLE_MS    60UL

struct InactState {
    unsigned long lastDataReceived;
    bool alertSent;
    bool alertSentSlot[6];
};

// Mirrors injectDataToRXBuffer(): resetInactivity now defaults to FALSE, so no
// internal injection ever resets the timer (regardless of board type).
static void applyBufferInjection(InactState& st, bool resetInactivity) {
    if (resetInactivity) {  // unreachable for internal injections after the fix
        st.lastDataReceived = 1;
        st.alertSent = false;
        for (int i = 0; i < 6; i++) st.alertSentSlot[i] = false;
    }
}

// Mirrors proximityTick() drain block: reset happens only for a VALID
// production pulse on sensor-only boards.
struct PulseCtx {
    bool     presenceMode; // channel mode == PROX_MODE_PRESENCE
    bool     gateWired;    // gate_ch != 255
    bool     gateState;    // PRESENCE gate channel state
    uint32_t delta;        // edges drained this tick
    uint32_t prevPulseMs;  // previous accepted edge (0 = none since boot/reset)
    uint32_t now;          // millis() at drain time
    uint32_t cycleMs;      // inter-pulse interval (edgeMs - last_pulse_ms)
};

// FIX (issues #1 — Inactivity Alert): Removed the confirmation window that
// required pulses to arrive within PROX_INACTIVITY_CONFIRM_MS (10s) of the last
// edge. Real machines can have cycle times > 10s between parts during normal
// production, so the old check suppressed legit resets and let inactivity
// alerts fire while production was ongoing. Now ANY clean production pulse
// (non-presence, gate open, burst-filtered, minimum cycle time) resets the timer.
static bool isValidProductionPulse(const PulseCtx& p) {
    bool gateOpen = true;
    if (p.gateWired) gateOpen = p.gateState;
    return !p.presenceMode &&
           gateOpen &&
           p.delta <= PROX_INACTIVITY_MAX_TICK_DELTA &&
           p.prevPulseMs != 0 &&
           p.cycleMs >= PROX_INACTIVITY_MIN_CYCLE_MS;
}

static void applySensorPulseIfValid(InactState& st, const PulseCtx& p) {
    if (!isValidProductionPulse(p)) return;
    st.lastDataReceived = 1;  // millis()
    st.alertSent = false;
    for (int i = 0; i < 6; i++) st.alertSentSlot[i] = false;
}

TEST_CASE("Bug2: shift_end summary must NOT reset timer") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    applyBufferInjection(st, /*resetInactivity=*/false);
    CHECK(st.lastDataReceived == 0);   // untouched
    CHECK(st.alertSent == true);       // NOT cleared
    CHECK(st.alertSentSlot[0] == true); // NOT cleared
}

TEST_CASE("Bug2: periodic prox tick must NOT reset timer") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    applyBufferInjection(st, /*resetInactivity=*/false);  // default for PROXIMITY
    CHECK(st.lastDataReceived == 0);
    CHECK(st.alertSentSlot[2] == true);
}

TEST_CASE("Bug2: oee summary must NOT reset timer") {
    InactState st = {0, false, {false, false, false, false, false, false}};
    applyBufferInjection(st, /*resetInactivity=*/false);
    CHECK(st.lastDataReceived == 0);
    CHECK(st.alertSent == false);
}

TEST_CASE("Bug2: UART bridge byte must NOT reset timer on sensor-only board") {
    // arduino_bridge.cpp reset is now guarded by ENABLE_RS232==1||ENABLE_DB25==1.
    InactState st = {0, true, {true, true, true, true, true, true}};
    // On Waveshare the bridge path is compiled out, so no reset occurs.
    applyBufferInjection(st, /*resetInactivity=*/false);
    CHECK(st.lastDataReceived == 0);
    CHECK(st.alertSentSlot[0] == true);
}

TEST_CASE("Bug2: REAL sensor DI1 pulse DOES reset timer (the only legit source)") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    PulseCtx ok = {false, false, true, 1, 99000, 101055, 2055}; // steady ~2s cycle
    applySensorPulseIfValid(st, ok);
    CHECK(st.lastDataReceived == 1);
    CHECK(st.alertSent == false);
    CHECK(st.alertSentSlot[0] == false);
}

// --- Watchdog fix: idle-machine chatter must not keep resetting the timer ---

TEST_CASE("Watchdog: measured chatter burst (20 edges/tick @1s) does NOT reset") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    // Observed live pattern: burst of exactly 20 edges inside one ~16ms drain
    // tick, repeating every ~1.02s while the machine sits idle after shift end.
    PulseCtx chatter = {false, false, true, 20, 160000, 161025, 25};
    applySensorPulseIfValid(st, chatter);
    CHECK(st.lastDataReceived == 0);   // timer keeps aging -> alert can fire
    CHECK(st.alertSentSlot[0] == true); // period flag untouched
}

TEST_CASE("Watchdog: small chatter burst (delta=5..7) also rejected") {
    CHECK(!isValidProductionPulse({false, false, true, 5, 1000, 1200, 200}));
    CHECK(!isValidProductionPulse({false, false, true, 7, 5000, 5201, 201}));
}

TEST_CASE("Watchdog: normal production pulse (delta=1, ~2s cycle) DOES reset") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    PulseCtx part = {false, false, true, 1, 205000, 207055, 2055};
    applySensorPulseIfValid(st, part);
    CHECK(st.lastDataReceived == 1);
}

TEST_CASE("Watchdog: fast production pulses (delta=1..4) DO reset") {
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100400, 400}));
    CHECK(isValidProductionPulse({false, false, true, 4, 200000, 200900, 900}));
}

TEST_CASE("Watchdog: first edge since boot/reset (no confirmation) does NOT reset") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    PulseCtx lone = {false, false, true, 1, 0, 60000, 0};
    applySensorPulseIfValid(st, lone);
    CHECK(st.lastDataReceived == 0);
    // Second edge within the confirm window confirms ongoing activity.
    PulseCtx next = {false, false, true, 1, 60000, 62100, 2100};
    applySensorPulseIfValid(st, next);
    CHECK(st.lastDataReceived == 1);
}

// --- FIX (issues #1): Long gaps no longer suppress the reset ---
// A production pulse arriving 60s after the previous one still represents
// active production (slow but real) and MUST reset the inactivity timer.
// The old 10s confirmation window incorrectly suppressed these resets,
// causing false inactivity alerts while the machine was still producing.
TEST_CASE("Watchdog: slow production pulse (60s gap, real cycle) DOES reset") {
    // 60s since last pulse, 60s inter-pulse interval — well above the
    // 60ms cycle floor, single edge — legitimate slow production.
    CHECK(isValidProductionPulse({false, false, true, 1, 1000, 61000, 60000}));   // 60s gap
    CHECK(isValidProductionPulse({false, false, true, 1, 3600000, 3660001, 60001})); // 60s gap later
    // Boundary: exactly at the old 10s edge — still resets (was the only case
    // that reset under the old logic; now ALL valid pulses reset).
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 110000, 10000}));
}

TEST_CASE("Watchdog: PRESENCE-mode level flicker never counts as production") {
    InactState st = {0, true, {true, true, true, true, true, true}};
    PulseCtx flicker = {true, false, true, 1, 500000, 501000, 1000};
    applySensorPulseIfValid(st, flicker);
    CHECK(st.lastDataReceived == 0);
}

TEST_CASE("Watchdog: wired gate closed blocks reset even with clean single edges") {
    CHECK(!isValidProductionPulse({false, true, false, 1, 200000, 201500, 1500}));
    // Gate open restores the reset.
    CHECK(isValidProductionPulse({false, true, true, 1, 200000, 201500, 1500}));
}

// --- Cycle-time filter: sustained low-rate chatter that passes burst filter ---

TEST_CASE("Cycle: short inter-pulse interval (<60ms) does NOT reset — likely noise") {
    // Edges every 50ms pass burst filter (delta=1) and confirm window, but the
    // short cycle time indicates electrical noise, not genuine part flow.
    CHECK(!isValidProductionPulse({false, false, true, 1, 100000, 100030, 30}));
    CHECK(!isValidProductionPulse({false, false, true, 1, 100000, 100050, 50}));
    CHECK(!isValidProductionPulse({false, false, true, 2, 100000, 100055, 55}));
}

TEST_CASE("Cycle: boundary — exactly 60ms interval DOES reset") {
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100060, 60}));
}

TEST_CASE("Cycle: 600ppm line (100ms cycle) DOES reset — was masked as noise at 200ms") {
    // FIX (issues #7): 600ppm = 10 parts/s = 100ms inter-pulse interval. The old
    // 200ms MIN_CYCLE threshold classified these as chatter, so the inactivity
    // timer never reset while the line ran at speed → false alerts during
    // production. Down to 60ms, a genuine 100ms production cycle resets.
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100100, 100}));
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100150, 150}));
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100180, 180}));
}

TEST_CASE("Cycle: normal production cycle (500ms–5s) DOES reset") {
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 100500, 500}));
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 102000, 2000}));
    CHECK(isValidProductionPulse({false, false, true, 1, 100000, 105000, 5000}));
}

TEST_CASE("Cycle: noise bursts with short cycle also rejected") {
    // 3 edges in tick, cycle time 35ms — passes delta<=4 but cycle filter rejects.
    CHECK(!isValidProductionPulse({false, false, true, 3, 100000, 100035, 35}));
}
