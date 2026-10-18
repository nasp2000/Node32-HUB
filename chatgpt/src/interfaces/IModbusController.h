#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

enum class ModbusMedium : uint8_t {
    RTU = 0,
    TCP = 1,
    ASCII = 2
};

struct ModbusDeviceConfig {
    uint8_t slaveId;
    uint16_t registerAddress;
    uint16_t registerCount;
    uint32_t pollIntervalMs;
    const char* label;
    ModbusMedium medium;
};

struct ModbusRegister {
    uint16_t address;
    uint16_t value;
    uint32_t lastUpdated;
    bool valid;
};

class IModbusController {
public:
    virtual ~IModbusController() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool addDevice(const ModbusDeviceConfig& config) = 0;
    virtual bool removeDevice(uint8_t slaveId, uint16_t address) = 0;
    virtual void clearDevices() = 0;

    virtual bool readHoldingRegisters(uint8_t slaveId, uint16_t addr, uint16_t count, uint16_t* out) = 0;
    virtual bool readInputRegisters(uint8_t slaveId, uint16_t addr, uint16_t count, uint16_t* out) = 0;
    virtual bool writeSingleRegister(uint8_t slaveId, uint16_t addr, uint16_t value) = 0;
    virtual bool writeMultipleRegisters(uint8_t slaveId, uint16_t addr, const uint16_t* values, uint16_t count) = 0;

    virtual bool readCoils(uint8_t slaveId, uint16_t addr, uint16_t count, bool* out) = 0;
    virtual bool readDiscreteInputs(uint8_t slaveId, uint16_t addr, uint16_t count, bool* out) = 0;
    virtual bool writeSingleCoil(uint8_t slaveId, uint16_t addr, bool value) = 0;

    virtual uint32_t getLastPollTime(uint8_t slaveId, uint16_t addr) const = 0;
    virtual uint32_t getErrorCount(uint8_t slaveId, uint16_t addr) const = 0;
    virtual void resetErrorCount(uint8_t slaveId, uint16_t addr) = 0;

    virtual size_t deviceCount() const = 0;
    virtual const ModbusDeviceConfig* getDevice(size_t index) const = 0;

    virtual bool isSlaveEnabled() const = 0;
    virtual void setSlaveId(uint8_t id) = 0;
    virtual uint8_t getSlaveId() const = 0;
};

class IModbusRtuTransport {
public:
    virtual ~IModbusRtuTransport() = default;
    virtual bool begin(int rxPin, int txPin, int dePin, unsigned long baud) = 0;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    virtual int receive(uint8_t* buffer, size_t maxLen, uint32_t timeoutMs) = 0;
    virtual void flush() = 0;
};

class IModbusTcpTransport {
public:
    virtual ~IModbusTcpTransport() = default;
    virtual bool begin(uint16_t port) = 0;
    virtual void loop() = 0;
    virtual bool send(uint8_t slaveId, const uint8_t* data, size_t len) = 0;
    virtual size_t activeConnections() const = 0;
};
