#include "sensor_ina226_config.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_INA226==1

#include "sensor_ina226.h"
#include <ArduinoJson.h>
#include <time.h>
#include "feature_flags.h"
#include "../psram_helpers.h"
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "touch7_i2c.h" // bus-free guard (GT911 can hold SDA pre-reset)
#endif

extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

INA226ConfigManager ina226ConfigManager;

// ====================================================================================
// Initialization
// ====================================================================================

bool INA226ConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);

    if (_config.sensor_enabled) {
        // Touch-LCD-7: all sensors share Wire1 (display stack owns I2C0).
        TwoWire* bus = &Wire;
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
        bus = &Wire1;
        // T7-24c: local I2C unsupported on TOUCH7 (shared-bus deadlock, see
        // sensor_aht20_bmp280.cpp). Never transact; report absent.
        Serial.println(F("[INA226_CFG] local I2C disabled on TOUCH7 (shared bus)"));
#else
        bool ok = ina226Sensor.begin(
            (uint8_t)_config.i2c_addr,
            _config.shunt_ohm,
            _config.max_current_a,
            bus);

        if (ok) {
            ina226Sensor.setAveraging(_avgFromInt(_config.averaging));
        }
#endif
    }

    _initialized = true;
    Serial.printf("[INA226_CFG] Ready addr=0x%02X shunt=%.4fΩ maxI=%.3fA avg=%d\n",
                  _config.i2c_addr, _config.shunt_ohm,
                  _config.max_current_a, _config.averaging);
    return true;
}

// ====================================================================================
// Sensor reading
// ====================================================================================

bool INA226ConfigManager::performReading() {
    if (!_initialized || !_config.sensor_enabled) return false;
    if (!ina226Sensor.isInitialized()) return false;

    float busV, shuntV, currentA, powerW;
    if (!ina226Sensor.readAll(busV, shuntV, currentA, powerW)) return false;

    _config.last_bus_v     = busV;
    _config.last_shunt_v   = shuntV;
    _config.last_current_a = currentA;
    _config.last_power_w   = powerW;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _config.last_reading_time = (unsigned long)tv.tv_sec;

    if (_config.inject_to_buffer) injectToBuffer();

    markReadingDone();
    return true;
}

// ====================================================================================
// Buffer injection
// ====================================================================================

void INA226ConfigManager::injectToBuffer() const {
    float displayCurrent = (_config.current_unit == (int)INA226_UNIT_MA)
                           ? _config.last_current_a * 1000.0f
                           : _config.last_current_a;
    const char* unitStr = (_config.current_unit == (int)INA226_UNIT_MA) ? "mA" : "A";

    String msg;
    if (_config.output_csv) {
        msg = String(F("INA226,voltage,")) + String(_config.last_bus_v, 3)
            + String(F(",current,")) + String(displayCurrent, 3) + String(',') + unitStr
            + String(F(",power,")) + String(_config.last_power_w, 3) + String('W');
    } else {
        msg = String(F("⚡ INA226: ")) + String(_config.last_bus_v, 2) + String(F("V  "))
            + String(displayCurrent, 3) + String(unitStr) + String(F("  "))
            + String(_config.last_power_w, 3) + String('W');
    }
    injectDataToRXBuffer(msg, F("INA226"));
}

// ====================================================================================
// Averaging helper
// ====================================================================================

INA226Avg INA226ConfigManager::_avgFromInt(int n) const {
    if (n <= 1)   return INA226_AVG_1;
    if (n <= 4)   return INA226_AVG_4;
    if (n <= 16)  return INA226_AVG_16;
    if (n <= 64)  return INA226_AVG_64;
    if (n <= 128) return INA226_AVG_128;
    if (n <= 256) return INA226_AVG_256;
    if (n <= 512) return INA226_AVG_512;
    return INA226_AVG_1024;
}

// ====================================================================================
// JSON serialization
// ====================================================================================

String INA226ConfigManager::toJson() const {
    SpiRamJsonDocument doc(512);
    doc["sensor_enabled"]     = _config.sensor_enabled;
    doc["i2c_addr"]           = _config.i2c_addr;
    doc["shunt_ohm"]          = _config.shunt_ohm;
    doc["max_current_a"]      = _config.max_current_a;
    doc["averaging"]          = _config.averaging;
    doc["current_unit"]       = _config.current_unit;
    doc["read_interval_sec"]  = _config.read_interval_sec;
    doc["inject_to_buffer"]   = _config.inject_to_buffer;
    doc["output_csv"]         = _config.output_csv;
    doc["show_bus_voltage"]   = _config.show_bus_voltage;
    doc["show_shunt_voltage"] = _config.show_shunt_voltage;
    doc["show_current"]       = _config.show_current;
    doc["show_power"]         = _config.show_power;
    doc["alert_current_max"]  = _config.alert_current_max;
    doc["alert_power_max"]    = _config.alert_power_max;
    doc["last_bus_v"]         = _config.last_bus_v;
    doc["last_shunt_v"]       = _config.last_shunt_v;
    doc["last_current_a"]     = _config.last_current_a;
    doc["last_power_w"]       = _config.last_power_w;
    doc["last_reading_time"]  = (uint32_t)_config.last_reading_time;
    String out;
    serializeJsonPretty(doc, out);
    return out;
}

bool INA226ConfigManager::updateFromJson(const String& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc.containsKey("sensor_enabled"))     _config.sensor_enabled     = doc["sensor_enabled"].as<bool>();
    if (doc.containsKey("i2c_addr"))           _config.i2c_addr           = doc["i2c_addr"].as<int>();
    if (doc.containsKey("shunt_ohm"))          _config.shunt_ohm          = doc["shunt_ohm"].as<float>();
    if (doc.containsKey("max_current_a"))      _config.max_current_a      = doc["max_current_a"].as<float>();
    if (doc.containsKey("averaging"))          _config.averaging          = doc["averaging"].as<int>();
    if (doc.containsKey("current_unit"))       _config.current_unit       = doc["current_unit"].as<int>();
    if (doc.containsKey("read_interval_sec"))  _config.read_interval_sec  = doc["read_interval_sec"].as<int>();
    if (doc.containsKey("inject_to_buffer"))   _config.inject_to_buffer   = doc["inject_to_buffer"].as<bool>();
    if (doc.containsKey("output_csv"))         _config.output_csv         = doc["output_csv"].as<bool>();
    if (doc.containsKey("show_bus_voltage"))   _config.show_bus_voltage   = doc["show_bus_voltage"].as<bool>();
    if (doc.containsKey("show_shunt_voltage")) _config.show_shunt_voltage = doc["show_shunt_voltage"].as<bool>();
    if (doc.containsKey("show_current"))       _config.show_current       = doc["show_current"].as<bool>();
    if (doc.containsKey("show_power"))         _config.show_power         = doc["show_power"].as<bool>();
    if (doc.containsKey("alert_current_max"))  _config.alert_current_max  = doc["alert_current_max"].as<float>();
    if (doc.containsKey("alert_power_max"))    _config.alert_power_max    = doc["alert_power_max"].as<float>();

    // Re-configure the running driver with updated parameters
    if (ina226Sensor.isInitialized()) {
        ina226Sensor.setAveraging(_avgFromInt(_config.averaging));
    }

    return saveToFile();
}

#endif // ENABLE_INA226==1
