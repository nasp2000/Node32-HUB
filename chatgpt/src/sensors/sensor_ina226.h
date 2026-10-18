#ifndef SENSOR_INA226_H
#define SENSOR_INA226_H

#include "../feature_flags.h"

#if ENABLE_INA226==1

#include <Arduino.h>
#include <Wire.h>

// ====================================================================================
// INA226 Bidirectional Current / Power Monitor — I2C driver (no external library)
// ====================================================================================
// The INA226 measures shunt voltage, bus voltage, current, and power via I2C.
// It uses a 16-bit delta-sigma ADC with programmable gain and averaging.
//
// Register map:
//   0x00 — Configuration
//   0x01 — Shunt Voltage (2.5µV/LSB, two's complement)
//   0x02 — Bus Voltage   (1.25mV/LSB, read-only)
//   0x03 — Power         (25 * current_lsb per LSB)
//   0x04 — Current       (signed, calibration-dependent)
//   0x05 — Calibration   (sets current_lsb = 0.00512 / (cal * shunt_ohm))
//   0x06 — Mask/Enable
//   0x07 — Alert Limit
//   0xFE — Manufacturer ID (must read 0x5449)
//   0xFF — Die ID (must read 0x2260)
//
// Calibration formula:
//   current_lsb = max_expected_current / 32768
//   cal_reg     = 0.00512 / (current_lsb * shunt_ohm)
//
// Default I2C address: 0x40 (A0=GND, A1=GND)
// Address range: 0x40 to 0x4F (A0, A1 connected to GND/VS/SDA/SCL)

#ifndef SENSOR_INA226_I2C_ADDR
  #define SENSOR_INA226_I2C_ADDR  0x40
#endif

// Default shunt resistor value in Ohms (100mOhm = 0.1 Ohm is common)
#ifndef INA226_DEFAULT_SHUNT_OHM
  #define INA226_DEFAULT_SHUNT_OHM  0.1f
#endif

// Default maximum expected current in Amperes (sets current_lsb resolution)
#ifndef INA226_DEFAULT_MAX_CURRENT_A
  #define INA226_DEFAULT_MAX_CURRENT_A  3.2768f
#endif

// INA226 register addresses
#define INA226_REG_CONFIG        0x00
#define INA226_REG_SHUNT_VOLTAGE 0x01
#define INA226_REG_BUS_VOLTAGE   0x02
#define INA226_REG_POWER         0x03
#define INA226_REG_CURRENT       0x04
#define INA226_REG_CALIBRATION   0x05
#define INA226_REG_MASK_ENABLE   0x06
#define INA226_REG_ALERT_LIMIT   0x07
#define INA226_REG_MANUFACTURER  0xFE
#define INA226_REG_DIE_ID        0xFF

// Configuration register bit fields
#define INA226_RESET_BIT         0x8000

// Averaging count options (bits 11:9 of config register)
enum INA226Avg : uint16_t {
    INA226_AVG_1    = 0x0000,
    INA226_AVG_4    = 0x0200,
    INA226_AVG_16   = 0x0400,
    INA226_AVG_64   = 0x0600,
    INA226_AVG_128  = 0x0800,
    INA226_AVG_256  = 0x0A00,
    INA226_AVG_512  = 0x0C00,
    INA226_AVG_1024 = 0x0E00
};

// ====================================================================================
// INA226 driver class
// ====================================================================================
class INA226Sensor {
public:
    INA226Sensor();

    // Initialize and calibrate. Returns true if the device responds on I2C.
    bool begin(uint8_t addr        = SENSOR_INA226_I2C_ADDR,
               float   shuntOhm   = INA226_DEFAULT_SHUNT_OHM,
               float   maxCurrentA = INA226_DEFAULT_MAX_CURRENT_A,
               TwoWire* wire        = &Wire);

    // Set averaging count (affects measurement noise vs. speed trade-off).
    void setAveraging(INA226Avg avg);

    // Trigger a software reset (restores all registers to default).
    void reset();

    // Read bus voltage in Volts.
    float readBusVoltage();

    // Read shunt voltage in Volts (small value, e.g. -0.08164 V max range).
    float readShuntVoltage();

    // Read calculated current in Amperes (requires calibration register to be set).
    float readCurrent();

    // Read calculated power in Watts (requires calibration register to be set).
    float readPower();

    // Read all four quantities in a single pass (4 separate I2C transactions).
    bool readAll(float& busV, float& shuntV, float& currentA, float& powerW);

    bool isInitialized() const { return _initialized; }

    uint8_t getAddress() const { return _addr; }
    float getShuntOhm()  const { return _shuntOhm; }
    float getCurrentLsb() const { return _currentLsb; }

    // Last cached values (updated by readAll).
    float getLastBusVoltage()  const { return _lastBusV; }
    float getLastShuntVoltage() const { return _lastShuntV; }
    float getLastCurrent()     const { return _lastCurrent; }
    float getLastPower()       const { return _lastPower; }

private:
    TwoWire* _wire;
    uint8_t  _addr;
    float    _shuntOhm;
    float    _currentLsb;
    bool     _initialized;

    float _lastBusV;
    float _lastShuntV;
    float _lastCurrent;
    float _lastPower;

    // Write a 16-bit value to a register (big-endian).
    bool _writeRegister(uint8_t reg, uint16_t value);

    // Read a 16-bit signed value from a register (big-endian).
    int16_t _readRegisterSigned(uint8_t reg);

    // Read a 16-bit unsigned value from a register.
    uint16_t _readRegister(uint8_t reg);
};

extern INA226Sensor ina226Sensor;

#endif // ENABLE_INA226==1
#endif // SENSOR_INA226_H
