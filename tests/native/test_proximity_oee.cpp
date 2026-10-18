#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"
#include <ctime>
#include <cstring>

// Compile the REAL production OEE/shift-duration helpers from proximity_manager.h
// (proximityComputeOee + proximityShiftSlotDurationSec) so the test exercises the
// exact formula shipped to the board. Standard OEE, ISO 22400-2 decomposition
// (user spec 2026-09): A = run/elapsed (elapsed = seconds since the count reset —
// "contas na hora" at any point of the shift), P = cIn/(tgtPpm*RUNNING minutes)
// (stops do not move the target — P isolates cadence), Q = good/total.
// Stubs provide Arduino.h / ArduinoJson.h / feature_flags.h.
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

// ── Standard OEE (ISO 22400-2 — user spec 2026-09) ─────────────────────
//   A = run / elapsed * 100        (elapsedSec = seconds since the count reset —
//                                   manual Reset or automatic shift rotation;
//                                   when the machine stops at 3h, at 8h A already
//                                   shows 3h/8h — the accounts are ready at any
//                                   moment of the shift)
//   P = cIn / theoretical * 100    theoretical = tgtPpm * RUNNING minutes (the
//                                   standard: stops do NOT move the target, so
//                                   P=100% whenever the machine holds cadence and
//                                   P<100% isolates real speed losses)
//   Q = effectiveGood / cIn * 100  good/total; never >100 naturally
//   OEE = A * P * Q
// Guards: elapsedSec==0 (fresh reset) → A=0; runSec==0 → P=0; A/P clipped to 100.
TEST_CASE("OEE: A = run/elapsed, P = cIn/theoretical(running), Q = good/total, OEE = A*P*Q") {
    // runSec=528, elapsed=3600 (1h window), cIn=8800, good=8800, tp=24
    ProximityOeeResult r = proximityComputeOee(528, 3600, 8800, 8800, 24.0f);
    // theoretical = 24 * (528/60) = 211 ; P = 8800/211*100 = 4170% → clipped to 100
    CHECK(r.theoretical == 211);
    CHECK(r.availability == doctest::Approx((float)528 * 100.0f / 3600.0f)); // 14.67%
    CHECK(r.performance == doctest::Approx(100.0f));   // >100, clipped
    CHECK(r.quality == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx(r.availability * r.performance * r.quality / 10000.0f).epsilon(0.001f));
}

TEST_CASE("OEE: regression with exact board values") {
    // runSec=439, elapsed=3600 (1h), tp=24, cIn=8800
    // theoretical = 24 * (439/60) = 175 ; P = 8800/175*100 = 5028% → clipped to 100
    ProximityOeeResult r = proximityComputeOee(439, 3600, 8800, 8800, 24.0f);
    CHECK(r.theoretical == 175);
    CHECK(r.availability == doctest::Approx((float)439 * 100.0f / 3600.0f)); // 12.19%
    CHECK(r.performance == doctest::Approx(100.0f));   // >100, clipped
    CHECK(r.quality == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx(r.availability * r.performance * r.quality / 10000.0f).epsilon(0.001f));
}

// STANDARD (Nuno 2026-09, supersedes the old elapsed-P requirement): stops do
// NOT move the Performance target — a 1 min stop drags A to 98.33% but P stays
// 100% when the machine held its cadence while running.
TEST_CASE("OEE standard: stops degrade A, never P (user requirement)") {
    // 1h window, 1 min stop (run 3540s, elapsed 3600s), machine at target cadence
    // (10/min for the 59 min it ran → cIn=590): theoretical = 10 * 59 = 590.
    ProximityOeeResult r = proximityComputeOee(3540, 3600, 590, 590, 10.0f);
    CHECK(r.theoretical == 590);
    CHECK(r.availability == doctest::Approx(3540.0f * 100.0f / 3600.0f).epsilon(0.01f)); // 98.33%
    CHECK(r.performance == doctest::Approx(100.0f));                                      // cadence held → 100
    CHECK(r.oee == doctest::Approx(r.availability * r.performance * r.quality / 10000.0f).epsilon(0.01f)); // 98.33%
}

// STANDARD diagnostic value: with no stops (A=100), P<100% isolates a SLOW
// machine — speed losses are distinguishable from stops.
TEST_CASE("OEE standard: half cadence with no stops → P=50%, OEE=50%") {
    // Full 8h running at half the 600/min target: cIn = 300*480 = 144000.
    // theoretical = 600 * 480 = 288000 ; P = 144000/288000*100 = 50%.
    ProximityOeeResult r = proximityComputeOee(28800, 28800, 144000, 144000, 600.0f);
    CHECK(r.theoretical == 288000);
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.performance == doctest::Approx(50.0f));
    CHECK(r.oee == doctest::Approx(50.0f));
}

TEST_CASE("OEE: P>100 clamps (cadence faster than target)") {
    // theoretical = 2.0 * (150*60/60) = 2.0*150 = 300 ; P = 2400/300*100 = 800% → clipped
    ProximityOeeResult r = proximityComputeOee(150 * 60, 150 * 60, 2400, 2400, 2.0f);
    CHECK(r.theoretical == 300);
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.performance == doctest::Approx(100.0f));   // 800% → clipped
    CHECK(r.quality == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx(100.0f));
}

TEST_CASE("OEE: Q is good/total, never > 100") {
    // goods 4861 / total 4921 = 98.78%; theoretical = 0.06 * 615 = 36
    ProximityOeeResult r = proximityComputeOee(615 * 60, 615 * 60, 4921, 4861, 0.06f);
    CHECK(r.theoretical == 36);
    CHECK(r.quality == doctest::Approx((float)4861 * 100.0f / 4921.0f).epsilon(0.01f)); // 98.78%
    CHECK(r.quality <= 100.0f);
    CHECK(r.performance == doctest::Approx(100.0f));   // clipped (cIn >> theoretical)
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx((float)100.0f * 100.0f * (float)4861 * 100.0f / 4921.0f / 10000.0f).epsilon(0.01f)); // 98.78%
}

TEST_CASE("OEE: individual P>100 cases") {
    // Normal cadence, low production (A=100 since measured full time running, P low).
    // theoretical = 2.0 * (200*60/60) = 2.0*200 = 400 ; P = 50/400*100 = 12.5%
    ProximityOeeResult a = proximityComputeOee(200 * 60, 200 * 60, 50, 50, 2.0f);
    CHECK(a.theoretical == 400);
    CHECK(a.availability == doctest::Approx(100.0f));
    CHECK(a.performance == doctest::Approx(12.5f));
    CHECK(a.quality == doctest::Approx(100.0f));
    CHECK(a.oee == doctest::Approx(100.0f * 12.5f * 100.0f / 10000.0f));

    // P only above 100 (full time running, fast cadence): performance clips.
    // theoretical = 1.0 * (100*60/60) = 100 ; P = 400/100*100 = 400% → clipped
    ProximityOeeResult p = proximityComputeOee(100 * 60, 100 * 60, 400, 400, 1.0f);
    CHECK(p.theoretical == 100);
    CHECK(p.availability == doctest::Approx(100.0f));
    CHECK(p.performance == doctest::Approx(100.0f));   // 400% → clipped
    CHECK(p.quality == doctest::Approx(100.0f));
    CHECK(p.oee == doctest::Approx(100.0f));
}

TEST_CASE("OEE: Q<100 keeps OEE below 100 even when A,P clip") {
    // No stops (A=100) + fast cadence (P clips to 100), but quality is only
    // 50% → OEE = 100*100*50/10000 = 50%, stays correctly below 100.
    // theoretical = 2.0 * (150*60/60) = 2.0*150 = 300 ; P = 2000/300*100 = 666% → clipped
    ProximityOeeResult r = proximityComputeOee(150 * 60, 150 * 60, 2000, 1000, 2.0f);
    CHECK(r.theoretical == 300);
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.performance == doctest::Approx(100.0f));
    CHECK(r.quality == doctest::Approx(50.0f));
    CHECK(r.oee == doctest::Approx(50.0f));
}

TEST_CASE("OEE standard: tgtPpm=0 -> theoretical=0, P=0 (no cycle-time fallback)") {
    // Even with production, no target configured means P=0 (standard: never invent
    // theoretical from observed cycle time).
    ProximityOeeResult r = proximityComputeOee(528, 3600, 8800, 8800, 0.0f);
    CHECK(r.theoretical == 0);
    CHECK(r.performance == doctest::Approx(0.0f));
    CHECK(r.availability == doctest::Approx((float)528 * 100.0f / 3600.0f));
    CHECK(r.oee == doctest::Approx(0.0f));
}

TEST_CASE("OEE standard: 100% only with zero stops AND on-target cadence") {
    // theoretical = 1.0 * (480*60/60) = 480 ; P = 480/480*100 = 100%
    ProximityOeeResult r = proximityComputeOee(480 * 60, 480 * 60, 480, 480, 1.0f);
    CHECK(r.theoretical == 480);
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.performance == doctest::Approx(100.0f));
    CHECK(r.quality == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx(100.0f));
}

// Waveshare .29 regression (measured on the panel): 8h shift, machine ran only
// 6281s (126020 parts). Standard P over RUNNING time: the machine ran at ~2x the
// 580/min target while producing, so P clips to 100 and OEE is availability-
// limited (21.81%) — the stop no longer drags P down.
TEST_CASE("OEE: 8h shift with heavy downtime → P clips, OEE availability-limited (waveshare)") {
    ProximityOeeResult r = proximityComputeOee(6281, 28800, 126020, 126020, 580.0f);
    CHECK(r.theoretical == 60716);   // 580 * (6281/60)
    CHECK(r.availability == doctest::Approx(6281.0f * 100.0f / 28800.0f).epsilon(0.01f)); // 21.81%
    CHECK(r.performance == doctest::Approx(100.0f));   // 126020/60716 = 207% → clipped
    CHECK(r.oee == doctest::Approx(21.81f).epsilon(0.05f));  // ≈ A, not A²
}

TEST_CASE("OEE standard: availability 0 when run=0; A guard for fresh reset") {
    // No running time (e.g. nothing measured yet) -> A=0. Keep OEE well-defined (0).
    ProximityOeeResult r = proximityComputeOee(0, 28800, 100, 100, 5.0f);
    CHECK(r.availability == doctest::Approx(0.0f));
    CHECK(r.oee == doctest::Approx(0.0f));

    // Run>0, no downtime: all measured time was running -> A=100.
    ProximityOeeResult r2 = proximityComputeOee(500, 500, 100, 100, 5.0f);
    CHECK(r2.theoretical == 41);      // 5 * (500/60) = 41
    CHECK(r2.availability == doctest::Approx(100.0f));
    CHECK(r2.performance == doctest::Approx(100.0f));   // 100/41*100 = 243% → clipped
    CHECK(r2.oee == doctest::Approx(r2.availability * r2.performance * r2.quality / 10000.0f).epsilon(0.01f));

    // Fresh reset (elapsed == 0): no div-by-zero — A stays 0 (production can't
    // exist without elapsed time either).
    ProximityOeeResult f = proximityComputeOee(0, 0, 0, 0, 5.0f);
    CHECK(f.availability == doctest::Approx(0.0f));
    CHECK(f.theoretical == 0);
    CHECK(f.performance == doctest::Approx(0.0f));
    CHECK(f.oee == doctest::Approx(0.0f));
}

// ── The exact user scenario (Nuno 2026-09): "contas na hora" ───────────────
// machine runs at 600ppm target for the first 3h of an 8h shift, then stops.
// STANDARD: at the 3h mark the accounts are perfect (100%); after the stop the
// counters freeze — A keeps degrading with the elapsed window (3h/8h = 37.5%),
// P stays 100% (cadence was held while running), OEE = 37.5%.
TEST_CASE("OEE standard: machine stops at 3h — at 8h A=37.5%, P=100%, OEE=37.5% (user spec)") {
    // At 3h (still running): elapsed=10800, run=10800, cIn=108000 (600/min * 180min)
    ProximityOeeResult at3h = proximityComputeOee(10800, 10800, 108000, 108000, 600.0f);
    CHECK(at3h.theoretical == 108000);             // 600 * (10800/60)
    CHECK(at3h.availability == doctest::Approx(100.0f));
    CHECK(at3h.performance == doctest::Approx(100.0f));
    CHECK(at3h.oee == doctest::Approx(100.0f));

    // At 8h (stopped at 3h): elapsed=28800, run still 10800, cIn still 108000.
    ProximityOeeResult at8h = proximityComputeOee(10800, 28800, 108000, 108000, 600.0f);
    CHECK(at8h.theoretical == 108000);             // 600 * (10800/60) — stops don't move the target
    CHECK(at8h.availability == doctest::Approx(10800.0f * 100.0f / 28800.0f).epsilon(0.01f)); // 37.5%
    CHECK(at8h.performance == doctest::Approx(100.0f));   // cadence was held → stays 100
    CHECK(at8h.oee == doctest::Approx(37.5f).epsilon(0.01f));
    CHECK(at8h.oee < at3h.oee);                    // degraded by the idle hours, not frozen
}

// ── proximityOeePlannedSec (disabled-shift fallback — kept as legacy utilities) ─
TEST_CASE("OEE planned fallback: disabled shift -> 86400 (full day)") {
    // Shift disabled -> proximityShiftSlotDurationSec returns 0, but the OEE
    // caller must NOT use 0 (would zero availability and OEE). Fallback = 24h.
    ProximityShiftConfig sc = {};
    sc.enabled = false; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 0);      // raw still 0
    CHECK(proximityOeePlannedSec(sc, 0) == 86400UL);       // fallback to full day
    CHECK(proximityOeePlannedSec(sc, -1) == 86400UL);      // no active shift -> fallback too
}

TEST_CASE("OEE planned with active shift: uses the real slot duration") {
    // Where a shift IS active, the fallback must NOT kick in.
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    CHECK(proximityOeePlannedSec(sc, 1) == 28800UL);       // 8h slot
    CHECK(proximityOeePlannedSec(sc, 0) == 28800UL);
}

TEST_CASE("OEE: availability is Elapsed-based — shift planning/disabled no longer matters") {
    // Regression for the Waveshare .29 report: a disabled shift (plannedSec=0) used
    // to pin availability=0 -> OEE=0 despite heavy production. With Elapsed-based
    // OEE the planned-shift fallback is irrelevant: 1h of full work against 1h of
    // elapsed time = 100%, no matter the shift config.
    ProximityOeeResult r = proximityComputeOee(3600, 3600, 600, 600, 10.0f);
    CHECK(r.availability == doctest::Approx(100.0f));
    CHECK(r.performance == doctest::Approx(100.0f));
    CHECK(r.oee == doctest::Approx(100.0f));
}

// ── Planned duration from configured shift slots ───────────────────────────
TEST_CASE("shift duration: 3 slots 06/14/22 -> every slot is 8h (28800 s)") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 28800);
    CHECK(proximityShiftSlotDurationSec(sc, 1) == 28800);
    CHECK(proximityShiftSlotDurationSec(sc, 2) == 28800); // wraps across midnight
}

TEST_CASE("shift duration: 4h/2h mixed slots use real span") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {10, 0};   // 4h to next
    sc.slots[2] = {12, 0};                          // 2h -> wraps to 06 next
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 4 * 3600);
    CHECK(proximityShiftSlotDurationSec(sc, 1) == 2 * 3600);
    CHECK(proximityShiftSlotDurationSec(sc, 2) == 18 * 3600);
}

TEST_CASE("shift duration: single slot -> 24h (86400 s)") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 1; sc.slots[0] = {0, 0};
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 86400);
}

TEST_CASE("shift duration: disabled or empty -> 0 (planned unknown)") {
    ProximityShiftConfig sc = {};
    sc.enabled = false; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 0);
    ProximityShiftConfig e = {};
    e.enabled = true; e.nSlots = 0;
    CHECK(proximityShiftSlotDurationSec(e, 0) == 0);
}

TEST_CASE("shift duration: out-of-range index clamps to slot 0") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    CHECK(proximityShiftSlotDurationSec(sc, 99) == 28800);
}

TEST_CASE("shift duration: different minute offsets respected") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 2;
    sc.slots[0] = {6, 30}; sc.slots[1] = {14, 45}; // 8h15m = 495 min
    CHECK(proximityShiftSlotDurationSec(sc, 0) == 495 * 60);
    CHECK(proximityShiftSlotDurationSec(sc, 1) == (1440 - 495) * 60); // wraps
}

// ── Slot sorting (fix for the 6h shift-end email never firing) ────────────
TEST_CASE("sort: night config entered as 22,6 reorders to ascending 6,22") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 2;
    sc.slots[0] = {22, 0}; sc.slots[1] = {6, 0};
    proximitySortShiftSlots(sc);
    CHECK(sc.slots[0].hour == 6);
    CHECK(sc.slots[1].hour == 22);
}

TEST_CASE("sort: minutes are part of the order (22:30 vs 6:15 vs 14:45)") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {14, 45}; sc.slots[1] = {22, 30}; sc.slots[2] = {6, 15};
    proximitySortShiftSlots(sc);
    CHECK(sc.slots[0].hour == 6);
    CHECK(sc.slots[0].minute == 15);
    CHECK(sc.slots[1].hour == 14);
    CHECK(sc.slots[1].minute == 45);
    CHECK(sc.slots[2].hour == 22);
    CHECK(sc.slots[2].minute == 30);
}

TEST_CASE("sort: already-sorted config is untouched and single slot unchanged") {
    ProximityShiftConfig sc = {};
    sc.enabled = true; sc.nSlots = 3;
    sc.slots[0] = {6, 0}; sc.slots[1] = {14, 0}; sc.slots[2] = {22, 0};
    ProximityShiftConfig before = sc;
    proximitySortShiftSlots(sc);
    CHECK(sc.slots[0].hour == before.slots[0].hour);
    CHECK(sc.slots[1].hour == before.slots[1].hour);
    CHECK(sc.slots[2].hour == before.slots[2].hour);

    ProximityShiftConfig one = {};
    one.enabled = true; one.nSlots = 1; one.slots[0] = {22, 30};
    proximitySortShiftSlots(one);
    CHECK(one.slots[0].hour == 22);
    CHECK(one.slots[0].minute == 30);
}

// ── OEE per-second window accumulation (proximityTick OEE bucket) ──────────
// FIX (issues #7): the window is cleared ONLY when a full second is consumed.
// Before the fix, oee_count_window was zeroed on EVERY tick, so on a fast loop
// (~tens of ms/tick) the sub-second edge accumulation was wiped and nearly every
// consumed second saw windowCount=0 → counted as downtime → A/OEE ~0 while the
// machine produced (live Waveshare .29: running_sec=40, downtime_sec=23775).
// proximityOeeWindowConsume() is the exact production helper.
TEST_CASE("OEE window: edges accumulate across ticks; second counts running, not downtime") {
    uint32_t msAccum = 0, window = 0, running = 0, downtime = 0;
    // Fast loop: 20 ticks of 50ms, one edge every 100ms (600ppm production).
    for (int t = 0; t < 20; t++) {
        if (t % 2 == 0) window += 1;          // edge every other tick
        msAccum += 50;
        proximityOeeWindowConsume(msAccum, window, /*gateOpen=*/true, running, downtime,
                                  /*lastEdgeMs=*/950, /*nowMs=*/1000, /*graceMs=*/0);
    }
    CHECK(msAccum == 0);                       // exactly 1000ms consumed (1s)
    CHECK(running  == 1);                      // 1000ms of busy production = 1 running second
    CHECK(downtime == 0);                      // edges present every second → never downtime
}

TEST_CASE("OEE window: idle (no edges) counts downtime, not running") {
    uint32_t msAccum = 0, window = 0, running = 0, downtime = 0;
    for (int t = 0; t < 20; t++) {
        msAccum += 50;
        proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                                  /*lastEdgeMs=*/0, /*nowMs=*/1000, /*graceMs=*/0);
    }
    CHECK(running  == 0);
    CHECK(downtime == 1);
}

TEST_CASE("OEE window: loop stall (2000ms in one tick) consumes 2s, both running") {
    // Regression for issues #3: a big elapsed (stall from OTA/SD) crossing the
    // 1000ms boundary twice must count BOTH seconds against the pre-captured
    // window, not mark the 2nd second as downtime by clearing mid-loop.
    uint32_t msAccum = 0, window = 5, running = 0, downtime = 0;
    msAccum += 2000;                            // one tick with a 2s gap
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/1500, /*nowMs=*/2000, /*graceMs=*/0);
    CHECK(running  == 2);                       // both consumed seconds saw edges
    CHECK(downtime == 0);
    CHECK(msAccum  == 0);
}

TEST_CASE("OEE window: gate closed → consumed seconds are not classified") {
    uint32_t msAccum = 0, window = 3, running = 0, downtime = 0;
    msAccum += 1500;                            // 1.5s of gate-closed time
    proximityOeeWindowConsume(msAccum, window, /*gateOpen=*/false, running, downtime,
                              /*lastEdgeMs=*/1000, /*nowMs=*/1500, /*graceMs=*/0);
    CHECK(running  == 0);
    CHECK(downtime == 0);
    CHECK(msAccum  == 500);                     // fraction beyond whole seconds stays pending
}

// ── Slow-machine grace (standard APT): a 2ppm line has a 30s ideal cycle, so
// a 60s grace keeps gap-seconds running; only a gap beyond grace is a stop ──
TEST_CASE("OEE window: slow machine inside grace counts running (no edges)") {
    // 5 consumed seconds, zero edges, last edge 30s ago, grace 60s (2ppm line).
    uint32_t msAccum = 5000, window = 0, running = 0, downtime = 0;
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/100000, /*nowMs=*/130000, /*graceMs=*/60000);
    CHECK(running  == 5);
    CHECK(downtime == 0);
    CHECK(msAccum  == 0);
}

TEST_CASE("OEE window: gap beyond grace counts downtime") {
    // Same 2ppm line, but the last edge was 120s ago (> 60s grace) → stopped.
    uint32_t msAccum = 3000, window = 0, running = 0, downtime = 0;
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/100000, /*nowMs=*/220000, /*graceMs=*/60000);
    CHECK(running  == 0);
    CHECK(downtime == 3);
}

TEST_CASE("OEE window: grace disabled (target 0) keeps legacy downtime behaviour") {
    uint32_t msAccum = 2000, window = 0, running = 0, downtime = 0;
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/100000, /*nowMs=*/101000, /*graceMs=*/0);
    CHECK(running  == 0);
    CHECK(downtime == 2);
}

TEST_CASE("OEE window: fresh reset (never pulsed) never inherits grace") {
    // Boot-guard: lastEdgeMs==0 forces downtime even when nowMs sits inside grace.
    uint32_t msAccum = 2000, window = 0, running = 0, downtime = 0;
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/0, /*nowMs=*/5000, /*graceMs=*/60000);
    CHECK(running  == 0);
    CHECK(downtime == 2);
}

// ── Grace helper: ideal EDGE cycle accounts for the multiplier ──────────
// Live Waveshare .29 case: 2050ms cycle, 10 parts/pulse, target 300/min.
// Ideal edge cycle = 60000*10/300 = 2000ms -> grace 4000ms, so the 2050ms
// inter-pulse gaps count as running (without the multiplier the grace would
// be 400ms and every gap would misread as downtime).
TEST_CASE("OEE grace: accounts for multiplier (live .29 case: tp=300, ml=10 -> 4000ms)") {
    CHECK(proximityOeeGraceMs(300.0f, 10) == 4000);
    CHECK(proximityOeeGraceMs(600.0f, 1) == 200);     // fast line: 100ms cycle
    CHECK(proximityOeeGraceMs(0.0f, 10) == 0);        // no target -> no grace
    CHECK(proximityOeeGraceMs(300.0f, 0) == 0);       // invalid multiplier -> no grace
    CHECK(proximityOeeGraceMs(1.0f, 9999) == 600000); // capped at 10 min
}

TEST_CASE("OEE grace: multiplier always applies with a divider (Nuno spec 2026-09-24)") {
    // Verificado na board: div=5, target 300/min.
    // Ideal EDGE cycle (multiplicative) = 60000*ml/(target*div).
    CHECK(proximityOeeGraceMs(300.0f, 1, 5) == 80);
    CHECK(proximityOeeGraceMs(300.0f, 2, 5) == 160);  // ml doubles the grace
    CHECK(proximityOeeGraceMs(600.0f, 2, 5) == 80);   // faster target halves it
}

// ── Count scaling: MULTIPLICATIVE ──────────────────────────────────────
// (Nuno spec 2026-09-24) The multiplier ALWAYS applies: parts = pulses*ml/div.
// Ex.: ml=1,div=5,5 counts->1 part; ml=2,div=5,5 counts->2 parts;
//      ml=10,div=1,1 count->10 parts.
TEST_CASE("Count divider and multiplier: multiplicative model (ml always applies)") {
    uint32_t rem = 0;
    // ml=1, div=5: 5 pulses -> 1 part, no remainder.
    CHECK(proxApplyDivider(5, 1, 5, &rem) == 1);
    CHECK(rem == 0);
    // ml=2, div=5: 5 pulses -> 2 parts.
    CHECK(proxApplyDivider(5, 2, 5, &rem) == 2);
    CHECK(rem == 0);
    // 12 pulses with ml=2,div=5: 12*2/5 = 4 parts (24/5), remainder 4 carried.
    rem = 0;
    CHECK(proxApplyDivider(12, 2, 5, &rem) == 4);
    CHECK(rem == 4);
    // Remainder carries across ticks: +3 more pulses -> (3*2+4)=10/5 = 2 parts.
    CHECK(proxApplyDivider(3, 2, 5, &rem) == 2);
    CHECK(rem == 0);
    // ml=10, div=1: 1 pulse -> 10 parts (direct multiplier mode).
    rem = 0;
    CHECK(proxApplyDivider(1, 10, 1, &rem) == 10);
    CHECK(rem == 0);
    // ml=10, div=5: 5 pulses -> 5*10/5 = 10 parts (Nuno git example).
    rem = 0;
    CHECK(proxApplyDivider(5, 10, 5, &rem) == 10);
    CHECK(rem == 0);
    // Multiplier alone still works (legacy behavior preserved).
    rem = 0;
    CHECK(proxApplyDivider(4, 10, 1, &rem) == 40);
    CHECK(rem == 0);
    rem = 0;
    CHECK(proxApplyDivider(1, 3, 1, &rem) == 3);
    // Legacy sanitization: ml/div < 1 behave as 1.
    CHECK(proxApplyDivider(5, 0, 0, &rem) == 5);
}

TEST_CASE("OEE window: 2050ms gaps inside 4000ms grace count running (live .29)") {
    // 3 consumed seconds, zero edges, last edge 2050ms ago, grace 4000ms.
    uint32_t msAccum = 3000, window = 0, running = 0, downtime = 0;
    proximityOeeWindowConsume(msAccum, window, true, running, downtime,
                              /*lastEdgeMs=*/100000, /*nowMs=*/102050, /*graceMs=*/4000);
    CHECK(running  == 3);
    CHECK(downtime == 0);
}

// Mirror of production _detectCurrentShift (proximity_manager.cpp) — picks the
// slot with the largest start <= current time; before the first slot -> nSlots-1.
static int8_t detectShift(const ProximityShiftConfig& sc, int curHour, int curMin) {
    if (!sc.enabled || sc.nSlots == 0) return -1;
    int cur = curHour * 60 + curMin;
    int best = -1, bestMin = -1;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        int sm = sc.slots[i].hour * 60 + sc.slots[i].minute;
        if (sm <= cur && sm > bestMin) { bestMin = sm; best = i; }
    }
    if (best == -1) best = sc.nSlots - 1; // before first slot -> night shift
    return (int8_t)best;
}

TEST_CASE("sort: unsorted 22,6 fires the boundary at 00:00 and NOT at 06:00 (bug) — sorting fixes it") {
    // Reproduces the Waveshare .29 report: night shift 22h->6h, slots entered
    // in the web UI as 22h then 6h and persisted unsorted {22,6}.
    ProximityShiftConfig unsorted = {};
    unsorted.enabled = true; unsorted.nSlots = 2;
    unsorted.slots[0] = {22, 0}; unsorted.slots[1] = {6, 0};

    int8_t at23h   = detectShift(unsorted, 23, 0);   // slot 0 (22h)
    int8_t at00h   = detectShift(unsorted, 0, 0);    // before first slot -> slot 1 (6h)
    int8_t at06h   = detectShift(unsorted, 6, 0);    // slot 1 (6h) — SAME as at00h
    CHECK(at23h == 0);
    CHECK(at00h == 1);
    CHECK(at06h == 1);                              // NO boundary at 06:00 -> no 6h email
    CHECK(at00h != at23h);                          // spurious boundary at 00:00 (wrong email)

    proximitySortShiftSlots(unsorted);              // production enforces order on load/save
    CHECK(unsorted.slots[0].hour == 6);
    CHECK(unsorted.slots[1].hour == 22);

    int8_t s23h = detectShift(unsorted, 23, 0);     // before first slot (6h) -> slot 1 (22h)
    int8_t s00h = detectShift(unsorted, 0, 0);      // still slot 1 (22h night shift)
    int8_t s06h = detectShift(unsorted, 6, 0);      // slot 0 (6h) -> boundary fires here
    CHECK(s23h == 1);
    CHECK(s00h == 1);
    CHECK(s06h == 0);
    CHECK(s06h != s00h);                            // the ONLY boundary is at 06:00
}

// ── Shift-start wall clock (reboot-immune shift elapsed, fix 2026-10-14) ──
// proxShiftStartWallFor: most recent slot start at/before `now` (before the
// first daily slot → yesterday's last slot). Built via mktime so the tests are
// TZ-independent, same pattern as test_proximity_day_aligned.cpp.
static time_t t23WallClock(int year, int mon, int day, int hour, int min, int sec) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    t.tm_isdst = -1;
    return mktime(&t);
}

TEST_CASE("proxShiftStartWallFor: mid-shift resolves to today's slot start") {
    ProximityShiftSlot slots[3] = {{6, 0}, {14, 0}, {22, 0}};
    time_t now = t23WallClock(2026, 9, 9, 23, 10, 0);
    time_t start = proxShiftStartWallFor(3, slots, now);
    CHECK(start == t23WallClock(2026, 9, 9, 22, 0, 0));
    CHECK((now - start) == 4200);                   // 70 min elapsed
}

TEST_CASE("proxShiftStartWallFor: overnight shift started yesterday") {
    ProximityShiftSlot slots[3] = {{6, 0}, {14, 0}, {22, 0}};
    time_t now = t23WallClock(2026, 9, 10, 3, 0, 0);
    time_t start = proxShiftStartWallFor(3, slots, now);
    CHECK(start == t23WallClock(2026, 9, 9, 22, 0, 0));
    CHECK((now - start) == 18000);                  // 5 h elapsed
}

TEST_CASE("proxShiftStartWallFor: exactly at slot start is zero elapsed") {
    ProximityShiftSlot slots[3] = {{6, 0}, {14, 0}, {22, 0}};
    time_t now = t23WallClock(2026, 9, 9, 14, 0, 0);
    CHECK(proxShiftStartWallFor(3, slots, now) == now);
}

TEST_CASE("proxShiftStartWallFor: single slot covers the whole day") {
    ProximityShiftSlot slots[1] = {{22, 0}};
    time_t now = t23WallClock(2026, 9, 9, 10, 0, 0);
    CHECK(proxShiftStartWallFor(1, slots, now) == t23WallClock(2026, 9, 8, 22, 0, 0));
}

TEST_CASE("proxShiftStartWallFor: empty config returns 0") {
    ProximityShiftSlot slots[1] = {{6, 0}};
    CHECK(proxShiftStartWallFor(0, slots, t23WallClock(2026, 9, 9, 12, 0, 0)) == 0);
    CHECK(proxShiftStartWallFor(3, nullptr, t23WallClock(2026, 9, 9, 12, 0, 0)) == 0);
    CHECK(proxShiftStartWallFor(3, slots, 0) == 0);
}
