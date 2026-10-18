#include "sensor_sound.h"

#include "feature_flags.h"
#if ENABLE_SOUND_SENSOR==1

#include <Arduino.h>
#include <math.h>
#include "feature_flags.h"

SoundSensor soundSensor;

SoundSensor::SoundSensor()
    : _pin(SENSOR_SOUND_PIN),
      _initialized(false),
      _peakToPeak(0),
      _peakMax(0),
      _peakMin(SOUND_ADC_RESOLUTION),
      _rmsAdc(0.0f)
{}

// ====================================================================================
// Initialization
// ====================================================================================

bool SoundSensor::begin(uint8_t pin) {
    _pin = pin;

    // Configure ADC for the full 0–3.3V range
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    analogSetPinAttenuation(_pin, ADC_11db);

    _initialized = true;
    Serial.printf("[SOUND] Ready on GPIO%u\n", _pin);
    return true;
}

// ====================================================================================
// Sampling
// ====================================================================================

bool SoundSensor::performReading(uint16_t windowMs, uint16_t sampleCount) {
    if (!_initialized) return false;
    if (sampleCount == 0) sampleCount = 1;

    int maxVal = 0;
    int minVal = SOUND_ADC_RESOLUTION;
    int64_t sumSq = 0;

    unsigned long startMs = millis();
    uint16_t taken = 0;

    while (millis() - startMs < windowMs && taken < sampleCount) {
        int raw = analogRead(_pin);
        if (raw > maxVal) maxVal = raw;
        if (raw < minVal) minVal = raw;

        // Accumulate (raw - midpoint)^2 for RMS calculation
        int centered = raw - (SOUND_ADC_RESOLUTION / 2);
        sumSq += (int64_t)centered * centered;
        taken++;

        // Short yield to prevent watchdog starvation during long windows
        if ((taken & 0x3F) == 0) yield();
    }

    _peakMax    = maxVal;
    _peakMin    = minVal;
    _peakToPeak = maxVal - minVal;

    // RMS: sqrt(mean(squared deviations from midpoint))
    if (taken > 0) {
        _rmsAdc = sqrtf((float)(sumSq / taken));
    } else {
        _rmsAdc = 0.0f;
    }

    return true;
}

// ====================================================================================
// Derived measurements
// ====================================================================================

float SoundSensor::getPeakToPeakMv() const {
    return ((float)_peakToPeak / SOUND_ADC_RESOLUTION) * SOUND_ADC_VREF_MV;
}

float SoundSensor::getRmsAdc() const {
    return _rmsAdc;
}

float SoundSensor::getDbfs() const {
    // dBFS = 20 * log10(peak_to_peak / ADC_full_scale)
    // ADC full scale for AC signal centered at mid-rail = ADC_RESOLUTION / 2
    if (_peakToPeak <= 0) return -90.0f;  // silence floor
    float ratio = (float)_peakToPeak / (float)SOUND_ADC_RESOLUTION;
    if (ratio <= 0.0f) return -90.0f;
    float db = 20.0f * log10f(ratio);
    return db;
}

#endif // ENABLE_SOUND_SENSOR==1
