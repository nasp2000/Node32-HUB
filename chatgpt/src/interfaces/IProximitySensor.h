#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

struct ProximityZone {
    uint8_t sensorId;
    float distanceMm;
    bool triggered;
    uint32_t lastTriggeredMs;
};

struct ProximityConfig {
    uint8_t sensorCount;
    uint16_t triggerThresholdMm;
    uint16_t releaseThresholdMm;
    uint32_t debounceMs;
    bool enableCounting;
    bool enableAlert;
};

struct ShiftEntry {
    uint32_t id;
    String operatorName;
    uint32_t startTime;
    uint32_t endTime;
    uint32_t partCount;
};

class IProximitySensor {
public:
    virtual ~IProximitySensor() = default;

    virtual bool init(const ProximityConfig& config) = 0;
    virtual void loop() = 0;

    virtual bool calibrate() = 0;
    virtual float readDistance(uint8_t sensorId) = 0;
    virtual bool isTriggered(uint8_t sensorId) = 0;

    virtual bool getZones(std::vector<ProximityZone>& out) = 0;
    virtual size_t zoneCount() const = 0;

    virtual uint32_t getPartCount(uint8_t sensorId) const = 0;
    virtual void resetPartCount(uint8_t sensorId) = 0;

    virtual bool startShift(const String& operatorName) = 0;
    virtual bool endShift() = 0;
    virtual bool getCurrentShift(ShiftEntry& out) const = 0;
    virtual bool getShiftHistory(std::vector<ShiftEntry>& out) const = 0;

    virtual bool setThresholds(uint16_t triggerMm, uint16_t releaseMm) = 0;
    virtual bool setDigitalOutput(bool enable, uint8_t pin = 255) = 0;

    virtual bool isReady() const = 0;
    virtual ProximityConfig getConfig() const = 0;

    virtual void setTriggerCallback(void (*cb)(uint8_t sensorId, uint32_t count)) = 0;
};
