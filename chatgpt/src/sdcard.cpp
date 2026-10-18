// =============================================================
// sdcard.cpp -- SD card implementation (SDMMC mode, ESP32-S3)
// =============================================================
// Split into focused impl files (impl-include pattern, single TU):
//   sd_locking_impl.h  -- SD mutex lock/unlock helpers
//   sd_failover_impl.h -- automatic failover and recovery API
//   sd_status_impl.h   -- card init, detection, loop, mount, state getters
//   sd_file_ops_impl.h -- folder/file ops, write, speed tests, daily file
//
// ESP32-S3 SDMMC MODE
// -------------------
// Uses native SDMMC interface (faster and more reliable than SPI).
// 1-bit mode: up to 25 MHz (vs ~10 MHz practical on SPI).
// No conflicts with Ethernet W5500 (dedicated SDMMC interface).
// Standard pinout for ESP32-S3 modules with camera and SD slot.
//
// 🔄 WRITE/READ SYNC FLOW:
// ------------------------------------------
// 1️⃣ WRITE: sdcardWriteData() opens file in O_APPEND, writes, SYNCs and closes
//    - file.sync() guarantees RAM buffer flush to physical SD
//    - Each write is a complete transaction (open→write→sync→close)
//    - ✅ No files kept open between writes
//
// 2️⃣ READ: emailday/sendEmailWithAttachments opens in FILE_READ
//    - 100ms delay before first read (guarantees SD controller sync)
//    - Verifies real size vs listing (detects cache issues)
//    - ✅ Direct streaming read (does not load whole file into RAM)
//
// 3️⃣ CONCURRENCY: SDMMC is full-duplex and uses a dedicated interface
//    - No conflicts with SPI/Ethernet
//    - Each operation opens/closes the file → no pending locks
//    - ✅ Safe to read while writes may happen at other times
//
// ⚠️ IMPORTANT:
//    - Always use sync() before close() to guarantee persistence
//    - Add minimum delay (100ms) between last write and first read
//    - SD controller may have internal block cache (not controllable by software)
//
// =============================================================
#include "sdcard.h"
#include "commands.h"
#include "src/structures.h" // 📦 StorageMode definition
#include "src/file_header_builder.h"
#include "src/psram_vfs.h"  // 🧠 PSRAM VFS access

#include "feature_flags.h"
#if ENABLE_SDCARD==1

#include <Arduino.h>
// SD_MMC.h is provided via sdcard.h -> sd_lib.h (included above at line 40).
// driver/gpio.h for gpio_set_pull_mode() — SDMMC mode only (pull-ups on CMD/D0 lines).
#if SDCARD_USE_SPI_MODE != 1
#include <driver/gpio.h>
#endif
#include <esp_heap_caps.h>
#include "ConfigManager.h" // 📌 Access to the chosen file format

// FreeRTOS (for background tasks and cooperative delays)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 🔗 External ConfigManager reference (declared in chatgpt.ino)
extern ConfigManager configManager;

// 🔗 Current global storage mode (SD vs PSRAM) - defined in chatgpt.ino
extern volatile StorageMode currentStorageMode;

// Firmware version and build info (declared in chatgpt.ino)
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;

// 📧 External callback to notify SD writes (implemented in chatgpt.ino)
extern void onSdcardDataWritten(const String &filepath, size_t startOffset, size_t endOffset, const String &source);

// 🔒 Global semaphore to protect SD Card access (only 1 task at a time)
// 🚨 SD_MMC has no internal mutex - application controls concurrency
SemaphoreHandle_t sdMutex = NULL;

// 🔒 CRITICAL: Mutex ownership tracking to prevent xTaskPriorityDisinherit crashes
// Wrapped in struct with canary values to detect memory corruption
struct {
    uint32_t canary_before = 0xDEADBEEF;
    TaskHandle_t ownerTask = NULL;  // Task holding the mutex
    uint32_t depth = 0;             // Recursion depth
    uint32_t canary_after = 0xCAFEBABE;
} sdMutexTracking;

TaskHandle_t sdMutexOwnerTask = NULL; // Legacy compatibility pointer
uint32_t sdMutexDepth = 0;             // Legacy compatibility variable
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED; // 🔐 Spinlock to protect tracking variables

static int _sdStatus = -1; // ✅ 0=OK, -1=error/not mounted
static bool _sdMounted = false; // ✅ Mount state
static uint64_t _sdTotal = 0;   // 📦 Total bytes
static uint64_t _sdUsed = 0;    // 📊 Used bytes
static unsigned long _sdWriteCount = 0; // 📝 Operation counter (session)
static unsigned long _sdWriteCountTotal = 0; // 📊 Operation counter (persistent total)

// 🔄 SD FAILOVER SYSTEM - Automatic fallback when SD fails
static bool _sdFallbackMode = false;           // True when operating without SD (fallback mode)
static unsigned long _sdConsecutiveFailures = 0; // Counter of consecutive write failures
static unsigned long _sdLastRetryAttempt = 0;    // Last time we attempted SD recovery
static const unsigned long SD_FAILOVER_THRESHOLD = 3;     // Failures before entering fallback
static const unsigned long SD_RETRY_INTERVAL_MS = 60000;  // Retry every 60s when in fallback
static uint64_t _sdBytesWritten = 0; // 📝 Bytes written (session)
static uint64_t _sdBytesWrittenTotal = 0; // 📊 Bytes written (persistent total)
static unsigned long _lastNvsSaveTime = 0; // 🕒 Last NVS persistence (millis)
const unsigned long NVS_SAVE_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL; // 📅 6 hours (4x/day, wear/persistence balance)

// Generic per-source file rotation table (max 8 unique sources per day).
// Entries are created on first write per source and cleared on day change.
struct SourceRotationEntry {
    char name[20]; // source name key (case-insensitive match)
    int  index;    // 0=base file, 1=_001, 2=_002, ...
};
static SourceRotationEntry _sourceRotTable[8] = {};
static uint8_t _sourceRotCount = 0;

// 🔌 Auto-mount state for hot-plug detection (integrated in sdcardLoop)
static bool _autoMountEnabled = false;             // Auto-mount feature enabled/disabled

// SD card implementation is split into focused impl files (impl-include pattern).
// All files are compiled as part of this single TU; they share all static variables above.
#include "sd_locking_impl.h"    // sdMutexLock, sdMutexUnlock
#include "sd_failover_impl.h"   // sdcardInFallbackMode, sdcardRegisterWriteFailure/Success, sdcardCheckRecovery
#include "sd_status_impl.h"     // sdcardDetect, sdcardBegin, sdcardLoop, mount task, sdcardUnmount, card state getters
#include "sd_file_ops_impl.h"   // sdcardWriteData, sdcardGetTodayFolder, speed tests, daily file, sdcardDeleteFolderRecursive
#else
// SDCARD disabled: define stub symbols that other TUs (log.cpp, command_executor.cpp,
// google_email.cpp, runtime_storage.cpp) extern-declare unconditionally.
SemaphoreHandle_t sdMutex          = NULL;
TaskHandle_t      sdMutexOwnerTask = NULL;
uint32_t          sdMutexDepth     = 0;
bool sdMutexLock(TickType_t) { return true; }
void sdMutexUnlock()         {}
bool sdMutexLockFast(TickType_t) { return true; }
void sdMutexUnlockFast()         {}
// Freeze-write stubs: needed by runtime_email.cpp when ENABLE_SDCARD=0 but ENABLE_SMTP=1.
// The SD double-buffer pipeline does not exist without SD, so these are always false/zero.
volatile bool     sdcardFreezeWrites            = false;
volatile uint32_t sdcardFreezeCutoffSeq_RS232   = 0;
volatile uint32_t sdcardFreezeCutoffSeq_LPT     = 0;
#endif
