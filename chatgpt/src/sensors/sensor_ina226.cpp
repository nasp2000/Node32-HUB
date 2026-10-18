#include "sensor_ina226.h"

#include "feature_flags.h"
#if ENABLE_INA226==1

#include <Arduino.h>
#include "feature_flags.h"

INA226Sensor ina226Sensor;

INA226Sensor::INA226Sensor()
    : _wire(&Wire),
      _addr(SENSOR_INA226_I2C_ADDR),
      _shuntOhm(INA226_DEFAULT_SHUNT_OHM),
      _currentLsb(INA226_DEFAULT_MAX_CURRENT_A / 32768.0f),
      _initialized(false),
      _lastBusV(0.0f),
      _lastShuntV(0.0f),
      _lastCurrent(0.0f),
      _lastPower(0.0f)
{}

// ====================================================================================
// Initialization and calibration
// ====================================================================================

bool INA226Sensor::begin(uint8_t addr, float shuntOhm, float maxCurrentA, TwoWire* wire) {
    _wire     = wire;
    _addr     = addr;
    _shuntOhm = shuntOhm;

    // Verify device identity (manufacturer ID = 0x5449 "TI")
    uint16_t mfr = _readRegister(INA226_REG_MANUFACTURER);
    if (mfr != 0x5449) {
        Serial.printf("[INA226] Unexpected manufacturer ID 0x%04X (expected 0x5449) at addr 0x%02X\n",
                      mfr, addr);
        // Continue anyway — some clones respond with different IDs
    }

    // Software reset to ensure clean state
    reset();
    delay(5);

    // Calculate calibration register:
    //   current_lsb = max_expected_current / 32768
    //   cal = trunc(0.00512 / (current_lsb * shunt_ohm))
    _currentLsb = maxCurrentA / 32768.0f;
    uint16_t cal = (uint16_t)(0.00512f / (_currentLsb * shuntOhm));
    if (!_writeRegister(INA226_REG_CALIBRATION, cal)) {
        Serial.printf("[INA226] I2C write failed at addr 0x%02X\n", addr);
        return false;
    }

    // Default config: 4 averages, 1.1ms bus + shunt conversion time, continuous mode
    // Bits [15:0]: RST=0, reserved=0, AVG=4 (01), VBUSCT=1.1ms (100), VSHCT=1.1ms (100), MODE=7 (continuous)
    uint16_t cfg = INA226_AVG_4 | (0x4 << 6) | (0x4 << 3) | 0x7;
    _writeRegister(INA226_REG_CONFIG, cfg);

    _initialized = true;
    Serial.printf("[INA226] Ready at 0x%02X shunt=%.4fΩ lsb=%.4fmA cal=%u\n",
                  addr, shuntOhm, _currentLsb * 1000.0f, cal);
    return true;
}

// ====================================================================================
// Averaging configuration
// ====================================================================================

void INA226Sensor::setAveraging(INA226Avg avg) {
    uint16_t cfg = _readRegister(INA226_REG_CONFIG);
    cfg &= ~0x0E00;  // Clear averaging bits [11:9]
    cfg |= (uint16_t)avg;
    _writeRegister(INA226_REG_CONFIG, cfg);
}

// ====================================================================================
// Software reset
// ====================================================================================

void INA226Sensor::reset() {
    _writeRegister(INA226_REG_CONFIG, INA226_RESET_BIT);
}

// ====================================================================================
// Voltage and current readings
// ====================================================================================

float INA226Sensor::readBusVoltage() {
    // Bus voltage register: 1.25mV per LSB (15:0), always unsigned
    int16_t raw = _readRegisterSigned(INA226_REG_BUS_VOLTAGE);
    return (float)raw * 0.00125f;  // 1.25mV/LSB
}

float INA226Sensor::readShuntVoltage() {
    // Shunt voltage register: 2.5µV per LSB, signed
    int16_t raw = _readRegisterSigned(INA226_REG_SHUNT_VOLTAGE);
    return (float)raw * 0.0000025f;  // 2.5µV/LSB
}

float INA226Sensor::readCurrent() {
    if (!_initialized) return 0.0f;
    int16_t raw = _readRegisterSigned(INA226_REG_CURRENT);
    return (float)raw * _currentLsb;
}

float INA226Sensor::readPower() {
    if (!_initialized) return 0.0f;
    // Power register: 25 * current_lsb per LSB, unsigned
    uint16_t raw = _readRegister(INA226_REG_POWER);
    return (float)raw * 25.0f * _currentLsb;
}

bool INA226Sensor::readAll(float& busV, float& shuntV, float& currentA, float& powerW) {
    if (!_initialized) return false;
    _lastBusV    = readBusVoltage();
    _lastShuntV  = readShuntVoltage();
    _lastCurrent = readCurrent();
    _lastPower   = readPower();
    busV     = _lastBusV;
    shuntV   = _lastShuntV;
    currentA = _lastCurrent;
    powerW   = _lastPower;
    return true;
}

// ====================================================================================
// Low-level I2C register access
// ====================================================================================

bool INA226Sensor::_writeRegister(uint8_t reg, uint16_t value) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write((uint8_t)(value >> 8));    // MSB first
    _wire->write((uint8_t)(value & 0xFF));  // LSB
    return (_wire->endTransmission() == 0);
}

uint16_t INA226Sensor::_readRegister(uint8_t reg) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->endTransmission(false);  // Repeated start

    uint8_t n = (uint8_t)_wire->requestFrom(_addr, (uint8_t)2);
    if (n < 2) return 0;
    uint16_t val = ((uint16_t)_wire->read() << 8) | _wire->read();
    return val;
}

int16_t INA226Sensor::_readRegisterSigned(uint8_t reg) {
    return (int16_t)_readRegister(reg);
}

#endif // ENABLE_INA226==1
