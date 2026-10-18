#ifndef SENSOR_INA226_CONFIG_H
#define SENSOR_INA226_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_INA226==1

#include <Arduino.h>
#include "sensor_ina226.h"
#include "sensor_config_base.h"

// ====================================================================================
// INA226 Configuration and Manager
// ====================================================================================
// Manages I2C address, shunt resistor configuration, averaging, alert thresholds,
// output injection, and persistent configuration via LittleFS.

// ====================================================================================
// Current display unit
// ====================================================================================
enum INA226CurrentUnit : uint8_t {
    INA226_UNIT_A  = 0,   // Amperes
    INA226_UNIT_MA = 1    // Milliamperes
};

// ====================================================================================
// INA226 configuration struct
// ====================================================================================
struct INA226Config {
    bool    sensor_enabled;
    int     i2c_addr;            // I2C address (0x40–0x4F)
    float   shunt_ohm;           // Shunt resistor value in Ohms
    float   max_current_a;       // Maximum expected current in Amperes (sets LSB)
    int     averaging;           // Averaging count: 1,4,16,64,128,256,512,1024
    int     current_unit;        // INA226CurrentUnit (0=A, 1=mA)
    int     read_interval_sec;   // Seconds between auto-reads
    bool    inject_to_buffer;
    bool    output_csv;

    // Individual measurement enable flags
    bool    show_bus_voltage;
    bool    show_shunt_voltage;
    bool    show_current;
    bool    show_power;

    // Alert thresholds (0 = disabled)
    float   alert_current_max;   // Alert if |current| exceeds this (A)
    float   alert_power_max;     // Alert if power exceeds this (W)

    // Last reading cache
    float   last_bus_v;
    float   last_shunt_v;
    float   last_current_a;
    float   last_power_w;
    unsigned long last_reading_time;

    INA226Config() :
        sensor_enabled(true),
        i2c_addr(SENSOR_INA226_I2C_ADDR),
        shunt_ohm(INA226_DEFAULT_SHUNT_OHM),
        max_current_a(INA226_DEFAULT_MAX_CURRENT_A),
        averaging(4),
        current_unit((int)INA226_UNIT_A),
        read_interval_sec(5),
        inject_to_buffer(false),
        output_csv(true),
        show_bus_voltage(true),
        show_shunt_voltage(false),
        show_current(true),
        show_power(true),
        alert_current_max(0.0f),
        alert_power_max(0.0f),
        last_bus_v(0.0f),
        last_shunt_v(0.0f),
        last_current_a(0.0f),
        last_power_w(0.0f),
        last_reading_time(0)
    {}
};

// ====================================================================================
// INA226 Config Manager class
// ====================================================================================
class INA226ConfigManager : public SensorConfigBase<INA226ConfigManager, INA226Config> {
public:
    bool begin(const String& filePath = "/config/sensor_ina226.json");

    bool performReading();

    // Required by SensorConfigBase
    String toJson() const;
    bool   updateFromJson(const String& json);

private:
    void injectToBuffer() const;
    INA226Avg _avgFromInt(int n) const;
};

extern INA226ConfigManager ina226ConfigManager;

#endif // ENABLE_INA226==1
#endif // SENSOR_INA226_CONFIG_H
