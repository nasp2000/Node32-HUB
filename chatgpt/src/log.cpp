#include "log.h"

#include "sd_lib.h" // SD backend shim (SDMMC or SPI)
#include <WiFi.h>
#include <FS.h>
#include <time.h>
#include <cstdint>
#include <cstring>

#include "esp_system.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"

#include "sdcard.h"
#include "littlefs_manager.h" // littlefsBegin() — crash-log fallback without SD
#include <LittleFS.h>
#include "psram_vfs.h"
#include "sd_rotation_manager.h" // stringToInterval() / intervalToMs()
#include "ConfigManager.h"       // getSdcardFlushInterval()

#include "feature_flags.h"
#if ENABLE_ETHERNET==1
#include "eth_w5500.h"
#endif

void loadStorageStatsFromNVS();
bool saveStorageStatsToNVS();
bool resetStorageStats();
uint64_t nvsGetWriteCount();
uint64_t nvsGetBytesWritten();
void nvsIncrementWriteCount(uint64_t bytes);

extern bool sdMutexLock(TickType_t timeoutTicks);
extern void sdMutexUnlock();

enum LogStorageType {
    LOG_STORAGE_SD = 0,
    LOG_STORAGE_PSRAM = 1,
    LOG_STORAGE_NONE = 2
};

enum LogEventType {
    LOG_CRASH = 0,
    LOG_CRASH_DETAIL,
    LOG_EMAIL_RETRY,
    LOG_QUEUE_FULL,
    LOG_SD_BUSY,
    LOG_RENAME,
    LOG_EMAIL_SENT,
    LOG_WIFI_RECONNECT,
    LOG_SYSTEM,
    LOG_OTHER,
    LOG_MAX_TYPES
};

struct LogDuplicateBudget {
    uint32_t hash;
    uint32_t hourBucket;
    uint8_t count;
};

static char*         _logBuf          = nullptr;
static size_t        _logBufLen       = 0;
static unsigned long _logLastFlushMs  = 0;
static portMUX_TYPE  _logBufMux       = portMUX_INITIALIZER_UNLOCKED;

static const size_t        _LOG_BUF_CAP          = 4096;
static const size_t        _LOG_BUF_FLUSH_THRESH = (_LOG_BUF_CAP * 3) / 4;
static const unsigned long _LOG_FLUSH_INTERVAL   = 30UL * 60UL * 1000UL;

static LogStorageType getLogStorageType() {
    if (sdcardIsMounted()) {
        return LOG_STORAGE_SD;
    } else if (psramVFSIsEnabled()) {
        return LOG_STORAGE_PSRAM;
    } else {
        return LOG_STORAGE_NONE;
    }
}

static const char* getLogStorageName(LogStorageType storageType) {
    switch (storageType) {
        case LOG_STORAGE_SD: return "SD";
        case LOG_STORAGE_PSRAM: return "PSRAM";
        case LOG_STORAGE_NONE: return "NONE";
        default: return "Unknown";
    }
}

#if ENABLE_FILESYSTEM==1
// Best-effort, wear-safe no-SD persistence to LittleFS at the same /logs path
// used on SD (persistent across reboots). Config space is protected: skips when
// free flash drops below 1MB. Never fails the caller on flash pressure.
static bool writeLogToLittleFS(const String& logPath, const char* data, size_t len) {
    if (sdcardShouldUseSD()) return false; // SD owns persistence
    if (!data || len == 0) return false;
    if (!littlefsBegin() || !littlefsMounted()) return false;
    if (littlefsFreeBytes() < (1024ULL * 1024ULL)) return false; // keep 1MB for /config
    // Serialize vs email/FTP flash readers (same crash guard as the sender).
    // 1s timeout: a skipped line stays buffered and is retried, never lost.
    extern bool lfsMutexLock(TickType_t timeoutTicks);
    extern void lfsMutexUnlock();
    if (!lfsMutexLock(pdMS_TO_TICKS(1000))) return false;
    int slash = logPath.lastIndexOf('/');
    if (slash > 0) {
        String dir = logPath.substring(0, slash);
        int second = dir.indexOf('/', 1);
        if (second > 0 && !LittleFS.exists(dir.substring(0, second).c_str()))
            LittleFS.mkdir(dir.substring(0, second).c_str());
        if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
    }
    File f = LittleFS.open(logPath.c_str(), FILE_APPEND);
    if (!f) {
        lfsMutexUnlock();
        return false;
    }
    size_t w = f.write((const uint8_t*)data, len);
    f.close();
    lfsMutexUnlock();
    storNoteLfsWrite(w);
    return (w == len);
}
#endif

static const char* getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        case ESP_RST_POWERON:    return "POWER_ON";
        case ESP_RST_EXT:        return "EXTERNAL_PIN";
        case ESP_RST_SW:         return "SOFTWARE_RESET";
        case ESP_RST_PANIC:      return "PANIC_CRASH";
        case ESP_RST_INT_WDT:    return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:   return "TASK_WATCHDOG";
        case ESP_RST_WDT:        return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT:   return "BROWNOUT_RESET";
        case ESP_RST_SDIO:       return "SDIO_RESET";
        default:                 return "UNDEFINED";
    }
}

// Rotation state for log files (reset on day change).
static int  _logRotIndex    = 0;
static char _logRotLastDay[16] = "";

// Tracks which day the PSRAM log file header was last written (reset on reboot).
static char _psramLogHeaderDay[16] = "";

// Write the standard log file header to the fallback backend on the first write
// of each day. FS packs (no cam) persist logs straight to LittleFS (VFS stays
// reserved for gcode); packs without LittleFS keep the volatile PSRAM VFS.
static void _writeLogHeaderIfNew(const String& logPath) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char dayStr[16];
    strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &ti);
    if (strncmp(_psramLogHeaderDay, dayStr, 15) == 0) return;
    strncpy(_psramLogHeaderDay, dayStr, 15);
    _psramLogHeaderDay[15] = '\0';
    String hdr = "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\n";
    hdr += "Device IP: (a aguardar liga\xC3\xA7\xC3\xA3o...)\n";
    hdr += "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\n";
#if ENABLE_FILESYSTEM==1
    writeLogToLittleFS(logPath, hdr.c_str(), hdr.length());
#else
    String psramSrc = logPath.substring(1);
    psramVFSWrite(psramSrc.c_str(), hdr.c_str(), hdr.length());
#endif
}

// Backend-dispatching no-SD log write: FS packs write to LittleFS (persistent,
// same /logs/<date>.txt path); packs without LittleFS keep PSRAM VFS. Only the
// FS==1 side is expected to fail (flash wear/protection) — the VFS path matches
// the historical best-effort semantics.
static bool _writeLogFallback(const String& logPath, const char* data, size_t len) {
#if ENABLE_FILESYSTEM==1
    return writeLogToLittleFS(logPath, data, len);
#else
    String psramSrc = logPath.substring(1);
    return psramVFSWrite(psramSrc.c_str(), data, len);
#endif
}

static String getLogFilePath() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char dayStr[16];
    strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &timeinfo);

    char filename[64];
    snprintf(filename, sizeof(filename), "/logs/log_%s/%s.txt", dayStr, dayStr);
    return String(filename);
}

// Returns the currently active log path, including the rotation suffix when needed.
// Resets the rotation index if the day has changed.
static String getActiveLogFilePath() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char dayStr[16];
    strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &timeinfo);

    if (strncmp(_logRotLastDay, dayStr, sizeof(_logRotLastDay) - 1) != 0) {
        _logRotIndex = 0;
        strncpy(_logRotLastDay, dayStr, sizeof(_logRotLastDay) - 1);
        _logRotLastDay[sizeof(_logRotLastDay) - 1] = '\0';
    }

    char filename[64];
    if (_logRotIndex > 0) {
        snprintf(filename, sizeof(filename), "/logs/log_%s/%s_%03d.txt", dayStr, dayStr, _logRotIndex);
    } else {
        snprintf(filename, sizeof(filename), "/logs/log_%s/%s.txt", dayStr, dayStr);
    }
    return String(filename);
}

static bool ensureLogDirectory() {
    if (sdcardIsMounted()) {
        // Boot migration: legacy /LOGs root moved to /logs (single log root on the card).
        if (SD_MMC.exists("/LOGs") && !SD_MMC.exists("/logs")) {
            if (SD_MMC.rename("/LOGs", "/logs")) {
                sendVirtualMessage(F("🗂️ LOG: moved legacy /LOGs to /logs"), INFO);
            }
        }
        if (!SD_MMC.exists("/logs")) {
            if (!SD_MMC.mkdir("/logs")) return false;
        }
        String logPath = getLogFilePath();
        int lastSlash = logPath.lastIndexOf('/');
        if (lastSlash > 0) {
            String logDir = logPath.substring(0, lastSlash);
            if (!SD_MMC.exists(logDir.c_str())) {
                return SD_MMC.mkdir(logDir.c_str());
            }
        }
    }
    return true;
}

static String getCurrentIPForLog() {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != IPAddress(0, 0, 0, 0)) {
            return ip.toString();
        }
    }

    #if ENABLE_ETHERNET==1
    IPAddress ethIP = ethernetLocalIP();
    if (ethIP != IPAddress(0, 0, 0, 0)) {
        return ethIP.toString();
    }
    #endif

    return "0.0.0.0";
}

static LogEventType getLogEventTypeEnum(const String &eventType) {
    if (eventType.indexOf("CRASH_DETAIL") >= 0) return LOG_CRASH_DETAIL;
    if (eventType.indexOf("CRASH") >= 0) return LOG_CRASH;
    if (eventType.indexOf("EMAIL_RETRY") >= 0) return LOG_EMAIL_RETRY;
    if (eventType.indexOf("QUEUE_FULL") >= 0) return LOG_QUEUE_FULL;
    if (eventType.indexOf("SD_BUSY") >= 0) return LOG_SD_BUSY;
    if (eventType.indexOf("RENAME") >= 0) return LOG_RENAME;
    if (eventType.indexOf("EMAIL_SENT") >= 0) return LOG_EMAIL_SENT;
    if (eventType.indexOf("WIFI_RECONNECT") >= 0) return LOG_WIFI_RECONNECT;
    if (eventType.indexOf("SYSTEM") >= 0) return LOG_SYSTEM;
    return LOG_OTHER;
}

static uint32_t logHashTextPart(uint32_t hash, const char* text) {
    if (!text) return hash;
    while (*text) {
        hash ^= (uint8_t)(*text++);
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t logHashSignature(const char* part1,
                                 const char* part2 = nullptr,
                                 const char* part3 = nullptr,
                                 const char* part4 = nullptr,
                                 const char* part5 = nullptr) {
    uint32_t hash = 2166136261u;
    hash = logHashTextPart(hash, part1);
    hash = logHashTextPart(hash, "\x1F");
    hash = logHashTextPart(hash, part2);
    hash = logHashTextPart(hash, "\x1F");
    hash = logHashTextPart(hash, part3);
    hash = logHashTextPart(hash, "\x1F");
    hash = logHashTextPart(hash, part4);
    hash = logHashTextPart(hash, "\x1F");
    hash = logHashTextPart(hash, part5);
    return hash;
}

static uint32_t logCurrentHourBucket() {
    time_t now = time(nullptr);
    if (now > 100000) return (uint32_t)(now / 3600);
    return millis() / 3600000UL;
}

static bool logAllowDuplicateSignatureThisHour(uint32_t signatureHash, uint8_t maxPerHour) {
    static LogDuplicateBudget budgets[24] = {};
    static portMUX_TYPE budgetMux = portMUX_INITIALIZER_UNLOCKED;

    if (signatureHash == 0 || maxPerHour == 0) return true;

    const uint32_t currentHour = logCurrentHourBucket();
    int reusableIndex = -1;

    portENTER_CRITICAL(&budgetMux);
    for (int i = 0; i < 24; ++i) {
        if (budgets[i].hash == signatureHash && budgets[i].hourBucket == currentHour) {
            if (budgets[i].count >= maxPerHour) {
                portEXIT_CRITICAL(&budgetMux);
                return false;
            }
            budgets[i].count++;
            portEXIT_CRITICAL(&budgetMux);
            return true;
        }

        if (reusableIndex < 0 && (budgets[i].hash == 0 || budgets[i].hourBucket != currentHour)) {
            reusableIndex = i;
        }
    }

    if (reusableIndex < 0) reusableIndex = 0;
    budgets[reusableIndex].hash = signatureHash;
    budgets[reusableIndex].hourBucket = currentHour;
    budgets[reusableIndex].count = 1;
    portEXIT_CRITICAL(&budgetMux);
    return true;
}

static bool stripLeadingPendingIpHeader(String &content) {
    if (!content.startsWith("════════")) return false;

    int nl1 = content.indexOf('\n');
    int nl2 = (nl1 >= 0) ? content.indexOf('\n', nl1 + 1) : -1;
    int nl3 = (nl2 >= 0) ? content.indexOf('\n', nl2 + 1) : -1;
    if (nl1 < 0 || nl2 < 0 || nl3 < 0) return false;

    String middleLine = content.substring(nl1 + 1, nl2);
    if (!middleLine.startsWith("Device IP:")) return false;

    content.remove(0, nl3 + 1);
    return true;
}

static bool isLogHeaderBlock(const String &block) {
    return block.startsWith("--- ") && block.indexOf(" - IP: ") >= 0 && block.indexOf(" ---") >= 0;
}

static bool stripLeadingSystemHeaders(String &content) {
    bool changed = false;

    while (true) {
        while (content.startsWith("\r") || content.startsWith("\n")) {
            content.remove(0, 1);
            changed = true;
        }

        int blockEnd = content.indexOf("\n\n");
        if (blockEnd < 0) break;

        String block = content.substring(0, blockEnd + 2);
        if (!isLogHeaderBlock(block)) break;

        content.remove(0, blockEnd + 2);
        changed = true;
    }

    return changed;
}

static const char* excCauseName(uint32_t cause) {
    switch (cause) {
        case  0: return "IllegalInstruction";
        case  1: return "Syscall";
        case  2: return "InstrFetchError";
        case  3: return "LoadStoreError";
        case  5: return "StackOverflow";
        case  6: return "DivideByZero";
        case  8: return "Privileged";
        case  9: return "LoadStoreAlignment";
        case 12: return "InstrPIFDataError";
        case 13: return "LoadStorePIFDataError";
        case 14: return "InstrPIFAddrError";
        case 15: return "LoadStorePIFAddrError";
        case 28: return "LoadProhibited";
        case 29: return "StoreProhibited";
        default: return "Unknown";
    }
}

static bool flushLogBuffer(bool skipIfBusy = true, bool ignoreSdWait = false);
static void _logBufAppend(const char* text, size_t len);
static void logCrashOrReboot();

// Build a single log line: "[DD-MM-YYYY HH:MM:SS] eventType | details | Uptime: Xh Ym Zs\n"
// Uses a stack buffer to avoid heap allocation on the hot path.
static String buildLogLine(const String &eventType, const String &details) {
    time_t nowTime = time(nullptr);
    struct tm timeinfo;
    localtime_r(&nowTime, &timeinfo);

    char timestamp[32];
    if (nowTime > 100000) {
        strftime(timestamp, sizeof(timestamp), "%d-%m-%Y %H:%M:%S", &timeinfo);
    } else {
        snprintf(timestamp, sizeof(timestamp), "BOOT+%lums", millis());
    }

    unsigned long uptimeSec = millis() / 1000;
    unsigned long hours   = uptimeSec / 3600;
    unsigned long minutes = (uptimeSec % 3600) / 60;
    unsigned long seconds = uptimeSec % 60;

    return String("[") + timestamp + "] " + eventType + " | " + details +
           " | Uptime: " + hours + "h " + minutes + "m " + seconds + "s\n";
}

bool logSystemEvent(const String &eventType, const String &details, bool useMutex) {
    static unsigned long lastEventWriteTime[LOG_MAX_TYPES] = {0};
    const unsigned long THROTTLE_INTERVAL_MS = 60000;

    unsigned long nowMillis = millis();
    LogEventType typeEnum = getLogEventTypeEnum(eventType);

    if (typeEnum != LOG_WIFI_RECONNECT && lastEventWriteTime[typeEnum] > 0) {
        unsigned long timeSinceLastWrite = nowMillis - lastEventWriteTime[typeEnum];
        if (timeSinceLastWrite < THROTTLE_INTERVAL_MS) {
            Serial.printf("⏭️ LOG: Throttled %s (wrote %lus ago)\n", eventType.c_str(), timeSinceLastWrite / 1000);
            return false;
        }
    }

    LogStorageType storageType = getLogStorageType();
    const char* storageName = getLogStorageName(storageType);
    if (storageType == LOG_STORAGE_NONE) {
        // Storage not ready yet (called at early boot before SD/PSRAM init).
        // Buffer the entry so it gets flushed when storage becomes available.
        String logEntry = buildLogLine(eventType, details);
        _logBufAppend(logEntry.c_str(), logEntry.length());
        return false;
    }

    if (storageType == LOG_STORAGE_PSRAM) {
        String logPath = getActiveLogFilePath();

        _writeLogHeaderIfNew(logPath);

        String logEntry = buildLogLine(eventType, details);
        bool success = _writeLogFallback(logPath, logEntry.c_str(), logEntry.length());
        if (success) {
            lastEventWriteTime[typeEnum] = nowMillis;
            Serial.printf("\xE2\x9C\x85 LOG [%s]: Written to %s: %s", storageName, logPath.c_str(), logEntry.c_str());
        } else {
            Serial.printf("\xE2\x9A\xA0\xEF\xB8\x8F LOG [%s]: Write failed\n", storageName);
        }
        return success;
    }

    if (storageType != LOG_STORAGE_SD) {
        return false;
    }

    fs::FS* logFS = &SD_MMC;
    if (useMutex && !sdMutexLock(pdMS_TO_TICKS(5000))) {
        Serial.println("⚠️ LOG: Failed to acquire SD mutex (timeout)");
        return false;
    }

    if (!ensureLogDirectory()) {
        Serial.printf("⚠️ LOG: Failed to create /logs directory on %s\n", storageName);
        if (useMutex) sdMutexUnlock();
        return false;
    }

    String logPath = getActiveLogFilePath();
    bool isNewFile = !logFS->exists(logPath.c_str());

    // Rotate to a new file if the current one has reached the size limit.
    if (!isNewFile) {
        File sizeCheck = logFS->open(logPath.c_str(), FILE_READ);
        if (sizeCheck) {
            size_t sz = sizeCheck.size();
            sizeCheck.close();
            if (sz >= STORAGE_ROTATED_FILE_MAX_SIZE_BYTES) {
                _logRotIndex++;
                logPath = getActiveLogFilePath();
                isNewFile = true;
            }
        }
    }

    File logFile = logFS->open(logPath.c_str(), FILE_APPEND);
    if (!logFile) {
        Serial.printf("⚠️ LOG: Failed to open %s on %s\n", logPath.c_str(), storageName);
        if (useMutex) sdMutexUnlock();
        return false;
    }

    if (isNewFile) {
        String ipHeader = "═══════════════════════════════════════════════\n";
        ipHeader += "Device IP: (a aguardar liga\xC3\xA7\xC3\xA3o...)\n";
        ipHeader += "═══════════════════════════════════════════════\n";
        logFile.print(ipHeader);
        Serial.printf("\xE2\x9C\x85 LOG [%s]: New file created (header pending IP): %s\n", storageName, logPath.c_str());
    }

    String logEntry = buildLogLine(eventType, details);

    size_t written = logFile.print(logEntry);
    logFile.close();

    if (useMutex) sdMutexUnlock();

    bool success = (written > 0);
    if (success) {
        lastEventWriteTime[typeEnum] = nowMillis;
        Serial.printf("✅ LOG [%s]: Written to %s: %s", storageName, logPath.c_str(), logEntry.c_str());
    } else {
        Serial.printf("⚠️ LOG [%s]: Write failed\n", storageName);
    }

    return success;
}

// Fallback persistence: when there is no SD card, the log lives in the
// volatile PSRAM VFS. Persist boot/crash events to LittleFS instead so they
// survive a reset and can be fetched later (e.g. /files). Safe here: runs at
// boot before the RGB panel starts streaming (T7), and crashes are rare.
static void appendBootEventToLittleFS(const char* eventType, const String& details) {
    if (sdcardIsMounted()) return;      // SD already stores the full log
    if (!littlefsBegin()) return;
    if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
    File f = LittleFS.open("/logs/boot.log", FILE_APPEND);
    if (!f) return;
    f.printf("%lu | %s | %s\n", (unsigned long)millis(), eventType, details.c_str());
    f.close();
}

static void logCrashOrReboot() {
    esp_reset_reason_t resetReason = esp_reset_reason();
    const char* reasonStr = getResetReasonString(resetReason);

    String eventType;
    String details;

    switch (resetReason) {
        case ESP_RST_POWERON:
            eventType = "BOOT_POWER_ON";
            details = "System powered on (cold boot)";
            break;
        case ESP_RST_SW:
            eventType = "BOOT_SOFTWARE";
            details = "Software reset (planned reboot)";
            break;
        case ESP_RST_PANIC:
            eventType = "CRASH_PANIC";
            details = "System crashed (panic/exception)";
            break;
        case ESP_RST_INT_WDT:
            eventType = "CRASH_INT_WDT";
            details = "Interrupt watchdog timeout (ISR hung)";
            break;
        case ESP_RST_TASK_WDT:
            eventType = "CRASH_TASK_WDT";
            details = "Task watchdog timeout (task hung)";
            break;
        case ESP_RST_WDT:
            eventType = "CRASH_WDT";
            details = "Watchdog timeout (other)";
            break;
        case ESP_RST_BROWNOUT:
            eventType = "CRASH_BROWNOUT";
            details = "Brownout reset (power supply issue)";
            break;
        case ESP_RST_DEEPSLEEP:
            eventType = "WAKE_DEEPSLEEP";
            details = "Woke from deep sleep";
            break;
        default:
            eventType = "BOOT_UNKNOWN";
            details = String("Reset reason: ") + reasonStr;
            break;
    }

    details += String(" (code=") + String((int)resetReason) + ")";

    // Attempt direct storage write; if it fails (mutex timeout, SD busy, etc.),
    // fall back to _logBuf so the event is written on the next periodic flush.
    if (!logSystemEvent(eventType, details)) {
        if (getLogStorageType() != LOG_STORAGE_NONE) {
            // LOG_STORAGE_NONE already buffers inside logSystemEvent(); avoid double-buffering.
            String entry = buildLogLine(eventType, details);
            _logBufAppend(entry.c_str(), entry.length());
        }
        logFlushNow();  // Try to flush the buffer to storage immediately.
    }
    appendBootEventToLittleFS(eventType.c_str(), details);

    Serial.println("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90");
    Serial.printf("SYSTEM REBOOT DETECTED: %s\n", eventType.c_str());
    Serial.printf("Details: %s\n", details.c_str());
    Serial.println("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90");

    if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_TASK_WDT) {
        if (esp_core_dump_image_check() == ESP_OK) {
            esp_core_dump_summary_t cd;
            if (esp_core_dump_get_summary(&cd) == ESP_OK) {
                // Build detail string with "|"-separated fields to match the rest of the log format.
                char task[20];
                snprintf(task, sizeof(task), "%.15s", cd.exc_task);

#if defined(CONFIG_IDF_TARGET_ESP32P4)
                // RISC-V (ESP32-P4): exc_bt_info is a raw stack dump (not a decoded backtrace) and
                // ex_info holds the machine registers. Log PC/RA/Cause/Addr/SP so the crash site can
                // be resolved with riscv32-esp-elf-addr2line against firmware.elf (RA = return address).
                //
                // The raw stack dump (exc_bt_info.stackdump, from SP upward) holds the saved return
                // addresses of the active call chain. Scan it for words falling into code regions
                // (ROM 0x4000_0000+, IRAM 0x4ff0_0000+, flash XIP 0x4fc0_0000+) and append them as
                // "STK:" candidates; each is a likely frame of the chain that reached esp_system_abort.
                char stk[320] = "STK:";
                {
                    uint32_t words = cd.exc_bt_info.dump_size / 4;
                    if (words > 200) words = 200;
                    const uint32_t *sp32 = (const uint32_t *)cd.exc_bt_info.stackdump;
                    size_t used = 4;
                    for (uint32_t i = 0; i < words && used < sizeof(stk) - 12; i++) {
                        uint32_t v = sp32[i];
                        bool isCode = (v >= 0x40000000u && v < 0x40200000u) ||
                                      (v >= 0x4fc00000u && v < 0x4fd00000u) ||
                                      (v >= 0x4ff00000u && v < 0x4ff40000u);
                        if (isCode) {
                            int n = snprintf(stk + used, sizeof(stk) - used, " %08x", (unsigned)v);
                            if (n > 0) used += (size_t)n;
                        }
                    }
                }
                char detail[520];
                snprintf(detail, sizeof(detail),
                    "Task=%s | PC=0x%08x | RA=0x%08x | Cause=0x%x | Addr=0x%08x | SP=0x%08x | %s",
                    task, (unsigned)cd.exc_pc,
                    (unsigned)cd.ex_info.ra, (unsigned)cd.ex_info.mcause,
                    (unsigned)cd.ex_info.mtval, (unsigned)cd.ex_info.sp,
                    stk);
#else
                char bt[320] = "";
                if (cd.exc_bt_info.depth > 0 && !cd.exc_bt_info.corrupted) {
                    int m = 0;
                    m += snprintf(bt + m, sizeof(bt) - m, "BT:");
                    for (uint32_t i = 0; i < cd.exc_bt_info.depth && m < (int)sizeof(bt) - 12; i++) {
                        m += snprintf(bt + m, sizeof(bt) - m, " 0x%08x", (unsigned)cd.exc_bt_info.bt[i]);
                    }
                } else if (cd.exc_bt_info.corrupted) {
                    snprintf(bt, sizeof(bt), "BT:corrupted");
                } else {
                    snprintf(bt, sizeof(bt), "BT:unavailable");
                }

                char detail[480];
                snprintf(detail, sizeof(detail),
                    "Task=%s | PC=0x%08x | Cause=%u(%s) | Addr=0x%08x | %s",
                    task,
                    (unsigned)cd.exc_pc,
                    (unsigned)cd.ex_info.exc_cause,
                    excCauseName(cd.ex_info.exc_cause),
                    (unsigned)cd.ex_info.exc_vaddr,
                    bt);
#endif

                if (!logSystemEvent("CRASH_DETAIL", String(detail))) {
                    if (getLogStorageType() != LOG_STORAGE_NONE) {
                        String entry = buildLogLine("CRASH_DETAIL", String(detail));
                        _logBufAppend(entry.c_str(), entry.length());
                    }
                    logFlushNow();
                }
                appendBootEventToLittleFS("CRASH_DETAIL", String(detail));

                Serial.println("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90");
                Serial.printf("CRASH DETAIL: %s\n", detail);
#if defined(CONFIG_IDF_TARGET_ESP32P4)
                Serial.println("   To decode: riscv32-esp-elf-addr2line -e firmware.elf <RA> (or <PC>)");
#else
                Serial.println("   To decode: xtensa-esp32s3-elf-addr2line -e firmware.elf <PC>");
#endif
                Serial.println("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90");
            } else {
                Serial.println("⚠️ LOG: Core dump present but summary read failed");
            }
        } else {
            Serial.println("⚠️ LOG: No valid core dump in flash (partition empty or wrong)");
        }
    }
    // Persist boot/crash evidence NOW (live .24 2026-10-14: panics every few
    // minutes reset the 30-min flush clock, so flash never received today's
    // log and remote diagnosis was blind). The forced drain bypasses the
    // early-boot SD-wait: crash forensics must land on flash immediately.
    // Cheap: one buffer drain per boot.
    portENTER_CRITICAL(&_logBufMux);
    bool crashHasData = (_logBuf != nullptr && _logBufLen > 0);
    portEXIT_CRITICAL(&_logBufMux);
    if (crashHasData) {
        flushLogBuffer(false, true);
        _logLastFlushMs = millis();
    }
}

bool fixLogFilenameAfterNtp() {
    time_t now = time(nullptr);
    if (now < 100000) {
        Serial.println("⚠️ LOG: Cannot fix filename - NTP not synced yet");
        return false;
    }

    if (!sdcardIsMounted()) {
        return true;
    }

    fs::FS* logFS = &SD_MMC;
    const char* fsName = "SD";

    const char* preferredOld = "/logs/log_01-01-1970/01-01-1970.txt";
    const char* legacyLogsData = "/logs/logs_data/1970-01-01.txt";
    const char* legacyRoot = "/logs/1970-01-01.txt";
    const char* oldLogPath = nullptr;
    if (logFS->exists(preferredOld)) {
        oldLogPath = preferredOld;
    } else if (logFS->exists(legacyLogsData)) {
        oldLogPath = legacyLogsData;
    } else if (logFS->exists(legacyRoot)) {
        oldLogPath = legacyRoot;
    }
    if (oldLogPath == nullptr) {
        return true;
    }

    String correctLogPath = getLogFilePath();
    if (String(oldLogPath) == correctLogPath) {
        return true;
    }

    Serial.printf("📝 LOG [%s]: Renaming %s → %s\n", fsName, oldLogPath, correctLogPath.c_str());

    if (!sdMutexLock(pdMS_TO_TICKS(5000))) {
        Serial.println("⚠️ LOG: Failed to acquire SD mutex for rename");
        return false;
    }

    bool success = false;
    if (logFS->exists(correctLogPath.c_str())) {
        File oldFile = logFS->open(oldLogPath, FILE_READ);
        File newFile = logFS->open(correctLogPath.c_str(), FILE_APPEND);

        if (oldFile && newFile) {
            newFile.println("\n====================================================");
            newFile.printf("LOG: Content below was imported from %s (pre-NTP)\n", oldLogPath);
            newFile.println("====================================================\n");

            while (oldFile.available()) {
                newFile.write(oldFile.read());
            }

            oldFile.close();
            newFile.close();

            logFS->remove(oldLogPath);
            Serial.printf("✅ LOG [%s]: Content merged and old file deleted\n", fsName);
            success = true;
        } else {
            if (oldFile) oldFile.close();
            if (newFile) newFile.close();
            Serial.println("⚠️ LOG [SD]: Failed to open files for merge");
        }
    } else {
        if (logFS->rename(oldLogPath, correctLogPath.c_str())) {
            Serial.printf("✅ LOG [%s]: File renamed successfully\n", fsName);

            File logFile = logFS->open(correctLogPath.c_str(), FILE_APPEND);
            if (logFile) {
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);
                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%d-%m-%Y %H:%M:%S", &timeinfo);

                logFile.printf("\n[%s] LOG_RENAME | File renamed from %s after NTP sync\n", timestamp, oldLogPath);
                logFile.close();
            }
            success = true;
        } else {
            Serial.println("⚠️ LOG [SD]: Rename failed");
        }
    }

    sdMutexUnlock();
    return success;
}

void initCrashLogger() {
    Serial.println("🔍 Initializing crash logger...");

    LogStorageType storageType = getLogStorageType();
    const char* storageName = getLogStorageName(storageType);

    if (storageType == LOG_STORAGE_NONE) {
        Serial.println("⚠️ LOG: No storage available (SD or PSRAM) - logs will be lost");
    } else {
        Serial.printf("📁 LOG: Using %s for logging\n", storageName);
    }

    time_t now = time(nullptr);
    if (now < 100000) {
        Serial.println("⚠️ LOG: NTP not synced - log filename will be incorrect (/logs/1970-01-01.txt)");
    } else {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%d-%m-%Y", &timeinfo);
        Serial.printf("✅ LOG: Using date: %s\n", buf);
    }

    logCrashOrReboot();
    Serial.println("✅ Crash logger initialized");
}

void logWriteSystemInfo(const String &hdr) {
    if (hdr.isEmpty()) return;

    LogStorageType st = getLogStorageType();
    if (st == LOG_STORAGE_NONE) return;
    if (!ensureLogDirectory()) return;

    String logPath = getActiveLogFilePath();
    if (st == LOG_STORAGE_PSRAM) {
        _writeLogFallback(logPath, hdr.c_str(), hdr.length());
        return;
    }

    if (!sdMutexLock(pdMS_TO_TICKS(6000))) return;

    // Check if the header is already the first thing in the file (avoid rewriting).
    if (SD_MMC.exists(logPath.c_str())) {
        File rf = SD_MMC.open(logPath.c_str(), FILE_READ);
        if (rf) {
            // Read only as many bytes as the header to compare cheaply.
            size_t peekLen = hdr.length();
            char peek[256];
            if (peekLen > sizeof(peek)) peekLen = sizeof(peek);
            size_t got = rf.readBytes(peek, peekLen);
            rf.close();
            if (got == peekLen && memcmp(peek, hdr.c_str(), peekLen) == 0) {
                // Header already at the top — nothing to do.
                sdMutexUnlock();
                return;
            }
        }
    }

    // Write header + existing content into a .tmp file, then atomically rename.
    // This avoids loading the entire file into RAM — only a small read buffer is needed.
    String tmpPath = logPath + ".tmp";
    File wf = SD_MMC.open(tmpPath.c_str(), FILE_WRITE);
    if (!wf) {
        Serial.printf("⚠️ LOG: Failed to open tmp file %s\n", tmpPath.c_str());
        sdMutexUnlock();
        return;
    }

    wf.print(hdr);
    storNoteSdWrite(hdr.length());  // new header bytes only (rest is copied content)

    if (SD_MMC.exists(logPath.c_str())) {
        File rf = SD_MMC.open(logPath.c_str(), FILE_READ);
        if (rf) {
            // Strip any outdated pending-IP header or old system header blocks.
            // To do this cheaply, read the first 4 KB into a small String, strip,
            // then stream the remainder directly — avoids loading the whole file.
            const size_t STRIP_WINDOW = 4096;
            char* window = (char*)malloc(STRIP_WINDOW + 1);
            if (window) {
                size_t windowRead = 0;
                while (rf.available() && windowRead < STRIP_WINDOW) {
                    int b = rf.read();
                    if (b < 0) break;
                    window[windowRead++] = (char)b;
                }
                window[windowRead] = '\0';

                String head(window);
                free(window);

                bool stripped = false;
                stripped |= stripLeadingPendingIpHeader(head);
                stripped |= stripLeadingSystemHeaders(head);
                while (head.startsWith("\r") || head.startsWith("\n")) {
                    head.remove(0, 1);
                    stripped = true;
                }
                (void)stripped;

                wf.print(head);
            }

            // Stream the rest of the file directly without loading it into RAM.
            char buf[256];
            while (rf.available()) {
                size_t got = rf.readBytes(buf, sizeof(buf));
                if (got == 0) break;
                wf.write((const uint8_t*)buf, got);
            }
            rf.close();
        }
    }

    wf.close();

    SD_MMC.remove(logPath.c_str());
    if (SD_MMC.rename(tmpPath.c_str(), logPath.c_str())) {
        Serial.printf("✅ LOG: System header written to %s\n", logPath.c_str());
    } else {
        Serial.printf("⚠️ LOG: Rename failed for %s\n", logPath.c_str());
        SD_MMC.remove(tmpPath.c_str());
    }

    sdMutexUnlock();
}

void logEvent(const String &eventType, const String &details) {
    logSystemEvent(eventType, details);
}

static bool flushLogBuffer(bool skipIfBusy, bool ignoreSdWait) {
    portENTER_CRITICAL(&_logBufMux);
    char* snapBuf = _logBuf;
    size_t snapLen = _logBufLen;
    portEXIT_CRITICAL(&_logBufMux);

    if (snapBuf == nullptr || snapLen == 0) {
        _logLastFlushMs = millis();
        return true;
    }

    LogStorageType st = getLogStorageType();
    if (st == LOG_STORAGE_NONE) return false;

    if (st == LOG_STORAGE_PSRAM) {
#if ENABLE_SDCARD == 1
        // If SD is compiled in but not yet mounted, wait up to 30s for it.
        // This keeps early boot events (reboot cause, crash) in the buffer so
        // they end up on SD together with the rest of the boot log instead of
        // being split across PSRAM (early) and SD (late).
        // Crash forensics bypasses the wait (ignoreSdWait): a board that
        // panics every few minutes would otherwise never persist the evidence.
        const unsigned long SD_WAIT_MS = 30000UL;
        if (millis() < SD_WAIT_MS && !ignoreSdWait) {
            return false;
        }
#endif
        String logPath = getActiveLogFilePath();
        _writeLogHeaderIfNew(logPath);
        bool ok = _writeLogFallback(logPath, snapBuf, snapLen);
        if (ok && snapLen > 0 && snapBuf[snapLen - 1] != '\n') {
            _writeLogFallback(logPath, "\n", 1);
        }
        if (ok) {
            portENTER_CRITICAL(&_logBufMux);
            if (_logBufLen > snapLen) {
                size_t extra = _logBufLen - snapLen;
                memmove(_logBuf, _logBuf + snapLen, extra);
                _logBufLen = extra;
            } else {
                _logBufLen = 0;
            }
            portEXIT_CRITICAL(&_logBufMux);
            _logLastFlushMs = millis();
        }
        return ok;
    }

    TickType_t tmo = skipIfBusy ? pdMS_TO_TICKS(300) : pdMS_TO_TICKS(10000);
    if (!sdMutexLock(tmo)) return false;

    if (!ensureLogDirectory()) {
        sdMutexUnlock();
        return false;
    }

    String logPath = getActiveLogFilePath();
    bool isNew = !SD_MMC.exists(logPath.c_str());

    // Rotate to a new file if the current one has reached the size limit.
    if (!isNew) {
        File sizeCheck = SD_MMC.open(logPath.c_str(), FILE_READ);
        if (sizeCheck) {
            size_t sz = sizeCheck.size();
            sizeCheck.close();
            if (sz >= STORAGE_ROTATED_FILE_MAX_SIZE_BYTES) {
                _logRotIndex++;
                logPath = getActiveLogFilePath();
                isNew = true;
            }
        }
    }

    File f = SD_MMC.open(logPath.c_str(), FILE_APPEND);
    if (!f) {
        sdMutexUnlock();
        return false;
    }
    size_t logStartSize = f.size();

    if (isNew) {
        String hdr = String(F("Device IP: ")) + getCurrentIPForLog() + F("\n---\n");
        f.print(hdr);
    }
    f.write((const uint8_t*)snapBuf, snapLen);
    if (snapLen > 0 && snapBuf[snapLen - 1] != '\n') {
        f.write((uint8_t)'\n');
    }
    size_t logEndSize = f.size();
    f.close();
    storNoteSdWrite(logEndSize > logStartSize ? logEndSize - logStartSize : 0);
    sdMutexUnlock();

    portENTER_CRITICAL(&_logBufMux);
    if (_logBufLen > snapLen) {
        size_t extra = _logBufLen - snapLen;
        memmove(_logBuf, _logBuf + snapLen, extra);
        _logBufLen = extra;
    } else {
        _logBufLen = 0;
    }
    portEXIT_CRITICAL(&_logBufMux);
    _logLastFlushMs = millis();
    return true;
}

static void _logBufAppend(const char* text, size_t len) {
    if (len == 0) return;

    portENTER_CRITICAL(&_logBufMux);
    bool needsAlloc = (_logBuf == nullptr);
    portEXIT_CRITICAL(&_logBufMux);

    if (needsAlloc) {
        char* newBuf = (char*)heap_caps_malloc(_LOG_BUF_CAP, MALLOC_CAP_SPIRAM);
        if (!newBuf) return;
        portENTER_CRITICAL(&_logBufMux);
        if (_logBuf == nullptr) {
            _logBuf = newBuf;
            _logBufLen = 0;
        } else {
            heap_caps_free(newBuf);
        }
        portEXIT_CRITICAL(&_logBufMux);
    }

    portENTER_CRITICAL(&_logBufMux);
    bool needsFlush = (_logBufLen + len >= _LOG_BUF_FLUSH_THRESH);
    portEXIT_CRITICAL(&_logBufMux);

    if (needsFlush) {
        flushLogBuffer(false);
    }

    portENTER_CRITICAL(&_logBufMux);
    if (_logBuf != nullptr && _logBufLen + len < _LOG_BUF_CAP) {
        memcpy(_logBuf + _logBufLen, text, len);
        _logBufLen += len;
    }
    portEXIT_CRITICAL(&_logBufMux);
}

void logEmailSent(const char* tipo, const char* motivo, const String &subject, const String &to) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    char ts[10];
    strftime(ts, sizeof(ts), "%H:%M:%S", &ti);

    String subj = subject.length() > 120 ? subject.substring(0, 117) + F("...") : subject;
    String dest = to.length() > 80 ? to.substring(0, 77) + F("...") : to;

    static const uint8_t LOG_EMAIL_DUPLICATE_MAX_PER_HOUR = 4;
    uint32_t signature = logHashSignature("EMAIL_SENT", tipo, motivo, subj.c_str(), dest.c_str());
    if (!logAllowDuplicateSignatureThisHour(signature, LOG_EMAIL_DUPLICATE_MAX_PER_HOUR)) {
        return;
    }

    char line[240];
    int n = snprintf(line, sizeof(line),
        "[%s] EMAIL_SENT | tipo=%s | motivo=%s | assunto=%s | para=%s\n",
        ts, tipo, motivo, subj.c_str(), dest.c_str());
    if (n > 0 && n < (int)sizeof(line)) {
        _logBufAppend(line, (size_t)n);
    }
}

void logCompactBufferedEvent(const char* eventType, const char* details) {
    if (!eventType || !details) return;

    static const uint8_t LOG_COMPACT_DUPLICATE_MAX_PER_HOUR = 4;
    uint32_t signature = logHashSignature(eventType, details);
    if (!logAllowDuplicateSignatureThisHour(signature, LOG_COMPACT_DUPLICATE_MAX_PER_HOUR)) {
        return;
    }

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    char ts[10];
    if (now > 100000) {
        strftime(ts, sizeof(ts), "%H:%M:%S", &ti);
    } else {
        snprintf(ts, sizeof(ts), "%08lu", millis() / 1000UL);
    }

    char line[256];
    int n = snprintf(line, sizeof(line), "[%s] %s | %s\n", ts, eventType, details);
    if (n > 0 && n < (int)sizeof(line)) {
        _logBufAppend(line, (size_t)n);
    }
}

void logCompactBufferedEventF(const __FlashStringHelper* eventType, const char* details) {
    if (!eventType || !details) return;
    char eventBuf[32];
    strncpy_P(eventBuf, (PGM_P)eventType, sizeof(eventBuf) - 1);
    eventBuf[sizeof(eventBuf) - 1] = '\0';
    logCompactBufferedEvent(eventBuf, details);
}

void logFlushIfDue() {
    // Use the same flush interval as the main data pipeline (from microSD page config).
    extern ConfigManager configManager;
#if ENABLE_SDCARD==1
    unsigned long flushInterval = intervalToMs(stringToInterval(configManager.getSdcardFlushInterval()));
#else
    unsigned long flushInterval = 30000UL; // fallback: 30s when SD is disabled
#endif
    if (millis() - _logLastFlushMs >= flushInterval) {
        flushLogBuffer(true);
    }
}

void logFlushNow() {
    portENTER_CRITICAL(&_logBufMux);
    bool hasData = (_logBuf != nullptr && _logBufLen > 0);
    portEXIT_CRITICAL(&_logBufMux);
    if (!hasData) return;
    flushLogBuffer(false);
    _logLastFlushMs = millis();
}