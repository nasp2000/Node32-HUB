#include "sensor_bh1750_config.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_BH1750==1

#include "sensor_bh1750.h"
#include <ArduinoJson.h>
#include <time.h>
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "touch7_i2c.h" // bus-free guard (GT911 can hold SDA pre-reset)
#endif

extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

BH1750ConfigManager bh1750ConfigManager;

bool BH1750ConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);

    if (_config.sensor_enabled) {
        // Touch-LCD-7: all sensors share Wire1 (display stack owns I2C0).
        TwoWire* bus = &Wire;
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
        bus = &Wire1;
        // T7-24c: local I2C unsupported on TOUCH7 (shared-bus deadlock, see
        // sensor_aht20_bmp280.cpp). Never transact; report absent.
        Serial.println(F("[BH1750_CFG] local I2C disabled on TOUCH7 (shared bus)"));
#else
        bh1750Sensor.begin((uint8_t)_config.i2c_addr, bus);
#endif
    }

    _initialized = true;
    Serial.printf("[BH1750_CFG] Ready addr=0x%02X\n", _config.i2c_addr);
    return true;
}

bool BH1750ConfigManager::performReading() {
    if (!_initialized || !_config.sensor_enabled) return false;
    if (!bh1750Sensor.isInitialized()) return false;

    _config.last_lux = bh1750Sensor.readLux();

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _config.last_reading_time = (unsigned long)tv.tv_sec;

    if (_config.inject_to_buffer) injectToBuffer();

    markReadingDone();
    return true;
}

void BH1750ConfigManager::injectToBuffer() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "[BH1750] %.1f lx", _config.last_lux);
    injectDataToRXBuffer(String(buf), "BH1750");
}

String BH1750ConfigManager::toJson() const {
    SpiRamJsonDocument doc(512);
    doc["sensor_enabled"] = _config.sensor_enabled;
    doc["i2c_addr"] = _config.i2c_addr;
    doc["read_interval_sec"] = _config.read_interval_sec;
    doc["inject_to_buffer"] = _config.inject_to_buffer;
    doc["output_csv"] = _config.output_csv;
    doc["last_lux"] = _config.last_lux;
    doc["last_reading_time"] = _config.last_reading_time;
    String out;
    serializeJson(doc, out);
    return out;
}

bool BH1750ConfigManager::updateFromJson(const String& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("sensor_enabled")) _config.sensor_enabled = doc["sensor_enabled"];
    if (doc.containsKey("i2c_addr")) {
        int a = (int)doc["i2c_addr"];
        if (a == 0x23 || a == 0x5C) _config.i2c_addr = a;
    }
    if (doc.containsKey("read_interval_sec")) {
        int v = (int)doc["read_interval_sec"];
        if (v >= 2 && v <= 3600) _config.read_interval_sec = v;
    }
    if (doc.containsKey("inject_to_buffer")) _config.inject_to_buffer = doc["inject_to_buffer"];
    if (doc.containsKey("output_csv")) _config.output_csv = doc["output_csv"];
    return saveToFile();
}

#endif // ENABLE_BH1750==1
