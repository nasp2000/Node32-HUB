#include "NtpManager.h"
#include "littlefs_manager.h"
#include <time.h>

extern String ntpServer;
extern String timezone;

NtpManager ntpManager;

void NtpManager::begin(const String& fallbackServer, const String& fallbackTz, int fallbackInterval) {
    littlefsBegin();
    if (!LittleFS.exists(filePath())) {
        // First run or upgrade: use fallback (from ConfigManager existing NTP) then persist
        _server = fallbackServer;
        _timezone = fallbackTz;
        _syncIntervalMinutes = fallbackInterval;
        save();
    } else {
        File f = LittleFS.open(filePath(), "r");
        if (f) {
            StaticJsonDocument<512> doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                _server = doc["server"] | "pool.ntp.org";
                _timezone = doc["timezone"] | "WET0WEST,M3.5.0/1,M10.5.0";
                _syncIntervalMinutes = doc["sync_interval_minutes"] | 60;
            }
        }
    }
    ntpServer = _server;
    timezone = _timezone;
}

void NtpManager::setServer(const String& value) {
    if (_server != value) {
        _server = value;
        _dirty = true;
        ntpServer = value;
    }
}

void NtpManager::setTimezone(const String& value) {
    if (_timezone != value) {
        _timezone = value;
        _dirty = true;
        timezone = value;
    }
}

void NtpManager::setSyncIntervalMinutes(int value) {
    if (_syncIntervalMinutes != value) {
        _syncIntervalMinutes = value;
        _dirty = true;
    }
}

void NtpManager::save() {
    littlefsBegin();
    StaticJsonDocument<512> doc;
    doc["server"] = _server;
    doc["timezone"] = _timezone;
    doc["sync_interval_minutes"] = _syncIntervalMinutes;

    String tmpPath = String(filePath()) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) { LittleFS.remove(tmpPath); return; }
    LittleFS.remove(filePath());
    LittleFS.rename(tmpPath, filePath());
    _dirty = false;
}

void NtpManager::sync() {
    configTzTime(_timezone.c_str(), _server.c_str());
    _lastSyncMillis = millis();
}
