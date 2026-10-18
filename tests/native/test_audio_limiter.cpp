#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdint>

// Standalone reimplementation of the soft-knee limiter used by the radio EQ
// pipeline (EqState::softLimit in radio_audio_runtime.cpp). Replaces the old
// hard clip at ±32768 that produced audible crackle/rasping when the peaking
// EQ (Rock default +6 dB lows) pushed hot-mastered local MP3 peaks past full
// scale.

static float softLimit(float x) {
    const float kKnee = 29491.0f;   // 0.9 * 32767
    const float kMax = 32767.0f;
    const float ax = (x < 0.0f) ? -x : x;
    if (ax <= kKnee) return x;
    float over = ax - kKnee;
    float pct = over / (kMax - kKnee);
    float compressed = kKnee + over / (1.0f + pct);
    return (x < 0.0f) ? -compressed : compressed;
}

TEST_CASE("softLimit: linear below the knee") {
    // In the linear region the sample passes through unchanged.
    CHECK(softLimit(0.0f) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(softLimit(1000.0f) == doctest::Approx(1000.0f).epsilon(1e-4));
    CHECK(softLimit(-5000.0f) == doctest::Approx(-5000.0f).epsilon(1e-4));
    CHECK(softLimit(29491.0f) == doctest::Approx(29491.0f).epsilon(1e-3));
}

TEST_CASE("softLimit: compresses above the knee without hard-clipping") {
    // A huge input (3x full scale) must come out well below full scale and
    // still positive — no hard clip, no wrap-around.
    float out = softLimit(3.0f * 32767.0f);
    CHECK(out > 0.0f);
    CHECK(out <= 32767.0f);
    CHECK(out > 31000.0f);   // heavily compressed but audible
}

TEST_CASE("softLimit: never exceeds full scale, monotonic, symmetric") {
    float prev = 0.0f;
    for (float x = -40000.0f; x <= 40000.0f; x += 1000.0f) {
        float y = softLimit(x);
        CHECK(std::abs(y) <= 32767.0f + 1.0f);
        if (x >= 0.0f) {
            CHECK(y >= prev);       // monotonic non-decreasing on the + side
            prev = y;
        }
        CHECK(softLimit(x) == doctest::Approx(-softLimit(-x)).epsilon(1e-3)); // odd symmetry
    }
}

TEST_CASE("softLimit: continuous at the knee (no discontinuity click)") {
    // The curve is C1 at the knee: value and derivative match on both sides.
    float below = softLimit(29491.0f);
    float above = softLimit(29491.0f + 0.5f);
    CHECK(above > below);
    CHECK(above - below < 1.0f);    // no jump — a jump would click
}

TEST_CASE("softLimit: typical hot-master peaks stay clean") {
    // A mastered track peaking at 0.95 full scale, EQ-boosted +6 dB on the low
    // band, reaches ~1.6x full scale after the biquads. Before this limiter the
    // old code hard-clipped (flat tops = rasping); now it is compressed.
    float boosted = 0.95f * 32767.0f * 1.6f;
    float out = softLimit(boosted);
    CHECK(out <= 32767.0f);
    CHECK(out > 31000.0f);          // loud but not clipped
    CHECK(out > 0.9f * 32767.0f);   // still near-full-scale volume
}
