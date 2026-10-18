#ifndef SENSOR_BH1750_CONFIG_H
#define SENSOR_BH1750_CONFIG_H

#include "../feature_flags.h"

#if ENABLE_BH1750==1

#include <Arduino.h>
#include "sensor_bh1750.h"
#include "sensor_config_base.h"

// ====================================================================================
// BH1750 Configuration and Manager (mirrors INA226 shape).
// ====================================================================================

struct BH1750Config {
    bool    sensor_enabled;
    int     i2c_addr;            // 0x23 (ADDR=GND) or 0x5C (ADDR=VCC)
    int     read_interval_sec;
    bool    inject_to_buffer;
    bool    output_csv;

    // Last reading cache
    float   last_lux;
    unsigned long last_reading_time;

    BH1750Config() :
        sensor_enabled(true),
        i2c_addr(SENSOR_BH1750_I2C_ADDR),
        read_interval_sec(10),
        inject_to_buffer(false),
        output_csv(true),
        last_lux(0.0f),
        last_reading_time(0)
    {}
};

class BH1750ConfigManager : public SensorConfigBase<BH1750ConfigManager, BH1750Config> {
public:
    bool begin(const String& filePath = "/config/sensor_bh1750.json");

    bool performReading();

    // Required by SensorConfigBase
    String toJson() const;
    bool   updateFromJson(const String& json);

private:
    void injectToBuffer() const;
};

extern BH1750ConfigManager bh1750ConfigManager;

#endif // ENABLE_BH1750==1
#endif // SENSOR_BH1750_CONFIG_H
