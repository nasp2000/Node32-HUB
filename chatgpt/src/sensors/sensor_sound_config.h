#ifndef SENSOR_SOUND_CONFIG_H
#define SENSOR_SOUND_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_SOUND_SENSOR==1

#include <Arduino.h>
#include "sensor_sound.h"
#include "sensor_config_base.h"

// ====================================================================================
// Sound Sensor Configuration and Manager
// ====================================================================================
// Manages ADC pin, sampling parameters, dB output, alert thresholds, and injection.

// ====================================================================================
// Sound sensor configuration struct
// ====================================================================================
struct SoundConfig {
    bool    sensor_enabled;
    int     adc_pin;             // ADC GPIO pin
    int     sample_window_ms;    // Sampling window in ms (default 50ms)
    int     sample_count;        // Number of ADC samples per window (default 200)
    int     read_interval_sec;   // Seconds between auto-readings (min 1)
    bool    inject_to_buffer;    // Inject readings into RX buffer
    bool    output_csv;          // CSV format for injection

    // Alert threshold
    float   alert_dbfs;          // Alert if dBFS rises above this (0 = disabled)

    // Last reading cache
    int     last_peak_to_peak;   // Raw ADC peak-to-peak
    float   last_peak_mv;        // Peak-to-peak in mV
    float   last_dbfs;           // dBFS level
    unsigned long last_reading_time;

    SoundConfig() :
        sensor_enabled(true),
        adc_pin(SENSOR_SOUND_PIN),
        sample_window_ms(SOUND_SAMPLE_WINDOW_MS_DEFAULT),
        sample_count(SOUND_SAMPLE_COUNT_DEFAULT),
        read_interval_sec(2),
        inject_to_buffer(false),
        output_csv(true),
        alert_dbfs(0.0f),
        last_peak_to_peak(0),
        last_peak_mv(0.0f),
        last_dbfs(-90.0f),
        last_reading_time(0)
    {}
};

// ====================================================================================
// Sound Config Manager class
// ====================================================================================
class SoundConfigManager : public SensorConfigBase<SoundConfigManager, SoundConfig> {
public:
    bool begin(const String& filePath = "/config/sensor_sound.json");

    bool performReading();

    // Required by SensorConfigBase
    String toJson() const;
    bool   updateFromJson(const String& json);

private:
    void injectToBuffer() const;
};

extern SoundConfigManager soundConfigManager;

#endif // ENABLE_SOUND_SENSOR==1
#endif // SENSOR_SOUND_CONFIG_H
