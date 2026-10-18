#ifndef SENSOR_SOUND_H
#define SENSOR_SOUND_H

#include "../feature_flags.h"

#if ENABLE_SOUND_SENSOR==1

#include <Arduino.h>

// ====================================================================================
// Sound Level Sensor Driver (MAX4466 / MAX9814 / generic electret module)
// ====================================================================================
// These modules output an analog signal centered around VCC/2 (≈1.65V on 3.3V supply).
// The driver samples the ADC over a configurable window and calculates:
//   - Peak-to-peak amplitude (millivolts)
//   - RMS-like dBFS level (relative to ADC full scale)
//   - Approximate decibel SPL (based on module sensitivity, requires calibration)
//
// Hardware connection:
//   AO (analog out) -> ESP32 ADC-capable GPIO
//   VCC -> 3.3V (MAX4466) or 3.3V/5V (MAX9814)
//   GND -> GND
//
// Note: MAX9814 has AGC and a configurable attack/release ratio via the GAIN pin.
//       MAX4466 has a fixed-gain trimmer pot.
//
// ADC considerations:
//   - Use attenuation ADC_11db for the full 0–3.3V range
//   - Readings are amplitude (not pressure) so SPL conversion is approximate

// Default ADC pin for the sound sensor analog output
#ifndef SENSOR_SOUND_PIN
  #define SENSOR_SOUND_PIN   5    // GPIO5 safe default (override for specific boards)
#endif

// Sampling window: how long to sample the ADC before calculating amplitude (ms)
#define SOUND_SAMPLE_WINDOW_MS_DEFAULT   50

// Number of ADC readings taken per window (higher = smoother but slower)
#define SOUND_SAMPLE_COUNT_DEFAULT      200

// ADC resolution (12-bit on ESP32)
#define SOUND_ADC_RESOLUTION           4095

// ADC reference voltage (mV)
#define SOUND_ADC_VREF_MV              3300

// Reference dB level for dBFS calculation (full-scale = SOUND_ADC_RESOLUTION)
#define SOUND_DBFS_REFERENCE   (SOUND_ADC_RESOLUTION / 2)

// ====================================================================================
// Sound sensor class
// ====================================================================================
class SoundSensor {
public:
    SoundSensor();

    // Initialize the sensor on the given ADC pin.
    bool begin(uint8_t pin = SENSOR_SOUND_PIN);

    // Sample the ADC for the configured window duration.
    // Fills internal state with peak, rms estimate and dBFS level.
    // Returns true on success.
    bool performReading(uint16_t windowMs    = SOUND_SAMPLE_WINDOW_MS_DEFAULT,
                        uint16_t sampleCount = SOUND_SAMPLE_COUNT_DEFAULT);

    // Peak-to-peak amplitude in ADC counts (0–4095)
    int   getPeakToPeak()    const { return _peakToPeak; }

    // Peak-to-peak amplitude in millivolts
    float getPeakToPeakMv()  const;

    // RMS amplitude in ADC counts (simplified: peak/sqrt(2) approximation)
    float getRmsAdc()        const;

    // dBFS level (-infinity to 0), calculated from peak-to-peak / ADC full scale
    float getDbfs()          const;

    bool isInitialized() const { return _initialized; }
    uint8_t getPin()     const { return _pin; }

private:
    uint8_t _pin;
    bool    _initialized;
    int     _peakToPeak;  // last reading peak-to-peak in ADC counts
    int     _peakMax;     // max sample in last window
    int     _peakMin;     // min sample in last window
    float   _rmsAdc;      // running RMS accumulator result
};

extern SoundSensor soundSensor;

#endif // ENABLE_SOUND_SENSOR==1
#endif // SENSOR_SOUND_H
