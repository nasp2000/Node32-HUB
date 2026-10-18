#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

enum class UsbDeviceClass : uint8_t {
    Unknown = 0,
    HID = 1,
    MassStorage = 2,
    CDC = 3,
    Hub = 4,
    Audio = 5,
    Video = 6,
    VendorSpecific = 255
};

struct UsbDeviceInfo {
    uint8_t address;
    uint16_t vid;
    uint16_t pid;
    UsbDeviceClass deviceClass;
    String manufacturer;
    String product;
    String serial;
    bool connected;
};

class IUsbHost {
public:
    virtual ~IUsbHost() = default;

    virtual bool begin() = 0;
    virtual void loop() = 0;
    virtual void end() = 0;

    virtual bool enumerateDevices(std::vector<UsbDeviceInfo>& out) = 0;
    virtual size_t deviceCount() const = 0;
    virtual bool isDeviceConnected(uint8_t address) const = 0;

    virtual bool claimInterface(uint8_t address, uint8_t interface_num) = 0;
    virtual bool releaseInterface(uint8_t address, uint8_t interface_num) = 0;

    virtual bool controlTransfer(uint8_t bmRequestType, uint8_t bRequest,
                                  uint16_t wValue, uint16_t wIndex,
                                  uint8_t* data, size_t len) = 0;

    virtual bool bulkTransfer(uint8_t pipe, uint8_t* data, size_t len, uint32_t timeoutMs = 1000) = 0;

    virtual bool setPower(bool on) = 0;
    virtual bool resetBus() = 0;

    virtual void setDeviceCallback(void (*cb)(const UsbDeviceInfo&, bool connected)) = 0;
};

class IUsbIpServer {
public:
    virtual ~IUsbIpServer() = default;

    virtual bool begin() = 0;
    virtual void loop() = 0;
    virtual void end() = 0;

    virtual bool isRunning() const = 0;
    virtual uint16_t port() const = 0;
    virtual size_t clientCount() const = 0;

    virtual bool shareDevice(uint8_t address) = 0;
    virtual bool unshareDevice(uint8_t address) = 0;
    virtual bool isDeviceShared(uint8_t address) const = 0;
};
