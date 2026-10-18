#pragma once

#include "feature_flags.h"

#if ENABLE_UDA1334A == 1

#include <Arduino.h>
#include <driver/i2s.h>

// UDA1334A DAC over I2S — passive DAC for audio playback (radio, music, alerts)
// Supports 16–192 kHz, 16/24/32-bit audio via I2S bus.

// Default I2S pin assignments for original N16R8 devkit
// Override in board_pins.h or user_config.h if needed.
#ifndef UDA1334A_I2S_BCLK_PIN
  #define UDA1334A_I2S_BCLK_PIN   26   // I2S bit clock (BCLK)
#endif
#ifndef UDA1334A_I2S_LRCK_PIN
  #define UDA1334A_I2S_LRCK_PIN   25   // I2S word select / LR clock (LRC)
#endif
#ifndef UDA1334A_I2S_DOUT_PIN
  #define UDA1334A_I2S_DOUT_PIN   27   // I2S serial data out (DIN to DAC)
#endif
#ifndef UDA1334A_I2S_MCLK_PIN
  #define UDA1334A_I2S_MCLK_PIN   -1   // Master clock (optional, disable = -1)
#endif

// MUTE control pin (UDA1334A pin 8, SSOP16). HIGH = soft-mute on, LOW = off.
// Optional: set to a free GPIO to silence the DAC output digitally when idle
// (kills the idle hum). Leave at -1 (or wire MUTE to GND) to disable.
#ifndef UDA1334A_MUTE_PIN
  #define UDA1334A_MUTE_PIN       -1
#endif

// I2S port (0 or 1 on ESP32-S3)
#ifndef UDA1334A_I2S_PORT
  #define UDA1334A_I2S_PORT  I2S_NUM_0
#endif

// Low-heap DMA defaults for I2S TX.
// Override in board_pins.h or user_config.h for higher throughput if needed.
#ifndef UDA1334A_I2S_DMA_BUF_COUNT
  #define UDA1334A_I2S_DMA_BUF_COUNT  4
#endif
#ifndef UDA1334A_I2S_DMA_BUF_LEN
  #define UDA1334A_I2S_DMA_BUF_LEN    256
#endif

// ==================================================================================
// Audio format configuration
// ==================================================================================
struct AudioFormat {
    uint32_t sampleRate;   // 16000, 22050, 44100, 48000, 96000, 192000 Hz
    int bitDepth;          // 16, 24, 32 bits
    int channels;          // 1 (mono) or 2 (stereo)
    
    AudioFormat(uint32_t sr = 44100, int bd = 16, int ch = 2)
        : sampleRate(sr), bitDepth(bd), channels(ch) {}
};

// ==================================================================================
// UDA1334A DAC Driver
// ==================================================================================
class UDA1334ADAC {
public:
    UDA1334ADAC();

    // Initialize I2S peripheral and configure for the DAC
    bool begin(const AudioFormat& fmt = AudioFormat());

    // Reconfigure audio format (sample rate, bit depth)
    bool setAudioFormat(const AudioFormat& fmt);

    // Write audio data to the DAC (blocking)
    // Returns number of bytes actually written
    size_t write(const uint8_t* buffer, size_t len);

    // Stop audio playback and deinitialize I2S
    void end();

    // Play a short test melody (several ascending tones) to verify the DAC,
    // left/right channels and the wiring. Blocking for ~2 seconds.
    void playTestSound();

    // I2S clock accuracy test: generate exactly `seconds` of PCM at the current
    // sample rate, write it through the same i2s_write path the decoders use,
    // and report the real wall-clock time it took to drain. If the I2S clock
    // runs fast/slow vs the requested rate, the actual elapsed time differs
    // from `seconds` and the ratio quantifies the error.
    void playTimingTest(uint32_t seconds);

    // Digital soft-mute via the optional UDA1334A_MUTE_PIN (no-op if pin is -1).
    // mute=true -> DAC outputs silenced; mute=false -> normal playback.
    void setMuted(bool mute);

    // Returns true if the DAC is initialized and ready
    bool isInitialized() const { return _initialized; }

    // Get current audio format
    AudioFormat getAudioFormat() const { return _format; }

private:
    bool _initialized;
    AudioFormat _format;
    i2s_pin_config_t _pinCfg;   // last applied pin mapping, reused by timing test
};

extern UDA1334ADAC udaDac;

#endif // ENABLE_UDA1334A == 1

