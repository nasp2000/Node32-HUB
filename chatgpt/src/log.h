// log.h - ESP32 Crash/Reboot Logger
// Records system events, crashes, and reboots to SD card (primary) or PSRAM VFS (fallback)
// Log files: /logs/log_DD-MM-YYYY/DD-MM-YYYY.txt  (e.g. /logs/log_28-02-2026/28-02-2026.txt)
//
// INTELLIGENT FALLBACK: Uses microSD if available, PSRAM VFS otherwise (NO Flash writes - logs preserved in volatile PSRAM only)
//
// Usage:
//   - Call initCrashLogger() in setup() after SD and RTC init
//   - Call logSystemEvent() for manual events (auto-throttled)
//   - Automatic detection of crash/reboot on boot
//
// LAST CHANGE: 2026-03-13 22:33 WET

#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

// Log levels. Shared guard: commands.h, debug_writer.h and log.cpp all use
// these; defined here so any TU that includes log.h gets them (avoids the
// circular-include ordering problem where INFO was undefined).
#ifndef LOG_LEVEL_ENUM_DEFINED
#define LOG_LEVEL_ENUM_DEFINED
enum LogLevel : int {
    INFO, WARN, ERROR, DEBUG, CRITICAL_EVENT, COMMAND_RESPONSE, TELNET_EVENT
};
#endif

bool logSystemEvent(const String &eventType, const String &details, bool useMutex = true);
bool fixLogFilenameAfterNtp();
void initCrashLogger();
void logWriteSystemInfo(const String &hdr);
void logEvent(const String &eventType, const String &details);
void logEmailSent(const char* tipo, const char* motivo, const String &subject, const String &to);
void logCompactBufferedEvent(const char* eventType, const char* details);
void logCompactBufferedEventF(const __FlashStringHelper* eventType, const char* details);
void logFlushIfDue();
void logFlushNow();

#endif // LOG_H