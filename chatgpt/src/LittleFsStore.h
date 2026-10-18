#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <sys/stat.h>

class LittleFsStore {
public:
    LittleFsStore();
    ~LittleFsStore();

    // Mount/check
    bool begin();
    bool isMounted();

    // File operations
    bool exists(const char* path);
    bool remove(const char* path);
    bool rename(const char* oldPath, const char* newPath);
    bool mkdir(const char* path);
    bool ensureDir(const char* path);

    // Read entire file into string
    String readFile(const char* path);

    // Atomic write: serialize JSON to .tmp, then rename to final path
    bool atomicWriteJson(const char* finalPath, JsonVariantConst payload,
                         size_t maxBytes = 0);

    // Atomic write: raw string content to .tmp + rename
    bool atomicWriteRaw(const char* finalPath, const String& content);

    // Recover from interrupted .tmp writes
    void recoverTmpFiles(const char* path);
};

// Path helpers provided as free functions (stateless)
String configStemPath(const char* stem);
String configTmpPath(const char* finalPath);
