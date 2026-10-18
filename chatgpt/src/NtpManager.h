#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "LittleFsStore.h"

class NtpManager {
public:
    void begin(const String& fallbackServer, const String& fallbackTz, int fallbackInterval);

    String getServer() const { return _server; }
    String getTimezone() const { return _timezone; }
    int getSyncIntervalMinutes() const { return _syncIntervalMinutes; }

    void setServer(const String& value);
    void setTimezone(const String& value);
    void setSyncIntervalMinutes(int value);

    void save();
    void sync();

    unsigned long getLastSyncMillis() const { return _lastSyncMillis; }
    void setLastSyncMillis(unsigned long ms) { _lastSyncMillis = ms; }

private:
    String _server = "pool.ntp.org";
    String _timezone = "WET0WEST,M3.5.0/1,M10.5.0";
    int _syncIntervalMinutes = 60;
    unsigned long _lastSyncMillis = 0;
    bool _dirty = false;

    static const char* filePath() { return "/config/ntp.json"; }
};

extern NtpManager ntpManager;
