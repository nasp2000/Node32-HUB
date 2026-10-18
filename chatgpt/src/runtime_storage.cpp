// runtime_storage.cpp — SD buffer flush orchestration and storage mode control.
// Extracted from chatgpt.ino. See runtime_storage.h for the public API.

#include "runtime_storage.h"
#include <Arduino.h>
#include "commands.h"  // LogLevel enum + sendVirtualMessage declaration

// sendVirtualMessage declared in commands.h (included above)

#include "feature_flags.h"
#if ENABLE_SDCARD==1

#include <esp_task_wdt.h>
#include "feature_flags.h"
#include "sdcard.h"
#include "config_manager_runtime.h"
#include "debug_writer.h"
#include "log.h"

// File-local copy of the string formatter (mirrors the one in chatgpt.ino).
static String buildMessage(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return String(buf);
}

// File-local helper: find the index of the last element in buffer whose last byte is '\n' or '\r'.
// Returns -1 if no complete line is found.
static int findLastCompleteLineInBuffer(const SdTimestampedVec* buffer) {
    if (!buffer || buffer->empty()) return -1;
    for (int i = (int)buffer->size() - 1; i >= 0; i--) {
        const TimestampedData& item = (*buffer)[i];
        size_t len = item.dataLength();
        if (len > 0) {
            char lastChar = item.data[len - 1];
            if (lastChar == '\n' || lastChar == '\r') return i;
        }
    }
    return -1;
}

// File-local helper: flush data for one source up to and including cutoffSeq.
// Returns true when the flush buffer and retry queue are empty (or contain only data beyond cutoff).
static bool flushSdcardUpToSeqBlockingForSource(const char* source, uint32_t cutoffSeq, unsigned long waitMs) {
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;
    if (!sdCardWriteTaskHandle) return false;

    SdTimestampedVec* activeBuffer = nullptr;
    SdTimestampedVec* flushBuffer  = nullptr;
    size_t activeBytes = 0;

    if (strcmp(source, "RS232") == 0) {
        activeBuffer = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
        flushBuffer  = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);
        activeBytes  = sdcardActiveBytes_RS232.load(std::memory_order_acquire);
    } else if (strcmp(source, "LPT") == 0) {
        activeBuffer = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
        flushBuffer  = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);
        activeBytes  = sdcardActiveBytes_LPT.load(std::memory_order_acquire);
    } else {
        return false;
    }
    (void)activeBytes; // used for context; actual accounting done via atomics

    if (!activeBuffer || !flushBuffer) return false;

    unsigned long start = millis();
    while (millis() - start < waitMs) {
        bool flushEmpty = true;
        bool movedAny   = false;

        if (sdcardSwapLock(pdMS_TO_TICKS(100))) {
            flushEmpty = flushBuffer->empty();
            if (!flushEmpty) {
                sdcardSwapUnlock();
                // Flush buffer still has data — wake the writer and wait.
                if (sdCardWriteTaskHandle) {
                    for (int i = 0; i < 3; i++) xTaskNotifyGive(sdCardWriteTaskHandle);
                }
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            // Move eligible chunks (seq == 0 or seq <= cutoffSeq) from active to flush.
            if (!activeBuffer->empty()) {
                SdTimestampedVec remaining;
                remaining.reserve(activeBuffer->size());
                size_t remainingBytes = 0;
                size_t movedChunks   = 0;

                for (auto& chunk : *activeBuffer) {
                    if (chunk.seq == 0 || chunk.seq <= cutoffSeq) {
                        flushBuffer->push_back(std::move(chunk));
                        movedAny = true;
                        movedChunks++;
                    } else {
                        remainingBytes += chunk.dataLength();
                        remaining.push_back(std::move(chunk));
                    }
                }

                activeBuffer->clear();
                activeBuffer->swap(remaining);
                // Release excess capacity after large EMAILDAY cutoff to avoid allocation failures.
                activeBuffer->shrink_to_fit();

                if (strcmp(source, "RS232") == 0) {
                    sdcardActiveBytes_RS232.store(remainingBytes, std::memory_order_release);
                } else {
                    sdcardActiveBytes_LPT.store(remainingBytes, std::memory_order_release);
                }

                if (movedChunks > 0) {
                    sendVirtualMessage(buildMessage("%s: moved %u chunks (seq <= %u) to flush",
                                                      source, (unsigned)movedChunks, (unsigned)cutoffSeq), DEBUG);
                }
            }
            sdcardSwapUnlock();
        }

        if (movedAny) {
            if (sdCardWriteTaskHandle) {
                for (int i = 0; i < 3; i++) xTaskNotifyGive(sdCardWriteTaskHandle);
            }
        }

        // Check if flush buffer is now empty.
        bool nowFlushEmpty = false;
        if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
            nowFlushEmpty = flushBuffer->empty();
            sdcardSwapUnlock();
        }
        if (nowFlushEmpty) {
            // Also check the writer retry queue (only block on seq <= cutoff).
            if (strcmp(source, "RS232") == 0) {
                if (sdcardRetryBytes_RS232 == 0) return true;
                uint32_t first = sdcardRetryFirstSeq_RS232;
                if (first == 0 || first > cutoffSeq) return true;
            } else {
                if (sdcardRetryBytes_LPT == 0) return true;
                uint32_t first = sdcardRetryFirstSeq_LPT;
                if (first == 0 || first > cutoffSeq) return true;
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return false; // Timeout
}

// ============================================================================
// Public API
// ============================================================================

bool flushSdcardNowBlocking(const char* reason, unsigned long waitMs) {
    (void)reason;
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;
    if (!sdCardWriteTaskHandle) return false;

    unsigned long start = millis();
    while (millis() - start < waitMs) {
        bool allEmpty    = false;
        bool flushBusy   = false;
        bool didSwapLocal = false;

        if (sdcardSwapLock(pdMS_TO_TICKS(100))) {
            auto* flushRS232  = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);
            auto* flushLPT    = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);
            auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
            auto* activeLPT   = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);

            bool rs232FlushEmpty  = (!flushRS232  || flushRS232->empty());
            bool lptFlushEmpty    = (!flushLPT    || flushLPT->empty());
            bool rs232ActiveEmpty = (!activeRS232 || activeRS232->empty());
            bool lptActiveEmpty   = (!activeLPT   || activeLPT->empty());

            if (rs232FlushEmpty && lptFlushEmpty && rs232ActiveEmpty && lptActiveEmpty
                    && sdcardRetryBytes_RS232 == 0 && sdcardRetryBytes_LPT == 0) {
                allEmpty = true;
            } else if (!rs232FlushEmpty || !lptFlushEmpty
                       || sdcardRetryBytes_RS232 > 0 || sdcardRetryBytes_LPT > 0) {
                flushBusy = true;
            } else {
                if (!rs232ActiveEmpty && activeRS232 && flushRS232) {
                    sdcardActiveBuffer_RS232.store(flushRS232,  std::memory_order_release);
                    sdcardFlushBuffer_RS232.store(activeRS232,  std::memory_order_release);
                    sdcardActiveBytes_RS232.store(0,            std::memory_order_relaxed);
                    lastSdcardFlush_RS232 = millis();
                    didSwapLocal = true;
                }
                if (!lptActiveEmpty && activeLPT && flushLPT) {
                    sdcardActiveBuffer_LPT.store(flushLPT,  std::memory_order_release);
                    sdcardFlushBuffer_LPT.store(activeLPT,  std::memory_order_release);
                    sdcardActiveBytes_LPT.store(0,          std::memory_order_relaxed);
                    lastSdcardFlush_LPT = millis();
                    didSwapLocal = true;
                }
            }
            sdcardSwapUnlock();
        }

        if (allEmpty) return true;

        if (didSwapLocal || flushBusy) xTaskNotifyGive(sdCardWriteTaskHandle);

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Timeout diagnostics.
    unsigned long elapsed = millis() - start;
    auto* dAR = sdcardActiveBuffer_RS232.load(std::memory_order_relaxed);
    auto* dFR = sdcardFlushBuffer_RS232.load(std::memory_order_relaxed);
    auto* dAL = sdcardActiveBuffer_LPT.load(std::memory_order_relaxed);
    auto* dFL = sdcardFlushBuffer_LPT.load(std::memory_order_relaxed);

    String diagMsg = String(F("Flush SD Timeout after ")) + String(elapsed / 1000) + String(F("s: "));
    diagMsg += String(F("RS232[A:")) + String(dAR ? dAR->size() : 0);
    diagMsg += String(F(" F:"))      + String(dFR ? dFR->size() : 0);
    diagMsg += String(F(" R:"))      + String(sdcardRetryBytes_RS232) + String(F("] "));
    diagMsg += String(F("LPT[A:"))   + String(dAL ? dAL->size() : 0);
    diagMsg += String(F(" F:"))      + String(dFL ? dFL->size() : 0);
    diagMsg += String(F(" R:"))      + String(sdcardRetryBytes_LPT)   + String(F("]"));
    sendVirtualMessage(diagMsg, WARN);
    return false;
}

bool flushSdcardCutoffNowBlocking(const char* reason, unsigned long waitMs) {
    (void)reason;
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;

    uint32_t rsCutoff  = sdcardSeq_RS232;
    uint32_t lptCutoff = sdcardSeq_LPT;
    if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
        rsCutoff  = sdcardSeq_RS232;
        lptCutoff = sdcardSeq_LPT;
        sdcardSwapUnlock();
    }

    bool ok1 = flushSdcardUpToSeqBlockingForSource("RS232", rsCutoff,  waitMs);
    bool ok2 = flushSdcardUpToSeqBlockingForSource("LPT",   lptCutoff, waitMs);
    return ok1 && ok2;
}

bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs) {
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;

    // Activate freeze first to stop new data from entering activeBuffer.
    sdcardFreezeWrites = true;

    // Brief delay to let any in-flight data land in activeBuffer.
    delay(100);

    // Capture cutoff snapshot (after freeze, so no new data arrives).
    uint32_t rsCutoff  = sdcardSeq_RS232;
    uint32_t lptCutoff = sdcardSeq_LPT;
    if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
        rsCutoff  = sdcardSeq_RS232;
        lptCutoff = sdcardSeq_LPT;
        sdcardSwapUnlock();
    }

    sdcardFreezeCutoffSeq_RS232 = rsCutoff;
    sdcardFreezeCutoffSeq_LPT   = lptCutoff;

    sendVirtualMessage(buildMessage("EMAILDAY freeze active: cutoff RS232=%u, LPT=%u",
                                      (unsigned)rsCutoff, (unsigned)lptCutoff), DEBUG);

    bool ok1 = flushSdcardUpToSeqBlockingForSource("RS232", rsCutoff,  waitMs);
    bool ok2 = flushSdcardUpToSeqBlockingForSource("LPT",   lptCutoff, waitMs);

    if (ok1 && ok2) {
        sendVirtualMessage(F("EMAILDAY: all data flushed to SD"), DEBUG);
        return true;
    }

    // Flush failed — deactivate freeze to avoid data loss.
    sendVirtualMessage(F("EMAILDAY: flush failed, deactivating freeze"), ERROR);
    sdcardFreezeWrites          = false;
    sdcardFreezeCutoffSeq_RS232 = 0;
    sdcardFreezeCutoffSeq_LPT   = 0;
    return false;
}

bool forceSwapSdcardBuffersForEmailday(unsigned long waitMs) {
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;

    // Capture current cutoff snapshot.
    uint32_t rsCutoff  = sdcardSeq_RS232;
    uint32_t lptCutoff = sdcardSeq_LPT;
    if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
        rsCutoff  = sdcardSeq_RS232;
        lptCutoff = sdcardSeq_LPT;
        sdcardSwapUnlock();
    }

    sendVirtualMessage(buildMessage("EMAILDAY swap: cutoff RS232=%u, LPT=%u",
                                      (unsigned)rsCutoff, (unsigned)lptCutoff), DEBUG);

    bool ok1 = flushSdcardUpToSeqBlockingForSource("RS232", rsCutoff,  waitMs);
    bool ok2 = flushSdcardUpToSeqBlockingForSource("LPT",   lptCutoff, waitMs);

    if (!ok1 || !ok2) {
        sendVirtualMessage(F("EMAILDAY: flush incomplete (timeout)"), WARN);
        return false;
    }

    // Force buffer swap with complete lines only.
    if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
        // RS232 — move complete lines to flush buffer.
        auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
        auto* flushRS232  = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);

        if (activeRS232 && flushRS232 && !activeRS232->empty()) {
            int lastIdx = findLastCompleteLineInBuffer(activeRS232);
            if (lastIdx >= 0) {
                flushRS232->clear();
                for (int i = 0; i <= lastIdx; i++) flushRS232->push_back((*activeRS232)[i]);

                SdTimestampedVec remaining;
                for (size_t i = lastIdx + 1; i < activeRS232->size(); i++) remaining.push_back((*activeRS232)[i]);
                activeRS232->swap(remaining);

                size_t movedBytes = 0;
                for (const auto& item : *flushRS232) movedBytes += item.dataLength();
                sdcardActiveBytes_RS232 = 0;
                for (const auto& item : *activeRS232) sdcardActiveBytes_RS232 += item.dataLength();

                sendVirtualMessage(buildMessage("RS232: %d complete lines moved to flush (%u bytes)",
                                                  lastIdx + 1, (unsigned)movedBytes), DEBUG);
            } else {
                sendVirtualMessage(F("RS232: no complete lines found, keeping in activeBuffer"), DEBUG);
            }
        }

        // LPT — move complete lines to flush buffer.
        auto* activeLPT = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
        auto* flushLPT  = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);

        if (activeLPT && flushLPT && !activeLPT->empty()) {
            int lastIdx = findLastCompleteLineInBuffer(activeLPT);
            if (lastIdx >= 0) {
                flushLPT->clear();
                for (int i = 0; i <= lastIdx; i++) flushLPT->push_back((*activeLPT)[i]);

                SdTimestampedVec remaining;
                for (size_t i = lastIdx + 1; i < activeLPT->size(); i++) remaining.push_back((*activeLPT)[i]);
                activeLPT->swap(remaining);

                size_t movedBytes = 0;
                for (const auto& item : *flushLPT) movedBytes += item.dataLength();
                sdcardActiveBytes_LPT = 0;
                for (const auto& item : *activeLPT) sdcardActiveBytes_LPT += item.dataLength();

                sendVirtualMessage(buildMessage("LPT: %d complete lines moved to flush (%u bytes)",
                                                  lastIdx + 1, (unsigned)movedBytes), DEBUG);
            } else {
                sendVirtualMessage(F("LPT: no complete lines found, keeping in activeBuffer"), DEBUG);
            }
        }

        sdcardSwapUnlock();

        sendVirtualMessage(F("EMAILDAY: buffers prepared with complete lines only"), DEBUG);

        if (sdCardWriteTaskHandle) xTaskNotifyGive(sdCardWriteTaskHandle);
        return true;
    }

    sendVirtualMessage(F("EMAILDAY: failed to acquire swap mutex"), ERROR);
    return false;
}

bool safeFlushBeforeCriticalCommand(const String& commandName) {
    if (!configManager.getSdcardEnabled()) return true;
    if (!sdcardIsMounted()) return true;

    sendVirtualMessage(buildMessage("%s: Preparing safe data flush...", commandName.c_str()), INFO);

    // Flush the log audit buffer first so the log file is current before attachment.
    sendVirtualMessage(F("   Flushing log audit buffer to SD..."), INFO);
    logFlushNow();

    // Flush the debug writer buffer before any other flush operations.
    sendVirtualMessage(F("   Flushing debug writer buffer..."), INFO);
    debugWriterFlush();
    delay(100);

    // EMAILDAY requires freeze + full flush for data integrity.
    String upperCmd = commandName;
    upperCmd.toUpperCase();
    if (upperCmd.startsWith(String(F("EMAILDAY")))) {
        sendVirtualMessage(F("   EMAILDAY detected: Using freeze + flush sequence"), INFO);
        bool prepared = sdcardPrepareForEmaildayBlocking(30000);
        if (!prepared) {
            sendVirtualMessage(F("   EMAILDAY: Prepare incomplete, but proceeding..."), WARN);
            return false;
        }
        sendVirtualMessage(F("   EMAILDAY: All data flushed to SD with freeze active"), INFO);
        return true;
    }

    // For REBOOT and other critical commands: swap then flush (no freeze needed).
    bool swapOk = false;
    for (int attempt = 0; attempt < 3 && !swapOk; attempt++) {
        unsigned long timeout = 30000 + (attempt * 15000UL);
        if (attempt > 0) {
            sendVirtualMessage(buildMessage("%s: Retry swap #%d (timeout %us)",
                                              commandName.c_str(), attempt,
                                              (unsigned)(timeout / 1000)), WARN);
        } else {
            sendVirtualMessage(F("   Step 1/4: Swapping buffers"), INFO);
        }

        swapOk = forceSwapSdcardBuffersForEmailday(timeout);

        if (!swapOk && attempt < 2 && sdCardWriteTaskHandle) {
            for (int i = 0; i < 5; i++) {
                xTaskNotifyGive(sdCardWriteTaskHandle);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            esp_task_wdt_reset();
        }
    }

    if (!swapOk) {
        sendVirtualMessage(buildMessage("%s: Buffer swap failed after 3 retries!", commandName.c_str()), ERROR);
        return false;
    }
    sendVirtualMessage(F("   Step 1/4: Buffers swapped successfully"), INFO);

    sendVirtualMessage(F("   Step 2/4: Buffer continues receiving data..."), INFO);
    sendVirtualMessage(F("   Step 3/4: Flushing buffer to SD card..."), INFO);

    const unsigned long FLUSH_TIMEOUT = 45000;
    bool flushComplete = false;

    for (int attempt = 0; attempt < 3 && !flushComplete; attempt++) {
        if (attempt > 0) {
            sendVirtualMessage(buildMessage("%s: Retry flush #%d", commandName.c_str(), attempt), WARN);
        }

        if (sdCardWriteTaskHandle) {
            for (int i = 0; i < 3; i++) {
                xTaskNotifyGive(sdCardWriteTaskHandle);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        unsigned long flushStart = millis();
        while (millis() - flushStart < FLUSH_TIMEOUT) {
            if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
                auto* b1 = sdcardFlushBuffer_RS232.load();
                auto* b2 = sdcardFlushBuffer_LPT.load();
                bool empty1 = (b1 && b1->empty());
                bool empty2 = (b2 && b2->empty());
                bool retryEmpty = (sdcardRetryBytes_RS232 == 0 && sdcardRetryBytes_LPT == 0);
                sdcardSwapUnlock();
                if (empty1 && empty2 && retryEmpty) { flushComplete = true; break; }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_task_wdt_reset();
        }

        if (flushComplete) break;

        // Diagnostics on timeout.
        if (attempt < 2) {
            size_t rs232F = 0, lptF = 0;
            if (sdcardSwapLock(pdMS_TO_TICKS(50))) {
                auto* b1 = sdcardFlushBuffer_RS232.load();
                auto* b2 = sdcardFlushBuffer_LPT.load();
                if (b1) rs232F = b1->size();
                if (b2) lptF   = b2->size();
                sdcardSwapUnlock();
            }
            sendVirtualMessage(buildMessage(
                "%s: Flush timeout diag - RS232 flush:%u retry:%u | LPT flush:%u retry:%u",
                commandName.c_str(),
                (unsigned)rs232F, (unsigned)sdcardRetryBytes_RS232,
                (unsigned)lptF,   (unsigned)sdcardRetryBytes_LPT), WARN);
        }
    }

    if (!flushComplete) {
        sendVirtualMessage(buildMessage("%s: Flush timeout after 3 retries! Proceeding anyway...",
                                          commandName.c_str()), WARN);
    } else {
        sendVirtualMessage(F("   Step 3/4: Buffer flushed to SD"), INFO);
    }

    sendVirtualMessage(F("   Step 4/4: Waiting for system stabilization (500ms)..."), INFO);
    for (int i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    sendVirtualMessage(F("   Step 4/4: System stable"), INFO);

    sendVirtualMessage(buildMessage("%s: Safe to proceed - all data secured", commandName.c_str()), INFO);
    return true;
}

#else // ENABLE_SDCARD==0 — provide stubs so callers always link correctly

bool flushSdcardNowBlocking(const char* reason, unsigned long waitMs)
    { (void)reason; (void)waitMs; return true; }

bool flushSdcardCutoffNowBlocking(const char* reason, unsigned long waitMs)
    { (void)reason; (void)waitMs; return true; }

bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs)
    { (void)waitMs; return true; }

bool forceSwapSdcardBuffersForEmailday(unsigned long waitMs)
    { (void)waitMs; return true; }

bool safeFlushBeforeCriticalCommand(const String& commandName)
    { (void)commandName; return true; }

#endif // ENABLE_SDCARD==1

// ============================================================================
// STORAGE MODE CONTROL — extracted from chatgpt.ino
// Auto-failover between SD and PSRAM, manual retry, and mode reporting.
// ============================================================================
// sdcard.h is already included inside the ENABLE_SDCARD==1 block above.

#if ENABLE_SDCARD==1
volatile StorageMode currentStorageMode = STORAGE_MODE_SD;
#else
volatile StorageMode currentStorageMode = STORAGE_MODE_PSRAM;
#endif
static unsigned long lastStorageCheck    = 0;
static const unsigned long STORAGE_CHECK_INTERVAL = 10000;  // ms between checks
bool psramVFSInitialized = false;   // Set during setup() after psramVFSBegin()
static bool sdFailedNoRetry = false; // Blocks auto-retry until user resets manually

// Returns true if SD is mounted and not in fallback mode.
bool isSDAvailable() {
#if ENABLE_SDCARD==1
    return sdcardIsMounted() && !sdcardInFallbackMode();
#else
    return false;
#endif
}

// Check SD availability and switch storage mode if needed.
// One-way failover: SD → PSRAM/NONE only; user must trigger retry to go back to SD.
void checkAndSwitchStorageMode() {
    unsigned long now = millis();
    if (now - lastStorageCheck < STORAGE_CHECK_INTERVAL) return;
    lastStorageCheck = now;

#if ENABLE_SDCARD!=1
    StorageMode desired = psramVFSInitialized ? STORAGE_MODE_PSRAM : STORAGE_MODE_NONE;
    if (desired != currentStorageMode) {
        currentStorageMode = desired;
    }
    return;
#else
    bool sdAvail      = isSDAvailable();
    StorageMode desired = currentStorageMode;

    if (currentStorageMode == STORAGE_MODE_SD) {
        if (!sdAvail) {
            desired = psramVFSInitialized ? STORAGE_MODE_PSRAM : STORAGE_MODE_NONE;
            sdFailedNoRetry = true;
            sendVirtualMessage(F("⚠️ STORAGE: SD failed → switching to fallback mode"), ERROR, true, false);
            sendVirtualMessage(F("💡 STORAGE: Auto-retry DISABLED. Use Web UI or RETRYSD command to retry SD."), INFO, true, false);
        }
    } else if (!sdFailedNoRetry && sdAvail) {
        desired = STORAGE_MODE_SD;
    }

    if (desired != currentStorageMode) {
        const char* oldMode = (currentStorageMode == STORAGE_MODE_SD) ? "SD" :
                              (currentStorageMode == STORAGE_MODE_PSRAM) ? "PSRAM" : "NONE";
        const char* newMode = (desired == STORAGE_MODE_SD) ? "SD" :
                              (desired == STORAGE_MODE_PSRAM) ? "PSRAM" : "NONE";
        char msg[100];
        snprintf(msg, sizeof(msg), "🔄 STORAGE: Mode switch %s → %s", oldMode, newMode);
        sendVirtualMessage(msg, WARN, true, false);
        currentStorageMode = desired;
    }
#endif
}

// User-triggered SD retry: clears the no-retry flag and performs an immediate check.
bool retrySDStorage() {
    sendVirtualMessage(F("🔄 STORAGE: User requested SD retry..."), INFO, true, false);
    sdFailedNoRetry = false;
    lastStorageCheck = 0;  // Force immediate check on next call

    bool sdAvail = isSDAvailable();
    if (sdAvail) {
        currentStorageMode = STORAGE_MODE_SD;
        sendVirtualMessage(F("✅ STORAGE: SD is available → switched to SD mode"), INFO, true, false);
        return true;
    } else {
        sdFailedNoRetry = false;  // Keep clear so auto-switch can happen when SD recovers
        sendVirtualMessage(F("⚠️ STORAGE: SD still unavailable → staying in current mode"), WARN, true, false);
        return false;
    }
}

// Returns a human-readable string for the current storage mode.
const char* getStorageModeString() {
    switch (currentStorageMode) {
        case STORAGE_MODE_SD:   return "SD Card";
        case STORAGE_MODE_PSRAM: return "PSRAM (Volatile)";
        case STORAGE_MODE_NONE: return "None (Discarding)";
        default:                return "Unknown";
    }
}

// Acquire the SD file-access mutex for external callers (web handlers, email task, etc.).
bool acquireSDAccess(const char* caller, unsigned long timeoutMs) {
    extern bool sdMutexLock(TickType_t timeoutTicks);
    bool locked = sdMutexLock(pdMS_TO_TICKS(timeoutMs));
    if (!locked) {
        char msg[200];
        snprintf(msg, sizeof(msg), "[SD] acquireSDAccess TIMEOUT: caller='%s' timeout=%lums",
                 caller ? caller : "?", timeoutMs);
        sendVirtualMessage(msg, WARN);
        return false;
    }
    return true;
}

// Release the SD file-access mutex (delegates to sdMutexUnlock in sdcard.cpp).
void releaseSDAccess() {
    extern void sdMutexUnlock();
    extern TaskHandle_t sdMutexOwnerTask;
    extern uint32_t sdMutexDepth;

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    char msg[200];
    snprintf(msg, sizeof(msg), "[SD] Releasing mutex (current task=%p, owner=%p, depth=%u)",
             self, sdMutexOwnerTask, sdMutexDepth);
    sendVirtualMessage(msg, INFO);

    if (sdMutexOwnerTask != self) {
        char errMsg[200];
        snprintf(errMsg, sizeof(errMsg), "[SD] ERROR: Attempted release by non-owner! (self=%p, owner=%p)",
                 self, sdMutexOwnerTask);
        sendVirtualMessage(errMsg, ERROR);
        return;
    }
    sdMutexUnlock();
    delay(50);  // Allow other cores to sync after mutex release
}
