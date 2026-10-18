#ifndef SENSOR_HX711_CONFIG_H
#define SENSOR_HX711_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_HX711==1

#include <Arduino.h>
#include "sensor_hx711.h"
#include "sensor_config_base.h"

// ====================================================================================
// HX711 Configuration and Manager
// ====================================================================================
// Manages calibration, tare, scale factor, unit conversion, output injection,
// and persistent configuration via LittleFS.

// ====================================================================================
// Weight unit options
// ====================================================================================
enum HX711WeightUnit : uint8_t {
    HX711_UNIT_GRAM = 0,
    HX711_UNIT_KG   = 1,
    HX711_UNIT_LBS  = 2,
    HX711_UNIT_OZ   = 3,
    HX711_UNIT_N    = 4   // Newtons (scale_factor set manually)
};

// ====================================================================================
// HX711 configuration struct
// ====================================================================================
struct HX711Config {
    bool    sensor_enabled;
    int     data_pin;           // DOUT pin
    int     clk_pin;            // PD_SCK pin
    int     gain;               // 128, 64, or 32
    int     samples;            // ADC averages per reading (1–20)
    int     read_interval_sec;  // Seconds between auto readings (min 2)
    float   scale_factor;       // (raw - tare) / scale_factor = physical weight
    int32_t tare_offset;        // Raw ADC offset for zero point
    int     weight_unit;        // HX711WeightUnit
    bool    inject_to_buffer;   // Inject readings into RX buffer
    bool    output_csv;         // CSV format for injection

    // Alert thresholds (0 = disabled)
    float alert_min;            // Trigger alert below this weight
    float alert_max;            // Trigger alert above this weight

    // Last reading cache
    float   last_weight;
    int32_t last_raw;
    unsigned long last_reading_time;  // Unix timestamp (0 = never)

    HX711Config() :
        sensor_enabled(true),
        data_pin(SENSOR_HX711_DATA_PIN),
        clk_pin(SENSOR_HX711_CLK_PIN),
        gain(HX711_DEFAULT_GAIN),
        samples(HX711_SAMPLES_DEFAULT),
        read_interval_sec(5),
        scale_factor(1.0f),
        tare_offset(0),
        weight_unit((int)HX711_UNIT_GRAM),
        inject_to_buffer(false),
        output_csv(true),
        alert_min(0.0f),
        alert_max(0.0f),
        last_weight(0.0f),
        last_raw(0),
        last_reading_time(0)
    {}
};

// ====================================================================================
// HX711 Config Manager class
// ====================================================================================
class HX711ConfigManager : public SensorConfigBase<HX711ConfigManager, HX711Config> {
public:
    bool begin(const String &filePath = "/config/sensor_hx711.json");

    bool performReading();
    bool performTare();
    const char* unitSuffix() const;

    // Required by SensorConfigBase
    String toJson() const;
    bool updateFromJson(const String& json);

private:
    void injectToBuffer() const;
};

extern HX711ConfigManager hx711ConfigManager;

#endif // ENABLE_HX711==1
#endif // SENSOR_HX711_CONFIG_H
