#include "sensor_hx711_config.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_HX711==1

#include "sensor_hx711.h"
#include <ArduinoJson.h>
#include <time.h>
#include "feature_flags.h"
#include "../psram_helpers.h"

// Injector for RX buffer — defined in chatgpt.ino
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

HX711ConfigManager hx711ConfigManager;

// ====================================================================================
// Initialization
// ====================================================================================

bool HX711ConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);

    // Initialize driver with stored pins and gain
    HX711Gain gain = HX711_GAIN_128;
    if (_config.gain == 64)  gain = HX711_GAIN_64;
    if (_config.gain == 32)  gain = HX711_GAIN_32;

    if (_config.sensor_enabled) {
        bool ok = hx711Sensor.begin((uint8_t)_config.data_pin,
                                     (uint8_t)_config.clk_pin, gain);
        if (ok) {
            hx711Sensor.setTareOffset(_config.tare_offset);
            hx711Sensor.setScaleFactor(_config.scale_factor);
        }
    }

    _initialized = true;
    Serial.printf("[HX711_CFG] Ready DATA=GPIO%d CLK=GPIO%d gain=%d\n",
                  _config.data_pin, _config.clk_pin, _config.gain);
    return true;
}

// ====================================================================================
// Sensor reading
// ====================================================================================

bool HX711ConfigManager::performReading() {
    if (!_initialized || !_config.sensor_enabled) return false;
    if (!hx711Sensor.isInitialized()) return false;

    float weight = hx711Sensor.readWeight((uint8_t)_config.samples);
    int32_t raw  = hx711Sensor.getTareOffset() + (int32_t)(weight * _config.scale_factor);

    _config.last_weight = weight;
    _config.last_raw    = raw;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _config.last_reading_time = (unsigned long)tv.tv_sec;

    if (_config.inject_to_buffer) injectToBuffer();

    markReadingDone();
    return true;
}

// ====================================================================================
// Tare
// ====================================================================================

bool HX711ConfigManager::performTare() {
    if (!_initialized || !hx711Sensor.isInitialized()) return false;
    hx711Sensor.tare((uint8_t)_config.samples);
    _config.tare_offset = hx711Sensor.getTareOffset();
    saveToFile();
    Serial.printf("[HX711_CFG] Tare saved: offset=%ld\n", (long)_config.tare_offset);
    return true;
}

// ====================================================================================
// Buffer injection
// ====================================================================================

const char* HX711ConfigManager::unitSuffix() const {
    switch ((HX711WeightUnit)_config.weight_unit) {
        case HX711_UNIT_KG:  return "kg";
        case HX711_UNIT_LBS: return "lbs";
        case HX711_UNIT_OZ:  return "oz";
        case HX711_UNIT_N:   return "N";
        default:             return "g";
    }
}

void HX711ConfigManager::injectToBuffer() const {
    String msg;
    if (_config.output_csv) {
        msg = String(F("HX711,weight,")) + String(_config.last_weight, 3)
            + String(',') + unitSuffix();
    } else {
        msg = String(F("⚖️ Peso: ")) + String(_config.last_weight, 3)
            + String(' ') + unitSuffix();
    }
    injectDataToRXBuffer(msg, F("HX711"));
}

// ====================================================================================
// JSON serialization
// ====================================================================================

String HX711ConfigManager::toJson() const {
    SpiRamJsonDocument doc(512);
    doc["sensor_enabled"]     = _config.sensor_enabled;
    doc["data_pin"]           = _config.data_pin;
    doc["clk_pin"]            = _config.clk_pin;
    doc["gain"]               = _config.gain;
    doc["samples"]            = _config.samples;
    doc["read_interval_sec"]  = _config.read_interval_sec;
    doc["scale_factor"]       = _config.scale_factor;
    doc["tare_offset"]        = _config.tare_offset;
    doc["weight_unit"]        = _config.weight_unit;
    doc["inject_to_buffer"]   = _config.inject_to_buffer;
    doc["output_csv"]         = _config.output_csv;
    doc["alert_min"]          = _config.alert_min;
    doc["alert_max"]          = _config.alert_max;
    doc["last_weight"]        = _config.last_weight;
    doc["last_raw"]           = _config.last_raw;
    doc["last_reading_time"]  = (uint32_t)_config.last_reading_time;
    String out;
    serializeJsonPretty(doc, out);
    return out;
}

bool HX711ConfigManager::updateFromJson(const String& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc.containsKey("sensor_enabled"))    _config.sensor_enabled    = doc["sensor_enabled"].as<bool>();
    if (doc.containsKey("data_pin"))          _config.data_pin          = doc["data_pin"].as<int>();
    if (doc.containsKey("clk_pin"))           _config.clk_pin           = doc["clk_pin"].as<int>();
    if (doc.containsKey("gain"))              _config.gain              = doc["gain"].as<int>();
    if (doc.containsKey("samples"))           _config.samples           = doc["samples"].as<int>();
    if (doc.containsKey("read_interval_sec")) _config.read_interval_sec = doc["read_interval_sec"].as<int>();
    if (doc.containsKey("scale_factor"))      _config.scale_factor      = doc["scale_factor"].as<float>();
    if (doc.containsKey("tare_offset"))       _config.tare_offset       = doc["tare_offset"].as<int32_t>();
    if (doc.containsKey("weight_unit"))       _config.weight_unit       = doc["weight_unit"].as<int>();
    if (doc.containsKey("inject_to_buffer"))  _config.inject_to_buffer  = doc["inject_to_buffer"].as<bool>();
    if (doc.containsKey("output_csv"))        _config.output_csv        = doc["output_csv"].as<bool>();
    if (doc.containsKey("alert_min"))         _config.alert_min         = doc["alert_min"].as<float>();
    if (doc.containsKey("alert_max"))         _config.alert_max         = doc["alert_max"].as<float>();

    // Apply calibration to running driver
    if (hx711Sensor.isInitialized()) {
        hx711Sensor.setScaleFactor(_config.scale_factor);
        hx711Sensor.setTareOffset(_config.tare_offset);
    }

    return saveToFile();
}

#endif // ENABLE_HX711==1
