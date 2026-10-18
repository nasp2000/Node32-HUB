// debug_writer.h - Persistent Debug Logger
// Captures ALL system messages to SD card: /DEBUG/DD-MM-YYYY.txt
// Separate from crash logs (/logs/) - includes INFO/WARN/ERROR/DEBUG
// 
// FEATURES:
// - Buffer 64KB (auto-flush at 48KB threshold via Priority Manager)
// - Configurable flush interval (default 5 min)
// - Priority Manager integration (P2 - SD writes)
// - Thread-safe (mutex protected)
// - Zero overhead when disabled (single if check)
// - PSRAM VFS fallback when SD unavailable
//
// USAGE:
//   debugWriterInit();                    // In setup() after SD init
//   debugWriterTask();                    // In loop() for periodic flush
//   DEBUG_WRITER ON|OFF|STATUS            // System commands
//   DEBUG_WRITER_FLUSH [INTERVAL]         // Set flush interval (seconds)
//   DEBUG_WRITER_NOW                      // Force immediate flush
//
// LAST CHANGE: 2025-02-18 00:05 WET

#ifndef DEBUG_WRITER_H
#define DEBUG_WRITER_H

#include <Arduino.h>
#include "sd_lib.h" // SD backend shim (SDMMC or SPI)
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Preferences.h> // 💾 Persistence
#include "structures.h"
#include "sdcard.h"
#include "sd_layout.h" // standard layout + clockReady() gate
#include "priority_manager.h"
#include "psram_vfs.h" // PSRAM Virtual Filesystem fallback
#if ENABLE_FILESYSTEM==1
#include "littlefs_manager.h" // littlefsBegin()/littlefsFreeBytes() — no-SD debug log
#include <LittleFS.h>
#endif
#include "log.h"       // LogLevel enum (self-contained for direct includers)

// ============================================================================
// CONFIGURATION
// ============================================================================

#define DEBUG_WRITER_BUFFER_SIZE 65536      // 64KB buffer (reduce SD writes)
#define DEBUG_WRITER_FLUSH_THRESHOLD 49152  // 48KB threshold for auto-flush
#define DEBUG_WRITER_FLUSH_INTERVAL 300000  // 5 min (ms) - configurable runtime
#define DEBUG_WRITER_ENABLED_DEFAULT false  // Disabled by default (enable via command)

// ============================================================================
// STATE
// ============================================================================

static bool debugWriterEnabled = DEBUG_WRITER_ENABLED_DEFAULT;
static std::vector<char> debugWriterBuffer;  // Intermediate buffer (PSRAM if available)
static unsigned long debugWriterLastFlush = 0;
static unsigned long debugWriterFlushInterval = DEBUG_WRITER_FLUSH_INTERVAL;
static SemaphoreHandle_t debugWriterMutex = NULL;
static size_t debugWriterTotalBytes = 0;     // Diagnostic counter
static size_t debugWriterFlushCount = 0;     // Flush operations counter
static size_t debugWriterDroppedMessages = 0; // Messages dropped (mutex timeout)
// Rotation state for debug files (reset on day change).
static int  debugWriterRotIndex   = 0;
static char debugWriterRotLastDay[16] = "";

// ============================================================================
// CORE FUNCTIONS
// ============================================================================

// Initialize debug writer (call in setup() after SD init)
static inline void debugWriterInit() {
    debugWriterMutex = xSemaphoreCreateMutex();
    if (!debugWriterMutex) {
        Serial.println("❌ DEBUG_WRITER: Failed to create mutex");
        debugWriterEnabled = false;
        return;
    }
    
    // Reserve buffer capacity (use PSRAM if available)
    debugWriterBuffer.reserve(DEBUG_WRITER_BUFFER_SIZE);

    // 💾 Load state from NVS
    Preferences p;
    p.begin("debug_writer", true); // Read-only
    debugWriterEnabled = p.getBool("enabled", DEBUG_WRITER_ENABLED_DEFAULT);
    debugWriterFlushInterval = p.getULong("flush_int", DEBUG_WRITER_FLUSH_INTERVAL);
    p.end();
    
    // /DEBUG directory is created lazily on first flush — no premature directory creation.
    if (debugWriterEnabled) {
        if (sdcardIsMounted()) {
            Serial.println("✅ DEBUG_WRITER: Initialized (/DEBUG/DD-MM-YYYY.txt) on SD");
        } else if (psramVFSIsEnabled()) {
            Serial.println("✅ DEBUG_WRITER: Initialized (/DEBUG/DD-MM-YYYY.txt) on PSRAM VFS");
        } else {
            Serial.println("⚠️ DEBUG_WRITER: Enabled but no storage available (SD or PSRAM)");
        }
    } else {
        Serial.println("ℹ️  DEBUG_WRITER: Disabled (use 'DEBUG WRITER ON' to activate and persist)");
    }
}

// Returns the active debug file path for today, including rotation suffix when needed.
// Resets the rotation index if the day has changed.
// Pre-NTP the day is unknown, so a "PENDING" token is used instead of a
// fabricated/1970 date; debugWriterMigratePending() folds it into today's
// real file after NTP sync.
static inline String getDebugFilePath() {
    time_t now = time(nullptr);
    char dayStr[16];
    if (sd_layout::clockReady(now)) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &timeinfo);
    } else {
        strncpy(dayStr, "PENDING", sizeof(dayStr));
    }

    // Reset rotation index on day change.
    if (strncmp(debugWriterRotLastDay, dayStr, sizeof(debugWriterRotLastDay) - 1) != 0) {
        debugWriterRotIndex = 0;
        strncpy(debugWriterRotLastDay, dayStr, sizeof(debugWriterRotLastDay) - 1);
        debugWriterRotLastDay[sizeof(debugWriterRotLastDay) - 1] = '\0';
    }

    char filename[40];
    if (debugWriterRotIndex > 0) {
        snprintf(filename, sizeof(filename), "/DEBUG/%s_%03d.txt", dayStr, debugWriterRotIndex);
    } else {
        snprintf(filename, sizeof(filename), "/DEBUG/%s.txt", dayStr);
    }
    return String(filename);
}

// One-time: after NTP sync, fold any pre-sync /DEBUG/PENDING*.txt files into
// today's real day file (rename if free, append+delete if the target already
// exists). One file per call — re-runs on the next flush until PENDING is gone.
static inline void debugWriterMigratePending() {
    if (!sdcardIsMounted()) return;
    if (!SD_MMC.exists("/DEBUG")) return;
    time_t now = time(nullptr);
    if (!sd_layout::clockReady(now)) return;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char dayStr[16];
    strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &timeinfo);

    File root = SD_MMC.open("/DEBUG");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File entry = root.openNextFile();
    while (entry) {
        String name = String(entry.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (name.startsWith("PENDING")) {
            String src = String("/DEBUG/") + name;
            String suffix = name.substring(7); // "", "_001.txt", ...
            String target = String("/DEBUG/") + dayStr + suffix;
            bool ok = false;
            entry.close();
            if (SD_MMC.exists(target.c_str())) {
                File inFile = SD_MMC.open(src.c_str(), FILE_READ);
                File outFile = SD_MMC.open(target.c_str(), FILE_APPEND);
                if (inFile && outFile) {
                    outFile.println(F("\n[DEBUG] Content below imported from PENDING (pre-NTP)"));
                    size_t migStart = outFile.size();
                    while (inFile.available()) outFile.write(inFile.read());
                    size_t migEnd = outFile.size();
                    ok = true;
                    storNoteSdWrite(migEnd > migStart ? migEnd - migStart : 0);
                }
                if (inFile) inFile.close();
                if (outFile) outFile.close();
                if (ok) SD_MMC.remove(src.c_str());
            } else {
                ok = SD_MMC.rename(src.c_str(), target.c_str());
            }
            if (ok) sendVirtualMessage(String(F("✅ DEBUG_WRITER: moved pre-NTP ")) + src + F(" → ") + target, INFO);
            return; // one file per call
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

// Flush buffer to SD card or PSRAM VFS (PRIORITY MANAGER INTEGRATION - P2)
static inline bool debugWriterFlush() {
    if (!debugWriterEnabled) return false;
    if (debugWriterBuffer.empty()) return true;
    
    // Determine storage type (SD > PSRAM)
    bool usingSD = sdcardIsMounted();
    bool usingPSRAM = !usingSD && psramVFSIsEnabled();
    
    if (!usingSD && !usingPSRAM) {
        // No storage available
        return false;
    }
    
    // Check Priority Manager before starting SD write (only for SD, not PSRAM)
    if (usingSD && !priorityManager().canStartSDWrite()) {
        // SD busy with higher priority operation - skip this flush
        return false;
    }
    
    if (!debugWriterMutex || xSemaphoreTake(debugWriterMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;  // Mutex timeout - skip flush
    }
    
    bool success = false;
    String filepath = getDebugFilePath();

    // Rotate to a new file if the current one has reached the size limit.
    if (usingSD && SD_MMC.exists(filepath.c_str())) {
        File sizeCheck = SD_MMC.open(filepath.c_str(), FILE_READ);
        if (sizeCheck) {
            size_t sz = sizeCheck.size();
            sizeCheck.close();
            if (sz >= STORAGE_ROTATED_FILE_MAX_SIZE_BYTES) {
                debugWriterRotIndex++;
                filepath = getDebugFilePath();
            }
        }
    }
    
    if (usingPSRAM) {
#if ENABLE_FILESYSTEM==1
        // ========== LITTLEFS WRITE (FS packs, no cam) ==========
        // Persist the debug log to LittleFS at the same /DEBUG/DD-MM-YYYY.txt
        // path used on SD. Same access pattern as mirrorLogToLittleFS (log.cpp);
        // VFS stays reserved for gcode. Buffer cleared only on full write.
        size_t totalToWrite = debugWriterBuffer.size();
        success = false;
        if (!sdcardShouldUseSD() && littlefsBegin() && littlefsMounted() &&
            littlefsFreeBytes() >= (1024ULL * 1024ULL)) {
            int slash = filepath.lastIndexOf('/');
            if (slash > 0) {
                String dir = filepath.substring(0, slash);
                int second = dir.indexOf('/', 1);
                if (second > 0 && !LittleFS.exists(dir.substring(0, second).c_str()))
                    LittleFS.mkdir(dir.substring(0, second).c_str());
                if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
            }
            File f = LittleFS.open(filepath.c_str(), FILE_APPEND);
            if (f) {
                size_t w = f.write((const uint8_t*)debugWriterBuffer.data(), totalToWrite);
                f.close();
                storNoteLfsWrite(w);
                success = (w == totalToWrite);
            }
        }
        if (success) {
            debugWriterTotalBytes += totalToWrite;
            debugWriterFlushCount++;
            debugWriterBuffer.clear();
        }
#else
        // ========== PSRAM VFS WRITE (packs without LittleFS: cam) ==========
        // Convert /DEBUG/DD-MM-YYYY.txt to DEBUG/DD-MM-YYYY.txt (remove leading slash)
        String psramSource = filepath.substring(1);
        
        // Write buffer to PSRAM VFS
        size_t totalToWrite = debugWriterBuffer.size();
        success = psramVFSWrite(psramSource, debugWriterBuffer.data(), totalToWrite);
        
        if (success) {
            debugWriterTotalBytes += totalToWrite;
            debugWriterFlushCount++;
            debugWriterBuffer.clear();
        }
#endif
    } else {
#if ENABLE_SDCARD==1
        // ========== SD CARD WRITE ==========
        // Acquire Priority Manager SD lock
        priorityManager().startSDWrite();
        
        if (sdMutexLock(pdMS_TO_TICKS(2000))) {
            // Create /DEBUG directory on first write (lazy creation — no empty dirs at boot).
            if (!SD_MMC.exists("/DEBUG")) SD_MMC.mkdir("/DEBUG");
            debugWriterMigratePending(); // fold pre-NTP PENDING files into today's file
            File f = SD_MMC.open(filepath.c_str(), FILE_APPEND);
            if (f) {
                size_t totalToWrite = debugWriterBuffer.size();
                size_t written = 0;
                
                // Write in chunks with yields (cooperative multitasking)
                const size_t CHUNK_SIZE = 4096; // 4KB chunks
                while (written < totalToWrite) {
                    size_t remaining = totalToWrite - written;
                    size_t toWrite = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
                    
                    size_t w = f.write((uint8_t*)&debugWriterBuffer[written], toWrite);
                    written += w;
                    
                    // Yield to Priority Manager between chunks
                    if (remaining > CHUNK_SIZE) {
                        priorityManager().yieldDuringSDWrite();
                    }
                    
                    if (w < toWrite) break; // Write error
                }
                
                f.close();
                storNoteSdWrite(written);
                
                if (written == totalToWrite) {
                    debugWriterTotalBytes += written;
                    debugWriterFlushCount++;
                    debugWriterBuffer.clear();
                    success = true;
                }
            }
            sdMutexUnlock();
        }
        
        // Release Priority Manager SD lock
        priorityManager().endSDWrite();
#endif // ENABLE_SDCARD
    }
    
    xSemaphoreGive(debugWriterMutex);
    debugWriterLastFlush = millis();
    return success;
}

// Log message to debug file (called from pushLogToPsram hook)
static inline void debugWriterLog(const SerialMessage& msg) {
    if (!debugWriterEnabled || !sdcardIsMounted()) return;
    
    // Format: [YYYY-MM-DD HH:MM:SS] [TYPE] message\n
    char line[256];
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    char timestamp[24];
    if (now > 100000) {
        strftime(timestamp, sizeof(timestamp), "%d-%m-%Y %H:%M:%S", &timeinfo);
    } else {
        // NTP not synced - use millis
        snprintf(timestamp, sizeof(timestamp), "%010lu", msg.timestamp);
    }
    
    int len = snprintf(line, sizeof(line), "[%s] [%s] %s\n", timestamp, msg.type, msg.text);
    if (len <= 0 || len >= (int)sizeof(line)) return;
    
    // Try to acquire mutex (non-blocking - don't slow down logging)
    if (!debugWriterMutex || xSemaphoreTake(debugWriterMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        debugWriterDroppedMessages++;
        return;  // Skip if mutex busy (don't block critical logging path)
    }
    
    // Append to buffer
    for (int i = 0; i < len; i++) {
        debugWriterBuffer.push_back(line[i]);
    }
    
    // Auto-flush if threshold reached (48KB/64KB = 75%)
    bool shouldFlush = (debugWriterBuffer.size() >= DEBUG_WRITER_FLUSH_THRESHOLD);
    
    xSemaphoreGive(debugWriterMutex);
    
    if (shouldFlush) {
        debugWriterFlush();  // Respects Priority Manager (P2 - SD write)
    }
}

// Periodic flush (call in loop())
static inline void debugWriterTask() {
    if (!debugWriterEnabled) return;
    
    unsigned long now = millis();
    if (now - debugWriterLastFlush >= debugWriterFlushInterval) {
        debugWriterFlush();
    }
}

// Enable/disable debug writer
static inline void debugWriterSetEnabled(bool enabled) {
    bool wasEnabled = debugWriterEnabled;
    debugWriterEnabled = enabled;
    
    // 💾 Save state to NVS
    Preferences p;
    p.begin("debug_writer", false); // Read/Write
    p.putBool("enabled", enabled);
    p.end();
    
    if (enabled && !wasEnabled) {
        // /DEBUG directory will be created lazily on first flush — not pre-created here.
        Serial.println("🟢 DEBUG_WRITER: ENABLED (persisted to NVS, survives reboot)");
    } else if (!enabled && wasEnabled) {
        // Disabling: final flush
        debugWriterFlush();
        Serial.println("🔴 DEBUG_WRITER: DISABLED (persisted to NVS, survives reboot)");
    }
}

// Set flush interval (seconds)
static inline void debugWriterSetFlushInterval(unsigned long seconds) {
    if (seconds < 10) seconds = 10;        // Min 10s
    if (seconds > 3600) seconds = 3600;    // Max 1 hour
    
    debugWriterFlushInterval = seconds * 1000;  // Convert to ms

    // 💾 Save interval to NVS
    Preferences p;
    p.begin("debug_writer", false); // Read/Write
    p.putULong("flush_int", debugWriterFlushInterval);
    p.end();
    
    char buf[64];
    snprintf(buf, sizeof(buf), "⏱️  DEBUG_WRITER: Flush interval set to %lu seconds", seconds);
    Serial.println(buf);
}

// Get status
static inline bool debugWriterIsEnabled() {
    return debugWriterEnabled;
}

static inline size_t debugWriterGetTotalBytes() {
    return debugWriterTotalBytes;
}

static inline size_t debugWriterGetFlushCount() {
    return debugWriterFlushCount;
}

static inline size_t debugWriterGetDroppedMessages() {
    return debugWriterDroppedMessages;
}

static inline size_t debugWriterGetBufferSize() {
    return debugWriterBuffer.size();
}

static inline unsigned long debugWriterGetFlushIntervalSeconds() {
    return debugWriterFlushInterval / 1000;
}

// Get status string
static inline String debugWriterGetStatus() {
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "DEBUG_WRITER: %s | Buff: %u/%u bytes | Flush: %lu/%lus | Total: %.2f KB | Drops: %u",
             debugWriterEnabled ? "ON" : "OFF",
             (unsigned)debugWriterBuffer.size(),
             (unsigned)DEBUG_WRITER_BUFFER_SIZE,
             debugWriterFlushCount,
             (unsigned long)(debugWriterFlushInterval / 1000),
             (float)debugWriterTotalBytes / 1024.0f,
             (unsigned)debugWriterDroppedMessages);
    return String(buf);
}

#endif // DEBUG_WRITER_H
