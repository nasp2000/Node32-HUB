#include "sensor_config.h"
#include "../languages/lang_active.h"
#include "../log.h"  // LogLevel INFO (sendVirtualMessage level; direct: pack-dependent transitive chains omit it)
#include "../sd_rotation_manager.h"  // stringToInterval + intervalToMs (direct: pack-dependent transitive chains omit them)

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1

#include "feature_flags.h"
#if ENABLE_AHT20_BMP280==1
#include "sensor_aht20_bmp280.h"
#endif
#include "../littlefs_manager.h"
#include "../email_message_builder.h"
#include "../ConfigManager.h"
#include "../psram_helpers.h"
#include "feature_flags.h"
#include "../chart_history.h"
#include "../sensor_history_merge.h"
#include <LittleFS.h>
#include <time.h>
#include <sys/stat.h>  // POSIX stat() for file existence check
#include <vector>
#if ENABLE_MQ135==1
#include "sensor_mq135_config.h"
#endif
#if ENABLE_HX711==1
#include "sensor_hx711_config.h"
#endif
#if ENABLE_INA226==1
#include "sensor_ina226_config.h"
#endif
#if ENABLE_BH1750==1
#include "sensor_bh1750_config.h"
#endif
#if ENABLE_SOUND_SENSOR==1
#include "sensor_sound_config.h"
#endif
#if ENABLE_MAX31865==1
#include "sensor_max31865_config.h"
#endif

// Global instance
SensorConfigManager sensorConfigManager;

// External sensor instance (defined in chatgpt.ino) — only when AHT20 is active
#if ENABLE_AHT20_BMP280==1
extern SensorAHT20_BMP280 environmentalSensor;
#endif

// External ConfigManager instance
extern ConfigManager configManager;

// Forward declaration: injects formatted sensor output into the RX visualization buffer
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

SensorConfigManager::SensorConfigManager() :
    configFilePath("/config/sensor_config.json"),
    lastAutoReadTime(0),
    initialized(false)
{
}

bool SensorConfigManager::begin(const String &filePath) {
    configFilePath = filePath;
    
    // Initialize history buffer in PSRAM FIRST.
    // History is an in-memory ring buffer - it does not need LittleFS.
    // Doing this before LittleFS ensures the buffer is always available
    // even when the flash filesystem fails to mount.
    Serial.println("[SENSOR_CFG] Initializing history buffer...");
    if (!history.begin()) {
        Serial.println("[SENSOR_CFG] WARNING: History buffer init failed - charts will be empty");
        Serial.printf("[SENSOR_CFG] PSRAM available: %s\n", psramFound() ? "YES" : "NO");
    } else {
        Serial.printf("[SENSOR_CFG] History buffer ready: %u/%u points\n", history.getCount(), HISTORY_BUFFER_SIZE);
    }
    
    // Mount LittleFS for config file persistence.
    // Use littlefsBegin() instead of LittleFS.begin() directly to prevent double-mount
    // PSRAM corruption (CONFIG_SPIRAM_USE_MALLOC=1 makes mount buffers deterministic).
    // taskInitFlash already mounted LittleFS at boot, so this typically skips mount.
    bool fsReady = littlefsMounted();
    if (!fsReady) {
        fsReady = littlefsBegin(); // Use system mount with _fsMounted tracking
    }
    if (!fsReady) {
        Serial.println("[SENSOR_CFG] ERROR: LittleFS mount failed - using defaults, config will NOT persist across reboots");
        applyDefaults();
        initialized = true; // Sensor reads still work without persistent config
        return true;        // Return true so the caller sets up the auto-read loop
    }
    
    // Ensure /config directory exists
    // Use POSIX stat check instead of exists() or open() to avoid PSRAM heap corruption
    // on failed LittleFS File accesses due to C++ File destructor bug.
    bool dirExists = false;
    struct stat st;
    if (stat("/littlefs/config", &st) == 0 && S_ISDIR(st.st_mode)) {
        dirExists = true;
    }
    
    if (!dirExists) {
        if (LittleFS.mkdir("/config")) {
            Serial.println("[SENSOR_CFG] Created /config directory");
            delay(10);
        } else {
            Serial.println("[SENSOR_CFG] WARNING: Failed to create /config directory");
        }
    }
    
    // Try to load existing configuration
    if (!loadFromFile()) {
        Serial.println("[SENSOR_CFG] No config file, using defaults");
        applyDefaults();
        
        Serial.println("[SENSOR_CFG] 💾 Creating default config file...");
        if (saveToFile()) {
            Serial.println("[SENSOR_CFG] ✅ Default config file created successfully");
        } else {
            Serial.println("[SENSOR_CFG] ❌ FAILED to create default config file!");
        }
    } else {
        Serial.println("[SENSOR_CFG] ✅ Config loaded from file successfully");
        Serial.println("[SENSOR_CFG] 🔍 Current configuration after load:");
        Serial.printf("[SENSOR_CFG]   - sensor_enabled: %s\n", config.sensor_enabled ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - output_pretty: %s\n", config.output_pretty ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - output_csv: %s\n", config.output_csv ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - inject_to_buffer: %s\n", config.inject_to_buffer ? "true" : "false");
    }
    
    initialized = true;
    Serial.println("[SENSOR_CFG] Initialized successfully");

    // Init MQ-135 after SensorConfig is ready so it can register its extra slots
#if ENABLE_MQ135==1
    mq135ConfigManager.begin();
    mq135ConfigManager.registerExtraSlots(config);
    Serial.println("[SENSOR_CFG] MQ-135 manager initialized and slots registered");
#endif

#if ENABLE_HX711==1
    hx711ConfigManager.begin();
    Serial.println("[SENSOR_CFG] HX711 manager initialized");
#endif

#if ENABLE_INA226==1
    ina226ConfigManager.begin();
    Serial.println("[SENSOR_CFG] INA226 manager initialized");
#endif

#if ENABLE_BH1750==1
    bh1750ConfigManager.begin();
    Serial.println("[SENSOR_CFG] BH1750 manager initialized");
#endif

#if ENABLE_SOUND_SENSOR==1
    soundConfigManager.begin();
    Serial.println("[SENSOR_CFG] Sound sensor manager initialized");
#endif

#if ENABLE_MAX31865==1
    max31865ConfigManager.begin();
    Serial.println("[SENSOR_CFG] MAX31865 manager initialized");
#endif

    return true;
}

void SensorConfigManager::applyDefaults() {
    config = SensorConfig();  // Use constructor defaults
}

bool SensorConfigManager::loadFromFile() {
    Serial.printf("[SENSOR_CFG] 📂 Attempting to load config from: %s\n", configFilePath.c_str());
    
    // CRITICAL FIX: Check file existence BEFORE creating File object
    // Creating File object (even when open fails) allocates PSRAM structures
    // that corrupt heap on destructor if not properly initialized.
    String fullPath = "/littlefs" + configFilePath;
    struct stat st;
    if (stat(fullPath.c_str(), &st) != 0) {
        Serial.println("[SENSOR_CFG] ⚠️ Config file does not exist (will use defaults)");
        delay(50);
        return false;
    }
    
    // STRATEGY: Read file to String in isolated scope to prevent PSRAM File/JSON corruption
    String content;
    {
        // Isolated scope for File - destructor runs at end of this block
        File file = LittleFS.open(configFilePath, "r");
        if (!file) {
            Serial.println("[SENSOR_CFG] ❌ Failed to open existing config file!");
            // File exists but open failed - serious error
            delay(200);
            return false;
        }
        
        Serial.printf("[SENSOR_CFG] ✅ File opened, size: %u bytes\n", file.size());
        
        // Read entire file into a String for both debug logging and JSON parsing.
        // Parsing from the String avoids relying on file.seek(0) after readString(),
        // which can silently fail on some ESP32 LittleFS builds, causing an empty-stream
        // parse error and a full reset back to defaults.
        content = file.readString();
        file.close(); // Only close on successfully opened file
        delay(100);  // CRITICAL: Increased delay for PSRAM cleanup
        // File destructor runs here - after delay, all PSRAM should be clean
    }
    
    delay(50);  // Extra safety margin after File destructor
    
    Serial.println("[SENSOR_CFG] 🔍 File contents:");
    Serial.println(content);
    
    // Use SpiRamJsonDocument: loadFromFile() only writes config fields now (no last_*/stats),
    // so 2048 bytes is sufficient. Using 4096 for safety margin.
    SpiRamJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, content);
    
    if (error) {
        Serial.printf("[SENSOR_CFG] ERROR: JSON parse failed: %s\n", error.c_str());
        return false;
    }
    
    Serial.println("[SENSOR_CFG] ✅ JSON parsed successfully");
    
    // Apply parsed JSON to config structure
    if (fromJson(doc)) {
        Serial.println("[SENSOR_CFG] ✅ Config structure loaded successfully");
        Serial.println("[SENSOR_CFG] 🔍 Verifying loaded values:");
        Serial.printf("[SENSOR_CFG]   - sensor_enabled: %s\n", config.sensor_enabled ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - read_interval: %u seconds\n", config.read_interval);
        Serial.printf("[SENSOR_CFG]   - output_pretty: %s\n", config.output_pretty ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - output_csv: %s\n", config.output_csv ? "true" : "false");
        Serial.printf("[SENSOR_CFG]   - inject_to_buffer: %s\n", config.inject_to_buffer ? "true" : "false");
        return true;
    } else {
        Serial.println("[SENSOR_CFG] ❌ FAILED to apply JSON to config structure");
        return false;
    }
}

bool SensorConfigManager::saveToFile() {
    Serial.printf("[SENSOR_CFG] 📝 Attempting to save config to: %s\n", configFilePath.c_str());
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    // TouchMeteo-7: quiesce RGB around the flash write (T7-20).
    extern void touch7FlashHold(bool hold);
    touch7FlashHold(true);
#endif

    String tmpPath = configFilePath + ".tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) {
        Serial.println("[SENSOR_CFG] ❌ ERROR: Cannot open config file for writing");
        Serial.println("[SENSOR_CFG]    Ensuring /config directory exists...");

        // CRITICAL FIX: Do NOT call close() on failed open!
        // Let destructor handle cleanup naturally.
        delay(200);  // Extra long delay for destructor

        // Create the directory if it is missing; do NOT call LittleFS.begin() again.
        // Use stat() to avoid C++ File destructor PSRAM issues
        struct stat st;
        bool dirExists = (stat("/littlefs/config", &st) == 0 && S_ISDIR(st.st_mode));

        if (!dirExists) {
            LittleFS.mkdir("/config");
            delay(100);   // Increased delay
        }

        file = LittleFS.open(tmpPath, "w");
        if (!file) {
            Serial.println("[SENSOR_CFG] ❌ ERROR: Still cannot open file after mkdir");
            // CRITICAL FIX: Do NOT call close() on failed retry open!
            delay(200);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            touch7FlashHold(false);
#endif
            return false;
        }
    }
    
    // Only persist actual config fields — NOT runtime data (last_*, stats, history_buffer).
    // Runtime data belongs in RAM only.  Keeping the file small ensures loadFromFile()
    // can always parse it with a modest JSON pool (2048 bytes is enough).
    // The web API GET /api/sensor-config still returns all fields via toJson().
    SpiRamJsonDocument doc(2048);

    doc["sensor_enabled"] = config.sensor_enabled;
    doc["read_interval"] = (int)config.read_interval;
    doc["output_pretty"] = config.output_pretty;
    doc["output_csv"] = config.output_csv;
    doc["inject_to_buffer"] = config.inject_to_buffer;
    doc["csv_separator"] = String(config.csv_separator);
    doc["csv_include_timestamp"] = config.csv_include_timestamp;
    doc["csv_include_headers"] = config.csv_include_headers;
    doc["sea_level_pressure_hpa"] = config.sea_level_pressure_hpa;
    
    Serial.println("[SENSOR_CFG] 📝 Saving config values:");
    Serial.printf("[SENSOR_CFG]   - sensor_enabled: %s\n", config.sensor_enabled ? "true" : "false");
    Serial.printf("[SENSOR_CFG]   - read_interval: %d\n", (int)config.read_interval);
    Serial.printf("[SENSOR_CFG]   - output_pretty: %s\n", config.output_pretty ? "true" : "false");
    Serial.printf("[SENSOR_CFG]   - output_csv: %s\n", config.output_csv ? "true" : "false");
    Serial.printf("[SENSOR_CFG]   - inject_to_buffer: %s\n", config.inject_to_buffer ? "true" : "false");

    JsonObject readings = doc.createNestedObject("readings");
    auto addReading = [&](const char* name, const SensorReadingConfig &cfg) {
        JsonObject obj = readings.createNestedObject(name);
        obj["enabled"] = cfg.enabled;
        obj["offset"] = cfg.offset;
        Serial.printf("[SENSOR_CFG]   - %s: enabled=%s, offset=%.2f\n", name, cfg.enabled ? "true" : "false", cfg.offset);
    };
    addReading("temp_aht20",    config.temp_aht20);
    addReading("humidity",      config.humidity);
    addReading("temp_bmp280",   config.temp_bmp280);
    addReading("pressure_hpa",  config.pressure_hpa);
    addReading("pressure_mmhg", config.pressure_mmhg);
    addReading("temp_avg",      config.temp_avg);
    addReading("dew_point",     config.dew_point);
    addReading("heat_index",    config.heat_index);
    addReading("altitude",      config.altitude);

    JsonObject alarmsObj = doc.createNestedObject("alarms");
    alarmsObj["throttle_ms"] = alarms.alarm_throttle_ms;
    alarmsObj["hysteresis_percent"] = alarms.hysteresis_percent;
    auto addAlarm = [&](const char* name, const AlarmThreshold &alarm) {
        JsonObject obj = alarmsObj.createNestedObject(name);
        obj["enabled"] = alarm.enabled;
        obj["min_value"] = alarm.min_value;
        obj["max_value"] = alarm.max_value;
        obj["email_on_alarm"] = alarm.email_on_alarm;
    };
    addAlarm("temperature", alarms.temperature);
    addAlarm("humidity",    alarms.humidity);
    addAlarm("pressure",    alarms.pressure);

    size_t written = serializeJsonPretty(doc, file);
    file.flush(); // CRITICAL: Ensure data is written to LittleFS before closing
    file.close();
    storNoteLfsWrite(written);
    delay(100);  // CRITICAL: Increased delay for PSRAM cleanup

    if (written > 0) {
        LittleFS.remove(configFilePath);
        LittleFS.rename(tmpPath, configFilePath);
        Serial.printf("[SENSOR_CFG] ✅ Config saved successfully (%u bytes written to %s)\n", written, configFilePath.c_str());

        // Verify write by using stat() to avoid C++ File destructor bug
        struct stat st;
        String fullPath = String("/littlefs") + configFilePath;
        if (stat(fullPath.c_str(), &st) == 0) {
            size_t fileSize = st.st_size;
            Serial.printf("[SENSOR_CFG] ✅ Verification: File exists with %u bytes\n", fileSize);
        } else {
            Serial.println("[SENSOR_CFG] ❌ Verification failed: stat() could not read file");
        }
    } else {
        LittleFS.remove(tmpPath);
        Serial.println("[SENSOR_CFG] ❌ ERROR: No bytes written to file");
    }

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    touch7FlashHold(false);
#endif
    return written > 0;
}

bool SensorConfigManager::load() {
    return loadFromFile();
}

bool SensorConfigManager::save() {
    return saveToFile();
}

bool SensorConfigManager::reset() {
    applyDefaults();
    return saveToFile();
}

bool SensorConfigManager::toJson(JsonDocument &doc) const {
    // DEBUG: Log saving process
    Serial.println("[SENSOR_CFG] 💾 toJson() - Serializing configuration...");
    
    doc["sensor_enabled"] = config.sensor_enabled;
    Serial.printf("[SENSOR_CFG]   - sensor_enabled: %s\n", config.sensor_enabled ? "true" : "false");
    
    doc["read_interval"] = (int)config.read_interval;
    Serial.printf("[SENSOR_CFG]   - read_interval: %d\n", (int)config.read_interval);
    
    doc["output_pretty"] = config.output_pretty;
    Serial.printf("[SENSOR_CFG]   - output_pretty: %s\n", config.output_pretty ? "true" : "false");
    
    doc["output_csv"] = config.output_csv;
    Serial.printf("[SENSOR_CFG]   - output_csv: %s\n", config.output_csv ? "true" : "false");
    
    doc["inject_to_buffer"] = config.inject_to_buffer;
    Serial.printf("[SENSOR_CFG]   - inject_to_buffer: %s\n", config.inject_to_buffer ? "true" : "false");
    doc["csv_separator"] = String(config.csv_separator);
    doc["csv_include_timestamp"] = config.csv_include_timestamp;
    doc["csv_include_headers"] = config.csv_include_headers;
    doc["sea_level_pressure_hpa"] = config.sea_level_pressure_hpa;
    
    JsonObject readings = doc.createNestedObject("readings");
    
    auto addReading = [&](const char* name, const SensorReadingConfig &cfg) {
        JsonObject obj = readings.createNestedObject(name);
        obj["enabled"] = cfg.enabled;
        obj["offset"] = cfg.offset;
    };
    
    addReading("temp_aht20", config.temp_aht20);
    addReading("humidity", config.humidity);
    addReading("temp_bmp280", config.temp_bmp280);
    addReading("pressure_hpa", config.pressure_hpa);
    addReading("pressure_mmhg", config.pressure_mmhg);
    addReading("temp_avg", config.temp_avg);
    addReading("dew_point", config.dew_point);
    addReading("heat_index", config.heat_index);
    addReading("altitude", config.altitude);

    // Serialize generic extra sensor slots
    JsonArray extraSlotsArr = doc.createNestedArray("extra_slots");
    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
        JsonObject slot = extraSlotsArr.createNestedObject();
        slot["enabled"]          = config.extra_slots[s].enabled;
        slot["name"]             = config.extra_slots[s].name;
        slot["label"]            = config.extra_slots[s].label;
        slot["unit"]             = config.extra_slots[s].unit;
        slot["scale"]            = config.extra_slots[s].scale;
        slot["offset"]           = config.extra_slots[s].offset;
        slot["inject_to_buffer"] = config.extra_slots[s].inject_to_buffer;
    }

    // Serialize alarms
    JsonObject alarmsObj = doc.createNestedObject("alarms");
    alarmsObj["throttle_ms"] = alarms.alarm_throttle_ms;
    alarmsObj["hysteresis_percent"] = alarms.hysteresis_percent;
    
    auto addAlarm = [&](const char* name, const AlarmThreshold &alarm) {
        JsonObject obj = alarmsObj.createNestedObject(name);
        obj["enabled"] = alarm.enabled;
        obj["min_value"] = alarm.min_value;
        obj["max_value"] = alarm.max_value;
        obj["email_on_alarm"] = alarm.email_on_alarm;
    };
    
    addAlarm("temperature", alarms.temperature);
    addAlarm("humidity", alarms.humidity);
    addAlarm("pressure", alarms.pressure);
    
    // Last reading values (for web UI display)
    doc["last_temp_aht20"] = config.last_temp_aht20;
    doc["last_humidity"] = config.last_humidity;
    doc["last_temp_bmp280"] = config.last_temp_bmp280;
    doc["last_pressure_hpa"] = config.last_pressure_hpa;
    doc["last_pressure_mmhg"] = config.last_pressure_mmhg;
    doc["last_temp_avg"] = config.last_temp_avg;
    doc["last_dew_point"] = config.last_dew_point;
    doc["last_heat_index"] = config.last_heat_index;
    doc["last_altitude"] = config.last_altitude;
    doc["last_reading_time"] = config.last_reading_time;
    
    // Serialize 24h stats (current values only, window resets on load)
    JsonObject statsObj = doc.createNestedObject("stats");
    
    // Add window time information
    if (stats.temperature.window_start > 0) {
        unsigned long window_age = (millis() - stats.temperature.window_start) / 1000;
        unsigned long hours = window_age / 3600;
        unsigned long minutes = (window_age % 3600) / 60;
        statsObj["window_hours"] = hours;
        statsObj["window_minutes"] = minutes;
    } else {
        statsObj["window_hours"] = 0;
        statsObj["window_minutes"] = 0;
    }
    
    if (stats.temperature.isValid()) {
        JsonObject tempStats = statsObj.createNestedObject("temperature");
        tempStats["min"] = stats.temperature.min_value;
        tempStats["max"] = stats.temperature.max_value;
        tempStats["avg"] = stats.temperature.getAverage();
        tempStats["count"] = stats.temperature.count;
    }
    if (stats.humidity.isValid()) {
        JsonObject humStats = statsObj.createNestedObject("humidity");
        humStats["min"] = stats.humidity.min_value;
        humStats["max"] = stats.humidity.max_value;
        humStats["avg"] = stats.humidity.getAverage();
        humStats["count"] = stats.humidity.count;
    }
    if (stats.pressure.isValid()) {
        JsonObject pressStats = statsObj.createNestedObject("pressure");
        pressStats["min"] = stats.pressure.min_value;
        pressStats["max"] = stats.pressure.max_value;
        pressStats["avg"] = stats.pressure.getAverage();
        pressStats["count"] = stats.pressure.count;
    }
    
    // Add history buffer info for diagnostics
    JsonObject historyInfo = doc.createNestedObject("history_buffer");
    historyInfo["count"] = history.getCount();
    historyInfo["capacity"] = HISTORY_BUFFER_SIZE;
    historyInfo["save_interval_ms"] = 300000; // 5 minutes
    historyInfo["initialized"] = (history.getCount() >= 0); // Buffer exists if we can read count
    
    return true;
}

bool SensorConfigManager::fromJson(const JsonDocument &doc) {
    // DEBUG: Log loading process
    Serial.println("[SENSOR_CFG] 🔍 fromJson() - Loading configuration...");
    
    config.sensor_enabled = doc["sensor_enabled"] | true;
    Serial.printf("[SENSOR_CFG]   - sensor_enabled: %s\n", config.sensor_enabled ? "true" : "false");
    
    config.read_interval = (SensorInterval)(doc["read_interval"] | INTERVAL_1M);
    Serial.printf("[SENSOR_CFG]   - read_interval: %d\n", (int)config.read_interval);
    
    config.output_pretty = doc["output_pretty"] | false;  // Default: off
    Serial.printf("[SENSOR_CFG]   - output_pretty: %s\n", config.output_pretty ? "true" : "false");
    
    config.output_csv = doc["output_csv"] | true;  // Default: on (import format)
    Serial.printf("[SENSOR_CFG]   - output_csv: %s\n", config.output_csv ? "true" : "false");
    
    config.inject_to_buffer = doc["inject_to_buffer"] | true;  // Default: enabled for better UX
    Serial.printf("[SENSOR_CFG]   - inject_to_buffer: %s\n", config.inject_to_buffer ? "true" : "false");
    
    String sep = doc["csv_separator"] | ",";
    config.csv_separator = sep.length() > 0 ? sep[0] : ',';
    
    config.csv_include_timestamp = doc["csv_include_timestamp"] | true;
    config.csv_include_headers = doc["csv_include_headers"] | true;
    config.sea_level_pressure_hpa = doc["sea_level_pressure_hpa"] | 1013.25f;
    
    JsonObjectConst readings = doc["readings"].as<JsonObjectConst>();
    if (readings) {
        Serial.println("[SENSOR_CFG] 📖 Loading readings configuration...");
        auto loadReading = [&](const char* name, SensorReadingConfig &cfg) {
            JsonObjectConst obj = readings[name].as<JsonObjectConst>();
            if (obj) {
                cfg.enabled = obj["enabled"] | true;
                cfg.offset = obj["offset"] | 0.0f;
                Serial.printf("[SENSOR_CFG]   - %s: enabled=%s, offset=%.2f\n", name, cfg.enabled ? "true" : "false", cfg.offset);
            }
        };
        
        loadReading("temp_aht20", config.temp_aht20);
        loadReading("humidity", config.humidity);
        loadReading("temp_bmp280", config.temp_bmp280);
        loadReading("pressure_hpa", config.pressure_hpa);
        loadReading("pressure_mmhg", config.pressure_mmhg);
        loadReading("temp_avg", config.temp_avg);
        loadReading("dew_point", config.dew_point);
        loadReading("heat_index", config.heat_index);
        loadReading("altitude", config.altitude);
    } else {
        Serial.println("[SENSOR_CFG] ⚠️ WARNING: No 'readings' object found in config, using defaults");
    }
    
    // Load alarms
    JsonObjectConst alarmsObj = doc["alarms"].as<JsonObjectConst>();
    if (alarmsObj) {
        alarms.alarm_throttle_ms = alarmsObj["throttle_ms"] | 3600000UL;
        alarms.hysteresis_percent = alarmsObj["hysteresis_percent"] | 2.0f;
        
        auto loadAlarm = [&](const char* name, AlarmThreshold &alarm) {
            JsonObjectConst obj = alarmsObj[name].as<JsonObjectConst>();
            if (obj) {
                alarm.enabled = obj["enabled"] | false;
                alarm.min_value = obj["min_value"] | alarm.min_value;
                alarm.max_value = obj["max_value"] | alarm.max_value;
                alarm.email_on_alarm = obj["email_on_alarm"] | false;
            }
        };
        
        loadAlarm("temperature", alarms.temperature);
        loadAlarm("humidity", alarms.humidity);
        loadAlarm("pressure", alarms.pressure);
    }
    
    return true;
}

String SensorConfigManager::toJsonString() const {
    // Build JSON manually — avoids allocating a 8192 B SpiRamJsonDocument on the heap.
    // The doc was the largest transient allocation in the sensor subsystem.
    String j;
    j.reserve(1400);

    auto bv  = [&](const char *k, bool v,   bool comma=true) { j += '"'; j += k; j += "\":"; j += v ? F("true") : F("false"); if (comma) j += ','; };
    auto iv  = [&](const char *k, int v,    bool comma=true) { j += '"'; j += k; j += "\":"; j += v; if (comma) j += ','; };
    auto fv  = [&](const char *k, float v,  int dec=2, bool comma=true) { j += '"'; j += k; j += "\":"; j += String(v, dec); if (comma) j += ','; };
    auto sv  = [&](const char *k, const char *v, bool comma=true) { j += '"'; j += k; j += "\":\""; j += v; j += '"'; if (comma) j += ','; };
    auto ulv = [&](const char *k, unsigned long v, bool comma=true) { j += '"'; j += k; j += "\":"; j += v; if (comma) j += ','; };

    j += '{';

    bv("sensor_enabled",        config.sensor_enabled);
    iv("read_interval",         (int)config.read_interval);
    bv("output_pretty",         config.output_pretty);
    bv("output_csv",            config.output_csv);
    bv("inject_to_buffer",      config.inject_to_buffer);
    { char sep[2] = { config.csv_separator, 0 }; sv("csv_separator", sep); }
    bv("csv_include_timestamp", config.csv_include_timestamp);
    bv("csv_include_headers",   config.csv_include_headers);
    fv("sea_level_pressure_hpa", config.sea_level_pressure_hpa);

    // readings object
    j += F("\"readings\":{");
    auto addReading = [&](const char *name, const SensorReadingConfig &cfg, bool comma=true) {
        j += '"'; j += name; j += F("\":{\"enabled\":");
        j += cfg.enabled ? F("true") : F("false");
        j += F(",\"offset\":"); j += String(cfg.offset, 2);
        j += '}'; if (comma) j += ',';
    };
    addReading("temp_aht20",    config.temp_aht20);
    addReading("humidity",      config.humidity);
    addReading("temp_bmp280",   config.temp_bmp280);
    addReading("pressure_hpa",  config.pressure_hpa);
    addReading("pressure_mmhg", config.pressure_mmhg);
    addReading("temp_avg",      config.temp_avg);
    addReading("dew_point",     config.dew_point);
    addReading("heat_index",    config.heat_index);
    addReading("altitude",      config.altitude, false);
    j += F("},");

    // extra_slots array
    j += F("\"extra_slots\":[");
    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
        const auto &sl = config.extra_slots[s];
        j += F("{\"enabled\":");  j += sl.enabled ? F("true") : F("false");
        j += F(",\"name\":\"");   j += sl.name;   j += '"';
        j += F(",\"label\":\"");  j += sl.label;  j += '"';
        j += F(",\"unit\":\"");   j += sl.unit;   j += '"';
        j += F(",\"scale\":"); j += String(sl.scale, 4);
        j += F(",\"offset\":"); j += String(sl.offset, 4);
        j += F(",\"inject_to_buffer\":"); j += sl.inject_to_buffer ? F("true") : F("false");
        j += '}'; if (s < EXTRA_SENSOR_SLOTS - 1) j += ',';
    }
    j += F("],");

    // alarms object
    j += F("\"alarms\":{");
    ulv("throttle_ms",          alarms.alarm_throttle_ms);
    fv("hysteresis_percent",    alarms.hysteresis_percent, 2, false);
    auto addAlarm = [&](const char *name, const AlarmThreshold &al, bool comma=true) {
        j += ','; j += '"'; j += name;
        j += F("\":{\"enabled\":"); j += al.enabled ? F("true") : F("false");
        j += F(",\"min_value\":"); j += String(al.min_value, 2);
        j += F(",\"max_value\":"); j += String(al.max_value, 2);
        j += F(",\"email_on_alarm\":"); j += al.email_on_alarm ? F("true") : F("false");
        j += '}'; if (!comma) (void)0;
    };
    addAlarm("temperature", alarms.temperature);
    addAlarm("humidity",    alarms.humidity);
    addAlarm("pressure",    alarms.pressure, false);
    j += F("},");

    // last reading values
    fv("last_temp_aht20",    config.last_temp_aht20);
    fv("last_humidity",      config.last_humidity);
    fv("last_temp_bmp280",   config.last_temp_bmp280);
    fv("last_pressure_hpa",  config.last_pressure_hpa, 1);
    fv("last_pressure_mmhg", config.last_pressure_mmhg, 1);
    fv("last_temp_avg",      config.last_temp_avg);
    fv("last_dew_point",     config.last_dew_point);
    fv("last_heat_index",    config.last_heat_index);
    fv("last_altitude",      config.last_altitude, 1);
    ulv("last_reading_time", config.last_reading_time);

    // stats object
    j += F("\"stats\":{");
    unsigned long window_hours = 0, window_minutes = 0;
    if (stats.temperature.window_start > 0) {
        unsigned long age = (millis() - stats.temperature.window_start) / 1000;
        window_hours   = age / 3600;
        window_minutes = (age % 3600) / 60;
    }
    iv("window_hours",   (int)window_hours);
    iv("window_minutes", (int)window_minutes, false);
    auto addStat = [&](const char *name, const auto &st) {
        if (!st.isValid()) return;
        j += F(",\""); j += name; j += F("\":{");
        j += F("\"min\":"); j += String(st.min_value, 2);
        j += F(",\"max\":"); j += String(st.max_value, 2);
        j += F(",\"avg\":"); j += String(st.getAverage(), 2);
        j += F(",\"count\":"); j += st.count;
        j += '}';
    };
    addStat("temperature", stats.temperature);
    addStat("humidity",    stats.humidity);
    addStat("pressure",    stats.pressure);
    j += F("},");

    // history_buffer info
    j += F("\"history_buffer\":{");
    iv("count",            (int)history.getCount());
    iv("capacity",         (int)HISTORY_BUFFER_SIZE);
    iv("save_interval_ms", 300000);
    bv("initialized",      true, false);
    j += '}';

    j += '}';
    return j;
}

bool SensorConfigManager::fromJsonString(const String &jsonStr) {
    // Incoming POST body only contains settings fields (no stats/history), but 4096 provides
    // a safe margin in case the client sends extra fields or alarms expand in the future.
    SpiRamJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        Serial.printf("[SENSOR_CFG] JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    return fromJson(doc);
}

bool SensorConfigManager::shouldAutoRead() {
#if ENABLE_AHT20_BMP280==1
    if (!config.sensor_enabled || !environmentalSensor.isInitialized()) {
        return false;
    }
#elif ENABLE_MQ135==1
    // No AHT20 — allow the periodic ticker to proceed so MQ-135 can be read
    (void)0;
#else
    return false; // No active sensor
#endif

    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)config.read_interval * 1000UL;
    
    if (lastAutoReadTime == 0 || (now - lastAutoReadTime) >= intervalMs) {
        return true;
    }
    
    return false;
}

void SensorConfigManager::markReadingDone() {
    lastAutoReadTime = millis();
}

bool SensorConfigManager::performReading() {
#if ENABLE_AHT20_BMP280==1
    if (!config.sensor_enabled || !environmentalSensor.isInitialized()) {
        Serial.println("[SENSOR_CFG] Sensor not enabled or not initialized");
        return false;
    }
    
    // Force sensor update
    if (!environmentalSensor.forceUpdate()) {
        Serial.println("[SENSOR_CFG] Sensor read failed");
        return false;
    }
    
    // Get raw reading
    SensorReading raw = environmentalSensor.getLastReading();
    if (!raw.valid) {
        Serial.println("[SENSOR_CFG] Invalid sensor reading");
        return false;
    }
    
    // Apply offsets and store calibrated values
    config.last_temp_aht20 = raw.temperature_aht20 + config.temp_aht20.offset;
    config.last_humidity = raw.humidity + config.humidity.offset;
    config.last_temp_bmp280 = raw.temperature_bmp280 + config.temp_bmp280.offset;
    config.last_pressure_hpa = raw.pressure + config.pressure_hpa.offset;
    config.last_pressure_mmhg = (config.last_pressure_hpa * 0.750062f) + config.pressure_mmhg.offset;
    
    // Computed values (apply offsets to computed results)
    config.last_temp_avg = environmentalSensor.computeAverageTemperature() + config.temp_avg.offset;
    config.last_dew_point = environmentalSensor.computeDewPoint() + config.dew_point.offset;
    config.last_heat_index = environmentalSensor.computeHeatIndex() + config.heat_index.offset;
    
    // Altitude calculation (requires BMP280)
    if (environmentalSensor.isBMP280Available()) {
        // Barometric formula: h = 44330 * (1 - (P/P0)^(1/5.255))
        float ratio = config.last_pressure_hpa / config.sea_level_pressure_hpa;
        config.last_altitude = 44330.0f * (1.0f - pow(ratio, 1.0f/5.255f)) + config.altitude.offset;
    } else {
        config.last_altitude = 0.0f;
    }
#endif // ENABLE_AHT20_BMP280==1
    
    // Get Unix timestamp (only if NTP synced, otherwise use 0 to indicate invalid)
    time_t now = time(nullptr);
    if (now > 100000) {
        // Valid timestamp (NTP synced - epoch > 1970)
        static bool ntpSyncNotified = false;
        if (!ntpSyncNotified) {
            Serial.println("[SENSOR_CFG] ✅ NTP synced - timestamps now valid for sensor readings");
            ntpSyncNotified = true;
        }
        config.last_reading_time = now;
    } else {
        // NTP not yet synced - wait for next reading
        Serial.println("[SENSOR_CFG] ⏳ Warning: NTP not synced yet, timestamp unavailable");
        config.last_reading_time = 0; // Mark as invalid
    }
    
    // Update 24h statistics
    updateStats();
    
    // Stage MQ-135 extra slot values BEFORE updateHistory() calls addPoint()
#if ENABLE_MQ135==1
    if (mq135ConfigManager.isEnabled()) {
        // Trigger a new read only when MQ-135 own interval has elapsed
        if (mq135ConfigManager.shouldAutoRead()) {
            mq135ConfigManager.performReading(config.last_temp_avg, config.last_humidity);
        }
        // Always stage last known values (fills extra slots in the history point)
        mq135ConfigManager.stageHistorySlots(history);
    }
#endif

    // Update history buffer (if interval elapsed)
    Serial.println("[SENSOR_CFG] \xf0\x9f\x93\x8a Calling updateHistory()...");
    updateHistory();
    
    // Check alarm thresholds
    checkAlarms();

    // Inject formatted reading into the monitor buffer (display + email rule matching)
    injectToBuffer();

    Serial.println("[SENSOR_CFG] ✅ Reading completed successfully");
    return true;
}



// Optimized pretty formatting with minimal allocations
String SensorConfigManager::formatPretty() const {
    static char buf[512];
    char *ptr = buf;
    int remaining = sizeof(buf);
    int written;
    
    // Compact single-line format: Temp: 14.99°C | Hum: 77.13% | Press: 986.73hPa | Alt: 152.3m
    bool first = true;
    
    if (config.temp_aht20.enabled) {
        written = snprintf(ptr, remaining, "%s🌡️ %.2f°C", first ? "" : " | ", config.last_temp_aht20);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.humidity.enabled) {
        written = snprintf(ptr, remaining, "%s💧 %.2f%%", first ? "" : " | ", config.last_humidity);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.temp_bmp280.enabled && !config.temp_aht20.enabled) {
        written = snprintf(ptr, remaining, "%s🌡️(BMP) %.2f°C", first ? "" : " | ", config.last_temp_bmp280);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.pressure_hpa.enabled) {
        written = snprintf(ptr, remaining, "%s🌐 %.2fhPa", first ? "" : " | ", config.last_pressure_hpa);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.pressure_mmhg.enabled && !config.pressure_hpa.enabled) {
        written = snprintf(ptr, remaining, "%s🌐 %.2fmmHg", first ? "" : " | ", config.last_pressure_mmhg);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.temp_avg.enabled && config.temp_aht20.enabled && config.temp_bmp280.enabled) {
        written = snprintf(ptr, remaining, "%s📈Avg %.2f°C", first ? "" : " | ", config.last_temp_avg);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.dew_point.enabled) {
        written = snprintf(ptr, remaining, "%s💧Dew %.2f°C", first ? "" : " | ", config.last_dew_point);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.heat_index.enabled && config.last_temp_aht20 >= 27.0f) {
        written = snprintf(ptr, remaining, "%s🔥HI %.2f°C", first ? "" : " | ", config.last_heat_index);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    if (config.altitude.enabled) {
        written = snprintf(ptr, remaining, "%s⛰️ %.1fm", first ? "" : " | ", config.last_altitude);
        if (written > 0) { ptr += written; remaining -= written; }
        first = false;
    }
    
    // Add newline
    if (remaining > 1) {
        *ptr++ = '\n';
        *ptr = '\0';
    }
    
    return String(buf);
}

String SensorConfigManager::formatCSV(bool includeHeaders) const {
    String output;
    output.reserve(256);

    // Build the separator string used between fields (e.g. ", " for comma, "; " for semicolon).
    // A trailing space improves readability in most CSV viewers.
    String sep = String(config.csv_separator) + " ";

    // Optionally prepend a human-readable timestamp as the first column.
    // Format: "YYYY-MM-DD HH:MM:SS<sep>"
    // Uses last_reading_time (Unix epoch) set when the reading was taken.
    if (config.csv_include_timestamp) {
        if (config.last_reading_time > 0) {
            char tsBuf[24];
            time_t ts = (time_t)config.last_reading_time;
            struct tm ti;
            localtime_r(&ts, &ti);
            strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &ti);
            output += tsBuf;
        } else {
            // NTP not yet synced — write a placeholder so column count stays consistent.
            output += "0000-00-00 00:00:00";
        }
        output += sep; // comma (or configured separator) between timestamp and first data field
    }

    // Flat key-value pair format: "key<sep>value<sep>key<sep>value ..."
    // Example: "temp_aht20, 19.30, humidity, 62.78, pressure_hpa, 1013.25"
    bool first = true;
    auto addPair = [&](const char* key, const String &val) {
        if (!first) output += sep;
        first = false;
        output += key;
        output += sep;
        output += val;
    };

    if (config.temp_aht20.enabled)     addPair("temp_aht20",    String(config.last_temp_aht20, 2));
    if (config.humidity.enabled)       addPair("humidity",      String(config.last_humidity, 2));
    if (config.temp_bmp280.enabled)    addPair("temp_bmp280",   String(config.last_temp_bmp280, 2));
    if (config.pressure_hpa.enabled)   addPair("pressure_hpa",  String(config.last_pressure_hpa, 2));
    if (config.pressure_mmhg.enabled)  addPair("pressure_mmhg", String(config.last_pressure_mmhg, 2));
    if (config.temp_avg.enabled)       addPair("temp_avg",      String(config.last_temp_avg, 2));
    if (config.dew_point.enabled)      addPair("dew_point",     String(config.last_dew_point, 2));
    if (config.heat_index.enabled)     addPair("heat_index",    String(config.last_heat_index, 2));
    if (config.altitude.enabled)       addPair("altitude",      String(config.last_altitude, 1));

    output += "\n"; // one reading per line in the buffer
    return output;
}

void SensorConfigManager::injectToBuffer() {
    if (!config.inject_to_buffer) {
        Serial.println(F("[SENSOR] Injection skipped: inject_to_buffer=false"));
        return;
    }
    
    String data;
    
    if (config.output_pretty && config.output_csv) {
        // Both enabled: send pretty first, then CSV
        data = formatPretty();
        data += "\n";
        data += formatCSV(true);  // Include headers
    } else if (config.output_pretty) {
        data = formatPretty();
    } else if (config.output_csv) {
        static bool firstCsvSent = false;
        data = formatCSV(!firstCsvSent);  // Headers only on first send
        firstCsvSent = true;
    } else {
        // Neither enabled, use CSV as default
        data = formatCSV(false);
    }
    
    // Call external injection function
    Serial.printf("[SENSOR] Injecting %d bytes to buffer (pretty=%d, csv=%d)\n", data.length(), config.output_pretty, config.output_csv);
    injectDataToRXBuffer(data, "SENSOR");
}

// ====================================================================================
// Alarm Management
// ====================================================================================

bool SensorConfigManager::checkAlarm(AlarmThreshold &alarm, float value, const String &name) {
    if (!alarm.enabled) {
        return false;
    }
    
    unsigned long now = millis();
    bool wasAlarming = alarm.currently_alarming;
    bool isAlarming = false;
    String alarmType = "";
    float threshold = 0.0f;
    bool isHigh = false;
    
    // Calculate hysteresis band
    float hysteresis = (alarm.max_value - alarm.min_value) * (alarms.hysteresis_percent / 100.0f);
    
    // Check thresholds with hysteresis
    if (value > alarm.max_value) {
        // High alarm
        isAlarming = true;
        alarmType = name + " HIGH";
        threshold = alarm.max_value;
        isHigh = true;
    } else if (value < alarm.min_value) {
        // Low alarm
        isAlarming = true;
        alarmType = name + " LOW";
        threshold = alarm.min_value;
        isHigh = false;
    } else if (wasAlarming) {
        // Check if we've re-entered safe zone (with hysteresis)
        if (value < (alarm.max_value - hysteresis) && value > (alarm.min_value + hysteresis)) {
            isAlarming = false;
        } else {
            isAlarming = true;  // Stay in alarm state until hysteresis clears
        }
    }
    
    // Update alarm state
    alarm.currently_alarming = isAlarming;
    
    // Send email if:
    // 1. Currently alarming
    // 2. Email enabled for this alarm
    // 3. Throttle period has elapsed
    // 4. State changed from non-alarming to alarming (edge trigger)
    if (isAlarming && !wasAlarming && alarm.email_on_alarm) {
        if ((now - alarm.last_alarm_time) > alarms.alarm_throttle_ms) {
            sendAlarmEmail(alarmType, value, threshold, isHigh);
            alarm.last_alarm_time = now;
            return true;  // Alarm triggered and email sent
        }
    }
    
    return false;
}

void SensorConfigManager::sendAlarmEmail(const String &alarmType, float value, float threshold, bool isHigh) {
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    String subject = String(F("\xF0\x9F\x9A\xA8 - ")) + buildEmailTitlePrefix() +
                     String(F(" - ")) + L(activeLang->emailSensorAlert) + String(F(" - ")) + alarmType +
                     L(activeLang->emailFoundPrefix) + String(value, 2) + String(F(")"));

    String opDesc  = isHigh ? String(F("> ")) + L(activeLang->opGreaterThan) + String(F(" ")) + String(threshold, 2)
                            : String(F("< ")) + L(activeLang->opLessThan) + String(F(" ")) + String(threshold, 2);
    String valStr  = String(value, 2);
    String sensors = formatPretty();

    String body = buildEmailInfoTable("", alarmType, opDesc, valStr, L(activeLang->emailAlarmImmediate), sensors, valStr, "");

    // Log locally
    Serial.println("[SENSOR_ALARM] " + alarmType + ": " + String(value, 2));

    // Queue email via existing system (needs external function)
    // This will be handled by checking a global flag or queue in chatgpt.ino
    extern void enqueueSensorAlarmEmail(const String &subject, const String &body);
    enqueueSensorAlarmEmail(subject, body);
#else
    // No email transport in this build (e.g. TouchMeteo-7 pack with SMTP=0):
    // alarm state tracking above still works; local log only. (Local voice
    // alarms are handled separately by the pack.)
    (void)threshold;
    (void)isHigh;
    Serial.println("[SENSOR_ALARM] " + alarmType + ": " + String(value, 2));
#endif
}

void SensorConfigManager::checkAlarms() {
    if (!config.sensor_enabled) {
        return;
    }
    
    // Check each alarm type
    checkAlarm(alarms.temperature, config.last_temp_avg, "Temperature");
    checkAlarm(alarms.humidity, config.last_humidity, "Humidity");
    checkAlarm(alarms.pressure, config.last_pressure_hpa, "Pressure");
}

// ====================================================================================
// Statistics Management
// ====================================================================================

void SensorConfigManager::updateStats() {
    if (!config.sensor_enabled) {
        return;
    }
    
    // Check if 24h window expired
    stats.checkWindowExpiry();
    
    // Initialize window if first reading
    if (stats.temperature.window_start == 0) {
        stats.reset();
    }
    
    // Update statistics
    stats.update(config.last_temp_avg, config.last_humidity, config.last_pressure_hpa);
}

void SensorConfigManager::resetStats() {
    stats.reset();
    Serial.println("[SENSOR_STATS] Statistics reset");
}

String SensorConfigManager::formatStats() const {
    String output;
    output.reserve(512);
    
    output += "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    output += "       24-HOUR STATISTICS\n";
    output += "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    unsigned long window_age = (millis() - stats.temperature.window_start) / 1000;
    unsigned long hours = window_age / 3600;
    unsigned long minutes = (window_age % 3600) / 60;
    
    output += "Window: " + String(hours) + "h " + String(minutes) + "m\n";
    output += "Samples: " + String(stats.temperature.count) + "\n\n";
    
    if (stats.temperature.isValid()) {
        output += "🌡️  TEMPERATURE\n";
        output += "   Min: " + String(stats.temperature.min_value, 2) + " °C\n";
        output += "   Max: " + String(stats.temperature.max_value, 2) + " °C\n";
        output += "   Avg: " + String(stats.temperature.getAverage(), 2) + " °C\n\n";
    }
    
    if (stats.humidity.isValid()) {
        output += "💧 HUMIDITY\n";
        output += "   Min: " + String(stats.humidity.min_value, 2) + " %RH\n";
        output += "   Max: " + String(stats.humidity.max_value, 2) + " %RH\n";
        output += "   Avg: " + String(stats.humidity.getAverage(), 2) + " %RH\n\n";
    }
    
    if (stats.pressure.isValid()) {
        output += "🌐 PRESSURE\n";
        output += "   Min: " + String(stats.pressure.min_value, 2) + " hPa\n";
        output += "   Max: " + String(stats.pressure.max_value, 2) + " hPa\n";
        output += "   Avg: " + String(stats.pressure.getAverage(), 2) + " hPa\n\n";
    }
    
    output += "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    
    return output;
}

// ====================================================================================
// History Management
// ====================================================================================

// RTC RAM mirror of the newest ring samples (RTC_SLOW_MEM — SRAM, zero flash
// wear). Survives esp_restart/OTA/WDT/deep sleep; a power cut clears it, in
// which case the SD daily .chart head (merge window) is the recovery path.
// magic is written LAST so a crash mid-mirror keeps the previous snapshot.
RTC_NOINIT_ATTR static SensorRtcTail s_sensorRtc;
// True once the RTC tail has been re-seeded into the ring this boot.
static bool s_sensorSeeded = false;

// Re-seed the post-boot ring from the RTC mirror after the first NTP-synced
// reading. Replaces the one or two pre-sync millis-timestamp bogus samples and
// hands the merge window real pre-reboot values (oldest→newest). No-op when the
// mirror is invalid (power-on / first boot) or the ring already holds data.
static bool _sensorSeedFromRtc(SensorHistory& h) {
    if (s_sensorSeeded) return false;
    if (!sensorRtcTailValid(s_sensorRtc.magic, s_sensorRtc.version, s_sensorRtc.count)) return false;
    if (h.getCount() > 4) return false; // only an early-boot ring is reseeded
    uint16_t n = s_sensorRtc.count;
    h.clear();
    for (uint16_t i = 0; i < n && i < h.capacity; i++) {
        const SensorRtcTailPoint& t = s_sensorRtc.p[i];
        HistoryPoint pt;
        pt = HistoryPoint(); // zero-init
        pt.timestamp       = t.timestamp;
        pt.temp_aht20_x10  = t.temp_aht20_x10;
        pt.temp_bmp280_x10 = t.temp_bmp280_x10;
        pt.temp_avg_x10    = t.temp_avg_x10;
        pt.humidity_x10    = t.humidity_x10;
        pt.pressure        = t.pressure;
        pt.dew_point_x10   = t.dew_point_x10;
        pt.heat_index_x10  = t.heat_index_x10;
        pt.altitude_m      = t.altitude_m;
        memcpy(pt.extra, t.extra, sizeof(pt.extra));
        h.buffer[h.head] = pt;
        h.head = (h.head + 1) % h.capacity;
        if (h.count < h.capacity) h.count++;
    }
    h.last_save_time = millis();
    s_sensorSeeded = true;
    sendVirtualMessage(F("Sensor history: ring seeded from RTC tail (pre-reboot samples)"), INFO);
    return true;
}

// Mirror the newest ring samples into RTC RAM. Called after every addPoint().
static void _sensorRtcMirror(SensorHistory& h) {
    uint16_t n      = sensorTailLen(h.getCount(), SENSOR_RTC_TAIL_SAMPLES);
    uint16_t start  = sensorTailStartPos(h.head, h.getCount(), h.capacity, n);
    for (uint16_t i = 0; i < n; i++) {
        const HistoryPoint& pt = h.buffer[(start + i) % h.capacity];
        SensorRtcTailPoint& t  = s_sensorRtc.p[i];
        t.timestamp       = pt.timestamp;
        t.temp_aht20_x10  = pt.temp_aht20_x10;
        t.temp_bmp280_x10 = pt.temp_bmp280_x10;
        t.temp_avg_x10    = pt.temp_avg_x10;
        t.humidity_x10    = pt.humidity_x10;
        t.pressure        = pt.pressure;
        t.dew_point_x10   = pt.dew_point_x10;
        t.heat_index_x10  = pt.heat_index_x10;
        t.altitude_m      = pt.altitude_m;
        memcpy(t.extra, pt.extra, sizeof(t.extra));
    }
    s_sensorRtc.count   = n;
    s_sensorRtc.version = SENSOR_RTC_VERSION;
    s_sensorRtc.reserved= 0;
    s_sensorRtc.magic   = SENSOR_RTC_MAGIC; // LAST: a partial mirror keeps the old magic
}

void SensorConfigManager::updateHistory() {
    // Called after every physical sensor reading to store data in PSRAM history buffer.
    
// ALWAYS save new readings (don't throttle based on save_interval)
    // The save_interval should only control SD card writes, not buffer adds

    // Debug: log current values being saved
    Serial.printf("[SENSOR_HISTORY] Saving point: aht20=%.1f bmp=%.1f avg=%.1f hum=%.1f press=%.0fhPa dp=%.1f hi=%.1f alt=%.0fm\n",
                 config.last_temp_aht20, config.last_temp_bmp280, config.last_temp_avg,
                 config.last_humidity, config.last_pressure_hpa,
                 config.last_dew_point, config.last_heat_index, config.last_altitude);

    // Buffer is circular: oldest entry is overwritten automatically when full.
    // Weekly SD archive is disabled — daily files on SD are sufficient.

    // RTC-tail recovery: on the first NTP-synced reading after boot, reseed the
    // ring with the pre-reboot mirror (soft-reboot bridges the chart gap without
    // any flash write). Power-cut data still comes from the SD .chart head.
    static bool s_seedChecked = false;
    if (!s_seedChecked && time(nullptr) > 1000000000) {
        s_seedChecked = true;
        _sensorSeedFromRtc(history);
    }

    // Store all sensor values in the history point
    history.addPoint(config.last_temp_aht20, config.last_temp_bmp280, config.last_temp_avg,
                     config.last_humidity, config.last_pressure_hpa,
                     config.last_dew_point, config.last_heat_index, config.last_altitude);
    Serial.printf("[SENSOR_HISTORY] Point saved (%u/%u = %.1f%%)\n",
                  history.getCount(), HISTORY_BUFFER_SIZE,
                  (history.getCount() * 100.0f) / HISTORY_BUFFER_SIZE);

    // RTC RAM mirror (no flash wear) — refresh after every reading so a soft
    // reboot loses at most one sample.
    _sensorRtcMirror(history);
}

void SensorConfigManager::clearHistory() {
    history.clear();
    Serial.println("[SENSOR_HISTORY] History cleared");
}

void SensorConfigManager::periodicPersist() {
    if (!initialized) return;

    unsigned long now = millis();

    // --- SD card: flush at the configured MicroSD flush interval (default 30m,
    // page-configurable 30m/20m/10m/...) so power-cut recovery integrated with
    // the merge window is as fresh as the user chose. Floor of 5 min guards
    // against accidental SD hammering. ---
    // Initialized to millis() so the first flush waits the full interval after
    // boot (config is freshly loaded on boot, an immediate flush writes nothing).
    static unsigned long lastSDFlush = millis();
    extern ConfigManager configManager;
    unsigned long sdIntervalMs = intervalToMs(stringToInterval(configManager.getSdcardFlushInterval()));
    if (sdIntervalMs < 5UL * 60UL * 1000UL) sdIntervalMs = 5UL * 60UL * 1000UL;
    
    if ((now - lastSDFlush) >= sdIntervalMs) {
        Serial.printf("[SENSOR_PERSIST] Running periodic flush (interval: %lus)...\n",
                      sdIntervalMs / 1000);
#if ENABLE_SDCARD==1
        if (sdcardShouldUseSD()) {
            bool ok = history.periodicFlushToSD(config);
            Serial.printf("[SENSOR_PERSIST] SD flush %s\n", ok ? "OK" : "no new data");
        } else {
            // SD absent: periodicFlushToSD self-routes (LittleFS mirror on
            // FS packs, PSRAM VFS otherwise). Keep PSRAM as last resort so
            // packs without LittleFS lose nothing.
            bool ok = history.periodicFlushToSD(config);
            if (!ok) ok = history.periodicFlushToPSRAM(config);
            Serial.printf("[SENSOR_PERSIST] SD absent/fallback; Flash/PSRAM flush %s\n", ok ? "OK" : "no new data");
        }
#else
        // SD not compiled: flush to PSRAM VFS
        bool ok = history.periodicFlushToPSRAM(config);
        Serial.printf("[SENSOR_PERSIST] PSRAM flush %s\n", ok ? "OK" : "no new data");
#endif
        lastSDFlush = now;
    }

    // --- LittleFS: auto-save config every 8 hours ---
    // Initialize to millis() so first save waits the full interval after boot.
    // Config was just loaded from file at boot — no need to re-save immediately.
    static unsigned long lastLFSSave = millis();
    const unsigned long LFS_SAVE_INTERVAL_MS = 8UL * 60UL * 60UL * 1000UL; // 8 hours
    if ((now - lastLFSSave) >= LFS_SAVE_INTERVAL_MS) {
        Serial.println("[SENSOR_PERSIST] Running periodic LittleFS config save...");
        bool ok = saveToFile();
        Serial.printf("[SENSOR_PERSIST] LittleFS save %s\n", ok ? "OK" : "failed");
        lastLFSSave = now;
    }
}

void SensorConfigManager::setHistorySaveInterval(uint32_t interval_ms) {
    history.setSaveInterval(interval_ms);
    Serial.printf("[SENSOR_HISTORY] Save interval set to %u ms\n", interval_ms);
}

// Optimized JSON formatting with minimal heap allocations
// Uses fixed 512-byte buffer and builds JSON incrementally
String SensorConfigManager::formatHistoryJSON(uint16_t maxPoints, uint32_t fromTimestamp) const {
    uint16_t total = history.getCount();
    uint16_t startIdx = 0;
    uint16_t count = total;

    if (fromTimestamp > 0) {
        // Find first point whose timestamp >= fromTimestamp (buffer is chronological)
        startIdx = total; // default: no match
        for (uint16_t i = 0; i < total; i++) {
            if (history.getPoint(i).timestamp >= fromTimestamp) {
                startIdx = i;
                break;
            }
        }
        count = (startIdx < total) ? (total - startIdx) : 0;
        // Still apply maxPoints as an upper safety cap
        if (maxPoints > 0 && count > maxPoints) {
            startIdx += (count - maxPoints);
            count = maxPoints;
        }
    } else if (maxPoints > 0 && maxPoints < total) {
        startIdx = total - maxPoints;
        count = maxPoints;
    }

    if (count == 0) {
        return "{\"count\":0,\"labels\":[],\"timestamps\":[]}";
    }

    // Use static buffer to minimize allocations (reused across calls)
    static char buf[512];
    String output;

    // Each point: ~35 label+ts + 9*8 values = ~110 bytes/point
    output.reserve(count * 110 + 400);

    snprintf(buf, sizeof(buf), "{\"count\":%u,\"labels\":[", count);
    output += buf;

    // Labels as HH:MM strings (backward-compat)
    time_t raw;
    struct tm *ti;
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        raw = (time_t)pt.timestamp;
        ti = localtime(&raw);
        snprintf(buf, sizeof(buf), "\"%02d:%02d\"%s",
                 ti->tm_hour, ti->tm_min,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Raw Unix timestamps
    output += "],\"timestamps\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        snprintf(buf, sizeof(buf), "%u%s",
                 (uint32_t)pt.timestamp,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // AHT20 temperature
    output += "],\"temp_aht20\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.temp_aht20_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // BMP280 temperature
    output += "],\"temp_bmp280\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.temp_bmp280_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Average temperature (also "temperature" for backward-compat)
    output += "],\"temperature\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.temp_avg_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }
    output += "],\"temp_avg\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.temp_avg_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Humidity
    output += "],\"humidity\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        uint16_t v = pt.humidity_x10;
        snprintf(buf, sizeof(buf), "%u.%u%s", v / 10, v % 10,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Pressure hPa (integer) + alias pressure_hpa + mmHg
    output += "],\"pressure\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        snprintf(buf, sizeof(buf), "%u%s", pt.pressure,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }
    output += "],\"pressure_hpa\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        snprintf(buf, sizeof(buf), "%u%s", pt.pressure,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }
    output += "],\"pressure_mmhg\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        // mmHg = hPa * 0.750064; use integer math: * 7501 / 10000 for 1 decimal
        uint32_t mmhg_x10 = (uint32_t)pt.pressure * 7501 / 10000;
        snprintf(buf, sizeof(buf), "%lu.%lu%s",
                 (unsigned long)(mmhg_x10 / 10), (unsigned long)(mmhg_x10 % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Dew point
    output += "],\"dew_point\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.dew_point_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Heat index
    output += "],\"heat_index\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        int16_t v = pt.heat_index_x10;
        snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Altitude (integer meters)
    output += "],\"altitude\":[";
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        snprintf(buf, sizeof(buf), "%d%s", pt.altitude_m,
                 (i < startIdx + count - 1) ? "," : "");
        output += buf;
    }

    // Extra sensor slots (only emit enabled slots)
    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
        if (!config.extra_slots[s].enabled) continue;
        float sc = (config.extra_slots[s].scale != 0.0f) ? config.extra_slots[s].scale : 10.0f;
        snprintf(buf, sizeof(buf), "],\"extra_%u\":[", s);
        output += buf;
        for (uint16_t i = startIdx; i < startIdx + count; i++) {
            const HistoryPoint &pt = history.getPoint(i);
            // Emit with 1 decimal (consistent with other series)
            int16_t v = (int16_t)((pt.extra[s] / sc) * 10.0f);
            snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, abs(v % 10),
                     (i < startIdx + count - 1) ? "," : "");
            output += buf;
        }
    }

    output += "]}";
    return output;
}

// Stream history JSON directly to a Print destination (e.g. AsyncResponseStream).
// Identical output to formatHistoryJSON but writes field-by-field — peak heap ~64 B.
void SensorConfigManager::streamHistoryJSON(Print &out, uint16_t maxPoints, uint32_t fromTimestamp) const {
    uint16_t total = history.getCount();
    uint16_t startIdx = 0, count = total;
    if (fromTimestamp > 0) {
        startIdx = total;
        for (uint16_t i = 0; i < total; i++) {
            if (history.getPoint(i).timestamp >= fromTimestamp) { startIdx = i; break; }
        }
        count = (startIdx < total) ? (total - startIdx) : 0;
        if (maxPoints > 0 && count > maxPoints) { startIdx += (count - maxPoints); count = maxPoints; }
    } else if (maxPoints > 0 && maxPoints < total) {
        startIdx = total - maxPoints;
        count = maxPoints;
    }
    if (count == 0) { out.print(F("{\"count\":0,\"labels\":[],\"timestamps\":[]}")); return; }

    static char buf[64];
    snprintf(buf, sizeof(buf), "{\"count\":%u,\"labels\":[", count);
    out.print(buf);

    time_t raw; struct tm *ti;
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        const HistoryPoint &pt = history.getPoint(i);
        raw = (time_t)pt.timestamp; ti = localtime(&raw);
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "\"%02d:%02d\"", ti->tm_hour, ti->tm_min);
        out.print(buf);
    }
    out.print("],\"timestamps\":[");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        if (i > startIdx) out.print(',');
        out.print((uint32_t)history.getPoint(i).timestamp);
    }
    // Helper: close current array and open a new named series
    auto ns = [&](const char *key) { out.print("],\""); out.print(key); out.print("\":["); };

    ns("temp_aht20");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).temp_aht20_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("temp_bmp280");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).temp_bmp280_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("temperature");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).temp_avg_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("temp_avg");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).temp_avg_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("humidity");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        uint16_t v = history.getPoint(i).humidity_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%u.%u", v / 10, v % 10); out.print(buf);
    }
    ns("pressure");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        if (i > startIdx) out.print(',');
        out.print(history.getPoint(i).pressure);
    }
    ns("pressure_hpa");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        if (i > startIdx) out.print(',');
        out.print(history.getPoint(i).pressure);
    }
    ns("pressure_mmhg");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        if (i > startIdx) out.print(',');
        uint32_t mmhg_x10 = (uint32_t)history.getPoint(i).pressure * 7501 / 10000;
        snprintf(buf, sizeof(buf), "%lu.%lu", (unsigned long)(mmhg_x10 / 10), (unsigned long)(mmhg_x10 % 10));
        out.print(buf);
    }
    ns("dew_point");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).dew_point_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("heat_index");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        int16_t v = history.getPoint(i).heat_index_x10;
        if (i > startIdx) out.print(',');
        snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
    }
    ns("altitude");
    for (uint16_t i = startIdx; i < startIdx + count; i++) {
        if (i > startIdx) out.print(',');
        out.print(history.getPoint(i).altitude_m);
    }
    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
        if (!config.extra_slots[s].enabled) continue;
        float sc = (config.extra_slots[s].scale != 0.0f) ? config.extra_slots[s].scale : 10.0f;
        char skey[12]; snprintf(skey, sizeof(skey), "extra_%u", s);
        ns(skey);
        for (uint16_t i = startIdx; i < startIdx + count; i++) {
            if (i > startIdx) out.print(',');
            int16_t v = (int16_t)((history.getPoint(i).extra[s] / sc) * 10.0f);
            snprintf(buf, sizeof(buf), "%d.%d", v / 10, abs(v % 10)); out.print(buf);
        }
    }
    out.print("]}");
}

// Stream daily CSV file as chart JSON directly to a Print destination.
// Multi-pass SD reads + PSRAM timestamp buffer keep heap usage below 1 KB.
void SensorHistory::streamDailyFile(Print &out, const String &dateId, const SensorConfig *cfgPtr) const {
#if ENABLE_SDCARD==1
    String chartFilename = chartHistorySensorPathForDate(dateId);
    if (!sdcardShouldUseSD()) {
        // No SD: Flash mirror first, else the live ring.
        if (!chartHistoryStreamSensorJson(out, chartFilename, 0, 0)) {
            out.print(buildBufferDailyJSON(dateId));
        }
        return;
    }
    if (!chartHistoryStreamSensorJson(out, chartFilename, 0, 0)) {
        Serial.printf("[STREAM_DAILY] .chart not found or invalid: %s\n", chartFilename.c_str());
        out.print("{\"count\":0,\"labels\":[],\"timestamps\":[]}");
    }
#else
    out.print(buildBufferDailyJSON(dateId));
#endif
}

void SensorHistory::streamDailyWindow(Print &out, const String &dateId, uint32_t fromTimestamp, uint16_t maxPoints,
                                      const SensorConfig *cfgPtr) const {
#if ENABLE_SDCARD==1
    if (fromTimestamp == 0) {
        streamDailyFile(out, dateId, cfgPtr);
        return;
    }
    String chartFilename = chartHistorySensorPathForDate(dateId);
    if (!sdcardShouldUseSD()) {
        // No SD: Flash mirror first, else the live ring.
        if (!chartHistoryStreamSensorJson(out, chartFilename, fromTimestamp, maxPoints)) {
            out.print(buildBufferDailyJSON(dateId));
        }
        return;
    }
    if (!chartHistoryStreamSensorJson(out, chartFilename, fromTimestamp, maxPoints)) {
        Serial.printf("[STREAM_DAILY] .chart not found or invalid: %s\n", chartFilename.c_str());
        out.print("{\"count\":0,\"labels\":[],\"timestamps\":[]}");
    }
#else
    out.print(buildBufferDailyJSON(dateId));
#endif
}

// Append live ring-tail values into one per-series JSON fragment, using exactly
// the same formatting as streamHistoryJSON so the merged head stays homogeneous.
// The fragment already holds the SD head values (comma-separated, trailing comma
// free); a separator comma is inserted before the first tail value if needed.
template <typename Fmt>
static void chartHistoryAppendTailSeries(std::vector<char, PsramAllocatorStd<char>> &buf,
                                         const SensorHistory &hist, uint16_t startIdx,
                                         uint16_t count, Fmt fmt) {
    if (buf.empty()) return;  // column absent from the day file: keep the series empty
    for (uint16_t k = 0; k < count; k++) {
        if (k > 0 || !buf.empty()) buf.push_back(',');
        char tmp[24];
        fmt(tmp, sizeof(tmp), hist.getPoint(startIdx + k));
        for (const char *p = tmp; *p; p++) buf.push_back(*p);
    }
}

// Stream [fromTimestamp, now) as ONE envelope: the persisted SD .chart head
// (samples recorded before the last reboot, kept by the periodic SD flush) plus
// the live ring tail. The ring does not reach back to fromTimestamp right after
// a reboot, so the handler used to serve only PSRAM once the first sample landed
// and the pre-reboot window silently vanished; this backfills it like the OEE
// dashboard backfills the current hour from its persisted hourly buckets.
void SensorConfigManager::streamMergedWindow(Print &out, const String &dateId,
                                             uint32_t fromTimestamp, uint16_t maxPoints) const {
    const SensorHistory &hist = history;
    uint16_t ringCount = hist.getCount();
    uint32_t ringOldest = hist.oldestTimestamp();
    // No gate here: chartHistoryReadSensorJson merges SD + Flash mirror, and
    // falls back to the live tail when neither has the day file.
    if (ringCount == 0 || ringOldest <= fromTimestamp) {
        // Nothing pre-boot to add (or the ring already covers the window):
        // the existing single-source path is already exact.
        streamHistoryJSON(out, maxPoints, fromTimestamp);
        return;
    }

#if ENABLE_SDCARD==1
    String chartFilename = chartHistorySensorPathForDate(dateId);
    ChartSensorJsonSet set;
    if (!chartHistoryReadSensorJson(set, chartFilename, fromTimestamp, ringOldest, maxPoints)) {
        // Persisted day file missing/unreadable: fall back to the live tail.
        streamHistoryJSON(out, maxPoints, fromTimestamp);
        return;
    }
    if (set.count == 0) {
        // Day file has no pre-boot samples yet (first flush pending): serve the
        // live-only view so series arrays stay aligned with timestamps.
        streamHistoryJSON(out, maxPoints, fromTimestamp);
        return;
    }

    // Live tail = ring samples with ts >= ringOldest (dedup boundary: the SD
    // head was read with endTimestamp=ringOldest, so there is zero overlap).
    // Budget = maxPoints minus the head count; newest tail points win.
    uint16_t tailStart = 0;
    uint16_t tailCount = ringCount;
    uint32_t tailBudget = sensorHistoryTailBudget(maxPoints, (uint16_t)set.count);
    if (tailCount > tailBudget) {
        tailCount = (uint16_t)tailBudget;
        tailStart = (uint16_t)(ringCount - tailCount);
    }

    for (uint16_t k = 0; k < tailCount; k++) {
        const HistoryPoint &pt = hist.getPoint(tailStart + k);
        time_t rawTs = (time_t)pt.timestamp;
        struct tm *ti = localtime(&rawTs);
        char tmp[24];
        if (k > 0 || !set.labels.empty()) set.labels.push_back(',');
        snprintf(tmp, sizeof(tmp), "\"%02d:%02d\"", ti->tm_hour, ti->tm_min);
        for (const char *p = tmp; *p; p++) set.labels.push_back(*p);
        if (k > 0 || !set.timestamps.empty()) set.timestamps.push_back(',');
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)pt.timestamp);
        for (const char *p = tmp; *p; p++) set.timestamps.push_back(*p);
    }

    chartHistoryAppendTailSeries(set.tempAht20, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.temp_aht20_x10 / 10, abs(pt.temp_aht20_x10 % 10)); });
    chartHistoryAppendTailSeries(set.tempBmp280, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.temp_bmp280_x10 / 10, abs(pt.temp_bmp280_x10 % 10)); });
    chartHistoryAppendTailSeries(set.temperature, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.temp_avg_x10 / 10, abs(pt.temp_avg_x10 % 10)); });
    chartHistoryAppendTailSeries(set.tempAvg, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.temp_avg_x10 / 10, abs(pt.temp_avg_x10 % 10)); });
    chartHistoryAppendTailSeries(set.humidity, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%u.%u", (unsigned)(pt.humidity_x10 / 10), (unsigned)(pt.humidity_x10 % 10)); });
    chartHistoryAppendTailSeries(set.pressure, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%u", (unsigned)pt.pressure); });
    chartHistoryAppendTailSeries(set.pressureHpa, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%u", (unsigned)pt.pressure); });
    chartHistoryAppendTailSeries(set.pressureMmhg, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) {
            uint32_t mmhg_x10 = (uint32_t)pt.pressure * 7501 / 10000;
            snprintf(o, n, "%lu.%lu", (unsigned long)(mmhg_x10 / 10), (unsigned long)(mmhg_x10 % 10));
        });
    chartHistoryAppendTailSeries(set.dewPoint, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.dew_point_x10 / 10, abs(pt.dew_point_x10 % 10)); });
    chartHistoryAppendTailSeries(set.heatIndex, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d.%d", pt.heat_index_x10 / 10, abs(pt.heat_index_x10 % 10)); });
    chartHistoryAppendTailSeries(set.altitude, hist, tailStart, tailCount,
        [](char *o, size_t n, const HistoryPoint &pt) { snprintf(o, n, "%d", pt.altitude_m); });

    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
        if (set.extra[s].empty()) continue;  // column absent from the day file
        if (!config.extra_slots[s].enabled) {
            // Column existed in the file but the slot is off now: pad with nulls
            // so every head series keeps count-aligned arrays.
            for (uint16_t k = 0; k < tailCount; k++) {
                if (k > 0 || !set.extra[s].empty()) set.extra[s].push_back(',');
                for (const char *p = "null"; *p; p++) set.extra[s].push_back(*p);
            }
            continue;
        }
        float sc = (config.extra_slots[s].scale != 0.0f) ? config.extra_slots[s].scale : 10.0f;
        chartHistoryAppendTailSeries(set.extra[s], hist, tailStart, tailCount,
            [s, sc](char *o, size_t n, const HistoryPoint &pt) {
                int16_t v = (int16_t)((pt.extra[s] / sc) * 10.0f);
                snprintf(o, n, "%d.%d", v / 10, abs(v % 10));
            });
    }

    set.count += tailCount;
    chartHistoryWriteSensorJson(out, set);
#endif
}

#endif // (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
