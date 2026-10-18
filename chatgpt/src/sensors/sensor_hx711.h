#ifndef SENSOR_HX711_H
#define SENSOR_HX711_H

#include "../feature_flags.h"

#if ENABLE_HX711==1

#include <Arduino.h>

// ====================================================================================
// HX711 24-bit Load Cell Amplifier — bit-bang driver
// ====================================================================================
// The HX711 is a precision 24-bit ADC designed for weigh scales and industrial
// control applications. It communicates via a simple 2-wire bit-bang protocol
// (DOUT + PD_SCK). No external library is required.
//
// Channel / Gain selection (set by number of PD_SCK pulses per conversion):
//   Channel A, Gain 128 -> 25 pulses (default, highest resolution)
//   Channel B, Gain 32  -> 26 pulses (lower resolution, external ref)
//   Channel A, Gain 64  -> 27 pulses (mid resolution)
//
// Protocol:
//   1. Wait for DOUT to go LOW (conversion ready, ~100ms at 10 SPS)
//   2. Read 24 bits MSB-first on rising CLK edge
//   3. Apply additional 1/2/3 CLK pulses to select gain for next reading
//   4. Result is 24-bit two's complement; sign-extend to int32_t
//
// Power saving:
//   Keep PD_SCK HIGH for >60us to enter power-down mode.
//   Set PD_SCK LOW to wake up (settle time ~400ms).

// Default DATA and CLK pins (overridden by board_pins.h for specific boards)
#ifndef SENSOR_HX711_DATA_PIN
  #define SENSOR_HX711_DATA_PIN   5   // GPIO5 — HX711 DOUT
#endif
#ifndef SENSOR_HX711_CLK_PIN
  #define SENSOR_HX711_CLK_PIN    4   // GPIO4 — HX711 PD_SCK
#endif

// Default gain: 128 (channel A) = most common for 10mV/V bridge sensors
#ifndef HX711_DEFAULT_GAIN
  #define HX711_DEFAULT_GAIN    128
#endif

// Maximum time to wait for DOUT to go LOW before declaring a timeout
#define HX711_READY_TIMEOUT_MS  500

// Number of readings to average per sample
#define HX711_SAMPLES_DEFAULT     5

// ====================================================================================
// HX711 gain modes
// ====================================================================================
enum HX711Gain : uint8_t {
    HX711_GAIN_128 = 128,  // Channel A, 25 CLK pulses
    HX711_GAIN_32  = 32,   // Channel B, 26 CLK pulses
    HX711_GAIN_64  = 64    // Channel A, 27 CLK pulses
};

// ====================================================================================
// HX711 driver class
// ====================================================================================
class HX711Sensor {
public:
    HX711Sensor();

    // Initialize the sensor with given pins and gain setting.
    // Returns true if the sensor responds within the ready timeout.
    bool begin(uint8_t dataPin, uint8_t clkPin, HX711Gain gain = HX711_GAIN_128);

    // Returns true if the sensor is powered and ready for a reading (DOUT LOW).
    bool isReady() const;

    // Read a single raw 24-bit value (blocks until ready or timeout; returns 0 on timeout).
    int32_t readRaw();

    // Read the average of multiple raw samples.
    int32_t readAverageRaw(uint8_t samples = HX711_SAMPLES_DEFAULT);

    // Read calibrated weight in the configured unit.
    // weight = (raw - tare) / scaleFactor
    float readWeight(uint8_t samples = HX711_SAMPLES_DEFAULT);

    // Set the tare offset (zero point). Call after placing an empty container.
    void tare(uint8_t samples = 10);

    // Set scale factor: maps (raw - tare) to the physical unit.
    // Calibrate: place a known weight, call readAverageRaw() and divide by known weight.
    void setScaleFactor(float scale) { _scaleFactor = scale; }
    float getScaleFactor() const     { return _scaleFactor; }

    // Directly set the tare offset (restored from persistent config).
    void setTareOffset(int32_t offset) { _tare = offset; }
    int32_t getTareOffset() const      { return _tare; }

    // Power management
    void powerDown();
    void powerUp();

    // Set gain for next reading (takes effect after the current conversion completes).
    void setGain(HX711Gain gain);
    HX711Gain getGain() const { return _gain; }

    // Last valid weight reading (updated by readWeight()).
    float getLastWeight() const { return _lastWeight; }

    bool isInitialized() const { return _initialized; }

private:
    uint8_t   _dataPin;
    uint8_t   _clkPin;
    HX711Gain _gain;
    int32_t   _tare;
    float     _scaleFactor;
    float     _lastWeight;
    bool      _initialized;

    // Returns the number of extra CLK pulses required for the current gain setting.
    uint8_t _gainPulses() const;

    // Read one 24-bit word from the sensor (does not wait for ready, caller must check).
    int32_t _shiftIn24();
};

extern HX711Sensor hx711Sensor;

#endif // ENABLE_HX711==1
#endif // SENSOR_HX711_H
