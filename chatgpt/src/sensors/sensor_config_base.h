#pragma once
#ifndef SENSOR_CONFIG_BASE_H
#define SENSOR_CONFIG_BASE_H

#include <Arduino.h>
#include "../littlefs_manager.h"
#include <LittleFS.h>
#include <sys/stat.h>

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
extern bool touch7RgbStreaming(); // flash-read guard, T7-23
#endif

// ---------------------------------------------------------------------------
// CRTP base for sensor config managers.
// Derived class must provide:
//   - String toJson() const
//   - bool updateFromJson(const String&)
// ---------------------------------------------------------------------------
template<typename Derived, typename ConfigType>
class SensorConfigBase {
public:
    SensorConfigBase()
        : _configFilePath(""),
          _lastAutoReadTime(0),
          _lastPersistTime(0),
          _initialized(false)
    {}

    static constexpr unsigned long PERSIST_INTERVAL_MS = 8UL * 3600UL * 1000UL;

    // --- filesystem init (called by derived begin()) ---
    bool initFilesystem(const String& filePath) {
        _configFilePath = filePath;

        bool fsReady = littlefsMounted();
        if (!fsReady) fsReady = littlefsBegin();

        if (!fsReady) {
            applyDefaults();
            return true;
        }

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
        // TOUCH7: config was already loaded at boot (before the panel started).
        // Re-reading flash while the RGB VSYNC ISR (flash-hosted restart path)
        // streams is a proven cache panic — only touch hardware needs a retry,
        // never a config reload (T7-23).
        if (touch7RgbStreaming()) return true;
#endif

        struct stat st;
        if (stat("/littlefs/config", &st) != 0 || !S_ISDIR(st.st_mode)) {
            LittleFS.mkdir("/config");
        }

        if (!loadFromFile()) {
            applyDefaults();
            saveToFile();
        }
        return true;
    }

    // --- accessors ---
    bool isInitialized() const { return _initialized; }
    bool isEnabled()     const { return _config.sensor_enabled; }

    // --- scheduling ---
    bool shouldAutoRead() const {
        if (!_initialized || !_config.sensor_enabled) return false;
        if (_lastAutoReadTime == 0) return true;
        return (millis() - _lastAutoReadTime) >= (unsigned long)_config.read_interval_sec * 1000UL;
    }

    void markReadingDone() { _lastAutoReadTime = millis(); }

    void periodicPersist() {
        if (!_initialized) return;
        unsigned long now = millis();
        if (_lastPersistTime == 0 || (now - _lastPersistTime) >= PERSIST_INTERVAL_MS) {
            saveToFile();
            _lastPersistTime = now;
        }
    }

    const ConfigType& getConfig() const { return _config; }

protected:
    ConfigType       _config;
    String           _configFilePath;
    unsigned long    _lastAutoReadTime;
    unsigned long    _lastPersistTime;
    bool             _initialized;

    void applyDefaults() { _config = ConfigType(); }

    Derived&       derived()       { return *static_cast<Derived*>(this); }
    const Derived& derived() const { return *static_cast<const Derived*>(this); }

    bool loadFromFile() {
        String fullPath = "/littlefs" + _configFilePath;
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) return false;

        File f = LittleFS.open(_configFilePath, "r");
        if (!f) return false;
        String content = f.readString();
        f.close();

        return derived().updateFromJson(content);
    }

    bool saveToFile() const {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
        // TouchMeteo-7: quiesce the RGB engine around the flash write
        // (erase/program suspends the cache; the per-VSYNC restart path runs
        // flash-hosted GDMA code — proven panic, T7-20).
        // NOTE: ets_printf here would spam; one-shot below instead.
        extern void touch7FlashHold(bool hold);
        touch7FlashHold(true);
#endif
        if (!littlefsBegin()) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            touch7FlashHold(false);
#endif
            return false;
        }
        String tmpPath = _configFilePath + ".tmp";
        File f = LittleFS.open(tmpPath, "w");
        if (!f) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            touch7FlashHold(false);
#endif
            return false;
        }
        size_t bytes = f.print(derived().toJson());
        f.close();
        storNoteLfsWrite(bytes);
        bool ok;
        if (bytes == 0) { LittleFS.remove(tmpPath); ok = false; }
        else {
            LittleFS.remove(_configFilePath);
            ok = LittleFS.rename(tmpPath, _configFilePath);
        }
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
        touch7FlashHold(false);
#endif
        return ok;
    }
};

#endif // SENSOR_CONFIG_BASE_H
