#pragma once
#include <string>
#include <map>

// Test-double for IConfigStore — uses std::map instead of NVS/LittleFS.
// Does NOT inherit from the production interface to avoid ArduinoJson dependency.
// API mirrors IConfigStore pattern: init(), load(), save(), remove(), exists(), clear().
class MockConfigStore {
private:
    std::map<std::string, std::string> data_;
    bool inited_ = false;
public:
    bool init() { inited_ = true; return true; }
    bool isInited() const { return inited_; }

    // load: returns true if key exists, fills json parameter
    bool load(const std::string& key, std::string& json) {
        auto it = data_.find(key);
        if (it == data_.end()) { json.clear(); return false; }
        json = it->second;
        return true;
    }

    // save: stores serialized JSON string under key
    bool save(const std::string& key, const std::string& json) {
        data_[key] = json;
        return true;
    }

    bool remove(const std::string& key) {
        return data_.erase(key) > 0;
    }

    bool exists(const std::string& key) {
        return data_.find(key) != data_.end();
    }

    bool clear() { data_.clear(); return true; }

    // Test helpers
    size_t size() const { return data_.size(); }
    std::string getRaw(const std::string& key) const {
        auto it = data_.find(key);
        return it != data_.end() ? it->second : std::string();
    }
    void setRaw(const std::string& key, const std::string& json) {
        data_[key] = json;
    }
};
