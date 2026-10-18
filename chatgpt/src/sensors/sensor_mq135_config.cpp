#include "sensor_mq135_config.h"
#include "../languages/lang_active.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_MQ135==1

#include "sensor_mq135.h"
#include <ArduinoJson.h>
#include <time.h>
#include "feature_flags.h"
#include "../psram_helpers.h"

// Global instance
MQ135ConfigManager mq135ConfigManager;

// Injector for RX buffer — defined in chatgpt.ino
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

// ====================================================================================
// Slot index constants (must match registerExtraSlots / stageHistorySlots)
// ====================================================================================
#define MQ135_SLOT_CO2      0
#define MQ135_SLOT_CO       1
#define MQ135_SLOT_NH3      2
#define MQ135_SLOT_NOX      3
#define MQ135_SLOT_ALCOHOL  4
#define MQ135_SLOT_AQI      5

// ====================================================================================
// Initialization
// ====================================================================================

bool MQ135ConfigManager::begin(const String &filePath) {
    initFilesystem(filePath);

    if (!mq135Sensor.begin((uint8_t)_config.adc_pin, _config.r0)) {
        Serial.println("[MQ135_CFG] Sensor driver init failed");
    }

    _initialized = true;
    Serial.printf("[MQ135_CFG] Ready on GPIO%d R0=%.2f\n", _config.adc_pin, _config.r0);
    return true;
}

// ====================================================================================
// Extra slot registration
// ====================================================================================

void MQ135ConfigManager::registerExtraSlots(SensorConfig &cfg) const {
    // When the sensor is disabled, clear all MQ-135 slots so the charts
    // dropdowns no longer show them. The next enable + save will restore them.
    if (!_config.sensor_enabled) {
        for (uint8_t i = MQ135_SLOT_CO2; i <= MQ135_SLOT_AQI; i++) {
            if (i >= EXTRA_SENSOR_SLOTS) break;
            cfg.extra_slots[i].enabled = false;
        }
        return;
    }

    auto setup = [&](uint8_t slot, const char *name, const char *label, const char *unit,
                     float scale, bool enabled) {
        if (slot >= EXTRA_SENSOR_SLOTS) return;
        cfg.extra_slots[slot].enabled          = enabled;
        cfg.extra_slots[slot].inject_to_buffer = false; // MQ-135 injects its own line
        cfg.extra_slots[slot].scale            = scale;
        cfg.extra_slots[slot].offset           = 0.0f;
        strncpy(cfg.extra_slots[slot].name,  name,  sizeof(cfg.extra_slots[slot].name)  - 1);
        strncpy(cfg.extra_slots[slot].label, label, sizeof(cfg.extra_slots[slot].label) - 1);
        strncpy(cfg.extra_slots[slot].unit,  unit,  sizeof(cfg.extra_slots[slot].unit)  - 1);
        cfg.extra_slots[slot].name[sizeof(cfg.extra_slots[slot].name)   - 1] = '\0';
        cfg.extra_slots[slot].label[sizeof(cfg.extra_slots[slot].label) - 1] = '\0';
        cfg.extra_slots[slot].unit[sizeof(cfg.extra_slots[slot].unit)   - 1] = '\0';
    };

    setup(MQ135_SLOT_CO2,     "mq135_co2",     "CO2 (MQ-135)",      "ppm", 1.0f,  _config.gas_co2.enabled);
    setup(MQ135_SLOT_CO,      "mq135_co",      "CO (MQ-135)",       "ppm", 10.0f, _config.gas_co.enabled);
    setup(MQ135_SLOT_NH3,     "mq135_nh3",     "NH3 (MQ-135)",      "ppm", 10.0f, _config.gas_nh3.enabled);
    setup(MQ135_SLOT_NOX,     "mq135_nox",     "NOx (MQ-135)",      "ppm", 10.0f, _config.gas_nox.enabled);
    setup(MQ135_SLOT_ALCOHOL, "mq135_alcohol", "Ethanol (MQ-135)",   "ppm", 10.0f, _config.gas_alcohol.enabled);
    setup(MQ135_SLOT_AQI,     "mq135_aqi",     "IQA (MQ-135)",      "",    1.0f,  _config.gas_aqi.enabled);

    Serial.println("[MQ135_CFG] Extra slots registered in SensorConfig");
}

// ====================================================================================
// Reading
// ====================================================================================

bool MQ135ConfigManager::performReading(float temperature, float humidity) {
    if (!_config.sensor_enabled) return false;

    // Use env compensation only if requested and valid values provided
    float t = _config.use_env_compensation ? temperature : NAN;
    float h = _config.use_env_compensation ? humidity    : NAN;

    if (!mq135Sensor.update(t, h)) {
        Serial.println("[MQ135_CFG] Sensor read failed");
        return false;
    }

    const MQ135Reading &r = mq135Sensor.getLastReading();

    // Cache last values with per-gas offsets
    _config.last_co2     = r.ppm_co2_corrected  + _config.gas_co2.offset;
    _config.last_co      = r.ppm_co             + _config.gas_co.offset;
    _config.last_nh3     = r.ppm_nh3            + _config.gas_nh3.offset;
    _config.last_nox     = r.ppm_nox            + _config.gas_nox.offset;
    _config.last_alcohol = r.ppm_alcohol        + _config.gas_alcohol.offset;
    _config.last_benzene = r.ppm_benzene        + _config.gas_benzene.offset;
    _config.last_smoke   = r.ppm_smoke          + _config.gas_smoke.offset;
    _config.last_aqi     = r.aqi                + _config.gas_aqi.offset;
    _config.last_rs      = r.rs;
    _config.last_ratio   = r.rs_r0_ratio;
    _config.last_raw_adc = r.raw_adc;
    _config.last_warmed_up = mq135Sensor.isWarmedUp();

    time_t now = time(nullptr);
    _config.last_reading_time = (now > 100000) ? (unsigned long)now : 0;

    markReadingDone();

    if (_config.inject_to_buffer) {
        injectToBuffer();
    }

    return true;
}

// ====================================================================================
// Stage history extra slots
// ====================================================================================

void MQ135ConfigManager::stageHistorySlots(SensorHistory &history) const {
    if (!_config.sensor_enabled) return;
    if (_config.gas_co2.enabled)
        history.setExtraSlot(MQ135_SLOT_CO2,     (int16_t)(_config.last_co2));
    if (_config.gas_co.enabled)
        history.setExtraSlot(MQ135_SLOT_CO,      (int16_t)(_config.last_co      * 10.0f));
    if (_config.gas_nh3.enabled)
        history.setExtraSlot(MQ135_SLOT_NH3,     (int16_t)(_config.last_nh3     * 10.0f));
    if (_config.gas_nox.enabled)
        history.setExtraSlot(MQ135_SLOT_NOX,     (int16_t)(_config.last_nox     * 10.0f));
    if (_config.gas_alcohol.enabled)
        history.setExtraSlot(MQ135_SLOT_ALCOHOL, (int16_t)(_config.last_alcohol * 10.0f));
    if (_config.gas_aqi.enabled)
        history.setExtraSlot(MQ135_SLOT_AQI,     (int16_t)(_config.last_aqi));
}

// ====================================================================================
// Formatting
// ====================================================================================

String MQ135ConfigManager::formatPretty() const {
    String out;
    out.reserve(200);
    bool first = true;
    auto add = [&](const char *emoji, const char *lbl, float val, const char *unit) {
        if (!first) out += F(" | ");
        first = false;
        out += emoji;
        out += ' ';
        out += lbl;
        out += ": ";
        out += String(val, 1);
        out += unit;
    };
    if (_config.gas_co2.enabled)     add("🌫️",  "CO2",    _config.last_co2,     " ppm");
    if (_config.gas_co.enabled)      add("💨",  "CO",     _config.last_co,      " ppm");
    if (_config.gas_nh3.enabled)     add("⚗️",  "NH3",    _config.last_nh3,     " ppm");
    if (_config.gas_nox.enabled)     add("🔴",  "NOx",    _config.last_nox,     " ppm");
    if (_config.gas_alcohol.enabled) { String n = L(activeLang->sensorAlcohol); add("\xF0\x9F\x8D\xBA",  n.c_str(), _config.last_alcohol, " ppm"); }
    if (_config.gas_benzene.enabled) { String n = L(activeLang->sensorBenzene); add("\xE2\x98\xA0\xEF\xB8\x8F",  n.c_str(), _config.last_benzene, " ppm"); }
    if (_config.gas_aqi.enabled)     add("🌡️", "IQA",    _config.last_aqi,     "");
    if (!mq135Sensor.isWarmedUp()) {
        out += L(activeLang->sensorWarmingUp);
    }
    out += '\n';
    return out;
}

String MQ135ConfigManager::formatCSV() const {
    String sep = ",";
    String out;
    out.reserve(160);
    bool first = true;
    auto addPair = [&](const char *key, float val, int dec) {
        if (!first) out += sep;
        first = false;
        out += key;
        out += sep;
        out += String(val, dec);
    };
    if (_config.gas_co2.enabled)     addPair("mq135_co2",     _config.last_co2,     1);
    if (_config.gas_co.enabled)      addPair("mq135_co",      _config.last_co,      1);
    if (_config.gas_nh3.enabled)     addPair("mq135_nh3",     _config.last_nh3,     1);
    if (_config.gas_nox.enabled)     addPair("mq135_nox",     _config.last_nox,     1);
    if (_config.gas_alcohol.enabled) addPair("mq135_alcohol", _config.last_alcohol, 1);
    if (_config.gas_benzene.enabled) addPair("mq135_benzene", _config.last_benzene, 1);
    if (_config.gas_aqi.enabled)     addPair("mq135_aqi",     _config.last_aqi,     0);
    out += '\n';
    return out;
}

void MQ135ConfigManager::injectToBuffer() const {
    String data;
    if (_config.output_pretty && _config.output_csv) {
        data = formatPretty();
        data += formatCSV();
    } else if (_config.output_pretty) {
        data = formatPretty();
    } else {
        data = formatCSV();
    }
    injectDataToRXBuffer(data, "MQ135");
}

// ====================================================================================
// JSON serialization
// ====================================================================================

String MQ135ConfigManager::toJson() const {
    // Build JSON manually to avoid large SpiRamJsonDocument on the heap
    String j;
    j.reserve(700);
    j = F("{");

    auto bv = [&](const char *k, bool v) { j += '"'; j += k; j += "\":"; j += v ? "true" : "false"; };
    auto nv = [&](const char *k, float v, int d=1) { j += '"'; j += k; j += "\":"; j += String(v,d); };
    auto iv = [&](const char *k, int v) { j += '"'; j += k; j += "\":"; j += v; };
    auto sep = [&]() { j += ','; };

    bv("sensor_enabled", _config.sensor_enabled);  sep();
    iv("adc_pin", _config.adc_pin);                 sep();
    nv("r0", _config.r0, 4);                        sep();
    nv("rl", _config.rl, 0);                        sep();
    iv("read_interval_sec", _config.read_interval_sec); sep();
    bv("use_env_compensation", _config.use_env_compensation); sep();
    bv("inject_to_buffer", _config.inject_to_buffer); sep();
    bv("output_pretty",    _config.output_pretty);  sep();
    bv("output_csv",       _config.output_csv);     sep();

    // Gas configs as flat keys
    auto addGas = [&](const char *prefix, const MQ135ReadingConfig &g, bool comma=true) {
        j += '"'; j += prefix; j += "_en\":";     j += g.enabled ? "true" : "false"; j += ',';
        j += '"'; j += prefix; j += "_offset\":"; j += String(g.offset, 2); j += ',';
        j += '"'; j += prefix; j += "_inject\":"; j += g.inject ? "true" : "false";
        if (comma) j += ',';
    };
    addGas("co2",     _config.gas_co2);
    addGas("co",      _config.gas_co);
    addGas("nh3",     _config.gas_nh3);
    addGas("nox",     _config.gas_nox);
    addGas("alcohol", _config.gas_alcohol);
    addGas("benzene", _config.gas_benzene);
    addGas("smoke",   _config.gas_smoke);
    addGas("aqi",     _config.gas_aqi, false);

    // Last reading
    j += F(",\"last_co2\":"); j += String(_config.last_co2, 1);
    j += F(",\"last_co\":"); j += String(_config.last_co, 1);
    j += F(",\"last_nh3\":"); j += String(_config.last_nh3, 1);
    j += F(",\"last_nox\":"); j += String(_config.last_nox, 1);
    j += F(",\"last_alcohol\":"); j += String(_config.last_alcohol, 1);
    j += F(",\"last_benzene\":"); j += String(_config.last_benzene, 1);
    j += F(",\"last_smoke\":"); j += String(_config.last_smoke, 1);
    j += F(",\"last_aqi\":"); j += String(_config.last_aqi, 0);
    j += F(",\"last_rs\":"); j += String(_config.last_rs, 1);
    j += F(",\"last_ratio\":"); j += String(_config.last_ratio, 3);
    j += F(",\"last_raw_adc\":"); j += _config.last_raw_adc;
    j += F(",\"warmed_up\":"); j += _config.last_warmed_up ? "true" : "false";
    j += F(",\"warmup_remaining_sec\":"); j += (int)(mq135Sensor.getWarmupRemaining() / 1000UL);
    j += F(",\"last_reading_time\":"); j += _config.last_reading_time;

    j += '}';
    return j;
}

bool MQ135ConfigManager::updateFromJson(const String &json) {
    // ~40 scalar fields: 1024 pool is borderline (NoMemory rejects full saves).
    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("[MQ135_CFG] JSON parse error");
        return false;
    }

    auto gas = [&](const char *prefix, MQ135ReadingConfig &g) {
        String enKey  = String(prefix) + "_en";
        String offKey = String(prefix) + "_offset";
        String injKey = String(prefix) + "_inject";
        if (doc.containsKey(enKey))  g.enabled = doc[enKey].as<bool>();
        if (doc.containsKey(offKey)) g.offset  = doc[offKey].as<float>();
        if (doc.containsKey(injKey)) g.inject  = doc[injKey].as<bool>();
    };

    if (doc.containsKey("sensor_enabled"))       _config.sensor_enabled       = doc["sensor_enabled"];
    if (doc.containsKey("adc_pin"))              _config.adc_pin              = doc["adc_pin"].as<int>();
    if (doc.containsKey("r0"))                   _config.r0                   = doc["r0"].as<float>();
    if (doc.containsKey("rl"))                   _config.rl                   = doc["rl"].as<float>();
    if (doc.containsKey("read_interval_sec"))    _config.read_interval_sec    = max(60, doc["read_interval_sec"].as<int>());
    if (doc.containsKey("use_env_compensation")) _config.use_env_compensation = doc["use_env_compensation"];
    if (doc.containsKey("inject_to_buffer"))     _config.inject_to_buffer     = doc["inject_to_buffer"];
    if (doc.containsKey("output_pretty"))        _config.output_pretty        = doc["output_pretty"];
    if (doc.containsKey("output_csv"))           _config.output_csv           = doc["output_csv"];

    gas("co2",     _config.gas_co2);
    gas("co",      _config.gas_co);
    gas("nh3",     _config.gas_nh3);
    gas("nox",     _config.gas_nox);
    gas("alcohol", _config.gas_alcohol);
    gas("benzene", _config.gas_benzene);
    gas("smoke",   _config.gas_smoke);
    gas("aqi",     _config.gas_aqi);

    // Apply new R0/pin to driver
    mq135Sensor.setR0(_config.r0);
    if (mq135Sensor.isInitialized()) {
        mq135Sensor.begin((uint8_t)_config.adc_pin, _config.r0);
    }

    return saveToFile();
}

// ====================================================================================
// LittleFS persistence
// ====================================================================================

#endif // ENABLE_MQ135==1
