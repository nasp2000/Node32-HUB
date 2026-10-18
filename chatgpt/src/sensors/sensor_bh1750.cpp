#include "sensor_bh1750.h"

#include "feature_flags.h"

#if ENABLE_BH1750==1

BH1750Sensor bh1750Sensor;

BH1750Sensor::BH1750Sensor()
    : _wire(&Wire), _addr(SENSOR_BH1750_I2C_ADDR),
      _initialized(false), _lastLux(0.0f) {}

bool BH1750Sensor::begin(uint8_t addr, TwoWire* wire) {
    _wire = wire ? wire : &Wire;
    _addr = addr;
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) {
        _initialized = false;
        return false;
    }
    _wire->beginTransmission(_addr);
    _wire->write(BH1750_CMD_POWER_ON);
    if (_wire->endTransmission() != 0) {
        _initialized = false;
        return false;
    }
    _wire->beginTransmission(_addr);
    _wire->write(BH1750_CMD_RESET);
    _wire->endTransmission();
    delay(10);
    _wire->beginTransmission(_addr);
    _wire->write(BH1750_CMD_CONT_HIRES);
    if (_wire->endTransmission() != 0) {
        _initialized = false;
        return false;
    }
    delay(130);
    _initialized = true;
    readLux();
    return true;
}

float BH1750Sensor::readLux() {
    if (!_initialized) return 0.0f;
    if (_wire->requestFrom(_addr, (uint8_t)2) < 2) return _lastLux;
    uint16_t raw = ((uint16_t)_wire->read() << 8) | _wire->read();
    _lastLux = (float)raw / 1.2f;
    return _lastLux;
}

#endif // ENABLE_BH1750==1
