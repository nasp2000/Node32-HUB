#pragma once

#include <stdint.h>
#include <math.h>

// ============================================================================
// Phase 1 PoC: known PCM -> ESP_I2S -> UDA1334A
//
// Pure helpers (no Arduino/ESP-IDF dependencies) so the PCM generation logic
// is directly native-testable in tests/native/test_audio_i2s_poc.cpp.
// The ESP_I2S output itself lives in audio_i2s_poc.cpp and is triggered on
// demand through POST /api/radio/i2s-poc.
// ============================================================================

struct I2sPocTone {
    uint32_t sampleRate;   // Hz (e.g. 44100)
    uint16_t channels;     // 2 (stereo, interleaved L/R)
    uint8_t  bitDepth;     // 16
    float    freqL;        // Hz, left channel
    float    freqR;        // Hz, right channel
    float    amplitude;    // 0..1 (fraction of full int16 range)
};

// Bytes needed to hold `frames` of interleaved PCM for the given bit depth.
inline uint32_t i2sPocBytesForFrames(uint32_t frames, uint16_t channels, uint8_t bitDepth) {
    return frames * channels * (bitDepth / 8);
}

// Fills `out` with `frames` of interleaved stereo int16 PCM (L/R sine tones).
// Returns the number of bytes written (frames * channels * bitDepth/8), or 0
// when the tone is not stereo/16-bit or the buffer is null.
inline uint32_t i2sPocFillTone(int16_t* out, uint32_t frames, const I2sPocTone& tone) {
    if (!out || frames == 0) return 0;
    if (tone.channels != 2 || tone.bitDepth != 16 || tone.sampleRate == 0) return 0;
    const float amp = tone.amplitude * 32767.0f;
    const float wL = 2.0f * 3.14159265358979f * tone.freqL / (float)tone.sampleRate;
    const float wR = 2.0f * 3.14159265358979f * tone.freqR / (float)tone.sampleRate;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < frames; i++) {
        const float tl = wL * (float)i;
        const float tr = wR * (float)i;
        out[idx++] = (int16_t)(amp * sinf(tl));
        out[idx++] = (int16_t)(amp * sinf(tr));
    }
    return idx * sizeof(int16_t);
}

// Runs the ESP_I2S proof-of-concept on the UDA1334A pins (blocking).
// Call from a dedicated task, never from the async web handler task.
void audioI2sPocRun();
