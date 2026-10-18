#ifndef SENSOR_MAX31865_CONFIG_H
#define SENSOR_MAX31865_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_MAX31865==1

#include <Arduino.h>
#include "sensor_max31865.h"
#include "sensor_config_base.h"

// ====================================================================================
// MAX31865 Configuration and Manager
// ====================================================================================
// Manages RTD type, wiring, SPI pins, temperature units, calibration offset,
// alert thresholds, and persistent configuration via LittleFS.

// ====================================================================================
// Temperature unit
// ====================================================================================
enum MAX31865TempUnit : uint8_t {
    MAX31865_CELSIUS    = 0,
    MAX31865_FAHRENHEIT = 1,
    MAX31865_KELVIN     = 2
};

// ====================================================================================
// MAX31865 configuration struct
// ====================================================================================
struct MAX31865Config {
    bool    sensor_enabled;
    int     cs_pin;              // SPI chip select GPIO
    int     mosi_pin;            // SPI MOSI GPIO
    int     miso_pin;            // SPI MISO GPIO
    int     clk_pin;             // SPI CLK GPIO
    int     rtd_type;            // 0=PT100, 1=PT1000
    int     wiring;              // 0=2-wire/4-wire, 1=3-wire
    int     temp_unit;           // MAX31865TempUnit
    float   calibration_offset;  // Temperature correction offset (°C)
    int     read_interval_sec;   // Seconds between auto-readings
    bool    inject_to_buffer;
    bool    output_csv;

    // Alert thresholds (NaN = disabled)
    float   alert_low;           // Alert if temperature drops below this
    float   alert_high;          // Alert if temperature exceeds this

    // Last reading cache
    float   last_temp_raw;       // Raw temperature from sensor (°C)
    float   last_temp_display;   // Displayed temperature (with calibration + unit conversion)
    uint8_t last_fault;          // Fault register byte (0 = no fault)
    unsigned long last_reading_time;

    MAX31865Config() :
        sensor_enabled(true),
        cs_pin(SENSOR_MAX31865_CS_PIN),
        mosi_pin(SENSOR_MAX31865_MOSI_PIN),
        miso_pin(SENSOR_MAX31865_MISO_PIN),
        clk_pin(SENSOR_MAX31865_CLK_PIN),
        rtd_type((int)MAX31865_PT100),
        wiring((int)MAX31865_2WIRE),
        temp_unit((int)MAX31865_CELSIUS),
        calibration_offset(0.0f),
        read_interval_sec(10),
        inject_to_buffer(false),
        output_csv(true),
        alert_low(-999.0f),
        alert_high(999.0f),
        last_temp_raw(NAN),
        last_temp_display(NAN),
        last_fault(0),
        last_reading_time(0)
    {}
};

// ====================================================================================
// MAX31865 Config Manager class
// ====================================================================================
class MAX31865ConfigManager : public SensorConfigBase<MAX31865ConfigManager, MAX31865Config> {
public:
    bool begin(const String& filePath = "/config/sensor_max31865.json");

    bool performReading();

    // Convert raw °C temperature to configured display unit
    float toDisplayUnit(float tempC) const;
    const char* unitSuffix() const;

    // Required by SensorConfigBase
    String toJson() const;
    bool   updateFromJson(const String& json);

private:
    void injectToBuffer() const;
};

extern MAX31865ConfigManager max31865ConfigManager;

#endif // ENABLE_MAX31865==1
#endif // SENSOR_MAX31865_CONFIG_H
