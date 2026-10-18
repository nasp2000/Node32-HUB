#include "sensor_aht20_bmp280.h"

#include "feature_flags.h"
#if ENABLE_AHT20_BMP280==1

#include <Adafruit_AHTX0.h>  // AHT20 sensor library
#include <Adafruit_BMP280.h>  // BMP280 sensor library
#include <math.h>             // For dew point and heat index calculations
#include "feature_flags.h"
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "touch7_i2c.h"       // bus-free guard (GT911 can hold SDA pre-reset)
#endif

// ====================================================================================
// Static sensor instances (persistent across calls)
// ====================================================================================
static Adafruit_AHTX0* aht20 = nullptr;
static Adafruit_BMP280* bmp280 = nullptr;

// ====================================================================================
// SensorAHT20_BMP280 Implementation
// ====================================================================================

SensorAHT20_BMP280::SensorAHT20_BMP280() :
    wire(nullptr),
    initialized(false),
    aht20_available(false),
    bmp280_available(false),
    lastReadTime(0)
{
}

bool SensorAHT20_BMP280::begin() {
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // T7-24c: local I2C sensors are UNSUPPORTED on Touch-LCD-7. Wire1
    // (port 1) shares pins 8/9 with the panel's I2C0 (GT911 + CH422G, polled
    // at ~30 Hz); the two controllers have no mutual arbitration and ANY
    // live Wire1 transaction can deadlock both to the 120 s task watchdog
    // (proven 17-09-2026: boot-time probe hang AND 60 s retry hang, both
    // TWDT-aborted). Display data comes from network APIs (weather/peers).
    // Always report absent so no code path ever transacts on the shared bus.
    Serial.println("[SENSOR] local I2C disabled on TOUCH7 (shared bus)");
    initialized = false;
    return false;
#endif
    // Use default I2C bus based on SENSOR_USE_I2C1 flag.
    // Touch-LCD-7: Wire1 is FORCED (display stack owns hardware port 0 with
    // the legacy driver; Arduino Wire on port 0 would abort the chip).
#if (SENSOR_USE_I2C1==1) || (defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1)
    // Use secondary I2C bus (Wire1), claimed lazily post-panel (T7-24).
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    touch7Wire1Begin(Wire1);
#else
    Wire1.begin(SENSOR_I2C1_SDA, SENSOR_I2C1_SCL, SENSOR_I2C_FREQ);
#endif
    wire = &Wire1;
    Serial.printf("[SENSOR] Using I2C1 (SDA=%d, SCL=%d)\n", SENSOR_I2C1_SDA, SENSOR_I2C1_SCL);
#else
    // Use primary I2C bus (Wire)
    Wire.begin(SENSOR_I2C0_SDA, SENSOR_I2C0_SCL, SENSOR_I2C_FREQ);
    wire = &Wire;
    Serial.printf("[SENSOR] Using I2C0 (SDA=%d, SCL=%d)\n", SENSOR_I2C0_SDA, SENSOR_I2C0_SCL);
    #endif
    
    delay(100); // Wait for I2C bus to stabilize
    
    // Allocate sensor instances with the correct I2C bus
    if (!aht20) {
        aht20 = new Adafruit_AHTX0();
    }
    if (!bmp280) {
        bmp280 = new Adafruit_BMP280(wire);  // Pass TwoWire* to constructor
    }
    
    // Detect and initialize AHT20
    aht20_available = detectAHT20();
    if (aht20_available) {
        aht20_available = initAHT20();
    }
    
    // Detect and initialize BMP280
    bmp280_available = detectBMP280();
    if (bmp280_available) {
        bmp280_available = initBMP280();
    }
    
    initialized = (aht20_available || bmp280_available);
    
    if (initialized) {
        Serial.print("[SENSOR] Initialization complete - AHT20:");
        Serial.print(aht20_available ? "OK" : "FAIL");
        Serial.print(" BMP280:");
        Serial.println(bmp280_available ? "OK" : "FAIL");
    } else {
        Serial.println("[SENSOR] ERROR: No sensors detected!");
    }
    
    return initialized;
}

bool SensorAHT20_BMP280::begin(TwoWire* customWire) {
    wire = customWire;
    return begin();
}

bool SensorAHT20_BMP280::detectAHT20() {
    if (!wire || !aht20) return false;
    
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // Probe first: Adafruit's begin() spins forever on 0xFF status when
    // nothing answers. Single cheap transaction instead.
    if (!touch7I2CProbe(*wire, 0x38)) {
        Serial.println("[SENSOR] AHT20 not detected at 0x38 (probe)");
        return false;
    }
#endif
    // Try to begin AHT20 communication
    if (!aht20->begin(wire)) {
        Serial.println("[SENSOR] AHT20 not detected at 0x38");
        return false;
    }
    
    Serial.println("[SENSOR] AHT20 detected at 0x38");
    return true;
}

bool SensorAHT20_BMP280::detectBMP280() {
    if (!wire || !bmp280) return false;
    
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    if (!touch7I2CProbe(*wire, BMP280_I2C_ADDR) && !touch7I2CProbe(*wire, 0x77)) {
        Serial.println("[SENSOR] BMP280 not detected (probe 0x76/0x77)");
        return false;
    }
#endif
    // Try primary address (0x76)
    if (bmp280->begin(BMP280_I2C_ADDR)) {
        Serial.printf("[SENSOR] BMP280 detected at 0x%02X\n", BMP280_I2C_ADDR);
        return true;
    }
    
    // Try alternate address (0x77)
    if (bmp280->begin(0x77)) {
        Serial.println("[SENSOR] BMP280 detected at 0x77");
        return true;
    }
    
    Serial.println("[SENSOR] BMP280 not detected (tried 0x76 and 0x77)");
    return false;
}

bool SensorAHT20_BMP280::initAHT20() {
    // AHT20 auto-calibrates on begin()
    delay(AHT20_CALIBRATION_TIME_MS);
    Serial.println("[SENSOR] AHT20 calibrated and ready");
    return true;
}

bool SensorAHT20_BMP280::initBMP280() {
    if (!bmp280) return false;
    
    // Configure BMP280 for optimal performance
    bmp280->setSampling(Adafruit_BMP280::MODE_NORMAL,     // Operating mode
                       Adafruit_BMP280::SAMPLING_X2,      // Temperature oversampling
                       Adafruit_BMP280::SAMPLING_X16,     // Pressure oversampling
                       Adafruit_BMP280::FILTER_X16,       // Filtering
                       Adafruit_BMP280::STANDBY_MS_500);  // Standby time
    
    delay(BMP280_INIT_TIME_MS);
    Serial.println("[SENSOR] BMP280 configured and ready");
    return true;
}

bool SensorAHT20_BMP280::readAHT20(float &temp, float &humidity) {
    if (!aht20_available || !aht20) return false;
    
    sensors_event_t humidity_event, temp_event;
    
    if (!aht20->getEvent(&humidity_event, &temp_event)) {
        Serial.println("[SENSOR] AHT20 read failed");
        return false;
    }
    
    temp = temp_event.temperature;
    humidity = humidity_event.relative_humidity;
    
    return true;
}

bool SensorAHT20_BMP280::readBMP280(float &temp, float &pressure) {
    if (!bmp280_available || !bmp280) return false;
    
    temp = bmp280->readTemperature();
    pressure = bmp280->readPressure() / 100.0F; // Convert Pa to hPa
    
    // Check for read errors (BMP280 returns specific error values)
    if (isnan(temp) || isnan(pressure)) {
        Serial.println("[SENSOR] BMP280 read failed");
        return false;
    }
    
    return true;
}

bool SensorAHT20_BMP280::validateReading(const SensorReading &reading) {
    // Validate temperature from AHT20
    if (aht20_available) {
        if (reading.temperature_aht20 < TEMP_MIN_VALID || 
            reading.temperature_aht20 > TEMP_MAX_VALID) {
            Serial.printf("[SENSOR] Invalid AHT20 temp: %.1f°C\n", reading.temperature_aht20);
            return false;
        }
        
        if (reading.humidity < HUMIDITY_MIN_VALID || 
            reading.humidity > HUMIDITY_MAX_VALID) {
            Serial.printf("[SENSOR] Invalid humidity: %.1f%%\n", reading.humidity);
            return false;
        }
    }
    
    // Validate temperature and pressure from BMP280
    if (bmp280_available) {
        if (reading.temperature_bmp280 < TEMP_MIN_VALID || 
            reading.temperature_bmp280 > TEMP_MAX_VALID) {
            Serial.printf("[SENSOR] Invalid BMP280 temp: %.1f°C\n", reading.temperature_bmp280);
            return false;
        }
        
        if (reading.pressure < PRESSURE_MIN_VALID || 
            reading.pressure > PRESSURE_MAX_VALID) {
            Serial.printf("[SENSOR] Invalid pressure: %.1fhPa\n", reading.pressure);
            return false;
        }
    }
    
    return true;
}

bool SensorAHT20_BMP280::update() {
    if (!initialized) return false;
    
    // Check minimum read interval
    unsigned long now = millis();
    if (lastReadTime > 0 && (now - lastReadTime) < SENSOR_MIN_READ_INTERVAL_MS) {
        return false; // Too soon, skip this update
    }
    
    return forceUpdate();
}

bool SensorAHT20_BMP280::forceUpdate() {
    if (!initialized) return false;
    
    SensorReading newReading;
    newReading.timestamp = millis();
    
    // Read AHT20 (temperature + humidity)
    if (aht20_available) {
        if (!readAHT20(newReading.temperature_aht20, newReading.humidity)) {
            Serial.println("[SENSOR] AHT20 read error");
            aht20_available = false; // Mark as unavailable
        }
    }
    
    // Small delay between sensor reads
    delay(50);
    
    // Read BMP280 (temperature + pressure)
    if (bmp280_available) {
        if (!readBMP280(newReading.temperature_bmp280, newReading.pressure)) {
            Serial.println("[SENSOR] BMP280 read error");
            bmp280_available = false; // Mark as unavailable
        }
    }
    
    // Validate reading
    newReading.valid = validateReading(newReading);
    
    if (newReading.valid) {
        lastReading = newReading;
        lastReadTime = newReading.timestamp;
        return true;
    }
    
    Serial.println("[SENSOR] Reading validation failed");
    return false;
}

bool SensorAHT20_BMP280::readTemperatureHumidity(float &temp, float &humidity) {
    if (!aht20_available) return false;
    return readAHT20(temp, humidity);
}

bool SensorAHT20_BMP280::readPressure(float &temp, float &pressure) {
    if (!bmp280_available) return false;
    return readBMP280(temp, pressure);
}

float SensorAHT20_BMP280::computeAverageTemperature() const {
    if (!lastReading.valid) return 0.0f;
    
    if (aht20_available && bmp280_available) {
        // Average both sensors
        return (lastReading.temperature_aht20 + lastReading.temperature_bmp280) / 2.0f;
    } else if (aht20_available) {
        return lastReading.temperature_aht20;
    } else if (bmp280_available) {
        return lastReading.temperature_bmp280;
    }
    
    return 0.0f;
}

float SensorAHT20_BMP280::computeDewPoint() const {
    if (!lastReading.valid || !aht20_available) return 0.0f;
    
    // Magnus formula for dew point calculation
    // T_d = (b * α(T,RH)) / (a - α(T,RH))
    // where α(T,RH) = (a*T)/(b+T) + ln(RH/100)
    // Constants: a = 17.27, b = 237.7°C
    
    float temp = lastReading.temperature_aht20;
    float humidity = lastReading.humidity;
    
    const float a = 17.27f;
    const float b = 237.7f;
    
    float alpha = ((a * temp) / (b + temp)) + log(humidity / 100.0f);
    float dewPoint = (b * alpha) / (a - alpha);
    
    return dewPoint;
}

float SensorAHT20_BMP280::computeHeatIndex() const {
    if (!lastReading.valid || !aht20_available) return 0.0f;
    
    // Heat index calculation (simplified Rothfusz regression)
    // Only valid for temperatures above 27°C (80°F)
    float temp = lastReading.temperature_aht20;
    float humidity = lastReading.humidity;
    
    if (temp < 27.0f) {
        return temp; // Heat index not applicable
    }
    
    // Convert to Fahrenheit for calculation
    float tempF = temp * 9.0f / 5.0f + 32.0f;
    float rh = humidity;
    
    // Rothfusz regression coefficients
    float hi = -42.379f
             + 2.04901523f * tempF
             + 10.14333127f * rh
             - 0.22475541f * tempF * rh
             - 6.83783e-3f * tempF * tempF
             - 5.481717e-2f * rh * rh
             + 1.22874e-3f * tempF * tempF * rh
             + 8.5282e-4f * tempF * rh * rh
             - 1.99e-6f * tempF * tempF * rh * rh;
    
    // Convert back to Celsius
    float heatIndexC = (hi - 32.0f) * 5.0f / 9.0f;
    
    return heatIndexC;
}

String SensorAHT20_BMP280::getStatusString() const {
    String status = "[SENSOR] Status: ";
    
    if (!initialized) {
        status += "NOT INITIALIZED";
        return status;
    }
    
    status += "AHT20=";
    status += aht20_available ? "OK" : "FAIL";
    status += " BMP280=";
    status += bmp280_available ? "OK" : "FAIL";
    
    if (lastReading.valid) {
        status += " | Last read: ";
        status += String((millis() - lastReadTime) / 1000);
        status += "s ago";
    } else {
        status += " | No valid readings";
    }
    
    return status;
}

void SensorAHT20_BMP280::reset() {
    Serial.println("[SENSOR] Resetting sensors...");
    
    initialized = false;
    aht20_available = false;
    bmp280_available = false;
    lastReading.valid = false;
    
    delay(100);
    begin();
}

#endif // ENABLE_AHT20_BMP280==1
