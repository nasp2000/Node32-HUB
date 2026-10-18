// =============================================================
// sdcard.h — Configuration for SD Card module (SDMMC mode)
// =============================================================
// Purpose: expose pins and API to initialize SD Card via SDMMC (ESP32-S3)
// Optional: enable by defining ENABLE_SDCARD=1 in build flags
// Typical connections (ESP32-S3 with camera and built-in SD slot):
//   Native SDMMC interface (1-bit mode for maximum compatibility):
//   • CLK  -> GPIO39 (SDMMC clock)
//   • CMD  -> GPIO38 (SDMMC command)
//   • D0   -> GPIO40 (SDMMC data 0 - required)
//   • D1   -> GPIO41 (SDMMC data 1 - optional for 4-bit)
//   • D2   -> GPIO42 (SDMMC data 2 - optional for 4-bit)
//   • D3   -> GPIO43 (SDMMC data 3 - optional for 4-bit)
// Advantages of MMC vs SPI:
//   • Speed: up to 25 MHz (vs ~10 MHz practical on SPI)
//   • Dedicated interface: no conflicts with Ethernet/SPI
//   • Standard for ESP32-S3 modules with camera OV2640/OV5640

#ifndef SDCARD_MODULE_H
#define SDCARD_MODULE_H

#include <Arduino.h>        // String type used in SDCARD=0 stubs
#include "feature_flags.h" // Flags (ENABLE_SDCARD)
#include <cstdint>          // Ensure definition of uint64_t

// Shared rotation threshold for daily files across SD card and PSRAM VFS fallback.
// Keep this value identical everywhere so EMAILDAY sees the same file split size.
#ifndef STORAGE_ROTATED_FILE_MAX_SIZE_BYTES
#define STORAGE_ROTATED_FILE_MAX_SIZE_BYTES (1024UL * 1024UL)
#endif

#if ENABLE_SDCARD==1

#include "sd_lib.h" // SD backend: SDMMC native (default) or SPI (Waveshare 8DI8DO)
#include <FS.h>     // Filesystem wrapper for ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Semaphore to protect SD Card access (single task at a time)
// Note: MMC has no internal mutex - application must control concurrent access
extern SemaphoreHandle_t sdMutex;

// CRITICAL: Ownership tracking (exposed for acquireSDAccess/releaseSDAccess in chatgpt.ino)
extern TaskHandle_t sdMutexOwnerTask;
extern uint32_t sdMutexDepth;
// SD Mutex lock/unlock helpers (exposed for cross-module use)
bool sdMutexLock(TickType_t timeoutTicks);
void sdMutexUnlock();
// Delay-free variants for the audio playback path (called on every SD read).
bool sdMutexLockFast(TickType_t timeoutTicks);
void sdMutexUnlockFast();
// SDMMC pins (MMC 1-bit mode for ESP32-S3 with built-in SD card)
// ESP32-S3 modules with camera (OV2640/OV5640) have SD card slot on the back
// MMC 1-bit mode is faster and more reliable than SPI for built-in SD cards
// Standard SDMMC pinout for ESP32-S3:
//    CLK  -> GPIO39 (SDMMC clock)
//    CMD  -> GPIO38 (SDMMC command)
//    D0   -> GPIO40 (SDMMC data 0 - 1-bit mode)
//    D1   -> GPIO41 (SDMMC data 1 - optional for 4-bit)
//    D2   -> GPIO42 (SDMMC data 2 - optional for 4-bit)
//    D3   -> GPIO43 (SDMMC data 3 - optional for 4-bit)

// 🎛️ Operation mode: 1-bit (maximum compatibility) or 4-bit (maximum speed)
// 4-bit mode: 4× theoretical bandwidth (CLK + CMD + D0-D3)
// 1-bit mode: Maximum compatibility, fewer pins (CLK + CMD + D0 only)
// ⚠️ IMPORTANT: 4-bit mode requires GPIO41, GPIO42, GPIO43 to be free
//    Check conflicts with camera or other peripherals before enabling
#ifndef SDCARD_USE_1BIT_MODE
#define SDCARD_USE_1BIT_MODE true // false = 4-bit mode (MAXIMUM SPEED), true = 1-bit mode
#endif

// 📌 SDMMC pinout (ESP32-S3 standard for modules with camera)
#ifndef SDCARD_CLK_PIN
#define SDCARD_CLK_PIN 39 // SDMMC CLK
#endif
#ifndef SDCARD_CMD_PIN
#define SDCARD_CMD_PIN 38 // SDMMC CMD
#endif
#ifndef SDCARD_D0_PIN
#define SDCARD_D0_PIN 40 // SDMMC DATA0 (mandatory)
#endif
// D1-D3 pins only for 4-bit mode
#ifndef SDCARD_D1_PIN
#define SDCARD_D1_PIN 41 // SDMMC DATA1 (optional)
#endif
#ifndef SDCARD_D2_PIN
#define SDCARD_D2_PIN 42 // SDMMC DATA2 (optional)
#endif
#ifndef SDCARD_D3_PIN
#define SDCARD_D3_PIN 43 // SDMMC DATA3 (optional)
#endif

// 🔎 Optional: physical card detect pin (card detect)
// - Many SD breakouts have a "CD" pin that goes LOW when the card is inserted.
// - Define `SDCARD_DETECT_PIN` for the GPIO used on your hardware. If undefined, physical detection is ignored and init is attempted.
#ifndef SDCARD_DETECT_PIN
#define SDCARD_DETECT_PIN -1 // -1 = no detect pin
#endif

// ⚙️ Minimal API
bool sdcardBegin();       // Initialize the card (returns true if OK)
void sdcardBeginAsync();  // Start background mount (non-blocking)
bool sdcardMountInProgress(); // true if a mount is in progress
bool sdcardDetect();      // 🔍 Quick check: verify card is present (without mounting)
void sdcardStartAutoMount(); // 🔌 Start automatic hot-plug detection (periodic task)
void sdcardStopAutoMount();  // 🛑 Stop automatic detection
bool sdcardIsAutoMountEnabled(); // 📊 Check whether auto-mount is active
int  sdcardStatus();      // 0 = OK, !=0 error
void sdcardLoop();        // Called in the main loop, if needed
bool sdcardIsMounted();   // Current mount state
bool sdcardInFallbackMode(); // True if operating without SD (automatic fallback)
// Returns true only when SD is the active storage (mounted AND not in fallback mode).
// Use this instead of sdcardIsMounted() to respect the one-way failover logic.
inline bool sdcardShouldUseSD() { return sdcardIsMounted() && !sdcardInFallbackMode(); }
void sdcardRegisterWriteFailure(); // Register write failure (may trigger fallback)
void sdcardRegisterWriteSuccess(); // Register write success (resets failure counter)
void sdcardCheckRecovery(); // Check if SD can be recovered from fallback (call in loop)
bool sdcardUnmount();     // Unmount/eject the card (if supported)
uint64_t sdcardTotalBytes(); // Total size (bytes) — 0 if unavailable
uint64_t sdcardUsedBytes();  // Used space (bytes) — 0 if unavailable
unsigned long sdcardGetWriteCount(); // 📝 Operation count since boot
unsigned long sdcardGetWriteCountTotal(); // 📊 Total operations (persistent)
uint64_t sdcardGetBytesWritten(); // 📝 Bytes written since boot
uint64_t sdcardGetBytesWrittenTotal(); // 📊 Total bytes written (persistent) - card lifetime
bool sdcardResetWriteCounter(); // 🔄 Reset counters (card replacement)

// 📊 Count folders and files recursively (for status page)
void sdcardCountFoldersAndFiles(int &folderCount, int &fileCount, const char* path = "/"); // 📂 Count recursively

// SD Mutex diagnostics (exposed from chatgpt.ino for cross-module tracking)
extern void sdMutexTrackAcquire(const char* holder);
extern void sdMutexTrackRelease();
extern void sdMutexDumpState();

// 📁 API for structured recording (Nome_DD-MM-YYYY/rs232_DD-MM-YYYY_HHMMSS.txt, etc.)
bool sdcardWriteData(const String &source, const String &data); // Record data (source: "RS232" or "LPT")

// 🔬 Performance test: writes and reads a temporary file with totalBytes to measure throughput (bytes)
bool sdcardSpeedTest(size_t totalBytes);

// Runs the speed test for multiple SPI clock rates (from 80MHz downwards)
bool sdcardSpeedTestAll(size_t totalBytes);
void sdcardSpeedTestAllAsync(size_t totalBytes);
String sdcardGetTodayFolder(); // Return today's folder path (create if missing)
String sdcardGetFolderByDate(int day, int month, int year); // 📅 Return specific-date folder path (does not create)
bool sdcardCreateFolder(const String &folderPath); // Create folder if missing
bool sdcardDeleteFolderRecursive(const String &folderPath); // Delete folder recursively

#else
// Stubs when disabled
static inline bool sdcardBegin() { return false; }
static inline void sdcardBeginAsync() {}
static inline bool sdcardMountInProgress() { return false; }
static inline bool sdcardDetect() { return false; }
static inline void sdcardStartAutoMount() {}
static inline void sdcardStopAutoMount() {}
static inline bool sdcardIsAutoMountEnabled() { return false; }
static inline int sdcardStatus() { return -1; }
static inline void sdcardLoop() {}
static inline bool sdcardIsMounted() { return false; }
static inline bool sdcardInFallbackMode() { return true; } // Always in fallback when disabled
static inline bool sdcardShouldUseSD() { return false; } // SD disabled — always use fallback storage
static inline void sdcardRegisterWriteFailure() {}
static inline void sdcardRegisterWriteSuccess() {}
static inline void sdcardCheckRecovery() {}
static inline bool sdcardUnmount() { return false; }
static inline uint64_t sdcardTotalBytes() { return 0ULL; }
static inline uint64_t sdcardUsedBytes() { return 0ULL; }
static inline unsigned long sdcardGetWriteCount() { return 0UL; } // 📝 Stub operations
static inline unsigned long sdcardGetWriteCountTotal() { return 0UL; } // 📊 Stub operations
static inline uint64_t sdcardGetBytesWritten() { return 0ULL; } // 📝 Stub bytes
static inline uint64_t sdcardGetBytesWrittenTotal() { return 0ULL; } // 📊 Stub bytes
static inline bool sdcardResetWriteCounter() { return false; } // 🔄 Stub
static inline bool sdcardWriteData(const String &source, const String &data) { return false; }
static inline String sdcardGetTodayFolder() { return ""; }
static inline String sdcardGetFolderByDate(int day, int month, int year) { return ""; }
static inline bool sdcardCreateFolder(const String &folderPath) { return false; }
static inline bool sdcardDeleteFolderRecursive(const String &folderPath) { return false; }
#endif // ENABLE_SDCARD==1

// RULE: every SD data write must be counted (daily stats, all packs).
// Call storNoteSdWrite(finalBytes) when the file is closed.
void storNoteSdWrite(uint64_t bytes);

#endif // SDCARD_MODULE_H
