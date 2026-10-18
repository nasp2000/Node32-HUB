#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class BuzzerTone : uint8_t {
    ShortBeep = 0,
    LongBeep = 1,
    DoubleBeep = 2,
    Error = 3,
    Success = 4,
    Alarm = 5,
    Custom = 6
};

struct BuzzerConfig {
    uint8_t pin = 0;
    uint8_t channel = 0;
    bool activeHigh = true;
    uint16_t defaultFrequency = 2000;
    uint8_t defaultVolume = 128;
};

class IBuzzer {
public:
    virtual ~IBuzzer() = default;

    virtual bool init(const BuzzerConfig& config) = 0;
    virtual void loop() = 0;

    virtual void beep(uint16_t frequency = 2000, uint16_t durationMs = 200) = 0;
    virtual void playTone(BuzzerTone tone) = 0;
    virtual void playMelody(const uint16_t* frequencies, const uint16_t* durations, size_t count) = 0;

    virtual void on() = 0;
    virtual void off() = 0;
    virtual void setFrequency(uint16_t freq) = 0;
    virtual void setVolume(uint8_t volume) = 0;

    virtual bool isPlaying() const = 0;
    virtual bool isReady() const = 0;

    virtual void mute(bool m) = 0;
    virtual bool isMuted() const = 0;
};
