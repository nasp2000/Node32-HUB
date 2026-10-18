#ifndef SENSOR_BH1750_H
#define SENSOR_BH1750_H

#include "../feature_flags.h"

#if ENABLE_BH1750==1

#include <Arduino.h>
#include <Wire.h>

// ====================================================================================
// BH1750 ambient light sensor driver (ROHM, I2C).
// ====================================================================================
// TouchMeteo-7 tile "lux" + backlight auto-brightness curve.
// Address 0x23 (ADDR=GND) or 0x5C (ADDR=VCC). 3.3V, shared bus 8/9 @100kHz.
// Continuous high-resolution mode (1 lx, ~120 ms). No library needed.

#ifndef SENSOR_BH1750_I2C_ADDR
  #define SENSOR_BH1750_I2C_ADDR 0x23
#endif

#define BH1750_CMD_POWER_ON   0x01
#define BH1750_CMD_RESET      0x07
#define BH1750_CMD_CONT_HIRES 0x10

class BH1750Sensor {
public:
    BH1750Sensor();

    // Initialize on the given bus/address. Returns true if ACKed.
    bool begin(uint8_t addr = SENSOR_BH1750_I2C_ADDR, TwoWire* wire = &Wire);

    // One-shot read in lux (up to ~120 ms blocking). Caches the value.
    float readLux();

    float lastLux() const { return _lastLux; }
    bool isInitialized() const { return _initialized; }
    uint8_t getAddr() const { return _addr; }

private:
    TwoWire* _wire;
    uint8_t _addr;
    bool _initialized;
    float _lastLux;
};

extern BH1750Sensor bh1750Sensor;

#endif // ENABLE_BH1750==1
#endif // SENSOR_BH1750_H
