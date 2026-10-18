#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class IConfigStore {
public:
    virtual ~IConfigStore() = default;

    virtual bool init() = 0;
    virtual bool load(const String& key, JsonDocument& doc) = 0;
    virtual bool save(const String& key, const JsonDocument& doc) = 0;
    virtual bool remove(const String& key) = 0;
    virtual bool exists(const String& key) = 0;
    virtual bool clear() = 0;
};

class INvsStore : public IConfigStore {
public:
    virtual ~INvsStore() = default;
    virtual bool setBlob(const String& key, const uint8_t* data, size_t len) = 0;
    virtual bool getBlob(const String& key, uint8_t* data, size_t len) = 0;
};

class ILittleFsStore : public IConfigStore {
public:
    virtual ~ILittleFsStore() = default;
    virtual bool list(const String& pattern, std::vector<String>& out) = 0;
};
