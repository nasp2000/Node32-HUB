#include "audio_dac_uda1334a_config.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1

#include <LittleFS.h>
#include "littlefs_manager.h"
#include "feature_flags.h"
#include "psram_helpers.h"

UDA1334AConfigManager uda1334aConfig;

UDA1334AConfigManager::UDA1334AConfigManager()
    : _dirty(false)
{
}

bool UDA1334AConfigManager::begin() {
    // Ensure LittleFS is mounted
    if (!littlefsBegin()) {
        Serial.println("[UDA1334A] LittleFS mount failed");
        _config.initError = true;
        return false;
    }

    // Load existing config or create defaults
    if (!LittleFS.exists(UDA1334A_CONFIG_PATH)) {
        Serial.println("[UDA1334A] Config not found, creating defaults...");
        return createDefaults();
    }

    return load();
}

bool UDA1334AConfigManager::load() {
    if (!littlefsBegin()) return false;

    File f = LittleFS.open(UDA1334A_CONFIG_PATH, "r");
    if (!f) {
        Serial.println("[UDA1334A] Failed to open config file for reading");
        return false;
    }

    SpiRamJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[UDA1334A] JSON parse error: %s\n", err.c_str());
        return false;
    }

    if (doc.containsKey("enabled"))
        _config.enabled = doc["enabled"].as<bool>();
    if (doc.containsKey("sampleRate"))
        _config.sampleRate = doc["sampleRate"].as<uint32_t>();
    if (doc.containsKey("bitDepth"))
        _config.bitDepth = doc["bitDepth"].as<int>();
    if (doc.containsKey("autoRoutingEnabled"))
        _config.autoRoutingEnabled = doc["autoRoutingEnabled"].as<bool>();

    // The DAC is always active in this firmware: never let a stale/stored
    // "enabled":false file (e.g. after a LittleFS wipe) disable the board and
    // leave the I2S driver uninstalled -> hard crash on first playback.
    _config.enabled = true;

    Serial.printf("[UDA1334A] Config loaded: enabled=%d, sr=%u Hz, bd=%d bits\n",
                  _config.enabled, _config.sampleRate, _config.bitDepth);
    _dirty = false;
    return true;
}

bool UDA1334AConfigManager::save() {
    if (!littlefsBegin()) return false;

    SpiRamJsonDocument doc(512);
    doc["enabled"] = _config.enabled;
    doc["sampleRate"] = _config.sampleRate;
    doc["bitDepth"] = _config.bitDepth;
    doc["autoRoutingEnabled"] = _config.autoRoutingEnabled;

    String tmpPath = UDA1334A_CONFIG_PATH;
    tmpPath += ".tmp";

    File f = LittleFS.open(tmpPath, "w");
    if (!f) {
        Serial.println("[UDA1334A] Failed to open .tmp file for writing");
        return false;
    }

    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);

    if (bytes == 0) {
        Serial.println("[UDA1334A] Failed to serialize JSON");
        LittleFS.remove(tmpPath);
        return false;
    }

    // Atomic rename
    LittleFS.remove(UDA1334A_CONFIG_PATH);
    if (!LittleFS.rename(tmpPath, UDA1334A_CONFIG_PATH)) {
        Serial.println("[UDA1334A] Failed to rename .tmp to final path");
        LittleFS.remove(tmpPath);
        return false;
    }

    Serial.println("[UDA1334A] Config saved");
    _dirty = false;
    return true;
}

bool UDA1334AConfigManager::createDefaults() {
    _config.enabled = true;
    _config.sampleRate = 44100;
    _config.bitDepth = 16;
    _config.autoRoutingEnabled = false;

    return save();
}

void UDA1334AConfigManager::setEnabled(bool enabled) {
    if (_config.enabled != enabled) {
        _config.enabled = enabled;
        _dirty = true;
    }
}

void UDA1334AConfigManager::setSampleRate(uint32_t sampleRate) {
    if (_config.sampleRate != sampleRate) {
        _config.sampleRate = sampleRate;
        _dirty = true;
    }
}

void UDA1334AConfigManager::setBitDepth(int bitDepth) {
    if (_config.bitDepth != bitDepth) {
        _config.bitDepth = bitDepth;
        _dirty = true;
    }
}

void UDA1334AConfigManager::setAutoRoutingEnabled(bool enabled) {
    if (_config.autoRoutingEnabled != enabled) {
        _config.autoRoutingEnabled = enabled;
        _dirty = true;
    }
}

String UDA1334AConfigManager::toJson() const {
    StaticJsonDocument<256> doc;
    doc["enabled"] = _config.enabled;
    doc["sampleRate"] = _config.sampleRate;
    doc["bitDepth"] = _config.bitDepth;
    doc["autoRoutingEnabled"] = _config.autoRoutingEnabled;
    doc["initialized"] = udaDac.isInitialized();

    String out;
    serializeJson(doc, out);
    return out;
}

bool UDA1334AConfigManager::updateFromJson(const JsonObject& obj) {
    // "enabled" is intentionally ignored: the DAC is always active in this
    // firmware, there is no "disable" option.
    if (obj.containsKey("sampleRate"))
        setSampleRate(obj["sampleRate"].as<uint32_t>());
    if (obj.containsKey("bitDepth"))
        setBitDepth(obj["bitDepth"].as<int>());
    if (obj.containsKey("autoRoutingEnabled"))
        setAutoRoutingEnabled(obj["autoRoutingEnabled"].as<bool>());

    if (_dirty) {
        return save();
    }
    return true;
}

#endif // ENABLE_UDA1334A == 1

