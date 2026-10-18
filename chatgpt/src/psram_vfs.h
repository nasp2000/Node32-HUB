// psram_vfs.h - Virtual Filesystem in PSRAM (Fallback when SD unavailable)
// 
// FEATURES:
// - 5MB circular buffer in PSRAM
// - Virtual "daily folders" (date-indexed data structures)
// - Auto-cleanup when buffer > 4MB (deletes oldest folder)
// - File rotation at 1MB (same as SD card for consistency)
// - Read-only snapshot mode for EMAILDAY (prevents race conditions)
// - Compatible API with SD card functions (for EMAILDAY command)
// - Priority Manager integration (P2 - same as SD writes)
//
// BEHAVIOR:
// - System works identically with or without SD card
// - With SD: data persisted to flash (guaranteed)
// - With PSRAM: data volatile (lost on reboot or auto-cleanup)
// - Email matching and EMAILDAY work transparently
//
// CRITICAL FIX (2026-02-22): PSRAM email corruption FULLY resolved
// - Issue: Data became corrupted mid-stream during EMAILDAY in PSRAM fallback mode
// - Root Cause #1: VirtualFile.size field could become stale due to PSRAM allocation failures
// - Root Cause #2: readChunk() obtained fileData pointer BEFORE validating size, causing stale pointer usage
// - Impact: readChunk() would read beyond valid data or from reallocated memory, returning corrupted bytes
// - Solution: Get file reference FIRST, always use vector.data()/size() directly, validate bounds before memcpy
//
// LAST CHANGE: 2026-03-13 22:28 WET

#ifndef PSRAM_VFS_H
#define PSRAM_VFS_H

#include <Arduino.h>
#include <vector>
#include <map>
#include "file_header_builder.h"
#include "sdcard.h"
#include "psram_helpers.h"
#include "priority_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 5MB total allocation by default (S3), override per pack for larger PSRAM (e.g. P4 → 16MB)
#ifndef PSRAM_VFS_BUFFER_SIZE
#define PSRAM_VFS_BUFFER_SIZE (5 * 1024 * 1024)  // 5MB
#endif
#define PSRAM_VFS_CLEANUP_THRESHOLD (4 * 1024 * 1024)  // 4MB
// File rotation threshold (same as SD card: 1024KB)
#define PSRAM_VFS_FILE_MAX_SIZE STORAGE_ROTATED_FILE_MAX_SIZE_BYTES

// Forward declarations
struct VirtualFile;
struct VirtualFolder;
class VirtualFilesystem;

// ============================================================================
// VIRTUAL FILE - Represents a single file within a folder
// ============================================================================
struct VirtualFile {
    String filename;  // e.g. "RS232_001.txt"
    std::vector<char, PsramAllocatorStd<char>> data;  // File contents
    size_t size;
    unsigned long lastModified;  // millis() timestamp

    VirtualFile();
    explicit VirtualFile(const String& name);

    void append(const char* buf, size_t len);
    void clear();
};

// ============================================================================
// VIRTUAL FOLDER - Represents a daily folder (e.g. "2026-02-17")
// ============================================================================
struct VirtualFolder {
    String date;  // YYYY-MM-DD format
    std::map<String, VirtualFile> files;  // filename -> VirtualFile
    std::map<String, int> sourceFileIndex;  // source -> current file index (for rotation)
    size_t totalSize;
    unsigned long creationTime;

    VirtualFolder();
    explicit VirtualFolder(const String& dateStr);

    String getCurrentFilename(const String& source, const String& ext = "txt");
    size_t getCurrentFileSize(const String& source);
    void rotateFile(const String& source);
    void appendToFile(const String& filename, const char* buf, size_t len, const String& source = "");
    const char* getFileData(const String& filename, size_t* outSize);
    std::vector<String> listFiles() const;
    void clear();
    size_t getTotalSize() const;
};

// ============================================================================
// VIRTUAL FILESYSTEM - Main manager with 5MB circular buffer + Double Buffering
// ============================================================================
class VirtualFilesystem {
private:
    std::map<String, VirtualFolder> folders;  // date -> VirtualFolder (stable data)
    size_t totalUsedBytes;
    SemaphoreHandle_t mutex;
    bool initialized;
    bool snapshotMode;  // NEW: Double buffering during EMAILDAY (like SD freeze)
    
    // DOUBLE BUFFERING: Pending writes during snapshot (equivalent to SD freeze buffer)
    struct PendingWrite {
        String source;
        std::vector<char, PsramAllocatorStd<char>> data;
        unsigned long timestamp;

        PendingWrite(const String& src, const char* buf, size_t len);
    };
    std::vector<PendingWrite, PsramAllocatorStd<PendingWrite>> pendingWrites;
    size_t pendingWritesBytes;

    // G-code blob — stored within the VFS budget, tracked against PSRAM_VFS_BUFFER_SIZE
    char* gcodeBlobData;
    size_t gcodeBlobSize;

    String getCurrentDate();
    void deleteOldestFolder();
    
public:
    VirtualFilesystem();
    ~VirtualFilesystem();

    bool begin();
    bool write(const String& source, const char* data, size_t len);
    
    // G-code blob — stored within the VFS budget (PSRAM_VFS_BUFFER_SIZE).
    // set:     copies data into VFS, adds to totalUsedBytes
    // get:     read-only pointer (null if no blob stored)
    // release: transfers ownership out (zero-copy), subtracts from totalUsedBytes
    bool setGcodeBlob(const uint8_t* data, size_t size);
    const char* getGcodeBlobData(size_t* outSize) const;
    char* releaseGcodeBlob();
    std::vector<String> listFolders();
    VirtualFolder* getFolder(const String& date);
    String getTodayFolderPath();
    bool isEnabled() const;
    size_t getTotalUsedBytes() const;
    size_t getAvailableBytes() const;
    size_t getFolderCount() const;
    size_t getFileSize(const String& folderPath, const String& filename);
    size_t readChunk(const String& folderPath, const String& filename, size_t offset, uint8_t* buffer, size_t length);
    String readFile(const String& folderPath, const String& filename);
    const char* getFileDataPtr(const String& folderPath, const String& filename, size_t* outSize);
    std::vector<String> listFilesInFolder(const String& folderPath);
    void enableSnapshot();
    void disableSnapshot();
    bool isSnapshotActive() const;
    void getSnapshotStats(size_t& pendingCount, size_t& pendingBytes);
    void clear();
};

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================
// CHANGED: From static to extern so it's shared across translation units
extern VirtualFilesystem psramVFS; 

// ============================================================================
// PUBLIC API (SD-compatible interface)
// ============================================================================

size_t psramVFSGetTotalFileCount();
bool psramVFSBegin();
bool psramVFSIsEnabled();
bool psramVFSWrite(const String& source, const char* data, size_t len);
String psramVFSGetTodayFolder();
std::vector<String> psramVFSListFolders();
std::vector<String> psramVFSListFiles(const String& folderPath);
size_t psramVFSGetFileSize(const String& folderPath, const String& filename);
size_t psramVFSReadChunk(const String& folderPath, const String& filename, size_t offset, uint8_t* buffer, size_t length);
String psramVFSReadFile(const String& folderPath, const String& filename);

// NEW: Get direct pointer to PSRAM file data (for streaming large files without String allocation)
// WARNING: Returned pointer is READ-ONLY and valid only until next PSRAM write/delete operation
// Use only during EMAILDAY snapshot mode when writes are frozen
const char* psramVFSGetFileDataPtr(const String& folderPath, const String& filename, size_t* outSize);

size_t psramVFSGetUsedBytes();
size_t psramVFSGetFreeBytes();
size_t psramVFSGetFolderCount();
void psramVFSClear();

// G-code blob API — stores in VFS within PSRAM_VFS_BUFFER_SIZE budget
bool psramVFSSetGcodeBlob(const uint8_t* data, size_t size);
const char* psramVFSGetGcodeBlobData(size_t* outSize);
char* psramVFSReleaseGcodeBlob();

// ============================================================================
// DOUBLE BUFFERING API (equivalent to SD freeze mechanism)
// ============================================================================

void psramVFSEnableSnapshot();
void psramVFSDisableSnapshot();
bool psramVFSIsSnapshotActive();
void psramVFSGetSnapshotStats(size_t& count, size_t& bytes);

#endif // PSRAM_VFS_H
