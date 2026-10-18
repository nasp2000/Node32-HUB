// Flash Filesystem Manager - Internal flash storage management
// Supports FFat (FAT filesystem) for FATFS partitions
// Provides unified API compatible with sdcard.h
//
// NOTE: Changed from LittleFS to FFat to support 9.9MB FATFS partition from Arduino IDE

#ifndef LITTLEFS_MANAGER_H
#define LITTLEFS_MANAGER_H

#include "feature_flags.h"
#include <Arduino.h>
#include <FS.h> // Common FS interface
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if ENABLE_FILESYSTEM==1

// Note: Implementation selects FS type based on feature_flags.h FILESYSTEM_PROVIDER

// Initialize Filesystem (format on failure)
bool littlefsBegin();

// Unmount
bool littlefsEnd();

// Check if mounted
bool littlefsMounted();

// Get total bytes
uint64_t littlefsTotalBytes();

// Get used bytes
uint64_t littlefsUsedBytes();

// Get free bytes
uint64_t littlefsFreeBytes();

// Format the filesystem (destructive — use only for factory reset)
bool littlefsFormat();

// Get filesystem object (returns LittleFS/FFat/SPIFFS instance cast to FS&)
fs::FS& getLittleFS();

// Cross-task LittleFS guard (all packs). The joltwallet LittleFS instance is
// NOT thread-safe: concurrent metadata ops (open/stat/mkdir/remove/rename/
// list) from the email task (flash attachments) and the loop task (sensor
// flush, journal, T24 archive, log flush, cleanup) corrupt the dir traversal
// and panic with IllegalInstruction in lfs_dir_fetchmatch (live .24,
// 2026-10-14 backtrace: emailQueueProc -> esp_vfs_stat -> lfs_stat_).
// Same discipline as sdMutex: recursive (same-task reentry safe), metadata
// ops take it, bulk streaming reads do not (torn data beats panic; writers
// are ms-fast so readers rarely wait). Single lock: no lock-order risk.
extern SemaphoreHandle_t lfsMutex;
bool lfsMutexLock(TickType_t timeoutTicks);
void lfsMutexUnlock();

#else
// Stubs when disabled
inline bool littlefsBegin() { return false; }
inline bool littlefsEnd() { return false; }
inline bool littlefsMounted() { return false; }
inline uint64_t littlefsTotalBytes() { return 0ULL; }
inline uint64_t littlefsUsedBytes() { return 0ULL; }
inline uint64_t littlefsFreeBytes() { return 0ULL; }
inline bool littlefsFormat() { return false; }
// Warning: calling getLittleFS() when disabled will cause compilation error or undefined behavior if used.
// It's better to guard calls with #if ENABLE_FILESYSTEM
#endif

// RULE: every LittleFS data write must be counted (daily stats, all packs).
// Call storNoteLfsWrite(finalBytes) when the file is closed.
void storNoteLfsWrite(uint64_t bytes);

#endif // LITTLEFS_MANAGER_H
