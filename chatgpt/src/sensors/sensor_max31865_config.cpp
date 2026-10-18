#include "sensor_max31865_config.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_MAX31865==1

#include "sensor_max31865.h"
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include "feature_flags.h"
#include "../psram_helpers.h"

extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

MAX31865ConfigManager max31865ConfigManager;

// ====================================================================================
// Initialization
// ====================================================================================

bool MAX31865ConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);

    if (_config.sensor_enabled) {
        // Initialize the SPI driver with stored pin configuration
        SPI.begin(_config.clk_pin, _config.miso_pin, _config.mosi_pin, -1);

        MAX31865RTDType rtdType = (_config.rtd_type == 1) ? MAX31865_PT1000 : MAX31865_PT100;
        MAX31865Wiring  wiring  = (_config.wiring == 1)   ? MAX31865_3WIRE  : MAX31865_2WIRE;

        max31865Sensor.begin((uint8_t)_config.cs_pin, rtdType, wiring, &SPI);
    }

    _initialized = true;
    Serial.printf("[MAX31865_CFG] Ready CS=GPIO%d type=%s unit=%s\n",
                  _config.cs_pin,
                  (_config.rtd_type == 1) ? "PT1000" : "PT100",
                  unitSuffix());
    return true;
}

// ====================================================================================
// Sensor reading
// ====================================================================================

bool MAX31865ConfigManager::performReading() {
    if (!_initialized || !_config.sensor_enabled) return false;
    if (!max31865Sensor.isInitialized()) return false;

    float tempC = max31865Sensor.readTemperature();
    uint8_t fault = max31865Sensor.readFaultStatus();

    _config.last_fault     = fault;
    _config.last_temp_raw  = tempC;
    _config.last_temp_display = isnan(tempC) ? NAN
                                              : toDisplayUnit(tempC + _config.calibration_offset);

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _config.last_reading_time = (unsigned long)tv.tv_sec;

    if (_config.inject_to_buffer && !isnan(_config.last_temp_display)) {
        injectToBuffer();
    }

    markReadingDone();
    return !isnan(tempC);
}

// ====================================================================================
// Unit conversion
// ====================================================================================

float MAX31865ConfigManager::toDisplayUnit(float tempC) const {
    switch ((MAX31865TempUnit)_config.temp_unit) {
        case MAX31865_FAHRENHEIT: return tempC * 9.0f / 5.0f + 32.0f;
        case MAX31865_KELVIN:     return tempC + 273.15f;
        default:                  return tempC;   // Celsius
    }
}

const char* MAX31865ConfigManager::unitSuffix() const {
    switch ((MAX31865TempUnit)_config.temp_unit) {
        case MAX31865_FAHRENHEIT: return "°F";
        case MAX31865_KELVIN:     return "K";
        default:                  return "°C";
    }
}

// ====================================================================================
// Buffer injection
// ====================================================================================

void MAX31865ConfigManager::injectToBuffer() const {
    String msg;
    if (_config.output_csv) {
        msg = String(F("MAX31865,temp,")) + String(_config.last_temp_display, 2)
            + String(',') + unitSuffix();
    } else {
        msg = String(F("🌡️ RTD: ")) + String(_config.last_temp_display, 2)
            + String(' ') + unitSuffix();
    }
    injectDataToRXBuffer(msg, F("MAX31865"));
}

// ====================================================================================
// JSON serialization
// ====================================================================================

String MAX31865ConfigManager::toJson() const {
    SpiRamJsonDocument doc(512);
    doc["sensor_enabled"]       = _config.sensor_enabled;
    doc["cs_pin"]               = _config.cs_pin;
    doc["mosi_pin"]             = _config.mosi_pin;
    doc["miso_pin"]             = _config.miso_pin;
    doc["clk_pin"]              = _config.clk_pin;
    doc["rtd_type"]             = _config.rtd_type;
    doc["wiring"]               = _config.wiring;
    doc["temp_unit"]            = _config.temp_unit;
    doc["calibration_offset"]   = _config.calibration_offset;
    doc["read_interval_sec"]    = _config.read_interval_sec;
    doc["inject_to_buffer"]     = _config.inject_to_buffer;
    doc["output_csv"]           = _config.output_csv;
    doc["alert_low"]            = _config.alert_low;
    doc["alert_high"]           = _config.alert_high;
    if (!isnan(_config.last_temp_display)) {
        doc["last_temp"]        = _config.last_temp_display;
    } else {
        doc["last_temp"]        = nullptr;
    }
    doc["last_unit"]            = unitSuffix();
    doc["last_fault"]           = _config.last_fault;
    doc["last_reading_time"]    = (uint32_t)_config.last_reading_time;
    String out;
    serializeJsonPretty(doc, out);
    return out;
}

bool MAX31865ConfigManager::updateFromJson(const String& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc.containsKey("sensor_enabled"))     _config.sensor_enabled     = doc["sensor_enabled"].as<bool>();
    if (doc.containsKey("cs_pin"))             _config.cs_pin             = doc["cs_pin"].as<int>();
    if (doc.containsKey("mosi_pin"))           _config.mosi_pin           = doc["mosi_pin"].as<int>();
    if (doc.containsKey("miso_pin"))           _config.miso_pin           = doc["miso_pin"].as<int>();
    if (doc.containsKey("clk_pin"))            _config.clk_pin            = doc["clk_pin"].as<int>();
    if (doc.containsKey("rtd_type"))           _config.rtd_type           = doc["rtd_type"].as<int>();
    if (doc.containsKey("wiring"))             _config.wiring             = doc["wiring"].as<int>();
    if (doc.containsKey("temp_unit"))          _config.temp_unit          = doc["temp_unit"].as<int>();
    if (doc.containsKey("calibration_offset")) _config.calibration_offset = doc["calibration_offset"].as<float>();
    if (doc.containsKey("read_interval_sec"))  _config.read_interval_sec  = doc["read_interval_sec"].as<int>();
    if (doc.containsKey("inject_to_buffer"))   _config.inject_to_buffer   = doc["inject_to_buffer"].as<bool>();
    if (doc.containsKey("output_csv"))         _config.output_csv         = doc["output_csv"].as<bool>();
    if (doc.containsKey("alert_low"))          _config.alert_low          = doc["alert_low"].as<float>();
    if (doc.containsKey("alert_high"))         _config.alert_high         = doc["alert_high"].as<float>();

    return saveToFile();
}

#endif // ENABLE_MAX31865==1
