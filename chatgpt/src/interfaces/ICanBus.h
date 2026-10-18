#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

struct CanFrame {
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
    bool isExtended;
    bool isRtr;
    uint32_t timestamp;
};

struct CanConfig {
    int txPin = 5;
    int rxPin = 4;
    unsigned long baudRate = 500000;
    bool loopback = false;
    bool silentMode = false;
    uint16_t rxQueueSize = 64;
    uint16_t txQueueSize = 32;
};

class ICanBus {
public:
    virtual ~ICanBus() = default;

    virtual bool init(const CanConfig& config) = 0;
    virtual void loop() = 0;

    virtual bool send(const CanFrame& frame) = 0;
    virtual bool send(uint32_t id, const uint8_t* data, uint8_t dlc, bool extended = false) = 0;

    virtual bool receive(CanFrame& frame, uint32_t timeoutMs = 0) = 0;
    virtual size_t available() const = 0;
    virtual void flushRx() = 0;
    virtual void flushTx() = 0;

    virtual bool setFilter(uint32_t id, uint32_t mask, bool extended = false) = 0;
    virtual bool clearFilter() = 0;

    virtual bool setMode(bool loopback, bool silent) = 0;
    virtual bool setBaudRate(unsigned long baud) = 0;

    virtual uint32_t errorCount() const = 0;
    virtual bool isBusOff() const = 0;
    virtual void recover() = 0;

    virtual bool isReady() const = 0;
    virtual bool isEnabled() const = 0;
    virtual CanConfig getConfig() const = 0;

    virtual void setReceiveCallback(void (*cb)(const CanFrame&)) = 0;
};
