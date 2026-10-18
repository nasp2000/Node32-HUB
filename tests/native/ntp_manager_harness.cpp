// Host definitions for the extern/non-config symbols the REAL production
// NtpManager.cpp (compiled as ntp_manager_linux.cpp) references. Linked into
// the NTP persistence test. Compile with the same ArduinoJson flags as the
// ConfigManager harness (REAL_INCLUDES + REAL_AJ_FLAGS).
//
// The POSIX-clashing global `timezone` is renamed to ntpTimeZoneGlobal in the
// ntp_manager_linux.cpp copy (glibc declares `extern long int timezone`, which
// conflicts with the production `extern String timezone;` at link/compile).

#include <Arduino.h>
#include <LittleFS.h>

LittleFSClass LittleFS;

String ntpServer = "pool.ntp.org";
String ntpTimeZoneGlobal = "WET0WEST,M3.5.0/1,M10.5.0";

bool littlefsBegin() { return true; }

void configTzTime(const char* /*tzPtr*/, const char* /*serverPtr*/) {}

// Test hook: wipe the in-memory LittleFS between scenarios.
void nativeResetNtpStorage() {
    LittleFS.files.clear();
}