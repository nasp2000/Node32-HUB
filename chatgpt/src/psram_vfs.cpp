#include "psram_vfs.h"
#include "ConfigManager.h"

// Global instance (extern declared in psram_vfs.h)
VirtualFilesystem psramVFS;

#include <algorithm>
#include <climits>
#include <cstring>

extern ConfigManager configManager;

// Returns the file extension for a given source, matching SD card behavior.
// SENSOR_HIST always uses csv; RS232/LPT use the configured storage format; others use txt.
static String _psramExtForSource(const String& source) {
    if (source.equalsIgnoreCase("SENSOR_HIST")) return String("csv");
    if (source.equalsIgnoreCase("RS232") || source.equalsIgnoreCase("LPT")) {
        String fmt = configManager.getSdcardFormat();
        if (fmt == "csv" || fmt == "bin") return fmt;
    }
    return String("txt");
}

VirtualFile::VirtualFile() : size(0), lastModified(0) {}

VirtualFile::VirtualFile(const String& name) : filename(name), size(0), lastModified(millis()) {}

void VirtualFile::append(const char* buf, size_t len) {
    if (!buf || len == 0) return;

    size_t sizeBefore = data.size();

    try {
        data.insert(data.end(), buf, buf + len);
    } catch (...) {
        Serial.print(F("PSRAM_VFS: ❌ CRITICAL: append() failed for "));
        Serial.print(filename);
        Serial.print(F(" (tried to add "));
        Serial.print(len);
        Serial.println(F(" bytes)"));
        return;
    }

    size_t sizeAfter = data.size();
    if (sizeAfter != sizeBefore + len) {
        Serial.print(F("PSRAM_VFS: ⚠️ APPEND SIZE MISMATCH: expected "));
        Serial.print(sizeBefore + len);
        Serial.print(F(" bytes, got "));
        Serial.print(sizeAfter);
        Serial.print(F(" bytes (file: "));
        Serial.print(filename);
        Serial.println(F(")"));
    }

    size = data.size();
    lastModified = millis();
}

void VirtualFile::clear() {
    data.clear();
    data.shrink_to_fit();
    size = 0;
}

VirtualFolder::VirtualFolder() : totalSize(0), creationTime(millis()) {}

VirtualFolder::VirtualFolder(const String& dateStr) : date(dateStr), totalSize(0), creationTime(millis()) {}

String VirtualFolder::getCurrentFilename(const String& source, const String& ext) {
    String systemName = getSystemNameForFilename();

    String dateStr = date;
    if (dateStr.isEmpty()) {
        systemName = source;
        dateStr = "";
    } else if (dateStr.length() == 10 && dateStr.charAt(4) == '-' && dateStr.charAt(7) == '-') {
        String year = dateStr.substring(0, 4);
        String month = dateStr.substring(5, 7);
        String day = dateStr.substring(8, 10);
        dateStr = day + "-" + month + "-" + year;
    }

    if (sourceFileIndex.find(source) == sourceFileIndex.end()) {
        sourceFileIndex[source] = 0;
    }

    int index = sourceFileIndex[source];
    char buf[128];

    if (index > 0) {
        snprintf(buf, sizeof(buf), "%s_%s_%03d.%s", systemName.c_str(), dateStr.c_str(), index, ext.c_str());
    } else {
        snprintf(buf, sizeof(buf), "%s_%s.%s", systemName.c_str(), dateStr.c_str(), ext.c_str());
    }

    return String(buf);
}

size_t VirtualFolder::getCurrentFileSize(const String& source) {
    String filename = getCurrentFilename(source, _psramExtForSource(source));
    auto it = files.find(filename);
    if (it == files.end()) return 0;
    return it->second.size;
}

void VirtualFolder::rotateFile(const String& source) {
    if (sourceFileIndex.find(source) == sourceFileIndex.end()) {
        sourceFileIndex[source] = 1;
    } else {
        sourceFileIndex[source]++;
    }

    Serial.print(F("PSRAM_VFS: Rotating "));
    Serial.print(source);
    Serial.print(F(" to file #"));
    Serial.println(sourceFileIndex[source]);
}

void VirtualFolder::appendToFile(const String& filename, const char* buf, size_t len, const String& source) {
    bool isNewFile = (files.find(filename) == files.end());

    if (isNewFile) {
        files[filename] = VirtualFile(filename);

        String actualSource = source;
        if (actualSource.isEmpty()) {
            actualSource = filename;
            int underscorePos = actualSource.indexOf('_');
            if (underscorePos > 0) {
                actualSource = actualSource.substring(0, underscorePos);
            }
        }

        // Skip firmware text header for CSV sources (SENSOR_HIST writes its own column header).
        if (!actualSource.equalsIgnoreCase("LPT") && !actualSource.equalsIgnoreCase("DB25") &&
            !actualSource.equalsIgnoreCase("SENSOR_HIST")) {
            String header = buildFileHeader(actualSource);
            if (header.length() > 0) {
                files[filename].append(header.c_str(), header.length());
                totalSize += header.length();
            }
        }
    }

    files[filename].append(buf, len);
    totalSize += len;
}

const char* VirtualFolder::getFileData(const String& filename, size_t* outSize) {
    auto it = files.find(filename);
    if (it == files.end() || it->second.data.empty()) {
        if (outSize) *outSize = 0;
        return nullptr;
    }

    if (outSize) *outSize = it->second.data.size();
    return it->second.data.data();
}

std::vector<String> VirtualFolder::listFiles() const {
    std::vector<String> result;
    for (const auto& pair : files) {
        result.push_back(pair.first);
    }
    return result;
}

void VirtualFolder::clear() {
    for (auto& pair : files) {
        pair.second.clear();
    }
    files.clear();
    totalSize = 0;
}

size_t VirtualFolder::getTotalSize() const {
    return totalSize;
}

VirtualFilesystem::PendingWrite::PendingWrite(const String& src, const char* buf, size_t len)
    : source(src), timestamp(millis()) {
    data.assign(buf, buf + len);
}

String VirtualFilesystem::getCurrentDate() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return String("1970-01-01");
    }

    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
    return String(buf);
}

void VirtualFilesystem::deleteOldestFolder() {
    if (folders.empty()) return;

    String oldestDate;
    unsigned long oldestTime = ULONG_MAX;

    for (const auto& pair : folders) {
        if (pair.second.creationTime < oldestTime) {
            oldestTime = pair.second.creationTime;
            oldestDate = pair.first;
        }
    }

    if (!oldestDate.isEmpty()) {
        size_t freedBytes = folders[oldestDate].getTotalSize();
        folders[oldestDate].clear();
        folders.erase(oldestDate);
        totalUsedBytes -= freedBytes;

        Serial.print(F("PSRAM_VFS: Deleted oldest folder "));
        Serial.print(oldestDate);
        Serial.print(F(" (freed "));
        Serial.print(freedBytes / 1024);
        Serial.println(F(" KB)"));
    }
}

VirtualFilesystem::VirtualFilesystem()
    : totalUsedBytes(0), initialized(false), snapshotMode(false), pendingWritesBytes(0),
      gcodeBlobData(nullptr), gcodeBlobSize(0) {
    mutex = xSemaphoreCreateRecursiveMutex();
}

VirtualFilesystem::~VirtualFilesystem() {
    if (gcodeBlobData) { heap_caps_free(gcodeBlobData); gcodeBlobData = nullptr; gcodeBlobSize = 0; }
    if (mutex) vSemaphoreDelete(mutex);
}

bool VirtualFilesystem::begin() {
    if (initialized) return true;

    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    if (!psramFound()) {
        Serial.println(F("PSRAM_VFS: ❌ PSRAM not available"));
        xSemaphoreGiveRecursive(mutex);
        return false;
    }

    size_t psramFree = ESP.getFreePsram();
    if (psramFree < PSRAM_VFS_BUFFER_SIZE) {
        Serial.print(F("PSRAM_VFS: ⚠️ Insufficient PSRAM (need "));
        Serial.print(PSRAM_VFS_BUFFER_SIZE / 1024 / 1024);
        Serial.print(F(" MB, have "));
        Serial.print(psramFree / 1024 / 1024);
        Serial.println(F(" MB free)"));
        xSemaphoreGiveRecursive(mutex);
        return false;
    }

    totalUsedBytes = 0;
    initialized = true;

    Serial.println(F("PSRAM_VFS: ✅ Initialized (5MB circular buffer)"));

    xSemaphoreGiveRecursive(mutex);
    return true;
}

bool VirtualFilesystem::write(const String& source, const char* data, size_t len) {
    if (!initialized) return false;
    if (!data || len == 0) return false;

    if (!priorityManager().canStartSDWrite()) {
        Serial.println(F("PSRAM_VFS: ⏸️ Blocked by Priority Manager (higher priority active)"));
        return false;
    }

    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    if (snapshotMode) {
        pendingWrites.emplace_back(source, data, len);
        pendingWritesBytes += len;

        static unsigned long lastLog = 0;
        if (millis() - lastLog > 5000) {
            Serial.print(F("PSRAM_VFS: 📦 Snapshot active - buffering writes ("));
            Serial.print(pendingWrites.size());
            Serial.print(F(" pending, "));
            Serial.print(pendingWritesBytes / 1024);
            Serial.println(F(" KB)"));
            lastLog = millis();
        }

        xSemaphoreGiveRecursive(mutex);
        return true;
    }

    String today = getCurrentDate();
    if (folders.find(today) == folders.end()) {
        folders[today] = VirtualFolder(today);
        Serial.print(F("PSRAM_VFS: 📁 Created folder "));
        Serial.println(today);
    }

    VirtualFolder& folder = folders[today];
    size_t currentSize = folder.getCurrentFileSize(source);
    if (currentSize >= PSRAM_VFS_FILE_MAX_SIZE) {
        folder.rotateFile(source);
    }

    String ext = _psramExtForSource(source);
    String filename = folder.getCurrentFilename(source, ext);

    // Auto-cleanup is always active (no UI toggle), two tiers sharing the policy:
    //   HARD (usage > threshold): the oldest folder goes, whatever its age.
    //   SOFT (usage <= threshold): only folders older than keep_days go.
    // Respects getSdKeepDays() in the soft tier; today's folder is never
    // a candidate here. A 95% hard limit always applies as OOM backstop.
    {
        int threshPct = configManager.getSdCleanupThreshold();
        size_t cleanupThreshold = (size_t)PSRAM_VFS_BUFFER_SIZE * (size_t)threshPct / 100;
        int keepDays = configManager.getSdKeepDays();
        // keep_days = 0 (UI "infinity"): no age limit in the soft tier.
        const bool over = totalUsedBytes > cleanupThreshold;
        String todayDate = getCurrentDate();
        time_t now_t = time(nullptr);
        String oldestDate;
        unsigned long oldestTime = ULONG_MAX;
        for (const auto& pair : folders) {
            if (pair.first == todayDate) continue;
            if (pair.second.creationTime < oldestTime) {
                oldestTime = pair.second.creationTime;
                oldestDate = pair.first;
            }
        }
        if (!oldestDate.isEmpty() && folders.size() > 1) {
            bool doDelete = over;
            if (!doDelete && now_t > 100000) {
                int fy, fm, fd;
                if (sscanf(oldestDate.c_str(), "%d-%d-%d", &fy, &fm, &fd) == 3) {
                    struct tm folder_tm = {};
                    folder_tm.tm_year = fy - 1900;
                    folder_tm.tm_mon  = fm - 1;
                    folder_tm.tm_mday = fd;
                    folder_tm.tm_hour = 12;
                    time_t folder_t = mktime(&folder_tm);
                    if (folder_t > 0) {
                        int ageDays = (int)(difftime(now_t, folder_t) / 86400.0);
                        doDelete = (ageDays >= 0) && (ageDays >= keepDays);
                    }
                }
            }
            if (doDelete) {
                size_t freedBytes = folders[oldestDate].getTotalSize();
                folders[oldestDate].clear();
                folders.erase(oldestDate);
                totalUsedBytes -= freedBytes;
                Serial.print(F("PSRAM_VFS: Auto-cleanup deleted "));
                Serial.print(oldestDate);
                Serial.print(F(" (freed "));
                Serial.print(freedBytes / 1024);
                Serial.println(F(" KB)"));
            }
        }
        // OOM backstop: never exceed 95% even if keep_days protects everything.
        const size_t hardLimit = (size_t)PSRAM_VFS_BUFFER_SIZE * 95 / 100;
        while (totalUsedBytes > hardLimit && folders.size() > 1) {
            deleteOldestFolder();
        }
    }

    folder.appendToFile(filename, data, len, source);
    totalUsedBytes += len;

    xSemaphoreGiveRecursive(mutex);
    return true;
}

std::vector<String> VirtualFilesystem::listFolders() {
    std::vector<String> result;
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    for (const auto& pair : folders) {
        result.push_back(pair.first);
    }

    xSemaphoreGiveRecursive(mutex);
    return result;
}

VirtualFolder* VirtualFilesystem::getFolder(const String& date) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    auto it = folders.find(date);
    if (it == folders.end()) {
        xSemaphoreGiveRecursive(mutex);
        return nullptr;
    }

    VirtualFolder* folder = &it->second;
    xSemaphoreGiveRecursive(mutex);
    return folder;
}

String VirtualFilesystem::getTodayFolderPath() {
    return "/" + getCurrentDate();
}

bool VirtualFilesystem::isEnabled() const {
    return initialized;
}

size_t VirtualFilesystem::getTotalUsedBytes() const {
    return totalUsedBytes;
}

size_t VirtualFilesystem::getAvailableBytes() const {
    return PSRAM_VFS_BUFFER_SIZE - totalUsedBytes;
}

bool VirtualFilesystem::setGcodeBlob(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    // Free existing blob first
    if (gcodeBlobData) {
        totalUsedBytes -= gcodeBlobSize;
        heap_caps_free(gcodeBlobData);
        gcodeBlobData = nullptr;
        gcodeBlobSize = 0;
    }

    // Check budget: total (including VFS logs) must fit in PSRAM_VFS_BUFFER_SIZE
    if (totalUsedBytes + size > PSRAM_VFS_BUFFER_SIZE) {
        xSemaphoreGiveRecursive(mutex);
        return false;
    }

    gcodeBlobData = (char*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!gcodeBlobData) {
        gcodeBlobData = (char*)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!gcodeBlobData) {
        xSemaphoreGiveRecursive(mutex);
        return false;
    }
    memcpy(gcodeBlobData, data, size);
    gcodeBlobSize = size;
    totalUsedBytes += size;

    xSemaphoreGiveRecursive(mutex);
    return true;
}

const char* VirtualFilesystem::getGcodeBlobData(size_t* outSize) const {
    // No mutex — read-only access to a pointer that is stable until release/clear.
    // Caller must not hold this pointer past the next VFS write operation.
    if (outSize) *outSize = gcodeBlobSize;
    return gcodeBlobData;
}

char* VirtualFilesystem::releaseGcodeBlob() {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    if (!gcodeBlobData || gcodeBlobSize == 0) {
        xSemaphoreGiveRecursive(mutex);
        return nullptr;
    }

    char* ptr = gcodeBlobData;
    size_t sz = gcodeBlobSize;
    gcodeBlobData = nullptr;
    gcodeBlobSize = 0;
    totalUsedBytes -= sz;

    xSemaphoreGiveRecursive(mutex);
    return ptr;
}

size_t VirtualFilesystem::getFolderCount() const {
    return folders.size();
}

size_t VirtualFilesystem::getFileSize(const String& folderPath, const String& filename) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    String date = folderPath;
    if (date.startsWith("/")) date = date.substring(1);

    auto it = folders.find(date);
    if (it == folders.end()) {
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    size_t size = 0;
    it->second.getFileData(filename, &size);

    xSemaphoreGiveRecursive(mutex);
    return size;
}

size_t VirtualFilesystem::readChunk(const String& folderPath, const String& filename, size_t offset, uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return 0;

    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    String date = folderPath;
    if (date.startsWith("/")) date = date.substring(1);

    auto it = folders.find(date);
    if (it == folders.end()) {
        Serial.print(F("PSRAM_VFS_CHUNK: ❌ Folder not found: "));
        Serial.println(date);
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    auto fileIt = it->second.files.find(filename);
    if (fileIt == it->second.files.end()) {
        Serial.print(F("PSRAM_VFS_CHUNK: ❌ File not found: "));
        Serial.println(filename);
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    size_t fileSize = fileIt->second.data.size();
    const char* fileData = fileIt->second.data.data();

    if (!fileData || fileSize == 0) {
        Serial.print(F("PSRAM_VFS_CHUNK: ❌ File data NULL or empty: "));
        Serial.println(filename);
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    if (offset >= fileSize) {
        Serial.print(F("PSRAM_VFS_CHUNK: ❌ Offset beyond EOF: offset="));
        Serial.print(offset);
        Serial.print(F(" fileSize="));
        Serial.println(fileSize);
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    size_t bytesToRead = std::min(length, fileSize - offset);

    static unsigned long lastLog = 0;
    if (millis() - lastLog > 2000) {
        Serial.print(F("PSRAM_VFS_CHUNK: Reading "));
        Serial.print(bytesToRead);
        Serial.print(F(" bytes @ offset "));
        Serial.print(offset);
        Serial.print(F(" from "));
        Serial.print(filename);
        Serial.print(F(" (fileSize="));
        Serial.print(fileSize);
        Serial.print(F(", dataPtr=0x"));
        Serial.print((unsigned long)fileData, HEX);
        Serial.println(F(")"));
        lastLog = millis();
    }

    if ((offset + bytesToRead) > fileSize) {
        Serial.println(F("PSRAM_VFS_CHUNK: ❌ CRITICAL: Bounds check failed before memcpy!"));
        xSemaphoreGiveRecursive(mutex);
        return 0;
    }

    memcpy(buffer, fileData + offset, bytesToRead);

    xSemaphoreGiveRecursive(mutex);
    return bytesToRead;
}

String VirtualFilesystem::readFile(const String& folderPath, const String& filename) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    String date = folderPath;
    if (date.startsWith("/")) date = date.substring(1);

    auto it = folders.find(date);
    if (it == folders.end()) {
        xSemaphoreGiveRecursive(mutex);
        return String("");
    }

    auto fileIt = it->second.files.find(filename);
    if (fileIt == it->second.files.end()) {
        Serial.print(F("PSRAM_VFS_READ: ❌ File not found: "));
        Serial.println(filename);
        xSemaphoreGiveRecursive(mutex);
        return String("");
    }

    size_t size = fileIt->second.data.size();
    const char* data = fileIt->second.data.data();

    String result;
    if (data && size > 0) {
        result.reserve(size + 1);
        result.concat(data, size);
    }

    xSemaphoreGiveRecursive(mutex);
    return result;
}

const char* VirtualFilesystem::getFileDataPtr(const String& folderPath, const String& filename, size_t* outSize) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    String date = folderPath;
    if (date.startsWith("/")) date = date.substring(1);

    auto it = folders.find(date);
    if (it == folders.end()) {
        xSemaphoreGiveRecursive(mutex);
        if (outSize) *outSize = 0;
        return nullptr;
    }

    auto fileIt = it->second.files.find(filename);
    if (fileIt == it->second.files.end()) {
        Serial.print(F("PSRAM_VFS_PTR: ❌ File not found: "));
        Serial.println(filename);
        xSemaphoreGiveRecursive(mutex);
        if (outSize) *outSize = 0;
        return nullptr;
    }

    if (outSize) *outSize = fileIt->second.data.size();
    const char* data = fileIt->second.data.data();

    xSemaphoreGiveRecursive(mutex);
    return data;
}

std::vector<String> VirtualFilesystem::listFilesInFolder(const String& folderPath) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    String date = folderPath;
    if (date.startsWith("/")) date = date.substring(1);

    auto it = folders.find(date);
    if (it == folders.end()) {
        xSemaphoreGiveRecursive(mutex);
        return std::vector<String>();
    }

    std::vector<String> files = it->second.listFiles();

    xSemaphoreGiveRecursive(mutex);
    return files;
}

void VirtualFilesystem::enableSnapshot() {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    if (snapshotMode) {
        Serial.println(F("PSRAM_VFS: ⚠️ Snapshot already active"));
        xSemaphoreGiveRecursive(mutex);
        return;
    }

    snapshotMode = true;
    pendingWrites.clear();
    pendingWrites.shrink_to_fit();
    pendingWritesBytes = 0;

    Serial.println(F("PSRAM_VFS: 🔒 SNAPSHOT ENABLED (double buffering active)"));
    Serial.print(F("   📸 Current state frozen ("));
    Serial.print(totalUsedBytes / 1024);
    Serial.println(F(" KB stable for EMAILDAY)"));
    Serial.println(F("   📦 New writes will buffer in pending queue"));

    xSemaphoreGiveRecursive(mutex);
}

void VirtualFilesystem::disableSnapshot() {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    if (!snapshotMode) {
        Serial.println(F("PSRAM_VFS: ⚠️ Snapshot not active"));
        xSemaphoreGiveRecursive(mutex);
        return;
    }

    Serial.println(F("PSRAM_VFS: 🔓 SNAPSHOT DISABLED - flushing pending writes"));
    Serial.print(F("   📦 Processing "));
    Serial.print(pendingWrites.size());
    Serial.print(F(" pending writes ("));
    Serial.print(pendingWritesBytes / 1024);
    Serial.println(F(" KB)"));

    for (const auto& pw : pendingWrites) {
        String today = getCurrentDate();

        if (folders.find(today) == folders.end()) {
            folders[today] = VirtualFolder(today);
        }

        VirtualFolder& folder = folders[today];
        size_t currentSize = folder.getCurrentFileSize(pw.source);
        if (currentSize >= PSRAM_VFS_FILE_MAX_SIZE) {
            folder.rotateFile(pw.source);
        }

        String ext = _psramExtForSource(pw.source);
        String filename = folder.getCurrentFilename(pw.source, ext);
        folder.appendToFile(filename, pw.data.data(), pw.data.size(), pw.source);
        totalUsedBytes += pw.data.size();
    }

    Serial.print(F("   ✅ Flushed "));
    Serial.print(pendingWrites.size());
    Serial.println(F(" writes to main files"));

    pendingWrites.clear();
    pendingWrites.shrink_to_fit();
    pendingWritesBytes = 0;
    snapshotMode = false;

    xSemaphoreGiveRecursive(mutex);
}

bool VirtualFilesystem::isSnapshotActive() const {
    return snapshotMode;
}

void VirtualFilesystem::getSnapshotStats(size_t& pendingCount, size_t& pendingBytes) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    pendingCount = pendingWrites.size();
    pendingBytes = pendingWritesBytes;
    xSemaphoreGiveRecursive(mutex);
}

void VirtualFilesystem::clear() {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);

    for (auto& pair : folders) {
        pair.second.clear();
    }
    folders.clear();
    if (gcodeBlobData) {
        heap_caps_free(gcodeBlobData);
        gcodeBlobData = nullptr;
        gcodeBlobSize = 0;
    }
    totalUsedBytes = 0;

    Serial.println(F("PSRAM_VFS: 🗑️ Cleared all data"));

    xSemaphoreGiveRecursive(mutex);
}

bool psramVFSBegin() {
    return psramVFS.begin();
}

bool psramVFSIsEnabled() {
    return psramVFS.isEnabled();
}

bool psramVFSWrite(const String& source, const char* data, size_t len) {
    return psramVFS.write(source, data, len);
}

String psramVFSGetTodayFolder() {
    return psramVFS.getTodayFolderPath();
}

std::vector<String> psramVFSListFolders() {
    return psramVFS.listFolders();
}

std::vector<String> psramVFSListFiles(const String& folderPath) {
    return psramVFS.listFilesInFolder(folderPath);
}

size_t psramVFSGetFileSize(const String& folderPath, const String& filename) {
    return psramVFS.getFileSize(folderPath, filename);
}

size_t psramVFSReadChunk(const String& folderPath, const String& filename, size_t offset, uint8_t* buffer, size_t length) {
    return psramVFS.readChunk(folderPath, filename, offset, buffer, length);
}

String psramVFSReadFile(const String& folderPath, const String& filename) {
    return psramVFS.readFile(folderPath, filename);
}

const char* psramVFSGetFileDataPtr(const String& folderPath, const String& filename, size_t* outSize) {
    return psramVFS.getFileDataPtr(folderPath, filename, outSize);
}

size_t psramVFSGetUsedBytes() {
    return psramVFS.getTotalUsedBytes();
}

size_t psramVFSGetFreeBytes() {
    return psramVFS.getAvailableBytes();
}

size_t psramVFSGetFolderCount() {
    return psramVFS.getFolderCount();
}

size_t psramVFSGetTotalFileCount() {
    size_t count = 0;
    std::vector<String> folders = psramVFS.listFolders();
    for (const String& folder : folders) {
        count += psramVFS.listFilesInFolder(folder).size();
    }
    return count;
}

void psramVFSClear() {
    psramVFS.clear();
}

void psramVFSEnableSnapshot() {
    psramVFS.enableSnapshot();
}

void psramVFSDisableSnapshot() {
    psramVFS.disableSnapshot();
}

bool psramVFSIsSnapshotActive() {
    return psramVFS.isSnapshotActive();
}

void psramVFSGetSnapshotStats(size_t& count, size_t& bytes) {
    psramVFS.getSnapshotStats(count, bytes);
}

bool psramVFSSetGcodeBlob(const uint8_t* data, size_t size) {
    return psramVFS.setGcodeBlob(data, size);
}

const char* psramVFSGetGcodeBlobData(size_t* outSize) {
    return psramVFS.getGcodeBlobData(outSize);
}

char* psramVFSReleaseGcodeBlob() {
    return psramVFS.releaseGcodeBlob();
}