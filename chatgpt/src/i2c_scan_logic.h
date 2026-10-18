// i2c_scan_logic.h — Pure I2C scan report logic (no hardware I/O).
// Shared between the production scanner (i2c_scan.cpp) and the native unit
// test (tests/native/test_i2c_scan_logic.cpp). Keep this header free of any
// ESP32 dependency so it compiles against stubs/Arduino.h on the host.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <Arduino.h>

struct I2cExpectedDevice {
    uint8_t addr;
    const char* name;
};

struct I2cBusReport {
    const char* label;
    uint8_t sda;
    uint8_t scl;
    const uint8_t* found;
    uint8_t foundCount;
};

struct I2cKnownDevice {
    uint8_t addr;
    const char* name;
};

static const I2cKnownDevice kI2cKnownDevices[] = {
    {0x20, "TCA9554/PCF8574 expander"},
    {0x27, "PCF8574 LCD backpack"},
    {0x38, "AHT20"},
    {0x39, "AHT10"},
    {0x3C, "SSD1306/SH1106 OLED"},
    {0x3D, "SSD1306 OLED (alt)"},
    {0x40, "INA226/INA219"},
    {0x44, "SHT30/SHT31"},
    {0x45, "SHT30/SHT31 (alt)"},
    {0x48, "ADS1115/LM75/ADC"},
    {0x4C, "LTC2945/EEPROM"},
    {0x50, "EEPROM 24C02"},
    {0x53, "MAX44009"},
    {0x5A, "SHT31/INA226 (alt)"},
    {0x5C, "AM2320"},
    {0x68, "DS3231 RTC / MPU6050"},
    {0x69, "DS3231 (alt)/MPU6050"},
    {0x76, "BMP280/BME280"},
    {0x77, "BMP280/BME280 (alt)"},
};

inline const char* i2cKnownName(uint8_t addr) {
    for (size_t i = 0; i < sizeof(kI2cKnownDevices) / sizeof(kI2cKnownDevices[0]); i++) {
        if (kI2cKnownDevices[i].addr == addr) return kI2cKnownDevices[i].name;
    }
    return nullptr;
}

inline bool i2cAddrIsConfigured(uint8_t addr, const I2cExpectedDevice* expected, uint8_t expectedCount) {
    for (uint8_t i = 0; i < expectedCount; i++) {
        if (expected[i].addr == addr) return true;
    }
    return false;
}

inline bool i2cAddrFound(const uint8_t* found, uint8_t foundCount, uint8_t addr) {
    for (uint8_t i = 0; i < foundCount; i++) {
        if (found[i] == addr) return true;
    }
    return false;
}

// Appends the full scan report JSON to `out`:
//   {"buses":[{"bus":"I2C0 (Wire)","sda":21,"scl":48,"devices":[
//      {"addr":"0x3C","name":"SSD1306/SH1106 OLED","known":true,"configured":true}]}],
//    "missing":[{"addr":"0x76","name":"BMP280"}]}
// `expected` is the set of chips compiled into this build (configured);
// a detected device with an address in that set is flagged configured=true.
// `missing` lists configured chips that were NOT detected on any bus.
inline void i2cAppendScanJson(String& out, const I2cExpectedDevice* expected, uint8_t expectedCount,
                              const I2cBusReport* buses, uint8_t busCount) {
    char tmp[128];
    out += "{\"buses\":[";
    for (uint8_t b = 0; b < busCount; b++) {
        const I2cBusReport& bus = buses[b];
        if (b > 0) out += ',';
        snprintf(tmp, sizeof(tmp), "{\"bus\":\"%s\",\"sda\":%u,\"scl\":%u,\"devices\":[",
                 bus.label, bus.sda, bus.scl);
        out += tmp;
        for (uint8_t i = 0; i < bus.foundCount; i++) {
            uint8_t addr = bus.found[i];
            const char* name = i2cKnownName(addr);
            if (i > 0) out += ',';
            snprintf(tmp, sizeof(tmp), "{\"addr\":\"0x%02X\",\"name\":\"%s\",\"known\":%s,\"configured\":%s}",
                     addr,
                     name ? name : "unknown",
                     name ? "true" : "false",
                     i2cAddrIsConfigured(addr, expected, expectedCount) ? "true" : "false");
            out += tmp;
        }
        out += "]}";
    }
    out += "],\"missing\":[";
    bool firstMissing = true;
    for (uint8_t e = 0; e < expectedCount; e++) {
        bool foundAnywhere = false;
        for (uint8_t b = 0; b < busCount; b++) {
            if (i2cAddrFound(buses[b].found, buses[b].foundCount, expected[e].addr)) {
                foundAnywhere = true;
                break;
            }
        }
        if (!foundAnywhere) {
            if (!firstMissing) out += ',';
            firstMissing = false;
            snprintf(tmp, sizeof(tmp), "{\"addr\":\"0x%02X\",\"name\":\"%s\"}",
                     expected[e].addr, expected[e].name);
            out += tmp;
        }
    }
    out += "]}";
}
