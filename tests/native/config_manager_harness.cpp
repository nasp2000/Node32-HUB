// Host definitions for the extern/non-config symbols the REAL ConfigManager.cpp
// (compiled as config_manager_stub.cpp) references. Linked into the
// real-ConfigManager tests (alerts/system persistence).
//
// This TU MUST be compiled with the same defines as config_manager_stub.cpp:
//   -DUSER_CONFIG_H_ROOT=1 -DFILESYSTEM_PROVIDER=1 -DENABLE_FILESYSTEM=1
//   -DPACK_EMAIL_SUBJECT=... -DPACK_SYSTEM_NAME=...
//   -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
//   -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1
// and with the real .pio ArduinoJson include dir BEFORE -Istubs.

#include "ConfigManager.h"
#include "NtpManager.h"
#include "LittleFsStore.h"
#include <LittleFS.h>

LittleFSClass LittleFS;
SerialStub Serial;
ESPClass _espInstance;

void sendVirtualMessage(const String& msg, int /*msgType*/,
                        bool /*alreadyFormatted*/, bool /*bypassDedup*/) {
    printf("[DIAG] %.*s\n", (int)msg.length(), msg.c_str());
}

void triggerScreenConfigSaved() {}
void triggerScreenCommandSent(const String&, const String&) {}
void triggerScreenNVSWarning(int) {}

bool littlefsBegin() { return true; }
bool littlefsEnd() { return true; }
bool littlefsMounted() { return true; }
uint64_t littlefsTotalBytes() { return 16ul * 1024 * 1024; }
uint64_t littlefsUsedBytes() { return 1ul; }
uint64_t littlefsFreeBytes() { return 16ul * 1024 * 1024; }
bool littlefsFormat() { return false; }
fs::FS& getLittleFS() {
    static fs::FS f;
    return f;
}

// ConfigManager has a LittleFsStore member but never calls its methods; only
// construction/destruction must link.
LittleFsStore::LittleFsStore() {}
LittleFsStore::~LittleFsStore() {}

void loadStorageStatsFromNVS() {}
bool saveStorageStatsToNVS() { return true; }
bool resetStorageStats() { return true; }
uint64_t nvsGetWriteCount() { return 0; }
uint64_t nvsGetBytesWritten() { return 0; }
void nvsIncrementWriteCount(uint64_t) {}
uint64_t flashGetWriteCount() { return 0; }
uint64_t flashGetBytesWritten() { return 0; }
void flashIncrementWriteCount(uint64_t) {}
// Daily storage stats: no-op on the host.
void storNoteSdWrite(uint64_t) {}
void storNoteLfsWrite(uint64_t) {}
bool storNoteCfgSave() { return true; }
void storNoteCfgSaveForced() {}
void storPersistDaily(bool) {}
uint32_t storDaysTracked() { return 1; }
uint32_t storSdTodayW() { return 0; }
uint64_t storSdTodayB() { return 0; }
uint32_t storLfsTodayW() { return 0; }
uint64_t storLfsTodayB() { return 0; }
uint32_t storCfgToday() { return 0; }

NtpManager ntpManager;

// Test hooks (declared here so tests stay clean): wipe both the in-memory
// LittleFS and the Preferences/NVS store between scenarios.
void nativeResetConfigStorage() {
    LittleFS.files.clear();
    Preferences::resetAll();
}