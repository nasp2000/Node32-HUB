#include "sensor_sound_config.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_SOUND_SENSOR==1

#include "sensor_sound.h"
#include <ArduinoJson.h>
#include <time.h>
#include "feature_flags.h"
#include "../psram_helpers.h"

extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

SoundConfigManager soundConfigManager;

// ====================================================================================
// Initialization
// ====================================================================================

bool SoundConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);

    if (_config.sensor_enabled) {
        soundSensor.begin((uint8_t)_config.adc_pin);
    }

    _initialized = true;
    Serial.printf("[SOUND_CFG] Ready GPIO%d window=%dms samples=%d\n",
                  _config.adc_pin, _config.sample_window_ms, _config.sample_count);
    return true;
}

// ====================================================================================
// Sensor reading
// ====================================================================================

bool SoundConfigManager::performReading() {
    if (!_initialized || !_config.sensor_enabled) return false;
    if (!soundSensor.isInitialized()) return false;

    bool ok = soundSensor.performReading(
        (uint16_t)_config.sample_window_ms,
        (uint16_t)_config.sample_count);
    if (!ok) return false;

    _config.last_peak_to_peak = soundSensor.getPeakToPeak();
    _config.last_peak_mv      = soundSensor.getPeakToPeakMv();
    _config.last_dbfs         = soundSensor.getDbfs();

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

void SoundConfigManager::injectToBuffer() const {
    String msg;
    if (_config.output_csv) {
        msg = String(F("SOUND,peak_mv,")) + String(_config.last_peak_mv, 1)
            + String(F(",dbfs,")) + String(_config.last_dbfs, 1);
    } else {
        msg = String(F("🔊 Som: ")) + String(_config.last_peak_mv, 1)
            + String(F("mV  ")) + String(_config.last_dbfs, 1) + String(F("dBFS"));
    }
    injectDataToRXBuffer(msg, F("SOUND"));
}

// ====================================================================================
// JSON serialization
// ====================================================================================

String SoundConfigManager::toJson() const {
    SpiRamJsonDocument doc(384);
    doc["sensor_enabled"]    = _config.sensor_enabled;
    doc["adc_pin"]           = _config.adc_pin;
    doc["sample_window_ms"]  = _config.sample_window_ms;
    doc["sample_count"]      = _config.sample_count;
    doc["read_interval_sec"] = _config.read_interval_sec;
    doc["inject_to_buffer"]  = _config.inject_to_buffer;
    doc["output_csv"]        = _config.output_csv;
    doc["alert_dbfs"]        = _config.alert_dbfs;
    doc["last_peak_to_peak"] = _config.last_peak_to_peak;
    doc["last_peak_mv"]      = _config.last_peak_mv;
    doc["last_dbfs"]         = _config.last_dbfs;
    doc["last_reading_time"] = (uint32_t)_config.last_reading_time;
    String out;
    serializeJsonPretty(doc, out);
    return out;
}

bool SoundConfigManager::updateFromJson(const String& json) {
    SpiRamJsonDocument doc(384);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;

    if (doc.containsKey("sensor_enabled"))    _config.sensor_enabled    = doc["sensor_enabled"].as<bool>();
    if (doc.containsKey("adc_pin"))           _config.adc_pin           = doc["adc_pin"].as<int>();
    if (doc.containsKey("sample_window_ms"))  _config.sample_window_ms  = doc["sample_window_ms"].as<int>();
    if (doc.containsKey("sample_count"))      _config.sample_count      = doc["sample_count"].as<int>();
    if (doc.containsKey("read_interval_sec")) _config.read_interval_sec = doc["read_interval_sec"].as<int>();
    if (doc.containsKey("inject_to_buffer"))  _config.inject_to_buffer  = doc["inject_to_buffer"].as<bool>();
    if (doc.containsKey("output_csv"))        _config.output_csv        = doc["output_csv"].as<bool>();
    if (doc.containsKey("alert_dbfs"))        _config.alert_dbfs        = doc["alert_dbfs"].as<float>();

    // Re-initialize driver if the pin changed
    if (soundSensor.isInitialized()) {
        soundSensor.begin((uint8_t)_config.adc_pin);
    }

    return saveToFile();
}

#endif // ENABLE_SOUND_SENSOR==1
