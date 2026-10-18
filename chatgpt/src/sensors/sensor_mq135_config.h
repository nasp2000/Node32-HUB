#ifndef SENSOR_MQ135_CONFIG_H
#define SENSOR_MQ135_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_MQ135==1

#include <Arduino.h>
#include "sensor_mq135.h"
#include "sensor_config.h"
#include "sensor_config_base.h"

// ====================================================================================
// MQ-135 Configuration and Manager
// ====================================================================================
// Manages calibration, per-gas enable toggles, output injection, and
// staging data into the SensorHistory extra slots.
//
// Extra slot assignment (fixed, registered in registerExtraSlots()):
//   Slot 0 — CO2  corrected ppm  (scale 1)
//   Slot 1 — CO   ppm            (scale 10 → 1 decimal)
//   Slot 2 — NH3  ppm            (scale 10)
//   Slot 3 — NOx  ppm            (scale 10)
//   Slot 4 — Alcohol ppm         (scale 10)
//   Slot 5 — AQI  (0-500)        (scale 1)

// ====================================================================================
// Per-gas reading configuration
// ====================================================================================

struct MQ135ReadingConfig {
    bool  enabled;        // Show / inject this gas reading
    float offset;         // Calibration offset added to ppm value
    bool  inject;         // Include in RX buffer injection

    MQ135ReadingConfig() : enabled(true), offset(0.0f), inject(true) {}
};

// ====================================================================================
// Main MQ-135 configuration struct
// ====================================================================================

struct MQ135Config {
    bool  sensor_enabled;          // Master enable
    int   adc_pin;                 // GPIO pin for AOUT
    float r0;                      // Calibrated R0 in clean air (Ohm)
    float rl;                      // Module load resistance (Ohm)
    int   read_interval_sec;       // Seconds between auto-reads (min 60, same scale as env sensor)
    bool  use_env_compensation;    // Apply AHT20 temp+humidity correction to CO2
    bool  inject_to_buffer;        // Inject readings into RX buffer
    bool  output_pretty;           // Pretty format with emojis
    bool  output_csv;              // CSV key-value format

    // Per-gas enable/offset/inject
    MQ135ReadingConfig gas_co2;
    MQ135ReadingConfig gas_co;
    MQ135ReadingConfig gas_nh3;
    MQ135ReadingConfig gas_nox;
    MQ135ReadingConfig gas_alcohol;
    MQ135ReadingConfig gas_benzene;   // display only (not stored in history)
    MQ135ReadingConfig gas_smoke;     // display only (not stored in history)
    MQ135ReadingConfig gas_aqi;

    // Last reading cache (for web display, updated on each reading)
    float last_co2;
    float last_co;
    float last_nh3;
    float last_nox;
    float last_alcohol;
    float last_benzene;
    float last_smoke;
    float last_aqi;
    float last_rs;
    float last_ratio;
    int   last_raw_adc;
    bool  last_warmed_up;
    unsigned long last_reading_time; // Unix timestamp (0 = never)

    MQ135Config() :
        sensor_enabled(true),
        adc_pin(SENSOR_MQ135_PIN),
        r0(MQ135_R0_DEFAULT),
        rl(MQ135_RL_VALUE),
        read_interval_sec(60),
        use_env_compensation(true),
        inject_to_buffer(true),
        output_pretty(false),
        output_csv(true),
        last_co2(0.0f), last_co(0.0f), last_nh3(0.0f), last_nox(0.0f),
        last_alcohol(0.0f), last_benzene(0.0f), last_smoke(0.0f), last_aqi(0.0f),
        last_rs(0.0f), last_ratio(0.0f), last_raw_adc(0), last_warmed_up(false),
        last_reading_time(0)
    {}
};

// ====================================================================================
// MQ-135 Config Manager class
// ====================================================================================

class MQ135ConfigManager : public SensorConfigBase<MQ135ConfigManager, MQ135Config> {
public:
    bool begin(const String &filePath = "/config/sensor_mq135.json");

    // Register MQ-135 extra slots metadata into the SensorConfig's extra_slots[] array.
    void registerExtraSlots(SensorConfig &cfg) const;

    bool performReading(float temperature = NAN, float humidity = NAN);
    void stageHistorySlots(SensorHistory &history) const;
    void injectToBuffer() const;
    String formatPretty() const;
    String formatCSV()    const;

    // Expose mutable config for direct field writes.
    MQ135Config &getConfigRW() { return _config; }

    // Required by SensorConfigBase
    String toJson() const;
    bool updateFromJson(const String &json);
};

extern MQ135ConfigManager mq135ConfigManager;

#endif // ENABLE_MQ135==1
#endif // SENSOR_MQ135_CONFIG_H
