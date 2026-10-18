#ifndef SENSOR_AHT20_BMP280_H
#define SENSOR_AHT20_BMP280_H

#include "../feature_flags.h"

#if ENABLE_AHT20_BMP280==1

#include <Arduino.h>
#include <Wire.h>

// ====================================================================================
// AHT20 + BMP280 Environmental Sensor Module Configuration
// ====================================================================================
// This module combines two I2C sensors on a single PCB:
// - AHT20: High-precision temperature and humidity sensor (I2C addr: 0x38)
// - BMP280: Barometric pressure and temperature sensor (I2C addr: 0x76 or 0x77)
//
// Both sensors share the same I2C bus. This module can use either:
// 1. Primary I2C bus (Wire) - shared with OLED/LCD (GPIO21=SDA, GPIO48=SCL)
// 2. Secondary I2C bus (Wire1) - dedicated pins for sensors
//
// Choose configuration based on your hardware setup and pin availability.

// I2C Bus Selection
// ------------------
// Set to 1 to use secondary I2C bus (Wire1), 0 to use primary bus (Wire)
#ifndef SENSOR_USE_I2C1
#define SENSOR_USE_I2C1 1  // Default: use secondary I2C bus to avoid conflicts
#endif

// I2C Pin Configuration
// ---------------------
// Primary I2C bus (Wire) - shared with OLED
// Note: These are the same pins used by screen.h
#ifndef SENSOR_I2C0_SDA
#define SENSOR_I2C0_SDA 21   // GPIO21 - SDA (shared with OLED)
#endif
#ifndef SENSOR_I2C0_SCL
#define SENSOR_I2C0_SCL 48   // GPIO48 - SCL (shared with OLED)
#endif

// Secondary I2C bus (Wire1) - dedicated for sensors
// You can change these pins based on your ESP32-S3 board pinout
// Recommended: use available GPIOs that don't conflict with other peripherals
#ifndef SENSOR_I2C1_SDA
#define SENSOR_I2C1_SDA 8    // GPIO8 - SDA for secondary I2C
#endif

#ifndef SENSOR_I2C1_SCL
#define SENSOR_I2C1_SCL 9    // GPIO9 - SCL for secondary I2C
#endif

// I2C Addresses
// -------------
// AHT20: Fixed address (cannot be changed)
#define AHT20_I2C_ADDR 0x38

// BMP280: Can be 0x76 or 0x77 depending on SDO pin connection
// - SDO to GND: 0x76 (default on most modules)
// - SDO to VCC: 0x77
#ifndef BMP280_I2C_ADDR
#define BMP280_I2C_ADDR 0x76  // Default address
#endif

// I2C Clock Speed
// ---------------
#define SENSOR_I2C_FREQ 100000  // 100kHz (standard mode) - safe for both sensors
// Note: Both AHT20 and BMP280 support up to 400kHz (fast mode) if needed

// Sensor Configuration
// --------------------
#define AHT20_CALIBRATION_TIME_MS 40    // Time to wait for AHT20 calibration
#define AHT20_MEASUREMENT_TIME_MS 80    // Time for AHT20 to complete measurement
#define BMP280_INIT_TIME_MS 100         // Time for BMP280 initialization

// Reading intervals (adjust based on application needs)
#define SENSOR_UPDATE_INTERVAL_MS 60000 // Read sensors every 60 seconds (default)
#define SENSOR_MIN_READ_INTERVAL_MS 2000 // Minimum 2 seconds between reads (sensor limitation)

// Sensor calibration/validation ranges (sanity checks)
#define TEMP_MIN_VALID -40.0   // °C
#define TEMP_MAX_VALID 85.0    // °C
#define HUMIDITY_MIN_VALID 0.0    // %RH
#define HUMIDITY_MAX_VALID 100.0  // %RH
#define PRESSURE_MIN_VALID 300.0  // hPa (high altitude)
#define PRESSURE_MAX_VALID 1100.0 // hPa (sea level + margin)

// ====================================================================================
// Data Structures
// ====================================================================================

// Sensor reading structure
struct SensorReading {
    // AHT20 readings
    float temperature_aht20;  // Temperature from AHT20 (°C)
    float humidity;           // Relative humidity from AHT20 (%RH)
    
    // BMP280 readings
    float temperature_bmp280; // Temperature from BMP280 (°C)
    float pressure;           // Atmospheric pressure from BMP280 (hPa)
    
    // Reading metadata
    unsigned long timestamp;  // millis() when reading was taken
    bool valid;               // true if reading is valid
    
    // Constructor with default invalid state
    SensorReading() : 
        temperature_aht20(0.0f), 
        humidity(0.0f),
        temperature_bmp280(0.0f),
        pressure(0.0f),
        timestamp(0),
        valid(false) {}
};

// ====================================================================================
// Sensor Manager Class
// ====================================================================================

class SensorAHT20_BMP280 {
private:
    TwoWire* wire;              // I2C bus instance (Wire or Wire1)
    bool initialized;           // Initialization state
    bool aht20_available;       // AHT20 detected and responding
    bool bmp280_available;      // BMP280 detected and responding
    
    SensorReading lastReading;  // Last valid sensor reading
    unsigned long lastReadTime; // Last time sensors were read
    
    // Internal helper functions
    bool detectAHT20();
    bool detectBMP280();
    bool initAHT20();
    bool initBMP280();
    bool readAHT20(float &temp, float &humidity);
    bool readBMP280(float &temp, float &pressure);
    bool validateReading(const SensorReading &reading);
    
public:
    // Constructor
    SensorAHT20_BMP280();
    
    // Initialization
    bool begin();  // Initialize I2C and detect sensors
    bool begin(TwoWire* customWire);  // Initialize with custom I2C bus
    
    // Status
    bool isInitialized() const { return initialized; }
    bool isAHT20Available() const { return aht20_available; }
    bool isBMP280Available() const { return bmp280_available; }
    bool isAnyAvailable() const { return aht20_available || bmp280_available; }
    
    // Reading functions
    bool update();  // Read sensors (respects minimum interval)
    bool forceUpdate();  // Force immediate sensor read (ignores interval)
    SensorReading getLastReading() const { return lastReading; }
    unsigned long getLastReadTime() const { return lastReadTime; }
    
    // Individual sensor reads (for advanced usage)
    bool readTemperatureHumidity(float &temp, float &humidity);
    bool readPressure(float &temp, float &pressure);
    
    // Utility functions
    float computeAverageTemperature() const;  // Average of both temperature sensors
    float computeDewPoint() const;             // Calculate dew point from temp + humidity
    float computeHeatIndex() const;            // Calculate heat index (feels like)
    String getStatusString() const;            // Human-readable status
    
    // Reset
    void reset();  // Re-initialize sensors
};

// ====================================================================================
// Global Instance (optional - define in .cpp if using global)
// ====================================================================================
// Uncomment in .cpp file if you want a global instance:
// extern SensorAHT20_BMP280 environmentalSensor;

#endif // ENABLE_AHT20_BMP280==1
#endif // SENSOR_AHT20_BMP280_H
