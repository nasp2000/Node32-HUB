// buzzer.cpp
// Non-blocking active-buzzer driver. See buzzer.h for the public API.

#include "buzzer.h"

#include "feature_flags.h"
#if ENABLE_BUZZER == 1

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "littlefs_manager.h"
#include "feature_flags.h"

#define BUZZER_CFG_PATH  "/config/buzzer.json"

// ---------------------------------------------------------------------------
// Pattern table — sequence of {on_ms, off_ms} pairs; 0,0 = end.
// ---------------------------------------------------------------------------
struct BeepStep { uint16_t on_ms; uint16_t off_ms; };

static const BeepStep PATTERN_OK[]          = {{100, 0}, {0, 0}};
static const BeepStep PATTERN_WARN[]        = {{100, 100}, {100, 0}, {0, 0}};
static const BeepStep PATTERN_ERROR[]       = {{80, 80}, {80, 80}, {80, 0}, {0, 0}};
static const BeepStep PATTERN_ALARM_CYCLE[] = {{150, 100}, {0, 0}}; // repeated in loop
static const BeepStep PATTERN_LONG[]        = {{500, 0}, {0, 0}};
static const BeepStep PATTERN_DOUBLE_LONG[] = {{500, 200}, {500, 0}, {0, 0}};

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static BuzzerConfig s_cfg;

static const BeepStep* s_steps = nullptr;
static uint8_t         s_stepIdx = 0;
static bool            s_alarmLoop = false;  // true => repeat pattern continuously
static bool            s_pinHigh = false;
static unsigned long   s_stepStart = 0;
static bool            s_inOnPhase = false;  // true = currently in the ON period

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------
static void loadConfig() {
    if (!littlefsBegin()) return;
    File f = LittleFS.open(BUZZER_CFG_PATH, "r");
    if (!f) return;
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    s_cfg.enabled         = doc["enabled"]         | s_cfg.enabled;
    s_cfg.pin             = doc["pin"]              | s_cfg.pin;
    s_cfg.on_boot         = doc["on_boot"]          | s_cfg.on_boot;
    s_cfg.on_save         = doc["on_save"]          | s_cfg.on_save;
    s_cfg.on_alarm        = doc["on_alarm"]         | s_cfg.on_alarm;
    s_cfg.on_sensor_alert = doc["on_sensor_alert"]  | s_cfg.on_sensor_alert;
    s_cfg.on_network      = doc["on_network"]       | s_cfg.on_network;
    s_cfg.on_error        = doc["on_error"]         | s_cfg.on_error;
}

static void saveConfig() {
    if (!littlefsBegin()) return;
    StaticJsonDocument<256> doc;
    doc["enabled"]         = s_cfg.enabled;
    doc["pin"]             = s_cfg.pin;
    doc["on_boot"]         = s_cfg.on_boot;
    doc["on_save"]         = s_cfg.on_save;
    doc["on_alarm"]        = s_cfg.on_alarm;
    doc["on_sensor_alert"] = s_cfg.on_sensor_alert;
    doc["on_network"]      = s_cfg.on_network;
    doc["on_error"]        = s_cfg.on_error;
    File f = LittleFS.open("/config/buzzer.json.tmp", "w");
    if (!f) return;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(BUZZER_CFG_PATH);
        LittleFS.rename("/config/buzzer.json.tmp", BUZZER_CFG_PATH);
    } else {
        LittleFS.remove("/config/buzzer.json.tmp");
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void pinHigh() {
    if (!s_pinHigh) {
        digitalWrite(s_cfg.pin, HIGH);
        s_pinHigh = true;
    }
}

static void pinLow() {
    if (s_pinHigh) {
        digitalWrite(s_cfg.pin, LOW);
        s_pinHigh = false;
    }
}

static void startPattern(const BeepStep* steps, bool loopAlarm) {
    if (!s_cfg.enabled || steps == nullptr) return;
    pinLow();
    s_steps     = steps;
    s_stepIdx   = 0;
    s_alarmLoop = loopAlarm;
    s_inOnPhase = true;
    s_stepStart = millis();
    pinHigh();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void buzzerBegin() {
    // Apply compile-time or board_pins.h default before loading config.
#ifdef BUZZER_PIN
    s_cfg.pin = BUZZER_PIN;
#endif
    loadConfig();
    pinMode(s_cfg.pin, OUTPUT);
    digitalWrite(s_cfg.pin, LOW);
}

void buzzerLoop() {
    if (!s_cfg.enabled || s_steps == nullptr) return;

    unsigned long now = millis();
    const BeepStep& step = s_steps[s_stepIdx];

    // End of step sequence.
    if (step.on_ms == 0 && step.off_ms == 0) {
        if (s_alarmLoop) {
            // Restart from the beginning.
            s_stepIdx = 0;
            s_inOnPhase = true;
            s_stepStart = now;
            pinHigh();
        } else {
            pinLow();
            s_steps = nullptr;
        }
        return;
    }

    if (s_inOnPhase) {
        // We are in the ON period.
        if ((now - s_stepStart) >= step.on_ms) {
            pinLow();
            s_inOnPhase = false;
            s_stepStart = now;
            // If no off delay, advance immediately.
            if (step.off_ms == 0) {
                s_stepIdx++;
                s_inOnPhase = true;
                s_stepStart = now;
                const BeepStep& next = s_steps[s_stepIdx];
                if (next.on_ms > 0) pinHigh();
            }
        }
    } else {
        // We are in the OFF period.
        if ((now - s_stepStart) >= step.off_ms) {
            s_stepIdx++;
            s_inOnPhase = true;
            s_stepStart = now;
            const BeepStep& next = s_steps[s_stepIdx];
            if (next.on_ms > 0) pinHigh();
        }
    }
}

void buzzerBeep(BuzzerPattern p) {
    switch (p) {
        case BEEP_OK:          startPattern(PATTERN_OK,          false); break;
        case BEEP_WARN:        startPattern(PATTERN_WARN,        false); break;
        case BEEP_ERROR:       startPattern(PATTERN_ERROR,       false); break;
        case BEEP_ALARM:       startPattern(PATTERN_ALARM_CYCLE, true);  break;
        case BEEP_LONG:        startPattern(PATTERN_LONG,        false); break;
        case BEEP_DOUBLE_LONG: startPattern(PATTERN_DOUBLE_LONG, false); break;
        default: break;
    }
}

void buzzerSilence() {
    s_steps     = nullptr;
    s_alarmLoop = false;
    s_stepIdx   = 0;
    pinLow();
}

bool buzzerIsBusy() {
    return (s_steps != nullptr);
}

BuzzerConfig buzzerGetConfig() {
    return s_cfg;
}

bool buzzerApplyConfig(const BuzzerConfig& cfg) {
    uint8_t oldPin = s_cfg.pin;
    s_cfg = cfg;
    if (cfg.pin != oldPin) {
        // Reinitialize old pin and new pin.
        digitalWrite(oldPin, LOW);
        pinMode(cfg.pin, OUTPUT);
        digitalWrite(cfg.pin, LOW);
        s_pinHigh = false;
    }
    saveConfig();
    return true;
}

void buzzerReloadConfig() {
    // Called from taskInitFlash after LittleFS is mounted to re-apply saved config.
    // buzzerBegin() runs in setup() before LittleFS is ready, so loadConfig() returns
    // early there; this deferred call correctly applies the persisted values.
    uint8_t oldPin = s_cfg.pin;
    loadConfig();
    if (s_cfg.pin != oldPin) {
        // Saved config uses a different pin — reconfigure GPIO.
        digitalWrite(oldPin, LOW);
        pinMode(s_cfg.pin, OUTPUT);
        digitalWrite(s_cfg.pin, LOW);
        s_pinHigh = false;
    }
}

// ---------------------------------------------------------------------------
// Event convenience wrappers
// ---------------------------------------------------------------------------

void buzzerOnBoot() {
    if (s_cfg.on_boot) buzzerBeep(BEEP_OK);
}

void buzzerOnSave() {
    if (s_cfg.on_save) buzzerBeep(BEEP_OK);
}

void buzzerOnAlarm(bool active) {
    if (!s_cfg.on_alarm) return;
    if (active) buzzerBeep(BEEP_ALARM);
    else        buzzerSilence();
}

void buzzerOnSensorAlert() {
    if (s_cfg.on_sensor_alert) buzzerBeep(BEEP_WARN);
}

void buzzerOnNetwork() {
    if (s_cfg.on_network) buzzerBeep(BEEP_LONG);
}

void buzzerOnError() {
    if (s_cfg.on_error) buzzerBeep(BEEP_ERROR);
}

#endif // ENABLE_BUZZER
