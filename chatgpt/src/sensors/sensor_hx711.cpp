#include "sensor_hx711.h"

#include "feature_flags.h"
#if ENABLE_HX711==1

#include <Arduino.h>
#include "feature_flags.h"

HX711Sensor hx711Sensor;

HX711Sensor::HX711Sensor()
    : _dataPin(SENSOR_HX711_DATA_PIN),
      _clkPin(SENSOR_HX711_CLK_PIN),
      _gain(HX711_GAIN_128),
      _tare(0),
      _scaleFactor(1.0f),
      _lastWeight(0.0f),
      _initialized(false)
{}

// ====================================================================================
// Initialization
// ====================================================================================

bool HX711Sensor::begin(uint8_t dataPin, uint8_t clkPin, HX711Gain gain) {
    _dataPin = dataPin;
    _clkPin  = clkPin;
    _gain    = gain;

    pinMode(_dataPin, INPUT);
    pinMode(_clkPin, OUTPUT);
    digitalWrite(_clkPin, LOW);

    // A brief wakeup settling period after setting CLK low
    delay(10);

    // Verify the sensor responds within the timeout
    unsigned long t0 = millis();
    while (digitalRead(_dataPin) == HIGH) {
        if (millis() - t0 > HX711_READY_TIMEOUT_MS) {
            Serial.printf("[HX711] No response on DOUT (GPIO%u) within %ums\n",
                          _dataPin, HX711_READY_TIMEOUT_MS);
            return false;
        }
        delay(5);
    }

    // Consume the initial ready reading to configure gain for next conversion
    _shiftIn24();

    _initialized = true;
    Serial.printf("[HX711] Ready: DATA=GPIO%u CLK=GPIO%u gain=%u\n",
                  _dataPin, _clkPin, (uint8_t)_gain);
    return true;
}

// ====================================================================================
// Ready check
// ====================================================================================

bool HX711Sensor::isReady() const {
    if (!_initialized) return false;
    return digitalRead(_dataPin) == LOW;
}

// ====================================================================================
// Low-level 24-bit shift-in
// ====================================================================================

uint8_t HX711Sensor::_gainPulses() const {
    switch (_gain) {
        case HX711_GAIN_128: return 1;  // Channel A, 128 gain (25 total CLK pulses)
        case HX711_GAIN_32:  return 2;  // Channel B, 32  gain (26 total CLK pulses)
        case HX711_GAIN_64:  return 3;  // Channel A, 64  gain (27 total CLK pulses)
        default:             return 1;
    }
}

int32_t HX711Sensor::_shiftIn24() {
    uint32_t raw = 0;

    // Disable interrupts during bit-bang to prevent timing glitches
    noInterrupts();

    // Read 24 bits MSB first
    for (int i = 0; i < 24; i++) {
        digitalWrite(_clkPin, HIGH);
        delayMicroseconds(1);
        raw = (raw << 1) | (digitalRead(_dataPin) ? 1 : 0);
        digitalWrite(_clkPin, LOW);
        delayMicroseconds(1);
    }

    // Extra CLK pulses select gain mode for the next conversion
    uint8_t extra = _gainPulses();
    for (uint8_t i = 0; i < extra; i++) {
        digitalWrite(_clkPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(_clkPin, LOW);
        delayMicroseconds(1);
    }

    interrupts();

    // Sign-extend 24-bit two's complement to int32_t
    if (raw & 0x800000UL) {
        raw |= 0xFF000000UL;
    }
    return (int32_t)raw;
}

// ====================================================================================
// Single raw read
// ====================================================================================

int32_t HX711Sensor::readRaw() {
    if (!_initialized) return 0;

    unsigned long t0 = millis();
    while (digitalRead(_dataPin) == HIGH) {
        if (millis() - t0 > HX711_READY_TIMEOUT_MS) {
            Serial.println("[HX711] Timeout waiting for DOUT");
            return 0;
        }
        delay(1);
    }
    return _shiftIn24();
}

// ====================================================================================
// Averaged raw read
// ====================================================================================

int32_t HX711Sensor::readAverageRaw(uint8_t samples) {
    if (!_initialized || samples == 0) return 0;
    int64_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += readRaw();
    }
    return (int32_t)(sum / samples);
}

// ====================================================================================
// Calibrated weight read
// ====================================================================================

float HX711Sensor::readWeight(uint8_t samples) {
    if (!_initialized) return 0.0f;
    if (_scaleFactor == 0.0f) return 0.0f;

    int32_t raw = readAverageRaw(samples);
    _lastWeight = (float)(raw - _tare) / _scaleFactor;
    return _lastWeight;
}

// ====================================================================================
// Tare (zero point)
// ====================================================================================

void HX711Sensor::tare(uint8_t samples) {
    if (!_initialized) return;
    _tare = readAverageRaw(samples);
    Serial.printf("[HX711] Tare set to %ld\n", (long)_tare);
}

// ====================================================================================
// Power management
// ====================================================================================

void HX711Sensor::powerDown() {
    if (!_initialized) return;
    digitalWrite(_clkPin, HIGH);
    delayMicroseconds(80); // >60µs to trigger power-down
    Serial.println("[HX711] Power down");
}

void HX711Sensor::powerUp() {
    if (!_initialized) return;
    digitalWrite(_clkPin, LOW);
    delay(400); // Settling time after wakeup
    Serial.println("[HX711] Power up");
}

// ====================================================================================
// Gain selection
// ====================================================================================

void HX711Sensor::setGain(HX711Gain gain) {
    _gain = gain;
    // A dummy read is needed to clock the new gain into the device
    if (_initialized) {
        readRaw();
    }
}

#endif // ENABLE_HX711==1
