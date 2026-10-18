// buzzer.h
// Non-blocking active-buzzer driver.
// Active only when ENABLE_BUZZER == 1 (set in user_config.h).
// Uses a single GPIO driven HIGH/LOW — no PWM needed for active buzzers.
// Patterns are played step-by-step from loop() — zero blocking.
//
// Config persisted in LittleFS at /config/buzzer.json.
// Call buzzerBegin() from setup(), buzzerLoop() every loop() iteration.

#pragma once
#include "feature_flags.h"

#if ENABLE_BUZZER == 1

#include <Arduino.h>

// Available patterns.
enum BuzzerPattern : uint8_t {
    BEEP_NONE       = 0,
    BEEP_OK         = 1,  // 1 short bip (100 ms)
    BEEP_WARN       = 2,  // 2 short bips (100 ms each, 100 ms gap)
    BEEP_ERROR      = 3,  // 3 fast bips (80 ms each, 80 ms gap)
    BEEP_ALARM      = 4,  // Continuous bips (150 ms on, 100 ms off) — until buzzerSilence()
    BEEP_LONG       = 5,  // 1 long bip (500 ms)
    BEEP_DOUBLE_LONG = 6, // 2 long bips (500 ms each, 200 ms gap)
};

// Config loaded from / saved to /config/buzzer.json.
struct BuzzerConfig {
    bool enabled        = true;
    uint8_t pin         = 46;   // Default GPIO — Waveshare onboard buzzer. Override via BUZZER_PIN.
    bool on_boot        = true; // Beep on successful boot
    bool on_save        = true; // Beep when config is saved via web UI
    bool on_alarm       = true; // Beep on proximity alarm active
    bool on_sensor_alert = false; // Beep when sensor threshold exceeded
    bool on_network     = false; // Beep on network connect / disconnect
    bool on_error       = true; // Beep on critical error (SD, I2C, etc.)
};

// Initialize GPIO and load config from LittleFS.
void buzzerBegin();

// Advance the current pattern sequence — call every loop() iteration.
void buzzerLoop();

// Enqueue a pattern to play. If BEEP_ALARM, plays continuously until buzzerSilence().
void buzzerBeep(BuzzerPattern p);

// Stop any active pattern immediately and set pin LOW.
void buzzerSilence();

// Returns true if a pattern is currently playing.
bool buzzerIsBusy();

// Return a copy of the current config (for web handler to serialize).
BuzzerConfig buzzerGetConfig();

// Apply and save a new config. Reinitializes pin if it changed.
bool buzzerApplyConfig(const BuzzerConfig& cfg);

// Re-apply config from LittleFS after deferred mount (called from taskInitFlash).
void buzzerReloadConfig();

// Convenience helpers — check event flags before playing.
void buzzerOnBoot();
void buzzerOnSave();
void buzzerOnAlarm(bool active);
void buzzerOnSensorAlert();
void buzzerOnNetwork();
void buzzerOnError();

#endif // ENABLE_BUZZER
