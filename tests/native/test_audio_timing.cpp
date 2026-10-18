#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdint>

// Standalone reimplementation of the I2S clock accuracy math used by
// UDA1334ADAC::playTimingTest() (audio_dac_uda1334a.cpp). The test writes
// exactly `seconds` of stereo 16-bit PCM through the same i2s_write path the
// decoders use and measures the real wall-clock drain time. A fast/slow I2S
// clock (sample-rate mismatch) drains it in less/more than `seconds`, and the
// ratio quantifies the error. These checks validate the math that converts the
// measured wall time back into the effective sample rate.

namespace {
struct TimingResult {
    double expectedSec;   // seconds of PCM requested (= framesTotal / rate)
    double ratio;         // expectedSec / actualSec, >1 = clock runs fast
    double effectiveRate; // actual drain rate implied by the wall time
};

TimingResult analyzeTiming(uint32_t seconds, uint32_t rate, double actualSec) {
    const uint64_t framesTotal = (uint64_t)seconds * rate;
    const double expectedSec = (double)framesTotal / (double)rate;
    const double ratio = (expectedSec > 0.0 && actualSec > 0.0)
                             ? expectedSec / actualSec
                             : 1.0;
    const double effectiveRate = (actualSec > 0.0) ? (double)framesTotal / actualSec : 0.0;
    return {expectedSec, ratio, effectiveRate};
}

// Mirrors Minimp3Decoder::loop() EOF report: total decoded samples (per
// channel) vs the wall-clock playback time. expectedSec = samples / rate.
TimingResult decodeStats(uint64_t totalSamples, uint32_t rate, double wallSec) {
    const double expectedSec = (rate > 0) ? (double)totalSamples / (double)rate : 0.0;
    const double ratio = (expectedSec > 0.0 && wallSec > 0.0)
                             ? expectedSec / wallSec
                             : 1.0;
    return {expectedSec, ratio, wallSec > 0.0 ? (double)totalSamples / wallSec : 0.0};
}
} // namespace

TEST_CASE("timing: correct clock drains in exactly the requested seconds") {
    // 60 s at 44.1 kHz played back at the true 44.1 kHz rate.
    TimingResult r = analyzeTiming(60, 44100, 60.0);
    CHECK(r.expectedSec == doctest::Approx(60.0).epsilon(1e-9));
    CHECK(r.ratio == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(r.effectiveRate == doctest::Approx(44100.0).epsilon(1e-6));
}

TEST_CASE("timing: clock ~16% fast drains in proportionally less wall time") {
    // A ~1.16x clock plays 60 s of PCM in ~51.7 s; ratio must be ~1.16 and the
    // implied rate ~51200 Hz. This is the exact bug class reported by the user
    // (4:54 track playing in 4:14).
    TimingResult r = analyzeTiming(60, 44100, 60.0 / 1.16);
    CHECK(r.expectedSec == doctest::Approx(60.0).epsilon(1e-9));
    CHECK(r.ratio == doctest::Approx(1.16).epsilon(1e-6));
    CHECK(r.effectiveRate == doctest::Approx(44100.0 * 1.16).epsilon(1e-6));
}

TEST_CASE("timing: different duration/rate combos give consistent ratios") {
    // 30 s at 48 kHz with a 1.5x clock.
    TimingResult r = analyzeTiming(30, 48000, 30.0 / 1.5);
    CHECK(r.expectedSec == doctest::Approx(30.0).epsilon(1e-9));
    CHECK(r.ratio == doctest::Approx(1.5).epsilon(1e-6));
    CHECK(r.effectiveRate == doctest::Approx(72000.0).epsilon(1e-6));

    // 120 s at 22.05 kHz, clock 0.98x (slow) -> wall time longer than requested.
    TimingResult slow = analyzeTiming(120, 22050, 120.0 / 0.98);
    CHECK(slow.ratio == doctest::Approx(0.98).epsilon(1e-6));
    CHECK(slow.effectiveRate == doctest::Approx(22050.0 * 0.98).epsilon(1e-6));
}

TEST_CASE("timing: zero/negative wall time is guarded") {
    TimingResult r = analyzeTiming(60, 44100, 0.0);
    CHECK(r.ratio == doctest::Approx(1.0).epsilon(1e-9)); // fallback, no div-by-zero
    CHECK(r.effectiveRate == doctest::Approx(0.0).epsilon(1e-9));
}

// Mirrors Minimp3Decoder::loop() EOF report: total decoded samples (per channel)
// vs the wall-clock playback time. Uses the real teste.mp3 numbers.
TEST_CASE("decoder EOF report: correct playback -> ratio ~1.0") {
    const uint64_t totalSamples = 12993408ULL; // teste.mp3, 44100 Hz
    TimingResult r = decodeStats(totalSamples, 44100, 294.6);
    CHECK(r.expectedSec == doctest::Approx(294.635).epsilon(1e-6));
    CHECK(r.ratio == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("decoder EOF report: accelerated playback -> ratio ~1.16") {
    // If the radio really plays 4:54 in 4:14 (254 s wall), the ratio reports it.
    const uint64_t totalSamples = 12993408ULL;
    TimingResult r = decodeStats(totalSamples, 44100, 254.0);
    CHECK(r.expectedSec == doctest::Approx(294.635).epsilon(1e-6));
    CHECK(r.ratio == doctest::Approx(294.635 / 254.0).epsilon(1e-6));
    CHECK(r.ratio > 1.10); // clearly accelerated
}