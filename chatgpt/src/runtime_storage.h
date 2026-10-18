#pragma once
// runtime_storage.h — SD buffer flush orchestration and storage mode control.
// Provides blocking flush helpers and storage mode failover functions.
// State variables defined in runtime_storage.cpp.

#include "feature_flags.h"
#include <Arduino.h>
#include "structures.h"  // for StorageMode enum

// ---- Globals exported from chatgpt.ino (only available when SDCARD is enabled) ----
#if ENABLE_SDCARD==1

#include <atomic>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "structures.h"
#include "psram_helpers.h"

// Shorthand for the PSRAM-backed timestamped data vector type.
using SdTimestampedVec = std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>;

// Atomic active/flush buffer pointers (double-buffering system).
extern std::atomic<SdTimestampedVec*> sdcardActiveBuffer_RS232;
extern std::atomic<SdTimestampedVec*> sdcardFlushBuffer_RS232;
extern std::atomic<SdTimestampedVec*> sdcardActiveBuffer_LPT;
extern std::atomic<SdTimestampedVec*> sdcardFlushBuffer_LPT;

// Byte counters for active buffers (lock-free size tracking).
extern std::atomic<size_t> sdcardActiveBytes_RS232;
extern std::atomic<size_t> sdcardActiveBytes_LPT;

// Per-source sequence counters (monotonically increasing per received chunk).
extern uint32_t sdcardSeq_RS232;
extern uint32_t sdcardSeq_LPT;

// Retry buffer accounting (set by SD writer task on write failure).
extern volatile size_t   sdcardRetryBytes_RS232;
extern volatile size_t   sdcardRetryBytes_LPT;
extern volatile uint32_t sdcardRetryFirstSeq_RS232;
extern volatile uint32_t sdcardRetryFirstSeq_LPT;

// EMAILDAY freeze state: when true, new data is redirected to freeze buffer.
extern volatile bool     sdcardFreezeWrites;
extern volatile uint32_t sdcardFreezeCutoffSeq_RS232;
extern volatile uint32_t sdcardFreezeCutoffSeq_LPT;

// Timestamps of last successful flush per source.
extern unsigned long lastSdcardFlush_RS232;
extern unsigned long lastSdcardFlush_LPT;

// SD writer task handle (notified to trigger writes).
extern TaskHandle_t sdCardWriteTaskHandle;

// Swap mutex helpers (defined in chatgpt.ino, promoted from static inline).
bool sdcardSwapLock(TickType_t timeoutTicks);
void sdcardSwapUnlock();

#endif // ENABLE_SDCARD==1

// ---- Public flush API (always declared; stubs return true when SDCARD disabled) ----

// Force an immediate full flush of all SD buffers; waits up to waitMs for completion.
bool flushSdcardNowBlocking(const char* reason, unsigned long waitMs);

// Flush only data up to the current sequence snapshot; safe for partial flush at command time.
bool flushSdcardCutoffNowBlocking(const char* reason, unsigned long waitMs);

// Prepare for EMAILDAY: freeze new writes, capture cutoff, flush all data up to cutoff.
bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs);

// Force buffer swap for EMAILDAY: move complete lines from active to flush buffer.
bool forceSwapSdcardBuffersForEmailday(unsigned long waitMs);

// Safe flush before critical commands (EMAILDAY, REBOOT): full integrity sequence.
bool safeFlushBeforeCriticalCommand(const String& commandName);

// ---- Storage mode control (auto-failover SD <-> PSRAM) ----
// State owned by runtime_storage.cpp.

extern volatile StorageMode currentStorageMode;
extern bool psramVFSInitialized;  // Set during setup() after psramVFSBegin().

// Acquire / release the SD file-access mutex (delegates to sdMutexLock/Unlock in sdcard.cpp).
bool acquireSDAccess(const char* caller, unsigned long timeoutMs = 5000);
void releaseSDAccess();

// True if SD is mounted and not in fallback mode.
bool isSDAvailable();

// Check SD availability and switch storage mode if needed (call periodically from loop).
void checkAndSwitchStorageMode();

// User-triggered SD retry: clears the no-retry flag and performs an immediate availability check.
bool retrySDStorage();

// Returns the current storage mode as a human-readable string.
const char* getStorageModeString();
