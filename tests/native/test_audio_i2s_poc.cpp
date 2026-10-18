#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>

// Compiles the production pure-PCM helper header directly (extract-header
// strategy). Covers the known PCM generation used by the Phase 1 ESP_I2S PoC
// (audio_i2s_poc.cpp): byte accounting and stereo sine fill.
#include "audio_i2s_poc.h"

TEST_CASE("i2sPocBytesForFrames: stereo 16-bit accounting") {
    // 1 s of 44.1 kHz stereo 16-bit = 44100 * 2 * 2 bytes.
    CHECK(i2sPocBytesForFrames(44100, 2, 16) == 44100 * 4);
    // 0.5 s at 48 kHz stereo 16-bit.
    CHECK(i2sPocBytesForFrames(24000, 2, 16) == 24000 * 4);
    // 1 frame of mono 16-bit = 2 bytes.
    CHECK(i2sPocBytesForFrames(1, 1, 16) == 2);
    // Zero frames -> zero bytes regardless of channels.
    CHECK(i2sPocBytesForFrames(0, 2, 16) == 0);
}

TEST_CASE("i2sPocFillTone: returns exact byte count and writes expected bytes") {
    I2sPocTone tone;
    tone.sampleRate = 44100;
    tone.channels = 2;
    tone.bitDepth = 16;
    tone.freqL = 440.0f;
    tone.freqR = 880.0f;
    tone.amplitude = 0.5f;

    int16_t buf[512];
    uint32_t bytes = i2sPocFillTone(buf, 100, tone);
    CHECK(bytes == 100 * 4);

    // Peak amplitude must stay within the int16 range.
    int32_t maxAbs = 0;
    for (size_t i = 0; i < 200; i++) {
        int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
        if (v > maxAbs) maxAbs = v;
    }
    CHECK(maxAbs <= (int32_t)(0.5f * 32767.0f + 1));

    // 2 s at 48 kHz stereo = 96000 frames.
    tone.sampleRate = 48000;
    uint32_t bytes2 = i2sPocFillTone(buf, 100, tone);
    CHECK(bytes2 == 400);
}

TEST_CASE("i2sPocFillTone: left/right channels are independent tones") {
    I2sPocTone tone;
    tone.sampleRate = 8000;   // slow rate so the phase advances visibly
    tone.channels = 2;
    tone.bitDepth = 16;
    tone.freqL = 250.0f;      // period = 32 samples, quarter = frame 8
    tone.freqR = 500.0f;      // period = 16 samples, quarter = frame 4
    tone.amplitude = 1.0f;

    int16_t buf[4];           // 2 frames
    i2sPocFillTone(buf, 2, tone);

    // Frame 0: sin(0) = 0 on both channels.
    CHECK(buf[0] == 0);
    CHECK(buf[1] == 0);

    int16_t buf2[32];         // 16 frames
    i2sPocFillTone(buf2, 16, tone);
    // Left at frame 8 (quarter period of 250 Hz): near +32767.
    CHECK(buf2[16] > 32000);
    // Right at frame 4 (quarter period of 500 Hz): near +32767.
    CHECK(buf2[9] > 32000);
    // At frame 4 the left (period 32) is at sin(pi/4) ~ 0.707, right is at peak.
    CHECK(buf2[8] < buf2[9]);
    // Frame 8: left at peak, right at full period (sin(2*pi) ~ 0).
    CHECK(buf2[17] < 1000);
}

TEST_CASE("i2sPocFillTone: rejects invalid configurations") {
    I2sPocTone tone;
    tone.sampleRate = 44100;
    tone.channels = 1;         // mono not supported by this helper
    tone.bitDepth = 16;
    tone.freqL = 440.0f;
    tone.freqR = 880.0f;
    tone.amplitude = 0.5f;

    int16_t buf[32];
    CHECK(i2sPocFillTone(buf, 8, tone) == 0);   // mono -> rejected

    tone.channels = 2;
    tone.bitDepth = 24;                          // 24-bit not supported
    CHECK(i2sPocFillTone(buf, 8, tone) == 0);

    CHECK(i2sPocFillTone(nullptr, 8, tone) == 0); // null buffer
    CHECK(i2sPocFillTone(buf, 0, tone) == 0);     // zero frames
}
