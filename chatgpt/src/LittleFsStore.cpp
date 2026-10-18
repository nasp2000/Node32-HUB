#include "LittleFsStore.h"
#include "littlefs_manager.h"
#include <cstring>

LittleFsStore::LittleFsStore() {}
LittleFsStore::~LittleFsStore() {}

bool LittleFsStore::begin() {
    return littlefsBegin();
}

bool LittleFsStore::isMounted() {
    return littlefsMounted();
}

bool LittleFsStore::exists(const char* path) {
    return LittleFS.exists(path);
}

bool LittleFsStore::remove(const char* path) {
    return LittleFS.remove(path);
}

bool LittleFsStore::rename(const char* oldPath, const char* newPath) {
    return LittleFS.rename(oldPath, newPath);
}

bool LittleFsStore::mkdir(const char* path) {
    return LittleFS.mkdir(path);
}

bool LittleFsStore::ensureDir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return true;
    return mkdir(path);
}

String LittleFsStore::readFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String content = f.readString();
    f.close();
    return content;
}

bool LittleFsStore::atomicWriteJson(const char* finalPath, JsonVariantConst payload,
                                    size_t maxBytes) {
    if (maxBytes > 0) {
        String sizeCheck;
        serializeJson(payload, sizeCheck);
        if (sizeCheck.length() > maxBytes) {
            Serial.printf("CFG: Payload too large (%u > %u bytes) - skipping\n",
                          (unsigned)sizeCheck.length(), (unsigned)maxBytes);
            return false;
        }
    }

    String tmpPath = configTmpPath(finalPath);
    File f = LittleFS.open(tmpPath, "w");
    if (!f) {
        Serial.printf("CFG: Failed to open %s for write\n", tmpPath.c_str());
        return false;
    }

    size_t bytes = serializeJsonPretty(payload, f);
    f.close();
    storNoteLfsWrite(bytes);
    delay(50);

    if (bytes == 0) {
        Serial.printf("CFG: %s wrote 0 bytes - aborting\n", tmpPath.c_str());
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath)) {
        Serial.printf("CFG: Failed to rename %s\n", tmpPath.c_str());
        LittleFS.remove(tmpPath);
        return false;
    }

    return true;
}

bool LittleFsStore::atomicWriteRaw(const char* finalPath, const String& content) {
    String tmpPath = configTmpPath(finalPath);
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;

    size_t bytes = f.print(content);
    f.close();
    storNoteLfsWrite(bytes);

    if (bytes == 0) {
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath)) {
        LittleFS.remove(tmpPath);
        return false;
    }

    return true;
}

void LittleFsStore::recoverTmpFiles(const char* path) {
    String tmpPath = configTmpPath(path);
    if (!LittleFS.exists(tmpPath)) return;
    if (LittleFS.exists(path)) {
        // Final exists — just clean up tmp
        LittleFS.remove(tmpPath);
    } else {
        // Final missing — promote tmp
        LittleFS.rename(tmpPath, path);
    }
}

// Free function helpers
String configStemPath(const char* stem) {
    return String("/config/") + stem + ".json";
}

String configTmpPath(const char* finalPath) {
    return String(finalPath) + ".tmp";
}
