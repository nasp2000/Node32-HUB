#include "sensor_max31865.h"

#include "feature_flags.h"
#if ENABLE_MAX31865==1

#include <Arduino.h>
#include <math.h>
#include "feature_flags.h"

// Callendar-Van Dusen coefficients (ITS-90)
static constexpr float CVD_A =  3.9083e-3f;
static constexpr float CVD_B = -5.775e-7f;

MAX31865Sensor max31865Sensor;

MAX31865Sensor::MAX31865Sensor()
    : _spi(&SPI),
      _csPin(SENSOR_MAX31865_CS_PIN),
      _type(MAX31865_PT100),
      _wiring(MAX31865_2WIRE),
      _initialized(false),
      _lastTemp(NAN),
      _spiSettings(MAX31865_SPI_FREQ, MSBFIRST, SPI_MODE1)
{}

// ====================================================================================
// Initialization
// ====================================================================================

bool MAX31865Sensor::begin(uint8_t csPin, MAX31865RTDType type, MAX31865Wiring wiring, SPIClass* spi) {
    _csPin   = csPin;
    _type    = type;
    _wiring  = wiring;
    _spi     = spi;

    pinMode(_csPin, OUTPUT);
    _csHigh();

    // Configuration register value:
    //   Bit 7: VBIAS = 1 (always on — simplest mode)
    //   Bit 6: Conversion mode = 0 (one-shot, manual trigger)
    //   Bit 5: One-shot = 0 (will set to 1 when triggering)
    //   Bit 4: 3-wire (1) or 2/4-wire (0)
    //   Bits 3:2: Fault detection cycle (00 = none)
    //   Bit 1: Fault status clear = 0
    //   Bit 0: 50Hz/60Hz filter = 0 (60Hz rejection)
    uint8_t cfg = 0x80;  // VBIAS on, all else default
    if (_wiring == MAX31865_3WIRE) cfg |= (1 << 4);
    _writeRegister8(0x80, cfg);

    // Read config back to verify communication
    uint8_t cfgRead = _readRegister8(0x00);
    if ((cfgRead & 0x80) == 0) {
        Serial.printf("[MAX31865] Config readback failed (got 0x%02X), CS=GPIO%u\n", cfgRead, csPin);
        return false;
    }

    _initialized = true;
    Serial.printf("[MAX31865] Ready: CS=GPIO%u type=%s wiring=%s\n",
                  csPin,
                  (type == MAX31865_PT1000) ? "PT1000" : "PT100",
                  (wiring == MAX31865_3WIRE) ? "3-wire" : "2/4-wire");
    return true;
}

// ====================================================================================
// One-shot temperature read
// ====================================================================================

float MAX31865Sensor::readTemperature() {
    if (!_initialized) return NAN;

    // Trigger a one-shot conversion by setting bit 5 of the config register.
    // VBIAS must already be on (bit 7 = 1).
    uint8_t cfg = _readRegister8(0x00) | (1 << 5);
    _writeRegister8(0x80, cfg);

    // Wait for conversion: 65ms at 60Hz filter, 83ms at 50Hz filter
    delay(70);

    // Read the 15-bit RTD value (MSB of the 16-bit register is the fault bit)
    uint16_t raw = _readRegister16(0x01);

    if (raw & 0x01) {
        // Fault detected — read fault register and report
        uint8_t fault = readFaultStatus();
        Serial.printf("[MAX31865] Fault detected: 0x%02X\n", fault);
        clearFault();
        return NAN;
    }

    uint16_t adcCode = raw >> 1;  // Strip the fault bit
    _lastTemp = rawToTemperature(adcCode);
    return _lastTemp;
}

// ====================================================================================
// Raw 15-bit read
// ====================================================================================

uint16_t MAX31865Sensor::readRaw() {
    if (!_initialized) return 0;
    uint8_t cfg = _readRegister8(0x00) | (1 << 5);
    _writeRegister8(0x80, cfg);
    delay(70);
    uint16_t raw = _readRegister16(0x01);
    return raw >> 1;  // 15-bit ADC value
}

// ====================================================================================
// Callendar-Van Dusen temperature conversion
// ====================================================================================

float MAX31865Sensor::rawToTemperature(uint16_t raw) const {
    // Convert ADC code to resistance ratio
    // R_rtd = raw * Rref / 32768
    float rRtd = ((float)raw * _rRef()) / 32768.0f;
    float rNom = _rNom();

    // Callendar-Van Dusen for T >= 0°C:
    //   R = R0 * (1 + A*T + B*T^2)
    //   0 = B*T^2 + A*T + (1 - R/R0)
    //   T = (-A + sqrt(A^2 - 4B*(1 - R/R0))) / (2B)
    float z = (rRtd / rNom) - 1.0f;
    // Discriminant
    float disc = (CVD_A * CVD_A) - (4.0f * CVD_B * (-z));
    if (disc < 0.0f) return NAN;

    float temp = (-CVD_A + sqrtf(disc)) / (2.0f * CVD_B);

    // For temperatures below 0°C, use a polynomial approximation
    // (full sub-zero CVD requires solving a cubic — this linear correction is typical)
    if (temp < 0.0f) {
        // Newton-Raphson refinement for sub-zero range (one iteration)
        float t0 = temp;
        float r0 = rNom * (1.0f + CVD_A * t0 + CVD_B * t0 * t0
                           - 1.5e-9f * (t0 - 100.0f) * t0 * t0 * t0);  // CVD cubic term
        float dr = rRtd - r0;
        float dRdT = rNom * (CVD_A + 2.0f * CVD_B * t0);
        temp = t0 + dr / dRdT;
    }

    return temp;
}

// ====================================================================================
// Fault handling
// ====================================================================================

uint8_t MAX31865Sensor::readFaultStatus() {
    return _readRegister8(0x07);
}

void MAX31865Sensor::clearFault() {
    uint8_t cfg = _readRegister8(0x00);
    cfg &= ~(1 << 2);  // Ensure no fault detection cycle is running
    cfg |=  (1 << 1);  // Fault status clear bit
    _writeRegister8(0x80, cfg);
}

// ====================================================================================
// Low-level SPI register access
// ====================================================================================

uint8_t MAX31865Sensor::_readRegister8(uint8_t reg) {
    _spi->beginTransaction(_spiSettings);
    _csLow();
    _spi->transfer(reg & 0x7F);  // Read: MSB = 0
    uint8_t val = _spi->transfer(0x00);
    _csHigh();
    _spi->endTransaction();
    return val;
}

uint16_t MAX31865Sensor::_readRegister16(uint8_t reg) {
    _spi->beginTransaction(_spiSettings);
    _csLow();
    _spi->transfer(reg & 0x7F);
    uint16_t hi = _spi->transfer(0x00);
    uint16_t lo = _spi->transfer(0x00);
    _csHigh();
    _spi->endTransaction();
    return (hi << 8) | lo;
}

void MAX31865Sensor::_writeRegister8(uint8_t reg, uint8_t value) {
    _spi->beginTransaction(_spiSettings);
    _csLow();
    _spi->transfer(reg | 0x80);  // Write: MSB = 1
    _spi->transfer(value);
    _csHigh();
    _spi->endTransaction();
}

#endif // ENABLE_MAX31865==1
