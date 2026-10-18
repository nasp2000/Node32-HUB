#pragma once
#include <string>
#include <map>
#include <cstdlib>
#include <cstring>
#include "Arduino.h"

// In-memory stand-in for the ESP32 Preferences (NVS key-value) API used by the
// real NvsStore.cpp / ConfigManager.cpp. All namespaces share one backing map so
// that a "second instance" of ConfigManager sees what the first one persisted.
// Preferences::resetAll() clears the whole store (test isolation hook).
class Preferences {
public:
    typedef std::map<std::string, std::string> Namespace;
    typedef std::map<std::string, Namespace> Store;

    bool begin(const char* ns, bool readOnly = false) {
        (void)readOnly;
        if (!ns || *ns == '\0') return false;
        _ns = ns;
        return true;
    }

    void end() { _ns.clear(); }

    bool isKey(const char* key) const {
        if (_ns.empty() || !key) return false;
        Store& s = store();
        Store::const_iterator it = s.find(_ns);
        if (it == s.end()) return false;
        return it->second.count(key) != 0;
    }

    bool remove(const char* key) {
        if (_ns.empty() || !key) return false;
        Namespace& ns = store()[_ns];
        return ns.erase(key) > 0;
    }

    size_t putString(const char* key, const String& value) {
        if (_ns.empty() || !key) return 0;
        store()[_ns][key] = value.c_str() ? value.c_str() : "";
        return value.length();
    }

    String getString(const char* key, const String& defaultValue = "") {
        if (_ns.empty() || !key) return defaultValue;
        Namespace& ns = store()[_ns];
        Namespace::const_iterator it = ns.find(key);
        if (it == ns.end()) return defaultValue;
        return String(it->second.c_str());
    }

    bool putInt(const char* key, int32_t value) {
        if (_ns.empty() || !key) return false;
        store()[_ns][key] = std::to_string(value);
        return true;
    }

    int32_t getInt(const char* key, int32_t defaultValue = 0) const {
        if (!isKey(key)) return defaultValue;
        return (int32_t)strtol(store()[_ns][key].c_str(), nullptr, 10);
    }

    bool putUInt(const char* key, uint32_t value) {
        if (_ns.empty() || !key) return false;
        store()[_ns][key] = std::to_string(value);
        return true;
    }

    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) const {
        if (!isKey(key)) return defaultValue;
        return (uint32_t)strtoul(store()[_ns][key].c_str(), nullptr, 10);
    }

    bool putBool(const char* key, bool value) {
        if (_ns.empty() || !key) return false;
        store()[_ns][key] = value ? "1" : "0";
        return true;
    }

    bool getBool(const char* key, bool defaultValue = false) const {
        if (!isKey(key)) return defaultValue;
        return store()[_ns][key] == "1";
    }

    static Store& store() {
        static Store s;
        return s;
    }

    static void resetAll() { store().clear(); }

private:
    std::string _ns;
};