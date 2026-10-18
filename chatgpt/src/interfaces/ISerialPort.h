#pragma once
#include <Arduino.h>
#include <stdint.h>

struct SerialConfig {
    unsigned long baudRate = 115200;
    uint8_t dataBits = 8;
    uint8_t stopBits = 1;
    char parity = 'N';
    uint8_t rxPin = 3;
    uint8_t txPin = 1;
    uint8_t rtsPin = 255;
    uint8_t ctsPin = 255;
    bool useHardwareFlowControl = false;
    uint16_t bufferSize = 2048;
};

class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    virtual bool begin(const SerialConfig& config) = 0;
    virtual void end() = 0;
    virtual void loop() = 0;

    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t write(char c) = 0;
    virtual size_t write(const String& str) = 0;

    virtual int read() = 0;
    virtual size_t readBytes(uint8_t* buffer, size_t length) = 0;
    virtual String readStringUntil(char terminator) = 0;

    virtual int available() const = 0;
    virtual int availableForWrite() const = 0;
    virtual void flush() = 0;

    virtual bool isEnabled() const = 0;
    virtual unsigned long baudRate() const = 0;

    virtual void setDebugOutput(bool enable) = 0;
    virtual void setLineCallback(void (*cb)(const String& line)) = 0;
};

class IRs232Port : public ISerialPort {
public:
    virtual ~IRs232Port() = default;
    virtual bool setDtr(bool level) = 0;
    virtual bool setRts(bool level) = 0;
    virtual bool isDcdActive() = 0;
};
