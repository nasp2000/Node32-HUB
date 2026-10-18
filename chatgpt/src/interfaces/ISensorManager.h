#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class SensorType : uint8_t {
    None = 0,
    AHT20_BMP280 = 1,
    MQ135 = 2,
    HX711 = 3,
    INA226 = 4,
    Sound = 5,
    MAX31865 = 6,
    Proximity = 7
};

struct SensorReading {
    SensorType type;
    float value;
    String unit;
    uint32_t timestamp;
    bool valid;
};

struct SensorConfig {
    SensorType type;
    uint32_t pollIntervalMs = 5000;
    bool enabled = true;
    String label;
    float thresholdMin = 0;
    float thresholdMax = 0;
    bool sendAlert = false;
};

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual bool init() = 0;
    virtual void loop() = 0;
    virtual SensorReading read() = 0;
    virtual bool calibrate() = 0;
    virtual bool isReady() const = 0;
    virtual SensorType type() const = 0;
    virtual String name() const = 0;
};

class ISensorManager {
public:
    virtual ~ISensorManager() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool registerSensor(ISensor* sensor) = 0;
    virtual bool unregisterSensor(SensorType type) = 0;

    virtual SensorReading read(SensorType type) = 0;
    virtual bool readAll(std::vector<SensorReading>& out) = 0;

    virtual ISensor* getSensor(SensorType type) = 0;
    virtual size_t sensorCount() const = 0;

    virtual bool startLogging(SensorType type, uint32_t intervalMs) = 0;
    virtual bool stopLogging(SensorType type) = 0;
    virtual bool isLogging(SensorType type) const = 0;

    virtual bool setAlertCallback(void (*cb)(SensorType, float, float)) = 0;
    virtual bool setThreshold(SensorType type, float min, float max) = 0;
};
