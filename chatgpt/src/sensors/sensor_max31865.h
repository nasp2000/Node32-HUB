#ifndef SENSOR_MAX31865_H
#define SENSOR_MAX31865_H

#include "../feature_flags.h"

#if ENABLE_MAX31865==1

#include <Arduino.h>
#include <SPI.h>

// ====================================================================================
// MAX31865 RTD-to-Digital Converter — SPI driver (no external library)
// ====================================================================================
// The MAX31865 converts RTD (PT100 or PT1000) resistance to temperature.
// It uses SPI (mode 1 or 3) with a dedicated CS pin. The MOSI/MISO/CLK
// can share a bus with other SPI devices.
//
// Electrical connection:
//   CS   -> dedicated GPIO (do not share)
//   MOSI -> SPI MOSI
//   MISO -> SPI MISO
//   CLK  -> SPI CLK
//   RTD+ / RTD- / FORCE+ / FORCE- -> per RTD wiring (2/3/4-wire)
//
// Supported RTD types:
//   PT100  (R at 0°C = 100Ω,  reference resistor typically 430Ω)
//   PT1000 (R at 0°C = 1000Ω, reference resistor typically 4300Ω)
//
// Conversion formula (Callendar-Van Dusen):
//   For T >= 0°C: T = (-A + sqrt(A^2 - 4B*(1 - R/R0))) / (2B)
//   For T < 0°C:  Iterative Newton-Raphson or polynomial approximation
//   where A = 3.9083e-3, B = -5.775e-7
//
// Registers:
//   0x00 — Configuration (read)
//   0x80 — Configuration (write)
//   0x01/0x02 — RTD MSB/LSB (D15:D1 = 15-bit result, D0 = fault bit)
//   0x07 — Fault status

// Default SPI chip-select pin (board-specific override via board_pins.h)
#ifndef SENSOR_MAX31865_CS_PIN
  #define SENSOR_MAX31865_CS_PIN   5    // GPIO5 safe default
#endif

// SPI bus pins — default to VSPI bus (ESP32); override for board-specific buses
#ifndef SENSOR_MAX31865_MOSI_PIN
  #define SENSOR_MAX31865_MOSI_PIN  23
#endif
#ifndef SENSOR_MAX31865_MISO_PIN
  #define SENSOR_MAX31865_MISO_PIN  19
#endif
#ifndef SENSOR_MAX31865_CLK_PIN
  #define SENSOR_MAX31865_CLK_PIN   18
#endif

// SPI bus speed (MAX31865 max = 5 MHz)
#define MAX31865_SPI_FREQ   4000000UL

// RTD type selector
enum MAX31865RTDType : uint8_t {
    MAX31865_PT100  = 0,   // R0 = 100Ω, Rref = 430Ω
    MAX31865_PT1000 = 1    // R0 = 1000Ω, Rref = 4300Ω
};

// Wiring configuration
enum MAX31865Wiring : uint8_t {
    MAX31865_2WIRE = 0,
    MAX31865_3WIRE = 1,
    MAX31865_4WIRE = 0   // Same config register bit as 2-wire
};

// ====================================================================================
// MAX31865 driver class
// ====================================================================================
class MAX31865Sensor {
public:
    MAX31865Sensor();

    // Initialize the SPI bus and configure the MAX31865.
    // Returns true if the configuration register reads back correctly.
    bool begin(uint8_t csPin         = SENSOR_MAX31865_CS_PIN,
               MAX31865RTDType type  = MAX31865_PT100,
               MAX31865Wiring wiring = MAX31865_2WIRE,
               SPIClass* spi         = &SPI);

    // Trigger a one-shot conversion and wait for it to complete (~65ms).
    // Returns temperature in °C, or NAN on fault.
    float readTemperature();

    // Read the raw 15-bit ADC value (RTD resistance ratio * 2^15 / Rref).
    uint16_t readRaw();

    // Convert a raw 15-bit value to temperature (°C).
    float rawToTemperature(uint16_t raw) const;

    // Read and return the fault status register (0 = no fault).
    uint8_t readFaultStatus();

    // Clear fault condition in the configuration register.
    void clearFault();

    bool isInitialized() const { return _initialized; }
    float getLastTemperature() const { return _lastTemp; }
    MAX31865RTDType getRTDType() const { return _type; }

private:
    SPIClass*       _spi;
    uint8_t         _csPin;
    MAX31865RTDType _type;
    MAX31865Wiring  _wiring;
    bool            _initialized;
    float           _lastTemp;

    // Reference resistor and RTD nominal resistance per type
    float _rRef() const  { return (_type == MAX31865_PT1000) ? 4300.0f : 430.0f; }
    float _rNom() const  { return (_type == MAX31865_PT1000) ? 1000.0f : 100.0f; }

    // SPI helpers
    uint8_t  _readRegister8(uint8_t reg);
    uint16_t _readRegister16(uint8_t reg);
    void     _writeRegister8(uint8_t reg, uint8_t value);

    void _csLow()  { digitalWrite(_csPin, LOW); }
    void _csHigh() { digitalWrite(_csPin, HIGH); }

    SPISettings _spiSettings;
};

extern MAX31865Sensor max31865Sensor;

#endif // ENABLE_MAX31865==1
#endif // SENSOR_MAX31865_H
