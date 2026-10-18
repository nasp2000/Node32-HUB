#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstring>

// Standalone reimpl of proximity debounce logic.
// Tests debounce filtering, burst detection, and basic counting.

struct TestChannel {
    char name[32];
    uint8_t gpio_in;
    uint8_t gpio_out;
    uint8_t gpio_in2;
    uint8_t mode;
    bool monitor_only;
    uint32_t debounce_ms;
    uint32_t count_in;
    uint32_t count_out;
    uint32_t total_in;
    uint32_t total_out;
    uint32_t goal;
    uint32_t last_pulse_ms;
    uint32_t cycle_time_ms;
    float rate_ppm;
    bool state;
    uint32_t pulse_delay_ms;
    uint32_t pulse_dur_ms;
    bool do_active;
    uint32_t do_start_ms;
    uint8_t direction;
    uint32_t dir_edge_ms_in;
};

static TestChannel _ch;

static const uint32_t PROX_RATE_UPDATE_MS = 5000UL;
static const uint32_t PROX_RATE_IDLE_MS = 60000UL;
static const uint32_t PROX_ISR_BURST_THRESHOLD = 1000UL;

struct TestContext {
    uint32_t now;
    uint32_t _lastEdgeMs;
    uint32_t _rawCount;
    uint32_t _drainedCount;
    uint32_t _rateBaseCount;
    uint32_t _rateBaseMs;
    bool burstLogged;
    TestChannel* ch;

    TestContext() : now(0), _lastEdgeMs(0), _rawCount(0), _drainedCount(0),
        _rateBaseCount(0), _rateBaseMs(0), burstLogged(false), ch(&_ch) {}
};

static bool onEdge(TestContext& ctx, uint32_t edgeNow) {
    if (edgeNow - ctx._lastEdgeMs >= ctx.ch->debounce_ms) {
        ctx._rawCount++;
        ctx._lastEdgeMs = edgeNow;
        return true;
    }
    return false;
}

static void processTick(TestContext& ctx, uint32_t tickCount) {
    uint32_t raw = ctx._rawCount;
    uint32_t delta = raw - ctx._drainedCount;
    if (delta > 0) {
        ctx._drainedCount = raw;
        if (delta > PROX_ISR_BURST_THRESHOLD && ctx.ch->gpio_in != 255) {
            ctx.burstLogged = true;
            ctx.ch->gpio_in = 255; // disabled
        }
        if (ctx.ch->mode != 1) {
            ctx.ch->count_in += delta;
            ctx.ch->total_in += delta;
        }
        ctx.ch->last_pulse_ms = tickCount;
    }
    // Rate update
    uint32_t elapsed = tickCount - ctx._rateBaseMs;
    if (elapsed >= PROX_RATE_UPDATE_MS) {
        uint32_t newCounts = ctx.ch->count_in - ctx._rateBaseCount;
        if (newCounts > 0 && elapsed > 0) {
            ctx.ch->rate_ppm = (float)newCounts * 60000.0f / (float)elapsed;
        } else if (elapsed >= PROX_RATE_IDLE_MS) {
            ctx.ch->rate_ppm = 0.0f;
        }
        ctx._rateBaseCount = ctx.ch->count_in;
        ctx._rateBaseMs = tickCount;
    }
}

TEST_CASE("Debounce accepts first edge immediately") {
    TestChannel ch = {};
    ch.debounce_ms = 50;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;
    ctx.now = 1000;

    bool accepted = onEdge(ctx, 1000);
    CHECK(accepted == true);
    CHECK(ctx._rawCount == 1);
}

TEST_CASE("Debounce rejects edge within debounce window") {
    TestChannel ch = {};
    ch.debounce_ms = 50;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;

    onEdge(ctx, 1000);
    bool accepted = onEdge(ctx, 1020); // 20ms < 50ms
    CHECK(accepted == false);
    CHECK(ctx._rawCount == 1);
}

TEST_CASE("Debounce accepts edge after debounce window") {
    TestChannel ch = {};
    ch.debounce_ms = 50;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;

    onEdge(ctx, 1000);
    bool accepted = onEdge(ctx, 1100); // 100ms > 50ms
    CHECK(accepted == true);
    CHECK(ctx._rawCount == 2);
}

TEST_CASE("Debounce zero ms = accept all edges") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;

    onEdge(ctx, 1000);
    onEdge(ctx, 1001);
    onEdge(ctx, 1002);
    CHECK(ctx._rawCount == 3);
}

TEST_CASE("Burst detection triggers on >1000 edges per tick") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.gpio_in = 5; // valid GPIO
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;
    ctx.now = 1000;

    // Simulate 1500 edges in one tick
    ctx._rawCount = 1500;
    ctx._drainedCount = 0;
    processTick(ctx, 1000);
    CHECK(ctx.burstLogged == true);
    CHECK(ch.gpio_in == 255); // ISR disabled
}

TEST_CASE("Burst does not trigger on normal delta") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.gpio_in = 5;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;

    ctx._rawCount = 500;
    ctx._drainedCount = 0;
    processTick(ctx, 1000);
    CHECK(ctx.burstLogged == false);
    CHECK(ch.gpio_in == 5); // still enabled
}

TEST_CASE("Rate PPM calculated correctly from deltas") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;
    ctx._rateBaseMs = 0;

    // 10 edges accumulated over 5000ms = rate_ppm = 10 * 60000 / 5000 = 120
    ctx._rawCount = 10;
    ctx._drainedCount = 0;
    processTick(ctx, PROX_RATE_UPDATE_MS);
    CHECK(ch.count_in == 10);
    CHECK(ch.rate_ppm == doctest::Approx(120.0f));

    // Another 10 edges over next 5000ms (delta 10)
    ctx._rawCount = 20;
    processTick(ctx, PROX_RATE_UPDATE_MS * 2);
    CHECK(ch.count_in == 20);
    CHECK(ch.rate_ppm == doctest::Approx(120.0f));
}

TEST_CASE("Rate zeroes after idle window") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;
    ctx._rateBaseMs = 0;

    // Add 50 edges, let them be counted
    ctx._rawCount = 50;
    processTick(ctx, PROX_RATE_UPDATE_MS);
    CHECK(ch.rate_ppm > 0);

    // No new edges after idle window
    ctx._rateBaseCount = ch.count_in; // simulate base reset like production
    processTick(ctx, PROX_RATE_IDLE_MS + PROX_RATE_UPDATE_MS);
    CHECK(ch.rate_ppm == doctest::Approx(0.0f));
}

TEST_CASE("Count_in accumulated correctly with debounce") {
    TestChannel ch = {};
    ch.debounce_ms = 10;
    ch.mode = 0;
    TestContext ctx;
    ctx.ch = &ch;
    uint32_t t = 0;

    onEdge(ctx, t += 100); // accept
    processTick(ctx, t);
    CHECK(ch.count_in == 1);

    onEdge(ctx, t += 5);   // reject (<10ms)
    processTick(ctx, t);
    CHECK(ch.count_in == 1);

    onEdge(ctx, t += 10);  // accept
    processTick(ctx, t);
    CHECK(ch.count_in == 2);
}

TEST_CASE("Count_in and total_in follow mode") {
    TestChannel ch = {};
    ch.debounce_ms = 0;
    ch.mode = 1; // PRESENCE
    TestContext ctx;
    ctx.ch = &ch;
    uint32_t t = 0;

    onEdge(ctx, t += 100);
    processTick(ctx, t);
    CHECK(ch.count_in == 0);  // PRESENCE mode: no count
    CHECK(ch.total_in == 0); // total_in also skipped in PRESENCE

    ch.mode = 0; // COUNTER
    onEdge(ctx, t += 100);
    processTick(ctx, t);
    CHECK(ch.count_in == 1);
    CHECK(ch.total_in == 1);
}

// ── Expanded multiplier coverage (production logic) ─────────────────────
// Production: mul < 1 → 1 (legacy negatives/0 coerced); range clamped [1,9999].

static int32_t clampMultiplier(int32_t m) {
    if (m < 1) return 1;
    if (m > 9999) return 9999;
    return m;
}

// Applies a multiplier to a delta, mirroring production count update.
static void applyMulDelta(uint32_t& count, uint32_t& total, int32_t mul, uint32_t delta) {
    uint32_t d = delta * (uint32_t)clampMultiplier(mul);
    count += d;
    total += d;
}

TEST_CASE("Multiplier 2 doubles count") {
    uint32_t c = 0, t = 0;
    applyMulDelta(c, t, 2, 10);
    CHECK(c == 20);
    CHECK(t == 20);
}

TEST_CASE("Multiplier 1 is identity") {
    uint32_t c = 5, t = 5;
    applyMulDelta(c, t, 1, 3);
    CHECK(c == 8);
    CHECK(t == 8);
}

TEST_CASE("Multiplier 0 coerced to 1") {
    uint32_t c = 0, t = 0;
    applyMulDelta(c, t, 0, 7);   // 0 → 1
    CHECK(c == 7);
    CHECK(t == 7);
}

TEST_CASE("Multiplier negative coerced to 1 (legacy configs)") {
    CHECK(clampMultiplier(-1) == 1);
    CHECK(clampMultiplier(-20) == 1);
    CHECK(clampMultiplier(-50) == 1);
    uint32_t c = 100, t = 100;
    applyMulDelta(c, t, -2, 10);  // legacy -2 → 1 → +10
    CHECK(c == 110);
    CHECK(t == 110);
}

TEST_CASE("Multiplier clamps above 9999") {
    CHECK(clampMultiplier(50000) == 9999);
    CHECK(clampMultiplier(10000) == 9999);
}

TEST_CASE("Multiplier manual values 200x and 4000x accepted") {
    CHECK(clampMultiplier(200) == 200);
    CHECK(clampMultiplier(4000) == 4000);
    uint32_t c = 0, t = 0;
    applyMulDelta(c, t, 4000, 3);  // 3 pulses x 4000 = 12000 parts
    CHECK(c == 12000);
    CHECK(t == 12000);
}

TEST_CASE("Multiplier range edges accepted") {
    CHECK(clampMultiplier(1) == 1);
    CHECK(clampMultiplier(20) == 20);
    CHECK(clampMultiplier(9999) == 9999);
}
