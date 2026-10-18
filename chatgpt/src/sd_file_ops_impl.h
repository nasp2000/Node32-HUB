// sd_file_ops_impl.h -- SD card folder/file operations: write, speed tests, daily file.
// Included only from sdcard.cpp (single TU, all static variables shared).
// Do not include this file from any other translation unit.

#include "sd_layout.h" // standard layout + clockReady/date-parsing helpers
#include "flash_fallback_store.h" // LittleFS unload hook (no-SD packs persist data here)

// Forward decl from the proximity module (available only when proximity is built).
// Used to align the daily SD file/folder boundary to the last shift hour on the
// Waveshare pack, so shift reports are not cut near midnight.
#if ENABLE_PROXIMITY==1 && defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
uint8_t proximityShiftDayAlignedDate(time_t now, struct tm* out);
#endif

// Reset all source rotation indices when the date changes (new day = fresh files).
static void sdcardResetRotationOnDateChange(const char* dateSuffix) {
    static char lastDate[16] = "";
    if (strncmp(lastDate, dateSuffix, sizeof(lastDate) - 1) != 0) {
        memset(_sourceRotTable, 0, sizeof(_sourceRotTable));
        _sourceRotCount = 0;
        strncpy(lastDate, dateSuffix, sizeof(lastDate) - 1);
        lastDate[sizeof(lastDate) - 1] = '\0';
    }
}

// Return the rotation entry for the given source, creating one if not found.
// Returns nullptr if the table is full (8 sources maximum).
static SourceRotationEntry* getSourceRotation(const char* source) {
    for (uint8_t i = 0; i < _sourceRotCount; i++) {
        if (strcasecmp(_sourceRotTable[i].name, source) == 0) {
            return &_sourceRotTable[i];
        }
    }
    if (_sourceRotCount < 8) {
        SourceRotationEntry& e = _sourceRotTable[_sourceRotCount++];
        strncpy(e.name, source, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        e.index = 0;
        return &e;
    }
    return nullptr; // Table full — rotation disabled for this source
}

// Get today's folder reference for EMAILDAY use.
// Returns "/{sdcardName}@DD-MM-YYYY" — the "@" is a date filter separator, NOT a real path.
// Callers that need a real path (sdcardWriteData) build source subfolders inline.
// Callers that send email (enviarEmailComAnexosSD, googleEmailSendEmailDay) parse "@" to
// expand one level of date-matching subdirectories under "/{sdcardName}/".
String sdcardGetTodayFolder() {
    if (!_sdMounted) return "";

    time_t now = time(nullptr);

    if (now <= 1000) {
        now = 1640000000; // fallback: 2021-12-20 when RTC has never synced
        static bool rtcWarnLogged = false;
        if (!rtcWarnLogged) {
            Serial.println(F("SDCARD: RTC invalid in getTodayFolder, using fallback. Sync with NTP!"));
            rtcWarnLogged = true;
        }
    }

    struct tm info;
#if ENABLE_PROXIMITY==1 && defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    // Waveshare pack: align "today" to the shift-aligned day boundary so the
    // EMAILDAY report picks the file that actually holds the current day.
    proximityShiftDayAlignedDate(now, &info);
#else
    localtime_r(&now, &info);
#endif

    char dateSuffix[16];
    snprintf(dateSuffix, sizeof(dateSuffix), "%02d-%02d-%04d",
             info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);

    String folderNameBase = configManager.getSdcardName();
    if (folderNameBase.isEmpty()) {
        folderNameBase = "SD_LOG";
    }

    // Return "@"-encoded path: "/{base}@DD-MM-YYYY"
    // Email senders parse this to list subdirs matching the date under "/{base}/"
    return String("/") + folderNameBase + String("@") + String(dateSuffix);
}

// Get folder path for a specific date (format: "Name_DD-MM-YYYY")
// Does not create the folder if it does not exist (unlike getTodayFolder)
String sdcardGetFolderByDate(int day, int month, int year) {
    if (!_sdMounted) return "";

    // Format date suffix: "DD-MM-YYYY"
    char dateSuffix[16];
    snprintf(dateSuffix, sizeof(dateSuffix), "%02d-%02d-%04d", day, month, year);

    // Find any folder containing this date (historical folders may have old base names)
    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
        }
        return "";
    }
    
    String foundFolder = "";
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String fullName = String(entry.name());
            
            // Check whether folder contains the target date (ignore base name)
            if (fullName.indexOf(dateSuffix) >= 0) {
                foundFolder = fullName;
                if (!foundFolder.startsWith("/")) {
                    foundFolder = String("/") + foundFolder;
                }
                entry.close();
                vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
                break;
            }
        }
        entry.close();
        vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
        entry = root.openNextFile();
        yield();
    }
    root.close();
    vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    
    return foundFolder; // Returns empty string if folder does not exist
}

// Fold the pre-NTP "PENDING" staging folder for a source into the real day
// folder on the first write after NTP sync, so no fake/1970 dated folders
// remain on the card. Caller must hold the SD mutex.
static void sdcardMigratePendingFolder(const String &parentFolder, const String &sourceUpper,
                                       const String &targetFolder, const char* dateSufixBuf) {
    if (!_sdMounted) return;
    String pendingFolder = parentFolder + "/" + sourceUpper + "_PENDING";
    if (pendingFolder == targetFolder) return;
    if (!SD_MMC.exists(pendingFolder.c_str())) return;
    if (SD_MMC.exists(targetFolder.c_str())) {
        // Real day folder already has data (rare clock flicker) — keep both,
        // the PENDING one is user-manageable and never auto-deleted.
        return;
    }
    if (SD_MMC.rename(pendingFolder.c_str(), targetFolder.c_str())) {
        sendVirtualMessage(String(F("🗂️ SD: pre-NTP data migrated to ")) + targetFolder, INFO);
    }
}

// Create folder if missing
bool sdcardCreateFolder(const String &folderPath) {
    if (!_sdMounted || folderPath.length() == 0) return false;
    
    // Check whether folder already exists (extra safety)
    bool folderExists = false;
    {
        File f = SD_MMC.open(folderPath.c_str(), FILE_READ);
        if (f) {
            folderExists = f.isDirectory();
            f.close();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (folderExists) {
        return true; // Already exists
    }
    
    // Try to create folder with mkdir (not FILE_WRITE, which creates a file)
    if (!SD_MMC.mkdir(folderPath.c_str())) {
        return false; // Failed to create
    }
    
    return true; // Folder created successfully
}

// 📝 Write data to the file (source: "RS232" or "LPT")
bool sdcardWriteData(const String &source, const String &data) {
    // 🧠 PSRAM Failover Logic: If in PSRAM mode, write to VFS directly
    // This allows successful return to caller, enabling Recipient Detection logic
    // (The LittleFS unload hook lives at the pipeline entry points, same TU as
    // the pre-reboot flush — see flash_fallback_store.h.)
    if (currentStorageMode == STORAGE_MODE_PSRAM) {
        if (!psramVFSIsEnabled()) {
            return false; // PSRAM full or not initialized
        }
        if (data.length() == 0) return true; // Zero-length treated as success

#if ENABLE_FILESYSTEM==1
        // FS packs (no cam): persist data to the LittleFS mirror instead of
        // volatile PSRAM VFS. flashFbWrite buffers + flushes to LittleFS on the
        // same paths EMAILDAY/SensorDay merge from. VFS stays reserved for gcode.
        bool ok = flashFbWrite(source, data.c_str(), data.length());
#else
        // Packs without LittleFS (cam): keep volatile PSRAM VFS as before.
        bool ok = psramVFSWrite(source, data.c_str(), data.length());
#endif

        // Notify callback (optional - for debug logs if needed)
        // onSdcardDataWritten(psramVFSGetTodayFolder() + "/" + source + "_001.txt", 0, data.length(), source);

        return ok;
    }

    if (!_sdMounted || data.length() == 0) return false;

    // Acquire SD mutex with extended timeout (30s) to handle concurrent EMAILDAY + RS232 load
    // Short 2s timeout caused retry queue starvation during heavy contention
    if (!sdMutexLock(pdMS_TO_TICKS(30000))) {
        static unsigned long lastMutexWarn = 0;
        if (millis() - lastMutexWarn > 10000) {
            sendVirtualMessage(String(F("⚠️ SD Write: mutex timeout after 30s (")) + source + String(F(")")), ERROR);
            sdMutexDumpState(); // Debug: who holds the mutex?
            lastMutexWarn = millis();
        }
        return false;
    }
    sdMutexTrackAcquire("SD_WRITE_DATA"); // Track this acquisition

    bool result = false;
    // ... Rest of function (use 'result' for a single return at the end to ensure mutex release)
    // Due to multiple returns in the original function, wrap the block and use goto or release the mutex before each return.
    // Alternative: manually release the mutex at each exit point.
    
    // Get current date/time. IMPORTANT: NO fabricated dates — when the clock
    // has never been valid (RTC at epoch 0 until NTP syncs) the day is unknown,
    // so the folder/filename get a "PENDING" token instead of a fake/1970 date.
    // On the first post-sync write sdcardMigratePendingFolder() folds the staged
    // PENDING folder into the real day folder.
    time_t now = time(nullptr);
    struct tm info;
    char dateSufixBuf[16];
    char dateComp[16]; // date component used inside file names (PENDING pre-sync)
    const bool clockReadyFlag = sd_layout::clockReady(now);
    if (clockReadyFlag) {
        localtime_r(&now, &info);
#if ENABLE_PROXIMITY==1 && defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
        // Waveshare pack: align the daily folder/file boundary to the last shift
        // hour so the shift report is not cut near midnight.
        proximityShiftDayAlignedDate(now, &info);
#endif
        snprintf(dateSufixBuf, sizeof(dateSufixBuf), "%02d-%02d-%04d",
                 info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
        strncpy(dateComp, dateSufixBuf, sizeof(dateComp));
        dateComp[sizeof(dateComp) - 1] = '\0';
    } else {
        memset(&info, 0, sizeof(info));
        strncpy(dateSufixBuf, "PENDING", sizeof(dateSufixBuf));
        strncpy(dateComp, dateSufixBuf, sizeof(dateComp));
    }

    // Build new hierarchical folder: /{sdcardName}/{SOURCE_DD-MM-YYYY}/
    // Structure: parent = "/{sdcardName}/", actual = "/{sdcardName}/{SOURCE}_{date}/"
    String sdcardNameBase = configManager.getSdcardName();
    if (sdcardNameBase.isEmpty()) sdcardNameBase = "SD_LOG";
    String parentFolder = String("/") + sdcardNameBase;
    String sourceUpper = source;
    sourceUpper.toUpperCase();
    String folderPath = parentFolder + String("/") + sourceUpper + String("_") + String(dateSufixBuf);

    if (clockReadyFlag) {
        sdcardMigratePendingFolder(parentFolder, sourceUpper, folderPath, dateSufixBuf);
    }

    // Create parent folder first, then the source+date subfolder
    if (!SD_MMC.exists(parentFolder.c_str())) {
        SD_MMC.mkdir(parentFolder.c_str());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!sdcardCreateFolder(folderPath)) {
        sdMutexUnlock();
        return false;
    }

    // Reset all rotation indices when the date changes (new day starts fresh).
    sdcardResetRotationOnDateChange(dateSufixBuf);
    
    // 📝 Format DAILY file name (no hour) to accumulate data
    String sourcePrefix = source;
    sourcePrefix.toLowerCase(); // lowercase for header/LPT checks
    
    // 🎯 Get chosen format (txt, csv, bin) from configuration
    String format = configManager.getSdcardFormat();
    if (format.isEmpty() || (format != "txt" && format != "csv" && format != "bin")) {
        format = "txt";
    }
    
    // Lookup (or create) the rotation entry for this source.
    SourceRotationEntry* rot = getSourceRotation(sourceUpper.c_str());
    
    // Get system name from configuration (MUST match folder name!)
    String systemName = configManager.getSystemName();
    if (systemName.isEmpty()) {
        // CRITICAL: Never fallback to source prefix - use default instead
        systemName = "Node32-HUB"; // Use default instead of sourcePrefix to match folder naming
        Serial.println(F("⚠️ SD: system_name empty, using default 'Node32-HUB'"));
    }
    
    // Replace spaces with underscores and limit length
    systemName.replace(' ', '_');
    if (systemName.length() > 20) {
        systemName = systemName.substring(0, 20);
    }
    
    // Debug: Log system name used for filename
    static unsigned long lastNameLog = 0;
    if (millis() - lastNameLog > 60000) { // Log once per minute
        Serial.printf("📝 SD: Using system name '%s' for %s files\n", systemName.c_str(), source.c_str());
        lastNameLog = millis();
    }
    
    // 📝 Build file name (system name + date)
    char fileName[128];
    if (rot && rot->index > 0) {
        snprintf(fileName, sizeof(fileName), "%s_%s_%03d.%s",
                 systemName.c_str(),
                 dateComp,
                 rot->index,
                 format.c_str());
    } else {
        snprintf(fileName, sizeof(fileName), "%s_%s.%s",
                 systemName.c_str(),
                 dateComp,
                 format.c_str());
    }
    
    // 🧠 Build path without String to avoid heap fragmentation
    char filePath[256];
    snprintf(filePath, sizeof(filePath), "%s/%s", folderPath.c_str(), fileName);
    
    // 📏 Check current file size and APPLY rotation IMMEDIATELY if needed
    // Rotation happens at 1024KB to keep SD and PSRAM VFS behavior identical.
    // By rotating immediately (not on next flush), we ensure files end on complete lines
    const uint32_t MAX_FILE_SIZE = STORAGE_ROTATED_FILE_MAX_SIZE_BYTES;
    uint32_t currentSize = 0;
    {
        File checkFile = SD_MMC.open(filePath, FILE_READ);
        if (checkFile) {
            currentSize = checkFile.size();
            checkFile.close();
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    } // File destructor runs here
    if (currentSize > 0) {
        // If file reached the rotation threshold, rotate NOW (current write goes to new file)
        if (currentSize >= MAX_FILE_SIZE && rot) {
            rot->index++;
            
            char msg[120];
            snprintf(msg, sizeof(msg), "SD: File %s reached %u KB (>= %u KB), rotating to _%03d",
                     source.c_str(), (unsigned)(currentSize / 1024),
                     (unsigned)(MAX_FILE_SIZE / 1024), rot->index);
            sendVirtualMessage(String(msg), INFO);
            
            // Rebuild filename with new index
            snprintf(fileName, sizeof(fileName), "%s_%s_%03d.%s",
                     systemName.c_str(),
                     dateComp,
                     rot->index,
                     format.c_str());
            snprintf(filePath, sizeof(filePath), "%s/%s", folderPath.c_str(), fileName);
        }
    }
    
    // 📂 Check whether file already exists and has content BEFORE opening for append
    bool fileHasContent = false;
    {
        File checkFile = SD_MMC.open(filePath, FILE_READ);
        if (checkFile) {
            fileHasContent = (checkFile.size() > 0); // ✅ File not empty
            checkFile.close();
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    } // File destructor runs here
    
    // Write with basic corruption/IO protection:
    // - If open/write/sync fails, rotate to a new numbered file and retry.
    // - This does not guarantee detecting silent data corruption, but it reliably detects I/O failures.
    size_t startOffset = 0;
    size_t endOffset = 0;
    size_t written = 0;
    bool okWrite = false;

    // If the current file is unusable, switch to a new index (e.g. _001) and continue logging.
    const int MAX_ROTATE_RETRIES = 3;
    static unsigned long lastIoWarnMs = 0;
    for (int attempt = 0; attempt < MAX_ROTATE_RETRIES; ++attempt) {
        // Rebuild the path for the current rotation index (may change on retry).
        if (rot && rot->index > 0) {
            snprintf(fileName, sizeof(fileName), "%s_%s_%03d.%s",
                     systemName.c_str(),
                     dateComp,
                     rot->index,
                     format.c_str());
        } else {
            snprintf(fileName, sizeof(fileName), "%s_%s.%s",
                     systemName.c_str(),
                     dateComp,
                     format.c_str());
        }
        snprintf(filePath, sizeof(filePath), "%s/%s", folderPath.c_str(), fileName);

        File file = SD_MMC.open(filePath, FILE_APPEND);
        if (!file) {
            char msg[128];
            snprintf(msg, sizeof(msg), "SD write: OPEN FAILED (%s) - attempt=%d/%d, rotating to _%03d",
                     filePath, attempt + 1, MAX_ROTATE_RETRIES,
                     rot ? (rot->index + 1) : 1);
            sendVirtualMessage(String(msg), ERROR);
            lastIoWarnMs = millis();
            if (rot) {
                rot->index++;
            }
            continue;
        }

        // Capture exact offsets for streaming callbacks (RS232 only).
        startOffset = file.size();
        
        // Write file header if this is a NEW file (size==0) and source is NOT LPT
        // Header format: --- Node32-Hub v2.1 - Feb  9 2026 01:04:56 WET ---
        //                IP: 192.168.1.100
        //                (blank line)
        if (startOffset == 0 && !sourcePrefix.equalsIgnoreCase("lpt")) {
            String header = buildFileHeader(source);
            if (header.length() > 0) {
                size_t headerWritten = file.print(header);
                if (headerWritten > 0) {
                    startOffset += headerWritten; // Update offset to account for header
                }
            }
        }
        
        // 🚀 OPTIMIZED WRITE: Small chunks with frequent yields to prevent RS232/LPT data loss
        // CRITICAL: RS232/LPT data arrives every ~1s and MUST be processed without delays
        // Strategy: 16KB chunks + 2ms yields = RS232/LPT processes every ~160-320ms
        // - 16KB @ 100KB/s SD = ~160ms write time (± jitter with 4-bit SDMMC)
        // - Yield 2ms between chunks = enough time for RS232 (19200 baud = ~1.9KB/s = ~3.8KB in 2s)
        // - Total overhead: ~1% extra for loop, but 99% reliability for continuous RS232/LPT reception
        // Previous: 64KB chunks blocked for 640ms-1.6s → data loss in high-load scenarios
        const size_t MAX_SAFE_WRITE_CHUNK = 16 * 1024; // 16KB chunk size (balanced performance/responsiveness)
        
        size_t totalToWrite = data.length();
        size_t totalWritten = 0;
        const char* dataPtr = data.c_str();
        bool writeSuccess = true;
        
        // Write data in chunks with cooperative yields for RS232/LPT processing
        while (totalWritten < totalToWrite && writeSuccess) {
            size_t remainingBytes = totalToWrite - totalWritten;
            size_t chunkSize = (remainingBytes > MAX_SAFE_WRITE_CHUNK) ? MAX_SAFE_WRITE_CHUNK : remainingBytes;
            
            // Write chunk
            size_t bytesWritten = file.write((const uint8_t*)(dataPtr + totalWritten), chunkSize);
            
            if (bytesWritten != chunkSize) {
                // Write failed
                file.close();
                char msg[128];
                snprintf(msg, sizeof(msg), "⚠️ SD write: CHUNK WRITE FAILED (%s) - expected=%u, wrote=%u, attempt=%d/%d", 
                         filePath, (unsigned)chunkSize, (unsigned)bytesWritten, attempt + 1, MAX_ROTATE_RETRIES);
                sendVirtualMessage(String(msg), ERROR);
                lastIoWarnMs = millis();
                if (rot) rot->index++;
                writeSuccess = false;
                break;
            }
            
            totalWritten += bytesWritten;
            
            // Cooperative yield between chunks (increased from 1ms to 2ms for RS232/LPT processing)
            // Even for last chunk, yield to prevent starvation of RS232/LPT tasks
            vTaskDelay(pdMS_TO_TICKS(2)); // 2ms yield = RS232 can process ~4 bytes (19200 baud)
        }
        
        if (!writeSuccess) {
            continue; // Try next rotation index
        }
        
        written = totalWritten;

        // Add newline for txt/csv.
        if (format != "bin") {
            size_t nw = file.print('\n');
            if (nw == 0) {
                file.close();
                if (millis() - lastIoWarnMs > 5000UL) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "SD write: newline write failed (%s), rotating", filePath);
                    sendVirtualMessage(String(msg), WARN);
                    lastIoWarnMs = millis();
                }
                if (rot) rot->index++;
                continue;
            }
            written += nw; // Include newline in total written bytes
        }

        // ✅ OPTIMIZED: Calculate endOffset mathematically instead of file.flush() + file.size()
        // file.flush() blocks for 30+ seconds on slow SD cards, causing watchdog timeouts
        // Mathematical calculation: endOffset = startOffset + totalWritten + newline (if added)
        // This is faster, non-blocking, and mathematically correct
        endOffset = startOffset + written;

        // CRITICAL FIX: Release mutex BEFORE file.close() to prevent deadlock
        // file.close() can block for 60+ seconds waiting for SD_MMC internal flush
        // Holding mutex during close caused entire system freeze (web server, RS232, etc)
        // file.close() is thread-safe in SD_MMC driver, no mutex needed
        sdMutexUnlock(); // Release early (tracking automatic)

        // ✅ SD_MMC auto-flush on close (no manual sync needed)
        // This can take 60+ seconds during heavy SD load or card write speed degradation
        file.close();
        
        // CRITICAL: Delay after close() to prevent heap corruption on File destructor
        // SD_MMC library has known bug with rapid File destruction after close()
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms - allows SD_MMC internal cleanup

        okWrite = true;
        break;
    }

    if (okWrite) {
        size_t bytesWritten = (endOffset - startOffset);
        _sdWriteCount++; // Increment session operation counter
        _sdWriteCountTotal++; // Increment total operation counter
        _sdBytesWritten += bytesWritten; // Increment session bytes written
        _sdBytesWrittenTotal += bytesWritten; // Increment total bytes written
        storNoteSdWrite(bytesWritten); // Daily bucket (NVS, 30 min persist)
        
        // ✅ DIAGNOSTIC: Log all parameters before callback condition check
        char diagBuf[180];
        snprintf(diagBuf, sizeof(diagBuf), "🔍 SD write completed: source='%s', prefix='%s', offsets=[%u-%u], bytes=%u",
                 source.c_str(), sourcePrefix.c_str(), (unsigned)startOffset, (unsigned)endOffset, 
                 (unsigned)(endOffset - startOffset));
        sendVirtualMessage(String(diagBuf), INFO);
        
        // Notify callback about the exact written range (for SD streaming email; RS232 or LPT).
        // ✅ CRITICAL: Only call callback if actually wrote data (prevents 0-byte jobs)
        if ((sourcePrefix == "rs232" || sourcePrefix == "lpt") && (endOffset > startOffset)) {
            char logBuf[120];
            snprintf(logBuf, sizeof(logBuf), "✅ SD callback condition PASSED, invoking callback for %s [%u-%u] (%u bytes)",
                     filePath, (unsigned)startOffset, (unsigned)endOffset, (unsigned)(endOffset - startOffset));
            sendVirtualMessage(String(logBuf), INFO);
            onSdcardDataWritten(String(filePath), startOffset, endOffset, source);
        } else {
            char skipBuf[120];
            snprintf(skipBuf, sizeof(skipBuf), "⚠️ SD callback SKIPPED: prefix='%s' (must be rs232/lpt), range=[%u-%u]",
                     sourcePrefix.c_str(), (unsigned)startOffset, (unsigned)endOffset);
            sendVirtualMessage(String(skipBuf), INFO);
        }
        
        // Persist to NVS every 6 hours (4x/day) to balance flash wear vs data loss
        unsigned long now = millis();
        if ((now - _lastNvsSaveTime) >= NVS_SAVE_INTERVAL_MS) {
            Preferences prefs;
            if (prefs.begin("sdcard", false)) {
                prefs.putULong("write_total", _sdWriteCountTotal);
                prefs.putULong64("bytes_total", _sdBytesWrittenTotal);
                prefs.end();
                _lastNvsSaveTime = now; // ✅ Atualizar timestamp
                Serial.printf("💾 SD Stats persisted to NVS: %lu ops, %llu bytes\n", _sdWriteCountTotal, _sdBytesWrittenTotal);
            }
        }
        
        // Mutex already released above (before file.close)
        return okWrite;
    }

    // If write failed after all retries, release mutex and return false
    sdMutexUnlock(); // Tracking automatic
    return false;
}

// 🔬 SD performance test: write and read a temporary file to measure throughput
bool sdcardSpeedTest(size_t totalBytes) {
    if (!_sdMounted) {
        sendVirtualMessage(F("⚠️ SD: not mounted for test"), WARN);
        return false;
    }

    String tmpPath = String(F("/sd_speed_test.bin"));
    // Prepare buffer in PSRAM if possible
    bool usePsram = psramFound();
    size_t chunkSize = usePsram ? 8192 : 6144;
    uint8_t* buf = nullptr;
    if (usePsram) buf = (uint8_t*) heap_caps_malloc(chunkSize, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*) malloc(chunkSize);
    if (!buf) {
        sendVirtualMessage(F("❌ SD_SPEED_TEST: failed to allocate buffer"), ERROR);
        return false;
    }

    // Fill buffer with a pattern (not zeros) to avoid internal card compression
    for (size_t i = 0; i < chunkSize; ++i) buf[i] = (uint8_t)(i & 0xFF);

    // --- Write test ---
    File f = SD_MMC.open(tmpPath.c_str(), FILE_WRITE);
    if (!f) {
        sendVirtualMessage(String(F("❌ SD_SPEED_TEST: failed to open file for writing: ")) + tmpPath, ERROR);
        if (usePsram) heap_caps_free(buf); else free(buf);
        return false;
    }

    unsigned long start = millis();
    size_t written = 0;
    while (written < totalBytes) {
        size_t toWrite = (totalBytes - written) > chunkSize ? chunkSize : (totalBytes - written);
        int n = f.write(buf, toWrite);
        if (n <= 0) { f.close(); sendVirtualMessage(F("❌ SD_SPEED_TEST: write error"), ERROR); if (usePsram) heap_caps_free(buf); else free(buf); return false; }
        written += (size_t)n;
        // For large writes, yield occasionally
        if ((written % (chunkSize * 8)) == 0) { vTaskDelay(pdMS_TO_TICKS(1)); taskYIELD(); }
    }
    f.close(); // ✅ SD_MMC flush automático ao fechar
    storNoteSdWrite(written);
    vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    unsigned long endWrite = millis();
    unsigned long writeMs = (endWrite - start) ? (endWrite - start) : 1;
    double writeMBs = ((double)written / (1024.0 * 1024.0)) / ((double)writeMs / 1000.0);

    {
        char _msg[128];
        snprintf(_msg, sizeof(_msg), "📦 SD_SPEED_TEST WRITE: %u bytes em %lu ms -> %.2f MB/s", (unsigned)written, writeMs, writeMBs);
        sendVirtualMessage(String(_msg), INFO);
    }

    // --- Read test ---
    File fr = SD_MMC.open(tmpPath.c_str(), FILE_READ);
    if (!fr) {
        sendVirtualMessage(F("❌ SD_SPEED_TEST: failed to open file for reading"), ERROR);
        if (usePsram) heap_caps_free(buf); else free(buf);
        return false;
    }

    unsigned long startRead = millis();
    size_t readBytes = 0;
    while (readBytes < written) {
        size_t toRead = ((written - readBytes) > chunkSize) ? chunkSize : (written - readBytes);
        int r = fr.read(buf, toRead);
        if (r <= 0) break;
        readBytes += (size_t)r;
        if ((readBytes % (chunkSize * 8)) == 0) { vTaskDelay(pdMS_TO_TICKS(1)); taskYIELD(); }
    }
    unsigned long endRead = millis();
    fr.close();
    vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    unsigned long readMs = (endRead - startRead) ? (endRead - startRead) : 1;
    double readMBs = ((double)readBytes / (1024.0 * 1024.0)) / ((double)readMs / 1000.0);

    {
        char _msg[128];
        snprintf(_msg, sizeof(_msg), "📦 SD_SPEED_TEST READ: %u bytes em %lu ms -> %.2f MB/s", (unsigned)readBytes, readMs, readMBs);
        sendVirtualMessage(String(_msg), INFO);
    }

    // Cleanup
    SD_MMC.remove(tmpPath.c_str());

    if (usePsram) heap_caps_free(buf); else free(buf);

    return true;
}

// 🔬 Test performance (MMC does not support multiple speeds like SPI)
// 🚀 SD_MMC usa velocidade fixa definida no begin() (25/20/10 MHz)
bool sdcardSpeedTestAll(size_t totalBytes) {
    sendVirtualMessage(F("📊 SD_SPEED_TEST: MMC mode (fixed clock speed)"), INFO);
    
    if (!_sdMounted) {
        sendVirtualMessage(F("⚠️ SD: not mounted for test"), WARN);
        return false;
    }
    
    // ✅ Run a single test at the current MMC speed
    return sdcardSpeedTest(totalBytes);
}

// Async wrapper that creates a task to run sdcardSpeedTestAll
static void sdcardSpeedTestAllTask(void *pvParameters) {
    if (!pvParameters) vTaskDelete(NULL);
    size_t totalBytes = *((size_t*)pvParameters);
    free(pvParameters);
    sdcardSpeedTestAll(totalBytes);
    vTaskDelete(NULL);
}

void sdcardSpeedTestAllAsync(size_t totalBytes) {
    size_t *p = (size_t*)malloc(sizeof(size_t));
    if (!p) { sendVirtualMessage(F("❌ SD_SPEED_TEST: failed to allocate memory for task"), ERROR); return; }
    *p = totalBytes;
    BaseType_t r = xTaskCreatePinnedToCore(sdcardSpeedTestAllTask, "sd_speed_all", 8192, p, 1, NULL, 1);
    if (r != pdPASS) {
        free(p);
        sendVirtualMessage(F("❌ SD_SPEED_TEST: failed to create task"), ERROR);
    } else {
        sendVirtualMessage(F("🔬 SD_SPEED_TEST: started in background (ALL speeds)."), INFO);
    }
}


// 🗑️ Delete folder recursively (files and subfolders)
bool sdcardDeleteFolderRecursive(const String &folderPath) {
    if (!_sdMounted || folderPath.length() == 0) return false;
    if (folderPath.indexOf("..") >= 0) return false; // 🔐 Prevenir traversal
    
    File dir = SD_MMC.open(folderPath.c_str());
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
        }
        return false;
    }
    
    // 🔄 Iterate files/folders inside
    File entry = dir.openNextFile();
    while (entry) {
        String entryName = String(entry.name());
        String entryPath = String(folderPath);
        if (!entryPath.endsWith("/")) entryPath += "/";
        entryPath += entryName;
        
        if (entry.isDirectory()) {
            // 📁 Recursion: delete subfolder
            entry.close();
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
            if (!sdcardDeleteFolderRecursive(entryPath)) {
                dir.close();
                vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
                return false;
            }
        } else {
            // 📄 Delete file
            entry.close();
            vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
            if (!SD_MMC.remove(entryPath.c_str())) {
                dir.close();
                vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
                return false;
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    vTaskDelay(pdMS_TO_TICKS(10)); // Prevent heap corruption on File destructor
    
    // 🗂️ Apagar a pasta vazia
    return SD_MMC.rmdir(folderPath.c_str());
}

// 🔄 Forces physical sync of today's RS-232/LPT files to SD card.
