#pragma once
#include <Preferences.h>
#include <nvs_flash.h>
#include <Arduino.h>

class NvsStore {
public:
    NvsStore();
    ~NvsStore();

    // Open/close the primary config namespace ("esp32_config")
    bool begin(const char* ns = "esp32_config", bool readOnly = false);
    void end();

    // String operations
    bool putString(const char* key, const String& value);
    String getString(const char* key, const String& defaultValue = "");
    bool isKey(const char* key);

    // Integer operations
    bool putInt(const char* key, int32_t value);
    int32_t getInt(const char* key, int32_t defaultValue = 0);
    bool putUInt(const char* key, uint32_t value);
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0);
    bool putBool(const char* key, bool value);
    bool getBool(const char* key, bool defaultValue = false);

    // Remove a key
    bool remove(const char* key);

    // Write a string and read it back for verification
    bool putStringWithVerify(const char* key, const String& value);

    // NVS diagnostics
    bool getStats(nvs_stats_t& stats);

    // Volatile counters in "nvs_stats" namespace
    uint32_t loadVolatileCounter(const char* key, uint32_t defaultValue = 0);
    void saveVolatileCounter(const char* key, uint32_t value);

private:
    Preferences _prefs;
    char _currentNs[32];
    bool _open;
};
