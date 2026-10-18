#include "NvsStore.h"
#include <cstring>

NvsStore::NvsStore() : _open(false) {
    _currentNs[0] = '\0';
}

NvsStore::~NvsStore() {
    if (_open) end();
}

bool NvsStore::begin(const char* ns, bool readOnly) {
    if (_open) end();
    _open = _prefs.begin(ns, readOnly);
    if (_open) {
        strncpy(_currentNs, ns, sizeof(_currentNs) - 1);
        _currentNs[sizeof(_currentNs) - 1] = '\0';
    }
    return _open;
}

void NvsStore::end() {
    _prefs.end();
    _open = false;
    _currentNs[0] = '\0';
}

bool NvsStore::putString(const char* key, const String& value) {
    if (!_open) return false;
    size_t written = _prefs.putString(key, value);
    return written > 0;
}

String NvsStore::getString(const char* key, const String& defaultValue) {
    if (!_open) return defaultValue;
    return _prefs.getString(key, defaultValue);
}

bool NvsStore::isKey(const char* key) {
    if (!_open) return false;
    return _prefs.isKey(key);
}

bool NvsStore::putInt(const char* key, int32_t value) {
    if (!_open) return false;
    return _prefs.putInt(key, value);
}

int32_t NvsStore::getInt(const char* key, int32_t defaultValue) {
    if (!_open) return defaultValue;
    return _prefs.getInt(key, defaultValue);
}

bool NvsStore::putUInt(const char* key, uint32_t value) {
    if (!_open) return false;
    return _prefs.putUInt(key, value);
}

uint32_t NvsStore::getUInt(const char* key, uint32_t defaultValue) {
    if (!_open) return defaultValue;
    return _prefs.getUInt(key, defaultValue);
}

bool NvsStore::putBool(const char* key, bool value) {
    if (!_open) return false;
    return _prefs.putBool(key, value);
}

bool NvsStore::getBool(const char* key, bool defaultValue) {
    if (!_open) return defaultValue;
    return _prefs.getBool(key, defaultValue);
}

bool NvsStore::remove(const char* key) {
    if (!_open) return false;
    _prefs.remove(key);
    return true;
}

bool NvsStore::putStringWithVerify(const char* key, const String& value) {
    if (!putString(key, value)) return false;
    end();
    delay(10);
    if (!begin(_currentNs, true)) return false;
    String verify = getString(key, "");
    end();
    begin(_currentNs, false);
    return verify == value;
}

bool NvsStore::getStats(nvs_stats_t& stats) {
    return nvs_get_stats(NULL, &stats) == ESP_OK;
}

uint32_t NvsStore::loadVolatileCounter(const char* key, uint32_t defaultValue) {
    Preferences p;
    if (!p.begin("nvs_stats", true)) return defaultValue;
    uint32_t val = p.getUInt(key, defaultValue);
    p.end();
    return val;
}

void NvsStore::saveVolatileCounter(const char* key, uint32_t value) {
    Preferences p;
    if (!p.begin("nvs_stats", false)) return;
    p.putUInt(key, value);
    p.end();
}
