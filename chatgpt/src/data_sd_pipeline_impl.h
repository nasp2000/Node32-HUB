// data_sd_pipeline_impl.h
// Included only from chatgpt.ino (single TU).
// Data streaming pipeline and SD card buffer logging:
// hasRecipientMatchStreaming, logDataToSdcard, appendRs232LivePoints,
// addDataToSdcardBuffer*, flushSdcardBuffers.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
// ============================================================================
#include "sd_layout.h"   // standard SD layout: flat day-files, dated subdirs, aggregates
#include "flash_fallback_store.h" // no-SD LittleFS writers + email collect (stub when disabled)
#include "chart_ring_serve.h" // PSRAM-first serve math shared by all .chart endpoints
// QUEUE PROCESSOR TASK - Runs independently every 5s
// GARANTEE: Emails in queue are ALWAYS processed, even without RS232/LPT data
// ============================================================================
// outMatchedRules: receives "|"-separated names of rules that actually matched in data.
// These come from findRecipientsInLowerText on the real RS232/LPT payload, NOT from
// collectMatchedRulesFromContent (which scans the full accumulated buffer including the
// file header and can produce false positives for colN rules against date tokens).
static bool hasRecipientMatchStreaming(const String &data, String &outMatchedRules) {
    if (data.length() == 0) return false;
    
    // CRITICAL FIX: Remove inline timestamp prefix (YYYY-MM-DD HH:MM:SS\t) before matching
    // Timestamps are for display/storage only, should NOT affect recipient matching
    String dataToMatch = data;
    int tabPos = data.indexOf('\t');
    if (tabPos > 0 && tabPos < 25) { // Timestamp format: 19 chars + tab (YYYY-MM-DD HH:MM:SS\t)
        // Verify it looks like a timestamp: YYYY-MM-DD format at start
        if (data.length() >= 10 && 
            data[4] == '-' && data[7] == '-' && 
            isDigit(data[0]) && isDigit(data[1]) && isDigit(data[2]) && isDigit(data[3])) {
            // Skip timestamp prefix, use only the data after tab
            dataToMatch = data.substring(tabPos + 1);
            
            // DEBUG: Log timestamp removal (ALWAYS VISIBLE at WARN level)
            static unsigned long lastTimestampRemovalLog = 0;
            if (millis() - lastTimestampRemovalLog > 30000) { // Every 30s
                safeEnviarMensagemVirtual("[RECIP_MATCH] Timestamp prefix removed for matching (len: %d -> %d)", WARN,  (int)data.length(), (int)dataToMatch.length());
                lastTimestampRemovalLog = millis();
            }
        }
    }

    String searchLower = dataToMatch;
    searchLower.toLowerCase();

    // Use the exact same matcher as SD/email-prep so streaming and SD follow
    // one rule engine for tokens, CSV keys, colN, COUNT, Cw and hysteresis.
    std::vector<std::pair<String, String>> matchedRecipients;
    findRecipientsInLowerText(searchLower, matchedRecipients, configManager.getSdcardEnabled());

    if (!matchedRecipients.empty()) {
        String firstRule = matchedRecipients.front().first;
        if (firstRule.length() > 0) {
            sendVirtualMessage(String(F("✅ Streaming match ALLOWED: ")) + firstRule, WARN);
        }
        // Populate outMatchedRules with the rules found in this single data line.
        // These are used by the caller to build the window-level accumulated rule list
        // (streamingMatchedRulesAccum), which is later passed directly to
        // enqueueStreamingEmail — bypassing collectMatchedRulesFromContent.
        for (auto &p : matchedRecipients) {
            appendRulesToAccum(p.first, outMatchedRules);
        }
        return true;
    }

    return false;
}

// ============================================================================
// UNIFIED STORAGE API - Abstraction layer for SD / PSRAM backends
// ============================================================================

// List files in a folder (works with SD, PSRAM or Flash).
// Read order is always SD first, then the no-SD tiers: PSRAM (hot) merged
// with the LittleFS fallback mirror (dedupe by name, hot tier wins).
std::vector<StorageFileInfo> storageListFiles(const String &folderPath) {
    std::vector<StorageFileInfo> files;

    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        // Read from SD card
        File dir = SD.open(folderPath.c_str());
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                StorageFileInfo info;
                info.name = String(f.name());
                info.size = f.size();
                info.backend = STORAGE_BACKEND_SD;
                files.push_back(info);
                f.close();
                f = dir.openNextFile();
            }
            dir.close();
        }
    } else if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled()) {
        // Read from PSRAM VFS
        std::vector<String> fileNames = psramVFSListFiles(folderPath);
        for (const String& fname : fileNames) {
            StorageFileInfo info;
            info.name = fname;
            // Get file size from PSRAM (using size helper, avoid full read)
            info.size = psramVFSGetFileSize(folderPath, fname);
            info.backend = STORAGE_BACKEND_PSRAM;
            files.push_back(info);
        }
    }

    // Flash fallback mirror (no-SD only): merge persisted files.
    // Bulk overlap (same content as VFS) is skipped once the hot tier yielded
    // files; /oee + /sensor_history never overlap and always merge.
    if (!sdcardShouldUseSD()) {
        std::vector<StorageFileInfo> flashFiles;
        flashFbCollectEmailFiles(folderPath, "", flashFiles, files.empty());
        for (auto& fi : flashFiles) {
            bool dup = false;
            for (auto& e : files) {
                if (e.name == fi.name) { dup = true; break; }
            }
            if (!dup) files.push_back(fi);
        }
    }

    return files;
}

// Read entire file content (works with SD, PSRAM or Flash).
static String storageReadFile(const String &folderPath, const String &filename) {
    String content;
    
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        // Read from SD card
        String fullPath = folderPath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += filename;
        
        File f = SD.open(fullPath.c_str(), FILE_READ);
        if (f) {
            size_t fsize = f.size();
            if (fsize > 0) {
                content.reserve(fsize + 1);
                while (f.available()) {
                    content += (char)f.read();
                }
            }
            f.close();
        }
    } else if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled()) {
        // Read from PSRAM VFS
        content = psramVFSReadFile(folderPath, filename);
    }

    // Flash fallback mirror (no-SD only): same path, then /oee basename.
    if (content.isEmpty() && !sdcardShouldUseSD()) {
        String fullPath = folderPath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += filename;
        String flashPath;
        if (flashFbFindFile(fullPath, flashPath)) {
            File f = LittleFS.open(flashPath.c_str(), FILE_READ);
            if (f) {
                size_t fsize = f.size();
                if (fsize > 0 && fsize <= 200000UL) {
                    content.reserve(fsize + 1);
                    while (f.available()) {
                        content += (char)f.read();
                    }
                }
                f.close();
            }
        }
    }

    return content;
}

// ============================================================================

// Helper to write data to SD card based on configuration
// Supports formatting by structures (ISQ20) in tabular format
// NEW: Triggers immediate email if recipient match (streaming)
// timestampRecepcao_ms: millis() of the moment of the last real reception (for accurate timestamp)
static void logDataToSdcard(const String &source, const String &data, unsigned long timestampRecepcao_ms, bool forceRaw) {
#if ENABLE_SDCARD==1
    // Check if SD card logging is enabled
    if (!configManager.getSdcardEnabled()) return;
    
    // Check if data is not empty
    if (data.length() == 0) return;
    
    // Detect day change and reset header flags
    static int lastDayOfYear = -1;
    time_t nowRaw = time(nullptr);
    struct tm nowInfo;
    localtime_r(&nowRaw, &nowInfo);
    if (nowRaw > 100000) { // Only check if RTC is valid
        int currentDayOfYear = nowInfo.tm_yday;
        if (lastDayOfYear != -1 && currentDayOfYear != lastDayOfYear) {
            // New day! Reset header flags to write a new header
            sdcardLineCount_MTF1 = 0;
            sdcardLineCount_AWSE = 0;
        }
        lastDayOfYear = currentDayOfYear;
    }
    
    // Decide whether to format by structures
    String dataToLog = data;
    bool isLPT = source.equalsIgnoreCase("LPT");
    bool wantStructure = configManager.getSdcardStructured() && !forceRaw;
    // When structures are active, ALWAYS include timestamp (independent of email config)
    bool wantTimestamp = (wantStructure || configManager.getEmailIncludeTimestamp()) && !isLPT && !forceRaw; 
    
    // We enter the processing block if we want structure OR timestamps
    bool processingMode = wantStructure || wantTimestamp;
    
    if (processingMode) {
        // Streaming processing: avoid fixed arrays and support 'infinite' lines (limited by buffer RAM)
        // Calculate the real reception timestamp (not the write time)
        time_t nowTs = time(nullptr);
        if (timestampRecepcao_ms > 0) {
            unsigned long millisDecorridos = millis() - timestampRecepcao_ms;
            nowTs -= (millisDecorridos / 1000); // Subtract elapsed seconds
        }
        struct tm timeinfo;
        localtime_r(&nowTs, &timeinfo);
        char tsBuf[16];
        strftime(tsBuf, sizeof(tsBuf), "%H:%M:%S", &timeinfo);
        // Check if timestamp should be included (LPT never gets timestamps; RS232 gets when enabled)
        String timestamp = (wantTimestamp && nowTs > 100000) ? String(tsBuf) : "";

        // 1. Auto-detect: analyze the first lines to identify structure
        int mtf1Count = 0, awseCount = 0;
        size_t scanPos = 0;
        int linesScanned = 0;
        while (scanPos < data.length() && linesScanned < 100) {
            int nextEol = data.indexOf('\n', scanPos);
            if (nextEol == -1) nextEol = data.length();
            String line = data.substring(scanPos, nextEol);
            line.trim();
            if (!isHeaderLine(line)) {
                if (looksLikeISQ20_MTF1(line)) mtf1Count++;
                if (looksLikeISQ20_AWSE(line)) awseCount++;
                linesScanned++;
            }
            if (nextEol >= (int)data.length()) break;
            scanPos = nextEol + 1;
        }

        // Auto-Detect adjustment: for small streams (1-4 lines), use a reduced threshold
        // If linesScanned < 5, threshold=0 (1 match is enough). Otherwise, require >2 for robustness.
        int threshold = (linesScanned < 5) ? 0 : 2;
        
        bool useMtf1 = false, useAwse = false;
        if (wantStructure) {
            useMtf1 = (configManager.getAmadaISQ20MTF1Enabled() || (configManager.getStructuresAutoDetectEnabled() && mtf1Count > (awseCount + threshold)));
            useAwse = !useMtf1 && (configManager.getAmadaISQ20AWSEEnabled() || (configManager.getStructuresAutoDetectEnabled() && awseCount > 0));
        }

        // 2. Streaming processing: build the result without storing intermediate lines in an array
        String processed;
        // Reserve space to avoid reallocations: base + margin for timestamps/headers
        size_t reserveSize = data.length() + (timestamp.length() > 0 ? (data.length()/40)*20 : 512);
        processed.reserve(reserveSize);
        
        // Always use tabular format (CSV format option removed)
        bool useRecipientFormat = false;
        
        bool hasDataLines = false;
        size_t currentPos = 0;
        while (currentPos < data.length()) {
            int nextEol = data.indexOf('\n', currentPos);
            if (nextEol == -1) nextEol = data.length();
            String line = data.substring(currentPos, nextEol);
            line.trim();

            if (!isHeaderLine(line)) {
                hasDataLines = true;
                if (useMtf1) {
                    String rowData;
                    bool validRow = false;
                    if (useRecipientFormat) {
                        // CSV format: no headers, comma-separated key-value pairs
                        validRow = buildISQ20_MTF1_CSVRow(line, rowData);
                    } else {
                        // Tabular format: headers every 20 lines, tab-separated columns
                        if (sdcardLineCount_MTF1 % 20 == 0) { processed += buildISQ20_MTF1_TabularHeader(); processed += "\n"; }
                        validRow = buildISQ20_MTF1_TabularRow(line, rowData, timestamp);
                        if (validRow) sdcardLineCount_MTF1++;
                    }
                    if (validRow) { processed += rowData; processed += "\n"; }
                } else if (useAwse) {
                    String rowData;
                    bool validRow = false;
                    if (useRecipientFormat) {
                        // CSV format: no headers, comma-separated key-value pairs
                        validRow = buildISQ20_AWSE_CSVRow(line, rowData);
                    } else {
                        // Tabular format: headers every 20 lines, tab-separated columns
                        if (sdcardLineCount_AWSE % 20 == 0) { processed += buildISQ20_AWSE_TabularHeader(); processed += "\n"; }
                        validRow = buildISQ20_AWSE_TabularRow(line, rowData, timestamp);
                        if (validRow) sdcardLineCount_AWSE++;
                    }
                    if (validRow) { processed += rowData; processed += "\n"; }
                } else {
                    // Modo Normal: Timestamp + Dados ou apenas Dados filtrados
                    if (timestamp.length() > 0) {
                        processed += timestamp; processed += "\t"; processed += line; processed += "\n";
                    } else {
                        processed += line; processed += "\n";
                    }
                }
            }
            if (nextEol >= (int)data.length()) break;
            currentPos = nextEol + 1;
            if (processed.length() % 2048 == 0) yield(); // ? Manter watchdog ativo em blocos grandes
        }

        if (!hasDataLines) return; // Nothing useful to write
        dataToLog = processed;
    }
    
    // ============================================================================
    // STORAGE AUTO-FAILOVER: SD → PSRAM → NONE
    // ============================================================================
    // In sniffer mode the only persistence target is LittleFS (30s periodic flush).
    // SD and PSRAM VFS are not used to avoid mixing sniffer traffic with normal data.
#if ENABLE_SNIFFER==1
    return;
#endif

    checkAndSwitchStorageMode();  // Auto-detect SD availability and switch mode
    
    // Write data based on current storage mode
    bool ok = false;
    
    if (currentStorageMode == STORAGE_MODE_SD) {
        // PRIMARY: Write to SD card
        ok = sdcardWriteData(source, dataToLog);
        if (!ok) {
            // SD write failed - log and try PSRAM fallback
            char errBuf[120];
            snprintf(errBuf, sizeof(errBuf), "⚠️ SD WRITE FAILED: source=%s, %u bytes - trying PSRAM fallback",
                     source.c_str(), (unsigned)dataToLog.length());
            sendVirtualMessage(String(errBuf), WARN);
            
            // Force re-check and potentially switch to PSRAM
            checkAndSwitchStorageMode();
        }
    }
    
    if (!ok && currentStorageMode == STORAGE_MODE_PSRAM) {
        // FALLBACK: no SD mounted. FS packs persist data to the LittleFS mirror
        // (VFS stays reserved for gcode); packs without LittleFS keep VFS.
#if ENABLE_FILESYSTEM==1
        ok = flashFbWrite(source, dataToLog.c_str(), dataToLog.length());
#else
        ok = psramVFSWrite(source, dataToLog.c_str(), dataToLog.length());
#endif
        if (!ok) {
            char errBuf[120];
            snprintf(errBuf, sizeof(errBuf), "⚠️ FALLBACK WRITE FAILED: source=%s, %u bytes",
                     source.c_str(), (unsigned)dataToLog.length());
            sendVirtualMessage(String(errBuf), ERROR);
            return;
        }
    }
    
    if (!ok && currentStorageMode == STORAGE_MODE_NONE) {
        // No storage available - data discarded
        static unsigned long lastDiscardWarning = 0;
        unsigned long now = millis();
        if (now - lastDiscardWarning > 30000) {  // Warn every 30s
            sendVirtualMessage(F("⚠️ NO STORAGE: Data being discarded (no SD, no PSRAM)"), ERROR);
            lastDiscardWarning = now;
        }
        return;
    }
    
    // NOTE: STREAMING EMAIL detection now happens at buffer entry (addDataToSdcardBuffer*)
    // Email is sent with a COPY of matched line, original line continues here to SD/PSRAM with timestamp
    // This ensures immediate detection (no 30min delay) while preserving original data flow
    
    // Note: If Email SD is disabled, system keeps the normal RAM-based sending (inactivity/lines/FF)
#endif
}

// NEW: Function to write data with individual timestamps per chunk
// Each chunk has its own reception timestamp, preserving correct timing on the SD card
static void appendRs232LivePoints(const String &processedData);
static size_t logDataToSdcardTimestamped(const String &source, std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>& dataChunks) {
#if ENABLE_SDCARD==1
    if (!configManager.getSdcardEnabled()) return dataChunks.size(); // SDCARD DISABLED: Discard data but return success to empty buffers
    if (dataChunks.empty()) return 0;
    
    // Detect day change and reset header flags
    static int lastDayOfYear = -1;
    time_t nowRaw = time(nullptr);
    struct tm nowInfo;
    localtime_r(&nowRaw, &nowInfo);
    if (nowRaw > 100000) {
        int currentDayOfYear = nowInfo.tm_yday;
        if (lastDayOfYear != -1 && currentDayOfYear != lastDayOfYear) {
            sdcardLineCount_MTF1 = 0;
            sdcardLineCount_AWSE = 0;
        }
        lastDayOfYear = currentDayOfYear;
    }
    
    bool isLPT = source.equalsIgnoreCase("LPT");
    bool wantStructure = configManager.getSdcardStructured();
    // When structures are active, ALWAYS include timestamp (independent of email config)
    bool wantTimestamp = (wantStructure || configManager.getEmailIncludeTimestamp()) && !isLPT;
    bool processingMode = wantStructure || wantTimestamp;
    
    // Process each chunk with its own timestamp.
    // During EMAILDAY freeze, do not write anything beyond the cutoff sequence.
    for (size_t chunkIndex = 0; chunkIndex < dataChunks.size(); ++chunkIndex) {
        auto &chunk = dataChunks[chunkIndex]; // Non-const to allow dataAsString() if needed
        if (chunk.dataLength() == 0) {
            continue;
        }

        // Yield periodically to prevent starving IDLE0 (task watchdog).
        if ((chunkIndex % 8) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (sdcardFreezeWrites) {
            uint32_t cutoff = 0;
            if (source.equalsIgnoreCase("RS232")) cutoff = sdcardFreezeCutoffSeq_RS232;
            else if (source.equalsIgnoreCase("LPT")) cutoff = sdcardFreezeCutoffSeq_LPT;
            if (cutoff != 0 && chunk.seq != 0 && chunk.seq > cutoff) {
                return chunkIndex;
            }
        }
        
        // Convert PSRAM vector to String for processing (one-time allocation per chunk, not per line)
        String dataToLog = chunk.dataAsString();
        
        if (processingMode) {
            // Calculate real timestamp for this specific chunk (not the write time)
            time_t nowTs = time(nullptr);
            if (chunk.timestamp_ms > 0) {
                unsigned long millisDecorridos = millis() - chunk.timestamp_ms;
                nowTs -= (millisDecorridos / 1000);
            }
            struct tm timeinfo;
            localtime_r(&nowTs, &timeinfo);
            char tsBuf[16];
            strftime(tsBuf, sizeof(tsBuf), "%H:%M:%S", &timeinfo);
            // Check if timestamp should be included (LPT never gets timestamps; RS232 gets when enabled)
            String timestamp = (wantTimestamp && nowTs > 100000) ? String(tsBuf) : "";
            
            // Auto-detection (similar to the original function)
            int mtf1Count = 0, awseCount = 0;
            size_t scanPos = 0;
            int linesScanned = 0;
            while (scanPos < dataToLog.length() && linesScanned < 100) {
                int nextEol = dataToLog.indexOf('\n', scanPos);
                if (nextEol == -1) nextEol = dataToLog.length();
                String line = dataToLog.substring(scanPos, nextEol);
                line.trim();
                if (!isHeaderLine(line)) {
                    if (looksLikeISQ20_MTF1(line)) mtf1Count++;
                    if (looksLikeISQ20_AWSE(line)) awseCount++;
                    linesScanned++;
                }
                if (nextEol >= (int)dataToLog.length()) break;
                scanPos = nextEol + 1;
            }
            
            int threshold = (linesScanned < 5) ? 0 : 2;
            bool useMtf1 = false, useAwse = false;
            
            if (wantStructure) {
                useMtf1 = (configManager.getAmadaISQ20MTF1Enabled() || (configManager.getStructuresAutoDetectEnabled() && mtf1Count > (awseCount + threshold)));
                useAwse = !useMtf1 && (configManager.getAmadaISQ20AWSEEnabled() || (configManager.getStructuresAutoDetectEnabled() && awseCount > 0));
            }
            
            // Processar linhas com timestamp deste chunk
            String processed;
            size_t reserveSize = dataToLog.length() + (timestamp.length() > 0 ? (dataToLog.length()/40)*20 : 512);
            processed.reserve(reserveSize);
            
            // Always use tabular format (CSV format option removed)
            bool useRecipientFormat = false;
            
            bool hasDataLines = false;
            size_t currentPos = 0;
            unsigned int linesProcessed = 0;
            while (currentPos < dataToLog.length()) {
                int nextEol = dataToLog.indexOf('\n', currentPos);
                if (nextEol == -1) nextEol = dataToLog.length();
                String line = dataToLog.substring(currentPos, nextEol);
                line.trim();
                
                if (!isHeaderLine(line)) {
                    hasDataLines = true;
                    if (useMtf1) {
                        String rowData;
                        bool validRow = false;
                        if (useRecipientFormat) {
                            // CSV format: no headers, comma-separated key-value pairs
                            validRow = buildISQ20_MTF1_CSVRow(line, rowData, timestamp);
                        } else {
                            // Tabular format: headers every 20 lines, tab-separated columns
                            if (sdcardLineCount_MTF1 % 20 == 0) { processed += buildISQ20_MTF1_TabularHeader(); processed += "\n"; }
                            validRow = buildISQ20_MTF1_TabularRow(line, rowData, timestamp);
                            if (validRow) sdcardLineCount_MTF1++;
                        }
                        if (validRow) { processed += rowData; processed += "\n"; }
                    } else if (useAwse) {
                        String rowData;
                        bool validRow = false;
                        if (useRecipientFormat) {
                            // CSV format: no headers, comma-separated key-value pairs
                            validRow = buildISQ20_AWSE_CSVRow(line, rowData, timestamp);
                        } else {
                            // Tabular format: headers every 20 lines, tab-separated columns
                            if (sdcardLineCount_AWSE % 20 == 0) { processed += buildISQ20_AWSE_TabularHeader(); processed += "\n"; }
                            validRow = buildISQ20_AWSE_TabularRow(line, rowData, timestamp);
                            if (validRow) sdcardLineCount_AWSE++;
                        }
                        if (validRow) { processed += rowData; processed += "\n"; }
                    } else {
                        // Modo Normal: Timestamp + Dados
                        if (timestamp.length() > 0) {
                            processed += timestamp; processed += "\t"; processed += line; processed += "\n";
                        } else {
                            processed += line; processed += "\n";
                        }
                    }
                }
                if (nextEol >= (int)dataToLog.length()) break;
                currentPos = nextEol + 1;
                if ((++linesProcessed % 20) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
            
            if (!hasDataLines) {
                continue;
            }
            dataToLog = processed;
        }
        
        // ✅ CRITICAL: Skip empty chunks before writing (prevents 0-byte callbacks)
        if (dataToLog.length() == 0) {
            continue;
        }

        if (source.equalsIgnoreCase("RS232")) {
            appendRs232LivePoints(dataToLog);
        }
        
        // Write this chunk to SD. On failure, stop so caller can retry without reordering.
        bool ok = sdcardWriteData(source, dataToLog);
        if (!ok) {
            return chunkIndex;
        }
        
        // NOTE: STREAMING EMAIL detection now happens at buffer entry (addDataToSdcardBuffer*)
        // This ensures immediate detection and email sending (no delay from flush intervals)
        // Email is sent with a COPY of matched line, original continues here to SD/PSRAM
    }
#endif

    return dataChunks.size();
}

static void appendRs232LivePoints(const String &processedData) {
    if (rs232LiveMutex == NULL || processedData.length() == 0) return;
    if (xSemaphoreTakeRecursive(rs232LiveMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    time_t nowTs = time(nullptr);
    struct tm nowInfo = {};
    localtime_r(&nowTs, &nowInfo);
    nowInfo.tm_hour = 0;
    nowInfo.tm_min = 0;
    nowInfo.tm_sec = 0;
    time_t midnightTs = mktime(&nowInfo);

    ChartRs232Writer chartWriter;
    bool chartWriterReady = false;
    String chartPath;
    if (configManager.getChartsPageEnabled()) {
        chartPath = chartHistoryRs232PathForTm(configManager.getSystemName(), nowInfo);
    }
    // Uniform persisted cadence: on LittleFS (no SD) the .chart mirror is
    // throttled to the configured flush interval — unthrottled per-line
    // appends would fill the unbounded daily file (and then the 1MB reserve
    // gate would stop ALL flash writes) within days. The live ring above
    // always runs (recent detail); SD behavior is unchanged.
    static unsigned long lastFlashChartMs = 0;
    const bool chartUseFlash = !chartPath.isEmpty() && !sdcardShouldUseSD();
    bool wantChart = !chartPath.isEmpty();
    if (chartUseFlash && lastFlashChartMs != 0 &&
        (millis() - lastFlashChartMs) < flashFbWindowMs()) {
        wantChart = false;
    }

    const char *data = processedData.c_str();
    size_t totalLen = processedData.length();
    size_t pos = 0;
    char lineBuf[512];
    // Serialized under lfsMutex when writing the flash mirror (same crash
    // guard as the email sender); the whole chunk shares one hold, released
    // at close below. On contention the mirror write is skipped for this
    // chunk (live ring + next chunk cover it). SD path untouched.
    bool rsLfsHeld = false;
    if (wantChart && chartUseFlash) {
#if ENABLE_FILESYSTEM==1
        extern bool lfsMutexLock(TickType_t timeoutTicks);
        rsLfsHeld = lfsMutexLock(pdMS_TO_TICKS(5000));
#endif
        if (!rsLfsHeld) wantChart = false;
    }
    while (pos < totalLen) {
        size_t lineLen = 0;
        while (pos < totalLen && lineLen < sizeof(lineBuf) - 1) {
            char ch = data[pos++];
            if (ch == '\n') break;
            if (ch != '\r') lineBuf[lineLen++] = ch;
        }
        while (pos < totalLen && data[pos] != '\n' && lineLen >= sizeof(lineBuf) - 1) pos++;
        if (pos < totalLen && data[pos] == '\n') pos++;
        lineBuf[lineLen] = '\0';
        if (lineLen == 0) continue;

        const char *payload = lineBuf;
        int hh = -1, mm = -1, ss = -1;
        if (lineLen >= 8 && isdigit((unsigned char)lineBuf[0]) && isdigit((unsigned char)lineBuf[1]) &&
            lineBuf[2] == ':' && isdigit((unsigned char)lineBuf[3]) && isdigit((unsigned char)lineBuf[4]) &&
            lineBuf[5] == ':' && isdigit((unsigned char)lineBuf[6]) && isdigit((unsigned char)lineBuf[7])) {
            hh = (lineBuf[0] - '0') * 10 + (lineBuf[1] - '0');
            mm = (lineBuf[3] - '0') * 10 + (lineBuf[4] - '0');
            ss = (lineBuf[6] - '0') * 10 + (lineBuf[7] - '0');
            if (lineBuf[8] == '\t' || lineBuf[8] == ',') payload = lineBuf + 9;
            else payload = lineBuf + 8;
        }
        while (*payload == ' ' || *payload == ',' || *payload == '\t') payload++;
        if (*payload == '\0') continue;

        Rs232LivePoint point;
        point.timestamp = (hh >= 0) ? (uint32_t)(midnightTs + hh * 3600 + mm * 60 + ss) : (uint32_t)nowTs;

        const char *cur = payload;
        while (*cur && point.valueCount < RS232_LIVE_MAX_COLS) {
            while (*cur == ' ' || *cur == ',' || *cur == '\t') cur++;
            if (!*cur) break;
            char *endPtr = nullptr;
            float value = strtof(cur, &endPtr);
            if (endPtr == cur) break;
            point.values[point.valueCount++] = value;
            cur = endPtr;
        }
        if (point.valueCount == 0) continue;
        rs232LiveBuffer.push(point);

        if (wantChart) {
            if (!chartWriterReady) {
                uint8_t chartColumnCount = point.valueCount;
                ChartColumnDef existingDefs[CHART_HISTORY_RS232_MAX_COLUMNS] = {};
                uint8_t existingColumnCount = 0;
                if (chartHistoryReadRs232Meta(chartPath, existingColumnCount, existingDefs) && existingColumnCount > 0) {
                    chartColumnCount = existingColumnCount;
                }
                chartWriterReady = chartHistoryOpenRs232Writer(chartWriter, chartPath,
                                                               chartColumnCount,
                                                               (uint32_t)midnightTs);
            }
            if (chartWriterReady) {
                Rs232ChartRecord record;
                record.timestamp = point.timestamp;
                record.valueCount = point.valueCount < chartWriter.columnCount ? point.valueCount : chartWriter.columnCount;
                for (uint8_t i = 0; i < record.valueCount && i < CHART_HISTORY_RS232_MAX_COLUMNS; ++i) {
                    record.values[i] = point.values[i];
                }
                chartHistoryAppendRs232Record(chartWriter, record);
            }
        }
    }

    if (chartWriterReady) {
        chartHistoryCloseRs232Writer(chartWriter);
        if (chartUseFlash) lastFlashChartMs = millis();
    }
#if ENABLE_FILESYSTEM==1
    if (rsLfsHeld) {
        extern void lfsMutexUnlock();
        lfsMutexUnlock();
    }
#endif

    xSemaphoreGiveRecursive(rs232LiveMutex);
}

// IMPROVEMENT 3: Add data to the active buffer and flush via instant swap
// Swap buffers (<1ms) + notify dedicated task for asynchronous writing
static void addDataToSdcardBuffer(const String &source, const String &data) {
    addDataToSdcardBufferTimestamped(source, data, millis());
}

// --- Modbus chart accumulator ---
// Aggregates register values from infrequent per-register SD log calls into a
// single chart record that is flushed to a .chart file every FLUSH_INTERVAL.
// Parses minimal SD log format: "mb_<slaveId>_<label>, <value>"
// Pretty-format lines (start with '[') are skipped: they are harder to parse
// and contain the same data already captured as minimal lines.
// Standard chart persistence (all packs): volatile 1-min PSRAM ring (~30 min,
// always on, no configuration) + persisted mirror — SD *and* LittleFS follow
// the configured flush interval (default 30m, same rule as the CSV ticks) at
// the IDENTICAL path. APIs serve file history + ring tail merged.
#define _CHART_RING_CAP        32      // ~30 min @ 1/min + margin (always on, no configuration)
#define _CHART_RING_PUSH_MS    60000UL // ring sample cadence (recent detail)
// Persisted mirror cadence follows the configured flush interval
// (flashFbWindowMs(): 10m/30m/60m, default 30m) — never fixed.
#if ENABLE_RS232_CHARTS==1

#define _MB_CHART_MAX_COLS            32
#define _MB_CHART_FLUSH_INTERVAL_MS   60000UL

static struct {
    char          keys[_MB_CHART_MAX_COLS][20]; // full key e.g. "mb_1_tensao"
    float         values[_MB_CHART_MAX_COLS];   // latest accumulated value per key
    uint8_t       count;
    bool          schemaFrozen;                 // true after first flush; prevents column changes mid-day
    unsigned long lastFlushMs;
    unsigned long lastPersistMs;                // persisted-write throttle (configured interval)
    char          lastDateId[11];               // DD-MM-YYYY (last flush day)
} _mbChartAccum = {};

static PsramCircularBuffer<ModbusChartRecord> _mbChartRing(_CHART_RING_CAP, true);
static SemaphoreHandle_t _mbChartRingMutex = NULL;
static unsigned long _mbChartRingLastMs = 0;

// Find or create a column slot for the given key.
// Returns -1 if the table is full or the schema is frozen (day-locked).
static int _mbChartFindOrAdd(const char* key) {
    for (int i = 0; i < (int)_mbChartAccum.count; i++) {
        if (strncmp(_mbChartAccum.keys[i], key, sizeof(_mbChartAccum.keys[0]) - 1) == 0) return i;
    }
    if (_mbChartAccum.count >= _MB_CHART_MAX_COLS || _mbChartAccum.schemaFrozen) return -1;
    int idx = (int)_mbChartAccum.count++;
    strncpy(_mbChartAccum.keys[idx], key, sizeof(_mbChartAccum.keys[0]) - 1);
    _mbChartAccum.keys[idx][sizeof(_mbChartAccum.keys[0]) - 1] = '\0';
    _mbChartAccum.values[idx] = NAN;
    return idx;
}

// Write one ModbusChartRecord from the current accumulator state and close the writer.
// Uniform persisted cadence: SD *and* LittleFS follow the configured flush
// interval (default 30m) at the IDENTICAL path.
static void _mbChartDoFlush() {
    if (_mbChartAccum.count == 0) return;

    time_t now = time(nullptr);
    if (!sd_layout::clockReady(now)) return; // no fabricated dates pre-NTP; first post-sync flush lands in the right day

    // Uniform persisted cadence (SD *and* LittleFS follow the configured flush
    // interval, default 30m — same rule as the CSV ticks). The 1-min PSRAM
    // ring below is the always-on recent detail (no configuration).
    if ((millis() - _mbChartAccum.lastPersistMs) < flashFbWindowMs() && _mbChartAccum.lastPersistMs != 0) {
        return;
    }
    const bool useFlash = !sdcardShouldUseSD();
    struct tm ti;
    localtime_r(&now, &ti);

    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);

    // New day: unfreeze schema so new registers are accepted.
    if (strncmp(_mbChartAccum.lastDateId, dateId, 10) != 0) {
        strncpy(_mbChartAccum.lastDateId, dateId, 10);
        _mbChartAccum.schemaFrozen = false;
    }

    // Build ChartColumnDef[] from accumulated key names.
    ChartColumnDef defs[_MB_CHART_MAX_COLS] = {};
    for (uint8_t i = 0; i < _mbChartAccum.count; i++) {
        strncpy(defs[i].key, _mbChartAccum.keys[i], sizeof(defs[i].key) - 1);
        // Human label: strip "mb_<digits>_" prefix if present.
        const char* p = _mbChartAccum.keys[i];
        if (p[0] == 'm' && p[1] == 'b' && p[2] == '_') {
            p += 3;
            while (*p && *p != '_') p++;
            if (*p == '_') p++;
        }
        strncpy(defs[i].label, (*p ? p : _mbChartAccum.keys[i]), sizeof(defs[i].label) - 1);
        defs[i].dataType = CHART_HISTORY_DATA_FLOAT32;
        defs[i].decimals = 3;
        defs[i].scale    = 1.0f;
    }

    String chartPath = chartHistoryModbusPathForDate(configManager.getSystemName(), String(dateId));
    uint32_t dayStart = chartHistoryDayStartFromRs232DateId(String(dateId));

    ChartRs232Writer writer;
    bool opened = false;
    bool mbLfsHeld = false;
    if (useFlash) {
        // Identical path on LittleFS (parity); 1MB /config reserve enforced.
        // Serialized under lfsMutex (same crash guard); skip on contention.
#if ENABLE_FILESYSTEM==1
        extern bool lfsMutexLock(TickType_t timeoutTicks);
        extern void lfsMutexUnlock();
        mbLfsHeld = lfsMutexLock(pdMS_TO_TICKS(5000));
#endif
        if (mbLfsHeld && littlefsBegin() && littlefsMounted() && littlefsFreeBytes() >= (1024ULL * 1024ULL)) {
            opened = chartHistoryOpenModbusWriterFS(writer, getLittleFS(), chartPath, defs, _mbChartAccum.count, dayStart);
        }
    } else {
        opened = chartHistoryOpenModbusWriter(writer, chartPath, defs, _mbChartAccum.count, dayStart);
    }
    if (opened) {
        _mbChartAccum.schemaFrozen = true;

        ModbusChartRecord rec;
        rec.timestamp  = (uint32_t)now;
        rec.valueCount = _mbChartAccum.count;
        for (uint8_t i = 0; i < _mbChartAccum.count; i++) {
            rec.values[i] = _mbChartAccum.values[i];
        }
        chartHistoryAppendModbusRecord(writer, rec);
        chartHistoryCloseRs232Writer(writer);
#if ENABLE_FILESYSTEM==1
        if (mbLfsHeld) lfsMutexUnlock();
#endif
        if (useFlash) {
            storNoteLfsWrite(sizeof(uint32_t) + 1 + 3 + (size_t)_mbChartAccum.count * sizeof(float));
        } else {
            storNoteSdWrite(sizeof(uint32_t) + 1 + 3 + (size_t)_mbChartAccum.count * sizeof(float));
        }
        _mbChartAccum.lastPersistMs = millis();
    }
    _mbChartAccum.lastFlushMs = millis();
    // Keep last-known values (NaN if no data since flush) for next record continuity.
}

// Push one 1-min record into the volatile PSRAM ring (recent detail for the
// .chart APIs; the persisted files above are the durable record).
static void _mbChartRingPush(uint8_t nCols, uint32_t nowSec) {
    if (!_mbChartRingMutex) return;
    if (xSemaphoreTake(_mbChartRingMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    ModbusChartRecord rec;
    rec.timestamp = nowSec;
    rec.valueCount = nCols;
    for (uint8_t i = 0; i < nCols && i < CHART_HISTORY_MODBUS_MAX_COLUMNS; i++) {
        rec.values[i] = _mbChartAccum.values[i];
    }
    _mbChartRing.push(rec);
    xSemaphoreGive(_mbChartRingMutex);
}

// Parse one minimal-format Modbus log line and update the accumulator.
// Handles: "mb_<id>_<label>, <value>" — skips pretty "[MODBUS]..." lines.
static void _mbChartAddLine(const char* data, size_t len) {
    if (len < 5 || data[0] == '[') return; // skip empty or pretty-format lines

    const char* comma = (const char*)memchr(data, ',', len);
    if (!comma) return;

    // Key: everything before the comma, right-trimmed.
    size_t keyLen = (size_t)(comma - data);
    while (keyLen > 0 && (data[keyLen - 1] == ' ' || data[keyLen - 1] == '\t')) keyLen--;
    if (keyLen == 0 || keyLen >= sizeof(_mbChartAccum.keys[0])) return;

    char key[20] = {};
    strncpy(key, data, keyLen);

    // Value: float after the comma.
    char* endPtr = nullptr;
    float value = strtof(comma + 1, &endPtr);
    if (endPtr == comma + 1 || !isfinite(value)) return;

    // Flush on interval before updating (so the previous full cycle is captured).
    unsigned long nowMs = millis();
    if (_mbChartAccum.lastFlushMs == 0) {
        _mbChartAccum.lastFlushMs = nowMs; // arm timer on first data
    } else if ((nowMs - _mbChartAccum.lastFlushMs) >= _MB_CHART_FLUSH_INTERVAL_MS) {
        _mbChartDoFlush();
    }

    int idx = _mbChartFindOrAdd(key);
    if (idx >= 0) _mbChartAccum.values[idx] = value;

    // Volatile 1-min ring (PSRAM-first serving for the .chart APIs).
    time_t mbNow = time(nullptr);
    if (sd_layout::clockReady(mbNow) && (nowMs - _mbChartRingLastMs) >= _CHART_RING_PUSH_MS) {
        _mbChartRingLastMs = nowMs;
        _mbChartRingPush(_mbChartAccum.count, (uint32_t)mbNow);
    }
}

// Periodic flush called from the main loop to guarantee final record even when
// Modbus polling pauses (e.g. during email send).
static void modbusChartPeriodicFlush() {
    if (_mbChartAccum.count > 0 && _mbChartAccum.lastFlushMs > 0 &&
        (millis() - _mbChartAccum.lastFlushMs) >= _MB_CHART_FLUSH_INTERVAL_MS) {
        _mbChartDoFlush();
    }
}

// Merged serving for GET /api/modbus-chart-data: file history (either
// backend, any cadence) + PSRAM ring tail — always minute-fresh, one stride
// budget over the union. Keys snapshotted from the day-locked accumulator
// schema (a midnight rollover inside the ring may relabel older points).
// Non-static: called from the unified handler TU.
bool mbChartServeMergedJson(Print& out, const String& path, uint32_t fromTs, uint16_t maxOut) {
    const uint16_t kRawCap = 1500; // dense legacy 1-min day (1440) + margin
    const uint16_t kTailCap = 40;  // ring holds 32
    ChartServePoint* raw = (ChartServePoint*)heap_caps_malloc((size_t)kRawCap * sizeof(ChartServePoint),
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw) return false;
    uint16_t nRaw = chartHistoryReadRawPoints(path, CHART_HISTORY_SOURCE_MODBUS,
                                              CHART_HISTORY_MODBUS_MAX_COLUMNS, fromTs, raw, kRawCap);
    ChartServePoint* tail = (ChartServePoint*)heap_caps_malloc((size_t)kTailCap * sizeof(ChartServePoint),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t nTail = 0;
    char keys[_MB_CHART_MAX_COLS][16];
    uint8_t nKeys = 0;
    if (tail && _mbChartRingMutex && xSemaphoreTake(_mbChartRingMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        nKeys = _mbChartAccum.count;
        for (uint8_t i = 0; i < nKeys; i++) {
            strncpy(keys[i], _mbChartAccum.keys[i], sizeof(keys[i]) - 1);
            keys[i][sizeof(keys[i]) - 1] = '\0';
        }
        size_t sz = _mbChartRing.size();
        for (size_t i = 0; i < sz && nTail < kTailCap; i++) {
            const ModbusChartRecord& r = _mbChartRing[i];
            if (r.timestamp >= fromTs) {
                ChartServePoint* q = &tail[nTail++];
                q->timestamp = r.timestamp;
                q->valueCount = (r.valueCount > CHART_SERVE_MAX_COLS) ? CHART_SERVE_MAX_COLS : r.valueCount;
                for (uint8_t c = 0; c < CHART_SERVE_MAX_COLS; c++) {
                    q->values[c] = (c < CHART_HISTORY_MODBUS_MAX_COLUMNS) ? r.values[c] : NAN;
                }
            }
        }
        xSemaphoreGive(_mbChartRingMutex);
    }
    if (nKeys == 0) {
        for (uint16_t i = 0; i < nRaw; i++) {
            if (raw[i].valueCount > nKeys) nKeys = raw[i].valueCount;
        }
        for (uint8_t c = 0; c < nKeys && c < _MB_CHART_MAX_COLS; c++) {
            snprintf(keys[c], sizeof(keys[c]), "col_%u", (unsigned)(c + 1));
        }
        if (nKeys > _MB_CHART_MAX_COLS) nKeys = _MB_CHART_MAX_COLS;
    }
    int8_t decs[_MB_CHART_MAX_COLS];
    for (uint8_t i = 0; i < _MB_CHART_MAX_COLS; i++) decs[i] = 3;
    uint16_t mCap = nRaw + nTail;
    ChartServePoint* merged = (ChartServePoint*)heap_caps_malloc(((size_t)mCap + 1) * sizeof(ChartServePoint),
                                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = false;
    if (merged) {
        uint32_t n = chartMergePoints(raw, nRaw, tail ? tail : raw, tail ? nTail : 0, merged, mCap + 1);
        ok = chartHistoryServePointsJson(out, merged, n, keys, decs, nKeys, maxOut);
        heap_caps_free(merged);
    }
    heap_caps_free(raw);
    if (tail) heap_caps_free(tail);
    return ok;
}

#endif // ENABLE_RS232_CHARTS

// --- Proximity chart accumulator ---
// Receives typed channel arrays from _proxInjectTick() (via extern callback).
// Schema is fixed at compile time: PROXIMITY_CHANNELS × 3 columns (in, out, ppm).
// One float32 record is flushed to a .chart file every _PROX_CHART_FLUSH_INTERVAL_MS.
#if ENABLE_PROXIMITY_CHARTS==1 && ENABLE_PROXIMITY==1

#define _PROX_CHART_FLUSH_INTERVAL_MS  60000UL

static struct {
    float         values[CHART_HISTORY_PROXIMITY_MAX_COLUMNS]; // latest value per column
    uint8_t       count;              // fixed: PROXIMITY_CHANNELS * 3 (set on first call)
    unsigned long lastFlushMs;
    unsigned long lastPersistMs;      // persisted-write throttle (configured interval)
    char          lastDateId[11];     // DD-MM-YYYY (last flush day)
} _proxChartAccum = {};

static PsramCircularBuffer<ProximityChartRecord> _proxChartRing(_CHART_RING_CAP, true);
static SemaphoreHandle_t _proxChartRingMutex = NULL;
static unsigned long _proxChartRingLastMs = 0;

// Write one ProximityChartRecord for the current accumulator state.
// Uniform persisted cadence: SD *and* LittleFS follow the configured flush
// interval (default 30m, same rule as the CSV ticks) at the IDENTICAL path.
static void _proxChartDoFlush() {
    if (_proxChartAccum.count == 0) return;

    time_t now = time(nullptr);
    if (!sd_layout::clockReady(now)) return; // no fabricated dates pre-NTP; first post-sync flush lands in the right day

    // Uniform persisted cadence (SD *and* LittleFS follow the configured flush
    // interval, default 30m — same rule as the CSV ticks). The 1-min PSRAM
    // ring below is the always-on recent detail (no configuration).
    if ((millis() - _proxChartAccum.lastPersistMs) < flashFbWindowMs() && _proxChartAccum.lastPersistMs != 0) {
        _proxChartAccum.lastFlushMs = millis();
        return;
    }
    const bool useFlash = !sdcardShouldUseSD();
    struct tm ti;
    localtime_r(&now, &ti);

    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    strncpy(_proxChartAccum.lastDateId, dateId, 10);

    // Build fixed-schema ChartColumnDef[]: prox_ch<i>_in, _out, _ppm per channel.
    uint8_t nCols = _proxChartAccum.count;
    ChartColumnDef defs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS] = {};
    uint8_t col = 0;
    uint8_t nCh = nCols / 3;
    for (uint8_t i = 0; i < nCh && col < CHART_HISTORY_PROXIMITY_MAX_COLUMNS - 2; i++) {
        // _in column
        snprintf(defs[col].key,   sizeof(defs[col].key),   "prox_ch%u_in",  (unsigned)i);
        snprintf(defs[col].label, sizeof(defs[col].label), "Ch%u In",        (unsigned)i);
        defs[col].dataType = CHART_HISTORY_DATA_FLOAT32;
        defs[col].decimals = 0;
        defs[col].scale    = 1.0f;
        col++;
        // _out column
        snprintf(defs[col].key,   sizeof(defs[col].key),   "prox_ch%u_out", (unsigned)i);
        snprintf(defs[col].label, sizeof(defs[col].label), "Ch%u Out",       (unsigned)i);
        defs[col].dataType = CHART_HISTORY_DATA_FLOAT32;
        defs[col].decimals = 0;
        defs[col].scale    = 1.0f;
        col++;
        // _ppm column
        snprintf(defs[col].key,   sizeof(defs[col].key),   "prox_ch%u_ppm", (unsigned)i);
        snprintf(defs[col].label, sizeof(defs[col].label), "Ch%u ppm",       (unsigned)i);
        defs[col].dataType = CHART_HISTORY_DATA_FLOAT32;
        defs[col].decimals = 1;
        defs[col].scale    = 1.0f;
        col++;
    }

    String chartPath = chartHistoryProximityPathForDate(configManager.getSdcardName(), String(dateId));
    uint32_t dayStart = 0;
    {
        int d, m, y;
        sscanf(dateId, "%d-%d-%d", &d, &m, &y);
        struct tm dayTm = {};
        dayTm.tm_year = y - 1900;
        dayTm.tm_mon  = m - 1;
        dayTm.tm_mday = d;
        dayStart = (uint32_t)mktime(&dayTm);
    }

    ChartRs232Writer writer;
    bool opened = false;
    bool proxLfsHeld = false;
    if (useFlash) {
        // Identical path on LittleFS (parity); 1MB /config reserve enforced.
        // Serialized under lfsMutex (same crash guard as the email sender);
        // on contention this interval's record is skipped, next one retries.
#if ENABLE_FILESYSTEM==1
        extern bool lfsMutexLock(TickType_t timeoutTicks);
        extern void lfsMutexUnlock();
        proxLfsHeld = lfsMutexLock(pdMS_TO_TICKS(5000));
#endif
        if (proxLfsHeld && littlefsBegin() && littlefsMounted() && littlefsFreeBytes() >= (1024ULL * 1024ULL)) {
            opened = chartHistoryOpenProximityWriterFS(writer, getLittleFS(), chartPath, defs, nCols, dayStart);
        }
    } else {
        opened = chartHistoryOpenProximityWriter(writer, chartPath, defs, nCols, dayStart);
    }
    if (opened) {
        ProximityChartRecord rec;
        rec.timestamp  = (uint32_t)now;
        rec.valueCount = nCols;
        for (uint8_t i = 0; i < nCols; i++) {
            rec.values[i] = _proxChartAccum.values[i];
        }
        chartHistoryAppendProximityRecord(writer, rec);
        chartHistoryCloseRs232Writer(writer);
#if ENABLE_FILESYSTEM==1
        if (proxLfsHeld) lfsMutexUnlock();
#endif
        if (useFlash) {
            storNoteLfsWrite(sizeof(uint32_t) + 1 + 3 + (size_t)nCols * sizeof(float));
        } else {
            storNoteSdWrite(sizeof(uint32_t) + 1 + 3 + (size_t)nCols * sizeof(float));
        }
        _proxChartAccum.lastPersistMs = millis();
    }
    _proxChartAccum.lastFlushMs = millis();
}

// Push one 1-min record into the volatile PSRAM ring (recent detail for the
// .chart APIs; the persisted files above are the durable record).
static void _proxChartRingPush(uint8_t nCols, uint32_t nowSec) {
    if (!_proxChartRingMutex) return;
    if (xSemaphoreTake(_proxChartRingMutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    ProximityChartRecord rec;
    rec.timestamp = nowSec;
    rec.valueCount = nCols;
    for (uint8_t i = 0; i < nCols && i < CHART_HISTORY_PROXIMITY_MAX_COLUMNS; i++) {
        rec.values[i] = _proxChartAccum.values[i];
    }
    _proxChartRing.push(rec);
    xSemaphoreGive(_proxChartRingMutex);
}

// Public function called by proximity_manager.cpp (via extern) each tick injection.
// chIn/chOut are absolute totals; chPpm is the current rate.
void proximityChartAddTick(uint8_t count, const uint32_t* chIn, const uint32_t* chOut, const float* chPpm) {
    if (!configManager.getChartsPageEnabled()) return;
    if (count == 0 || !chIn || !chOut || !chPpm) return;

    // Fix column count on first call (schema is static for the day).
    uint8_t nCols = count * 3;
    if (nCols > CHART_HISTORY_PROXIMITY_MAX_COLUMNS) nCols = CHART_HISTORY_PROXIMITY_MAX_COLUMNS;
    _proxChartAccum.count = nCols;

    // Flush on interval before updating values.
    unsigned long nowMs = millis();
    if (_proxChartAccum.lastFlushMs == 0) {
        _proxChartAccum.lastFlushMs = nowMs; // arm timer on first data
    } else if ((nowMs - _proxChartAccum.lastFlushMs) >= _PROX_CHART_FLUSH_INTERVAL_MS) {
        _proxChartDoFlush();
    }

    // Store latest values: layout [ch0_in, ch0_out, ch0_ppm, ch1_in, ...]
    uint8_t col = 0;
    uint8_t nCh = count;
    for (uint8_t i = 0; i < nCh && col < (uint8_t)CHART_HISTORY_PROXIMITY_MAX_COLUMNS - 2; i++) {
        _proxChartAccum.values[col++] = (float)chIn[i];
        _proxChartAccum.values[col++] = (float)chOut[i];
        _proxChartAccum.values[col++] = chPpm[i];
    }

    // Volatile 1-min ring (PSRAM-first serving for the .chart APIs).
    time_t rsNow = time(nullptr);
    if (sd_layout::clockReady(rsNow) && (nowMs - _proxChartRingLastMs) >= _CHART_RING_PUSH_MS) {
        _proxChartRingLastMs = nowMs;
        _proxChartRingPush(nCols, (uint32_t)rsNow);
    }
}

// Periodic flush called from the main loop to guarantee a final record even when
// proximity injections pause (e.g. during email send).
static void proximityChartPeriodicFlush() {
    if (_proxChartAccum.count > 0 && _proxChartAccum.lastFlushMs > 0 &&
        (millis() - _proxChartAccum.lastFlushMs) >= _PROX_CHART_FLUSH_INTERVAL_MS) {
        _proxChartDoFlush();
    }
}

// Merged serving for GET /api/proximity-chart-data: file history (either
// backend, any cadence) + PSRAM ring tail — always minute-fresh, one stride
// budget over the union. Schema is fixed (PROXIMITY_CHANNELS x in/out/ppm).
// Non-static: called from the unified handler TU.
bool proxChartServeMergedJson(Print& out, const String& path, uint32_t fromTs,
                              uint16_t maxOut, uint32_t untilTs = 0) {
    const uint16_t kRawCap = 1500; // dense legacy 1-min day (1440) + margin
    const uint16_t kTailCap = 40;  // ring holds 32
    ChartServePoint* raw = (ChartServePoint*)heap_caps_malloc((size_t)kRawCap * sizeof(ChartServePoint),
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw) return false;
    uint16_t nRaw = chartHistoryReadRawPoints(path, CHART_HISTORY_SOURCE_PROXIMITY,
                                              CHART_HISTORY_PROXIMITY_MAX_COLUMNS, fromTs, raw, kRawCap);
    ChartServePoint* tail = (ChartServePoint*)heap_caps_malloc((size_t)kTailCap * sizeof(ChartServePoint),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t nTail = 0;
    if (tail && _proxChartRingMutex && xSemaphoreTake(_proxChartRingMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t sz = _proxChartRing.size();
        for (size_t i = 0; i < sz && nTail < kTailCap; i++) {
            const ProximityChartRecord& r = _proxChartRing[i];
            // The live tail must not leak into a PAST date's response: the ring
            // always holds "now" samples, so a caller asking for yesterday would
            // otherwise get yesterday's file PLUS today's tail. The JS delta
            // chain then jumps across the gap and dumps the whole span into one
            // hour (the giant bar + empty hours bug). untilTs bounds the tail to
            // the requested day; 0 keeps the old unbounded behaviour.
            if (r.timestamp < fromTs) continue;
            if (untilTs != 0 && r.timestamp > untilTs) continue;
            ChartServePoint* q = &tail[nTail++];
            q->timestamp = r.timestamp;
            q->valueCount = (r.valueCount > CHART_SERVE_MAX_COLS) ? CHART_SERVE_MAX_COLS : r.valueCount;
            for (uint8_t c = 0; c < CHART_SERVE_MAX_COLS; c++) {
                q->values[c] = (c < CHART_HISTORY_PROXIMITY_MAX_COLUMNS) ? r.values[c] : NAN;
            }
        }
        xSemaphoreGive(_proxChartRingMutex);
    }
    uint8_t nCols = _proxChartAccum.count;
    if (nCols == 0) {
        for (uint16_t i = 0; i < nRaw; i++) {
            if (raw[i].valueCount > nCols) nCols = raw[i].valueCount;
        }
        for (uint16_t i = 0; i < nTail; i++) {
            if (tail[i].valueCount > nCols) nCols = tail[i].valueCount;
        }
        if (nCols > CHART_HISTORY_PROXIMITY_MAX_COLUMNS) nCols = CHART_HISTORY_PROXIMITY_MAX_COLUMNS;
    }
    uint16_t mCap = nRaw + nTail;
    ChartServePoint* merged = (ChartServePoint*)heap_caps_malloc(((size_t)mCap + 1) * sizeof(ChartServePoint),
                                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = false;
    if (merged) {
        uint32_t n = chartMergePoints(raw, nRaw, tail ? tail : raw, tail ? nTail : 0, merged, mCap + 1);
        char keys[CHART_HISTORY_PROXIMITY_MAX_COLUMNS][16];
        int8_t decs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS];
        uint8_t nCh = nCols / 3;
        for (uint8_t c = 0; c < nCh; c++) {
            snprintf(keys[c * 3], 16, "prox_ch%u_in", c);
            snprintf(keys[c * 3 + 1], 16, "prox_ch%u_out", c);
            snprintf(keys[c * 3 + 2], 16, "prox_ch%u_ppm", c);
            decs[c * 3] = 0; decs[c * 3 + 1] = 0; decs[c * 3 + 2] = 1;
        }
        ok = chartHistoryServePointsJson(out, merged, n, keys, decs, nCols, maxOut);
        heap_caps_free(merged);
    }
    heap_caps_free(raw);
    if (tail) heap_caps_free(tail);
    return ok;
}

#endif // ENABLE_PROXIMITY_CHARTS && ENABLE_PROXIMITY

// Init the volatile chart rings (non-fatal: APIs fall back to files when a
// ring is unavailable). Called from setup; safe under any flag combination.
// Non-static: setup TU calls it (same chatgpt.ino TU); stubbed in
// chatgpt_includes.h for builds without this pipeline.
void chartRingsBegin() {
#if ENABLE_PROXIMITY_CHARTS==1 && ENABLE_PROXIMITY==1
    if (!_proxChartRingMutex) _proxChartRingMutex = xSemaphoreCreateMutex();
    if (!_proxChartRingMutex || !_proxChartRing.begin()) {
        sendVirtualMessage(F("Prox chart ring unavailable (PSRAM?)"), WARN);
    }
#endif
#if ENABLE_RS232_CHARTS==1
    if (!_mbChartRingMutex) _mbChartRingMutex = xSemaphoreCreateMutex();
    if (!_mbChartRingMutex || !_mbChartRing.begin()) {
        sendVirtualMessage(F("Modbus chart ring unavailable (PSRAM?)"), WARN);
    }
#endif
}

// Forward declaration — defined further below.
static void addDataToSdcardBufferTimestampedRaw(bool isRS232, const char* data, size_t dataLen, unsigned long timestamp);

// Writes data to PSRAM VFS with optional per-line HH:MM:SS\t timestamp.
// Only adds timestamp for RS232 source when getEmailIncludeTimestamp() is enabled.
// Used exclusively by the ENABLE_SDCARD=0 paths so String allocation is acceptable.
static void _psramWriteTimestamped(const char* source, const char* data, size_t dataLen, unsigned long chunkTimestampMs) {
    if (!data || dataLen == 0 || !source) return;
    bool wantTimestamp = configManager.getEmailIncludeTimestamp() &&
                         (strcasecmp(source, "RS232") == 0);
    if (!wantTimestamp) {
        psramVFSWrite(String(source), data, dataLen);
        return;
    }
    time_t nowTs = time(nullptr);
    if (chunkTimestampMs > 0) {
        unsigned long elapsed = millis() - chunkTimestampMs;
        if (elapsed < 86400000UL) nowTs -= (time_t)(elapsed / 1000);
    }
    if (nowTs <= 100000) {
        psramVFSWrite(String(source), data, dataLen);
        return;
    }
    struct tm ti;
    localtime_r(&nowTs, &ti);
    char tsBuf[12];
    strftime(tsBuf, sizeof(tsBuf), "%H:%M:%S\t", &ti);
    size_t tsLen = strlen(tsBuf);
    // Write each non-empty line with timestamp prefix.
    const char* pos = data;
    const char* end = data + dataLen;
    while (pos < end) {
        const char* nl = (const char*)memchr(pos, '\n', (size_t)(end - pos));
        size_t lineLen = nl ? (size_t)(nl - pos) : (size_t)(end - pos);
        if (lineLen > 0) {
            psramVFSWrite(String(source), tsBuf, tsLen);
            psramVFSWrite(String(source), pos, lineLen);
            psramVFSWrite(String(source), "\n", 1);
        }
        pos = nl ? nl + 1 : end;
    }
}

// Generic source write callback — matches ModbusSdWriteFn(const char* source, ...) signature.
// Routes known high-frequency sources (RS232/LPT) through the double-buffer pipeline;
// all other sources (e.g. MODBUS) write directly to SD — safe because Modbus polls are
// infrequent (seconds apart) and do not need the double-buffer batching logic.
static void addDataToSdcardDirectRaw(const char* source, const char* data, size_t dataLen, unsigned long timestamp) {
#if ENABLE_SDCARD==1
    if (!source || !data || dataLen == 0) return;
    if (isEmailSending && ESP.getFreeHeap() < 12000) return;

    if (strcasecmp(source, "RS232") == 0) {
        addDataToSdcardBufferTimestampedRaw(true,  data, dataLen, timestamp);
    } else if (strcasecmp(source, "LPT") == 0) {
        addDataToSdcardBufferTimestampedRaw(false, data, dataLen, timestamp);
    } else {
        // Generic source (e.g. MODBUS): direct SD write, no double-buffer.
        // sdcardWriteData builds "/{sdcardName}/{SOURCE}_{DD-MM-YYYY}/" automatically.
        sdcardWriteData(String(source), String(data, dataLen));
    // LittleFS unload hook (same TU as the pre-reboot flush; self-skips on SD).
    flashFbWrite(source, data, dataLen);
#if ENABLE_RS232_CHARTS==1
        if (strcasecmp(source, "MODBUS") == 0 && configManager.getChartsPageEnabled()) {
            _mbChartAddLine(data, dataLen);
        }
#endif
    }
#else
    // ENABLE_SDCARD=0: route all sources directly to PSRAM VFS.
    if (!source || !data || dataLen == 0) return;
    if (psramVFSInitialized) {
        _psramWriteTimestamped(source, data, dataLen, timestamp);
    }
#endif
}

static void addDataToSdcardBufferTimestamped(const String &source, const String &data, unsigned long timestamp) {
#if ENABLE_SDCARD==1
    // NOTE: Do NOT update inactivity alert here - this processes buffered data, not NEW data from ports
    // Inactivity timestamp is only updated when data arrives from RS232/LPT physical ports
    
    // Critical protection: if an email is in progress and heap is critically low, drop SD logging to avoid a crash.
    if (isEmailSending && ESP.getFreeHeap() < 12000) {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 5000) {
            sendVirtualMessage(F("SD: dropped data (low heap + email in progress)"), ERROR);
            lastWarn = millis();
        }
        return;
    }

    if (data.length() == 0) return;

    // LittleFS unload hook (no-SD only; self-skips while SD owns persistence).
    // Same funnel as the SD path above: every accepted chunk is offered here;
    // the store coalesces (configured window, 64KB burst) and sheds beyond cap.
    // Single slot instance: this TU also runs the pre-reboot flush.
    flashFbWrite(source, data.c_str(), data.length());

    // FREEZE MODE: During EMAILDAY, route new data to freeze buffers instead of active buffers
    // This prevents data loss while email is being sent
    if (sdcardFreezeWrites) {
        if (source.equalsIgnoreCase("RS232")) {
            uint32_t seq = ++sdcardSeq_RS232;
            sdcardFreezeBuffer_RS232.push_back(TimestampedData(timestamp, data, seq));
            sdcardFreezeBytes_RS232 += data.length();
        } else if (source.equalsIgnoreCase("LPT")) {
            uint32_t seq = ++sdcardSeq_LPT;
            sdcardFreezeBuffer_LPT.push_back(TimestampedData(timestamp, data, seq));
            sdcardFreezeBytes_LPT += data.length();
        }
        
        // Rate-limited warning to indicate freeze is active
        static unsigned long lastFreezeInfo = 0;
        if (millis() - lastFreezeInfo > 30000) {
            sendVirtualMessage(buildMessage("FREEZE: buffering data in RAM (%u bytes RS232, %u bytes LPT)", 
                                              (unsigned)sdcardFreezeBytes_RS232, (unsigned)sdcardFreezeBytes_LPT), DEBUG);
            lastFreezeInfo = millis();
        }
        return;
    }
    
    unsigned long now = millis();
    // IMPROVEMENT 1: Use atomic pointers directly (no pointer-to-pointer needed)
    std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*>* activeBuffer = nullptr;
    std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*>* flushBuffer = nullptr;
    unsigned long *lastFlush = nullptr;
    unsigned long *lastDataReceived = nullptr;
    std::atomic<size_t>* activeBytes = nullptr;
    uint32_t *seqCounter = nullptr;
    
    // Select buffers based on the source.
    if (source.equalsIgnoreCase("RS232")) {
        activeBuffer = &sdcardActiveBuffer_RS232;
        flushBuffer = &sdcardFlushBuffer_RS232;
        lastFlush = &lastSdcardFlush_RS232;
        lastDataReceived = &lastSdcardDataReceived_RS232;
        activeBytes = &sdcardActiveBytes_RS232;
        seqCounter = &sdcardSeq_RS232;
    } else if (source.equalsIgnoreCase("LPT")) {
        activeBuffer = &sdcardActiveBuffer_LPT;
        flushBuffer = &sdcardFlushBuffer_LPT;
        lastFlush = &lastSdcardFlush_LPT;
        lastDataReceived = &lastSdcardDataReceived_LPT;
        activeBytes = &sdcardActiveBytes_LPT;
        seqCounter = &sdcardSeq_LPT;
    }
    
    if (!activeBuffer) return;
    
    // IMPROVEMENT 1: Load atomic pointers (lock-free, never blocks)
    auto* activePtr = activeBuffer->load(std::memory_order_acquire);
    auto* flushPtr = flushBuffer->load(std::memory_order_acquire);
    
    // Protection: ensure internal pointers were initialized.
    if (!activePtr || !flushPtr || !activeBytes || !seqCounter) {
        static unsigned long lastWarnNull = 0;
        if (millis() - lastWarnNull > 5000) {
            sendVirtualMessage(F("SD: buffers not initialized yet"), WARN);
            lastWarnNull = millis();
        }
        return;
    }

    // IMPROVEMENT 1: Lock-free push using atomic pointer (no mutex needed!)
    // Swap is done exclusively by SD writer task, so only it modifies pointers
    // This thread only reads pointer and pushes data - safe without mutex
    
    uint32_t seq = ++(*seqCounter);
    activePtr->push_back(TimestampedData(timestamp, data, seq));
    size_t newBytes = activeBytes->fetch_add(data.length(), std::memory_order_relaxed);
    *lastDataReceived = now;

    // STREAMING EMAIL: Check for recipient match IMMEDIATELY (at buffer entry)
    // Email is sent with a COPY of matched line (with optional timestamp), original line continues to SD/PSRAM with timestamp
    bool sdEmailActive = configManager.getSdcardEmailSend();
    if (configManager.getSdcardEnabled() && sdEmailActive) {
        // outMatchedRules receives the rule names that actually triggered on this data line.
        // We accumulate these in streamingMatchedRulesAccum across the window so the email
        // is later sent with the correct, data-derived rule set instead of the header-tainted
        // result from collectMatchedRulesFromContent.
        String lineMatchedRules;
        bool currentHasMatch = hasRecipientMatchStreaming(data, lineMatchedRules) && isEmailConfiguredAndWifiConnected();
        
        if (currentHasMatch) {
            // Prepare data for email: add timestamp if enabled
            String emailData = data;
            if (configManager.getEmailIncludeTimestamp() && !source.equalsIgnoreCase("LPT")) {
                // Convert milliseconds to time_t and format timestamp
                time_t ts = timestamp / 1000;
                if (ts > 100000) { // Valid timestamp check
                    struct tm timeinfo;
                    localtime_r(&ts, &timeinfo);
                    char tsBuf[24];
                    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    emailData = String(tsBuf) + "\t" + data;
                }
            }
            
            if (!streamingEmailPending) {
                // FIRST match: start accumulation window
                streamingEmailPending = true;
                firstMatchTimestamp = now;
                streamingMatchedRulesAccum = lineMatchedRules; // init with rules from first match
                streamingBufferClear();
                
                // Add file header at the beginning (same format as SD/PSRAM files)
                String header = buildFileHeader(source);
                if (header.length() > 0) {
                    streamingBufferAppend(header);
                }
                
                streamingBufferAppend(emailData);
                sendVirtualMessage(F("STREAMING: First match detected. Accumulating for 5s..."), INFO);
            } else {
                // Subsequent match: accumulate rules and check buffer limit
                appendRulesToAccum(lineMatchedRules, streamingMatchedRulesAccum);
                size_t newSize = streamingEmailBuffer.size() + emailData.length() + 1; // +1 for newline
                if (newSize <= MAX_STREAMING_BUFFER_SIZE) {
                    // Within limit: accumulate
                    streamingBufferAppend("\n");
                    streamingBufferAppend(emailData);
                    sendVirtualMessage(F("STREAMING: Additional match accumulated"), DEBUG);
                } else {
                    // LIMIT REACHED: enqueue current buffer and start new cycle
                    sendVirtualMessage(buildMessage("STREAMING: 64KB limit reached (%u bytes). Enqueuing!", 
                                                      (unsigned)streamingEmailBuffer.size()), INFO);
                    enqueueStreamingEmail(streamingBufferToString(), streamingMatchedRulesAccum);
                    
                    // Start new cycle with current data
                    streamingEmailPending = true;
                    firstMatchTimestamp = now;
                    streamingMatchedRulesAccum = lineMatchedRules; // reset for new cycle
                    streamingBufferClear();
                    
                    // Add file header at the beginning (same format as SD/PSRAM files)
                    String header = buildFileHeader(source);
                    if (header.length() > 0) {
                        streamingBufferAppend(header);
                    }
                    
                    streamingBufferAppend(emailData);
                }
            }
        }
        
        // Check if 5s elapsed since first match
        if (streamingEmailPending && (now - firstMatchTimestamp) >= 5000) {
            if (!streamingEmailBuffer.empty()) {
                sendVirtualMessage(buildMessage("STREAMING: 5s elapsed. Enqueuing with rules='%.60s'",
                                                  streamingMatchedRulesAccum.c_str()), INFO);
                enqueueStreamingEmail(streamingBufferToString(), streamingMatchedRulesAccum);
                
                // Reset state
                streamingMatchedRulesAccum = "";
                streamingBufferClear();
                streamingEmailPending = false;
                firstMatchTimestamp = 0;
            }
        }
        
        // Process email queue (send next email if idle)
        processEmailQueue();
    }

    const bool overThreshold = (newBytes >= SDCARD_BUFFER_THRESHOLD_FLUSH);
    const bool overMax = (newBytes >= SDCARD_BUFFER_MAX_SIZE);
    const bool flushBusy = (flushPtr && !flushPtr->empty());
    bool shouldNotify = false;

    // Only notify writer task if threshold/max reached
    // Actual swap is done by writer task under mutex (safe)
    if ((overThreshold || overMax) && !flushBusy) {
        shouldNotify = true;
    } else if ((overThreshold || overMax) && flushBusy) {
        // Flush buffer busy but we're over limit - nudge writer anyway
        shouldNotify = true;
        
        if (overMax) {
            static unsigned long lastWarnFull = 0;
            if (millis() - lastWarnFull > 5000) {
                sendVirtualMessage(buildMessage("SD: buffer full, writer busy (%s)", source.c_str()), WARN);
                lastWarnFull = millis();
            }
        }
    }

    // IMPROVEMENT 1: Notify writer task if buffer needs attention (lock-free)
    if (shouldNotify && sdCardWriteTaskHandle && !sdcardFreezeWrites) {
        xTaskNotifyGive(sdCardWriteTaskHandle);
    }
#else
    // ENABLE_SDCARD=0: write directly to PSRAM VFS (no double-buffer, no SD).
    // The psramVFS snapshot mode handles concurrent EMAILDAY writes correctly.
    if (psramVFSInitialized && data.length() > 0) {
        _psramWriteTimestamped(source.c_str(), data.c_str(), data.length(), timestamp);
    }
#endif
} // End of addDataToSdcardBufferTimestamped

// Optimized version: accepts const char* directly to avoid temporary String allocation on internal heap
// This function copies data directly to PSRAM buffer via TimestampedData(ts, char*, len, seq) constructor
static void addDataToSdcardBufferTimestampedRaw(bool isRS232, const char* data, size_t dataLen, unsigned long timestamp) {
#if ENABLE_SDCARD==1
    // NOTE: Do NOT update inactivity alert here - this processes buffered data, not NEW data from ports
    // Inactivity timestamp is only updated when data arrives from RS232/LPT physical ports
    
    if (dataLen == 0 || !data) return;

    // Critical protection: if an email is in progress and heap is critically low, drop SD logging
    if (isEmailSending && ESP.getFreeHeap() < 12000) {
        return;
    }

    // LittleFS unload hook (same funnel as above; self-skips while SD owns it).
    flashFbWrite(isRS232 ? "RS232" : "LPT", data, dataLen);

    unsigned long now = millis();
    // IMPROVEMENT 1: Use atomic pointers directly
    std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*>* activeBuffer = nullptr;
    std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*>* flushBuffer = nullptr;
    unsigned long *lastFlush = nullptr;
    unsigned long *lastDataReceived = nullptr;
    std::atomic<size_t>* activeBytes = nullptr;
    uint32_t *seqCounter = nullptr;
    
    if (isRS232) {
        activeBuffer = &sdcardActiveBuffer_RS232;
        flushBuffer = &sdcardFlushBuffer_RS232;
        lastFlush = &lastSdcardFlush_RS232;
        lastDataReceived = &lastSdcardDataReceived_RS232;
        activeBytes = &sdcardActiveBytes_RS232;
        seqCounter = &sdcardSeq_RS232;
    } else {
        activeBuffer = &sdcardActiveBuffer_LPT;
        flushBuffer = &sdcardFlushBuffer_LPT;
        lastFlush = &lastSdcardFlush_LPT;
        lastDataReceived = &lastSdcardDataReceived_LPT;
        activeBytes = &sdcardActiveBytes_LPT;
        seqCounter = &sdcardSeq_LPT;
    }
    
    // IMPROVEMENT 1: Load atomic pointers (lock-free)
    auto* activePtr = activeBuffer->load(std::memory_order_acquire);
    auto* flushPtr = flushBuffer->load(std::memory_order_acquire);
    
    if (!activePtr || !flushPtr || !activeBytes || !seqCounter) return;

    // STREAMING EMAIL: Check for recipient match BEFORE the freeze check.
    // Moving this block here ensures COUNT/colN/threshold rules fire even while EMAILDAY is
    // sending (sdcardFreezeWrites=true). The streaming email queue handles concurrency:
    // new emails simply wait behind the one that is currently in progress.
    bool sdEmailActive = configManager.getSdcardEmailSend();
    if (sdEmailActive) {
        // Create temporary String for match checking (destroyed immediately after)
        String dataStr;
        dataStr.reserve(dataLen + 1); // Pre-allocate to avoid fragmentation
        dataStr = String(data, dataLen);
        
        // DEBUG: Log buffer entry check (ALWAYS VISIBLE at WARN level for diagnosis)
        static unsigned long lastBufferEntryLog = 0;
        if (millis() - lastBufferEntryLog > 10000) {
            safeEnviarMensagemVirtual("[STREAM] Buffer entry: checking recipients (data len=%u)", WARN, (unsigned)dataLen);
            lastBufferEntryLog = millis();
        }
        
        // outMatchedRules receives the rule names that actually triggered on this data line.
        // We accumulate these in streamingMatchedRulesAccum across the window so the email
        // is sent with the exact rule set from the data, not from the full buffer scan in
        // collectMatchedRulesFromContent (which includes the file header and causes false
        // positives on colN rules due to date tokens like "2025-09-15").
        String lineMatchedRules;
        bool currentHasMatch = hasRecipientMatchStreaming(dataStr, lineMatchedRules) && isEmailConfiguredAndWifiConnected();
        
        if (currentHasMatch) {
            // DEBUG: Log recipient match at buffer entry (ALWAYS VISIBLE at WARN level)
            safeEnviarMensagemVirtual("✅ [STREAM] MATCH at buffer entry! Source=%s, Rules='%.40s', Data: %.30s", WARN, 
                                     isRS232 ? "RS232" : "LPT", lineMatchedRules.c_str(), dataStr.c_str());
            
            // Prepare data for email: add timestamp if enabled (COPY for email, original stays intact for SD)
            String emailData = dataStr;
            if (configManager.getEmailIncludeTimestamp() && isRS232) { // Only RS232 gets timestamps (not LPT)
                // Convert milliseconds to time_t and format timestamp
                time_t ts = timestamp / 1000;
                if (ts > 100000) { // Valid timestamp check
                    struct tm timeinfo;
                    localtime_r(&ts, &timeinfo);
                    char tsBuf[24];
                    strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    emailData = String(tsBuf) + "\t" + dataStr;
                    safeEnviarMensagemVirtual("[STREAM] Timestamp added to EMAIL COPY: %s", DEBUG, tsBuf);
                }
            }
            
            safeEnviarMensagemVirtual("[STREAM] Original data continues to SD/PSRAM: %.40s", DEBUG, dataStr.c_str());
            
            if (!streamingEmailPending) {
                // FIRST match: start accumulation window
                streamingEmailPending = true;
                firstMatchTimestamp = now;
                streamingMatchedRulesAccum = lineMatchedRules; // init with rules from first match
                streamingBufferClear();
                
                // Add file header at the beginning (same format as SD/PSRAM files)
                String header = buildFileHeader(isRS232 ? "RS232" : "LPT");
                if (header.length() > 0) {
                    streamingBufferAppend(header);
                }
                
                streamingBufferAppend(emailData);
                safeEnviarMensagemVirtual("📧 [STREAM] First match! Starting 5s accumulation window (rules='%.40s')",
                                         INFO, lineMatchedRules.c_str());
            } else {
                // Subsequent match: accumulate rules and check buffer limit
                appendRulesToAccum(lineMatchedRules, streamingMatchedRulesAccum);
                size_t newSize = streamingEmailBuffer.size() + emailData.length() + 1; // +1 for newline
                if (newSize <= MAX_STREAMING_BUFFER_SIZE) {
                    // Within limit: accumulate
                    streamingBufferAppend("\n");
                    streamingBufferAppend(emailData);
                    safeEnviarMensagemVirtual("[STREAM] Match accumulated (buffer=%u/%u bytes)", DEBUG, 
                                             (unsigned)newSize, (unsigned)MAX_STREAMING_BUFFER_SIZE);
                } else {
                    // LIMIT REACHED: enqueue current buffer and start new cycle
                    safeEnviarMensagemVirtual("⚠️ [STREAM] 64KB limit! Enqueuing %u bytes (rules='%.40s')", INFO,
                                             (unsigned)streamingEmailBuffer.size(),
                                             streamingMatchedRulesAccum.c_str());
                    enqueueStreamingEmail(streamingBufferToString(), streamingMatchedRulesAccum);
                    
                    // Start new cycle with current data
                    streamingEmailPending = true;
                    firstMatchTimestamp = now;
                    streamingMatchedRulesAccum = lineMatchedRules; // reset for new cycle
                    streamingBufferClear();
                    
                    // Add file header at the beginning (same format as SD/PSRAM files)
                    String header = buildFileHeader(isRS232 ? "RS232" : "LPT");
                    if (header.length() > 0) {
                        streamingBufferAppend(header);
                    }
                    
                    streamingBufferAppend(emailData);
                }
            }
        }
        
        // Check if 5s elapsed since first match
        if (streamingEmailPending && (now - firstMatchTimestamp) >= 5000) {
            if (!streamingEmailBuffer.empty()) {
                safeEnviarMensagemVirtual("⏰ [STREAM] 5s timeout! rules='%.60s' Enqueuing %u bytes", INFO,
                                         streamingMatchedRulesAccum.c_str(),
                                         (unsigned)streamingEmailBuffer.size());
                enqueueStreamingEmail(streamingBufferToString(), streamingMatchedRulesAccum);
                
                // Reset state
                streamingMatchedRulesAccum = "";
                streamingBufferClear();
                streamingEmailPending = false;
                firstMatchTimestamp = 0;
            }
        }
        
        // Process email queue (send next email if idle) - uses Priority Manager (P3)
        processEmailQueue();
    }

    // FREEZE MODE: During EMAILDAY, route new data to freeze buffers (AFTER streaming check above)
    if (sdcardFreezeWrites) {
        uint32_t seq = isRS232 ? ++sdcardSeq_RS232 : ++sdcardSeq_LPT;
        // Use new constructor that copies directly to PSRAM vector (no internal heap String)
        if (isRS232) {
            sdcardFreezeBuffer_RS232.push_back(TimestampedData(timestamp, data, dataLen, seq));
            sdcardFreezeBytes_RS232 += dataLen;
        } else {
            sdcardFreezeBuffer_LPT.push_back(TimestampedData(timestamp, data, dataLen, seq));
            sdcardFreezeBytes_LPT += dataLen;
        }
        return;
    }

    // IMPROVEMENT 1: Lock-free push using atomic pointer

    uint32_t seq = ++(*seqCounter);
    // Use new constructor that copies char* directly to PSRAM vector - NO internal heap allocation!
    activePtr->push_back(TimestampedData(timestamp, data, dataLen, seq));
    size_t newBytes = activeBytes->fetch_add(dataLen, std::memory_order_relaxed);
    *lastDataReceived = now;

    // Update last byte time for smart flush logic
    if (isRS232) {
        sdcardLastByteTime_RS232 = now;
    } else {
        sdcardLastByteTime_LPT = now;
    }

    const bool overThreshold = (newBytes >= SDCARD_BUFFER_THRESHOLD_FLUSH);
    const bool overMax = (newBytes >= SDCARD_BUFFER_MAX_SIZE);
    const bool flushBusy = (flushPtr && !flushPtr->empty());

    // 🚨 CRITICAL: Threshold reached - trigger immediate flush with COMPLETE LINES ONLY
    // This prevents buffer overflow while ensuring we never cut data mid-line
    if ((overThreshold || overMax) && !flushBusy && !sdcardFreezeWrites) {
        // Find last complete line in activeBuffer
        int lastCompleteIdx = findLastCompleteLineInBuffer(activePtr);
        
        if (lastCompleteIdx >= 0 && sdcardSwapLock(pdMS_TO_TICKS(10))) {
            // Move complete lines to flushBuffer
            flushPtr->clear();
            for (int i = 0; i <= lastCompleteIdx; i++) {
                flushPtr->push_back((*activePtr)[i]);
            }
            
            // Keep incomplete lines in activeBuffer
            std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> remaining;
            for (size_t i = lastCompleteIdx + 1; i < activePtr->size(); i++) {
                remaining.push_back((*activePtr)[i]);
            }
            activePtr->swap(remaining);
            
            // Recalculate activeBytes (only remaining data)
            size_t remainingBytes = 0;
            for (const auto& item : *activePtr) {
                remainingBytes += item.dataLength();
            }
            *activeBytes = remainingBytes;
            *lastFlush = now;
            
            sdcardSwapUnlock();
            
            // Notify writer task
            if (sdCardWriteTaskHandle) {
                xTaskNotifyGive(sdCardWriteTaskHandle);
            }
        } else if (overMax) {
            // Buffer critically full but no complete line - warn and notify anyway
            static unsigned long lastWarnIncomplete = 0;
            if (millis() - lastWarnIncomplete > 5000) {
                sendVirtualMessage(buildMessage("⚠️ SD: buffer full but no complete line (%s)", 
                                                  isRS232 ? "RS232" : "LPT"), WARN);
                lastWarnIncomplete = millis();
            }
            if (sdCardWriteTaskHandle) {
                xTaskNotifyGive(sdCardWriteTaskHandle);
            }
        }
    } else if ((overThreshold || overMax) && flushBusy) {
        // Flush buffer busy - just notify writer task
        if (sdCardWriteTaskHandle && !sdcardFreezeWrites) {
            xTaskNotifyGive(sdCardWriteTaskHandle);
        }
    }
#else
    // ENABLE_SDCARD=0: write directly to PSRAM VFS (no double-buffer, no SD).
    if (psramVFSInitialized && dataLen > 0 && data) {
        _psramWriteTimestamped(isRS232 ? "RS232" : "LPT", data, dataLen, timestamp);
    }
#endif
} // End of addDataToSdcardBufferTimestampedRaw

#if ENABLE_SDCARD==1
// Checks SD usage and deletes the oldest dated source folder when usage exceeds the configured
// cleanup threshold. Rate-limited to once per 6 hours to avoid excessive SD I/O.
// Autoclean is always active (no UI toggle). Only runs when the SD card is mounted
// (Flash fallback has its own flashAutoCleanupIfNeeded()).
static void sdAutoCleanupIfNeeded() {
    // Autoclean is always active (no UI toggle) — same policy for SD and Flash.
    if (!sdcardIsMounted()) return;

    static unsigned long lastCleanupMs = 0;
    unsigned long nowMs = millis();
    if (lastCleanupMs != 0 && (nowMs - lastCleanupMs) < 21600000UL) return; // max once per 6 hours

    uint64_t total = sdcardTotalBytes();
    if (total == 0) return;
    uint64_t used = sdcardUsedBytes();
    int threshold = configManager.getSdCleanupThreshold(); // percent
    // Two tiers sharing this threshold:
    //   HARD (usage > threshold): delete the oldest entry, whatever its age.
    //   SOFT (usage <= threshold): only entries older than keep_days go.
    const bool overThreshold = (used * 100ULL / total) >= (uint64_t)threshold;

    int keepDays = configManager.getSdKeepDays();
    // keep_days = 0 (UI "infinity"): no age limit — files stay until usage
    // crosses the threshold, then the oldest goes whatever its age.
    String sdcardBase = configManager.getSdcardName();
    if (sdcardBase.isEmpty()) sdcardBase = "SD_LOG";
    String parentPath = String("/") + sdcardBase;
    if (!SD_MMC.exists(parentPath.c_str())) {
        lastCleanupMs = nowMs;
        return;
    }

    // Collect dated source subfolders ("SOURCE_DD-MM-YYYY") AND standard flat
    // day-files ("SOURCE_DD-MM-YYYY.<ext>", see sd_layout.h). Aggregates
    // (wk*-oee.csv, .chart) are never candidates — same as today.
    struct DateFolder {
        String path;
        int year, month, day;
        bool isDir;
    };
    std::vector<DateFolder> candidates;

    File root = SD_MMC.open(parentPath.c_str());
    if (root && root.isDirectory()) {
        File entry = root.openNextFile();
        while (entry) {
            if (entry.isDirectory()) {
                String name = String(entry.name());
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                // Extract date from last 10 chars: "DD-MM-YYYY"
                if (name.length() >= 11) {
                    String datePart = name.substring(name.length() - 10);
                    int d = 0, m = 0, y = 0;
                    if (sscanf(datePart.c_str(), "%d-%d-%d", &d, &m, &y) == 3 && y > 2000) {
                        DateFolder df;
                        df.path = parentPath + "/" + name;
                        df.day = d; df.month = m; df.year = y;
                        df.isDir = true;
                        candidates.push_back(df);
                    }
                }
            } else {
                String name = String(entry.name());
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                String datePart;
                if (sd_layout::isDayFile(name, &datePart)) {
                    int d = 0, m = 0, y = 0;
                    if (sscanf(datePart.c_str(), "%d-%d-%d", &d, &m, &y) == 3) {
                        DateFolder df;
                        df.path = parentPath + "/" + name;
                        df.day = d; df.month = m; df.year = y;
                        df.isDir = false;
                        candidates.push_back(df);
                    }
                }
            }
            entry.close();
            entry = root.openNextFile();
            yield();
        }
        root.close();
    } else if (root) {
        root.close();
    }

    // Extended roots: camera, crash logs, debug, GRBL jobs and sensor history
    // also age out under the same keep_days policy. Aggregates/undated names
    // (PENDING, MOTION, PHOTOS, wk*-oee.csv, *.chart) never parse as dates, so
    // they are never candidates. One entry per invocation (same gentle cadence).
    const char* extraRoots[] = { "/CAM", "/logs", "/DEBUG", "/GRBL_JOBS", "/sensor_history" };
    for (int r = 0; r < 5; r++) {
        const char* rootPath = extraRoots[r];
        if (!SD_MMC.exists(rootPath)) continue;
        File rr = SD_MMC.open(rootPath);
        if (!rr || !rr.isDirectory()) { if (rr) rr.close(); continue; }
        File ee = rr.openNextFile();
        while (ee) {
            String name = String(ee.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            int ymd = sd_layout::parseDateFromName(name);
            if (ymd > 0) {
                DateFolder df;
                df.path = String(rootPath) + "/" + name;
                df.isDir = ee.isDirectory();
                sd_layout::dateYmdParts(ymd, df.year, df.month, df.day);
                candidates.push_back(df);
            }
            ee.close();
            ee = rr.openNextFile();
            yield();
        }
        rr.close();
    }

    if (candidates.empty()) { lastCleanupMs = nowMs; return; }

    // Sort oldest first.
    std::sort(candidates.begin(), candidates.end(), [](const DateFolder& a, const DateFolder& b) {
        if (a.year != b.year) return a.year < b.year;
        if (a.month != b.month) return a.month < b.month;
        return a.day < b.day;
    });

    time_t nowT = time(nullptr);
    for (const auto& df : candidates) {
        int ageDays = -1;
        if (nowT > 100000) {
            struct tm folder_tm = {};
            folder_tm.tm_year = df.year - 1900;
            folder_tm.tm_mon  = df.month - 1;
            folder_tm.tm_mday = df.day;
            folder_tm.tm_hour = 12;
            time_t folder_t = mktime(&folder_tm);
            if (folder_t > 0) {
                ageDays = (int)(difftime(nowT, folder_t) / 86400.0);
            }
        }
        // HARD tier: oldest goes whatever its age. SOFT tier: only past keep_days.
        if (!overThreshold && !sd_layout::cleanupDeleteAllowed(ageDays, keepDays)) continue;
        if (df.isDir ? sdcardDeleteFolderRecursive(df.path) : SD_MMC.remove(df.path.c_str())) {
            if (overThreshold) {
                sendVirtualMessage(buildMessage("🗑️ SD Auto-cleanup: deleted %s (usage >%d%%)",
                                                  df.path.c_str(), threshold), INFO);
            } else {
                sendVirtualMessage(buildMessage("🗑️ SD Auto-cleanup: deleted %s (older than %dd)",
                                                  df.path.c_str(), keepDays), INFO);
            }
        }
        break; // delete one folder per call, re-check on next invocation
    }
    lastCleanupMs = nowMs;
}
#endif

#include "flash_fallback_cleanup.h"

// Smart flush logic - flush after configured interval + first reception pause
// Rule: After flush_interval elapsed since last flush, wait for first reception pause to flush
// This ensures we write complete data bursts without interrupting active reception
static void flushSdcardBuffers() {
#if ENABLE_SDCARD==1
    static unsigned long lastCheck = 0;
    unsigned long now = millis();

    // Parse flush_interval from config (2s, 10s, 15s, 30s, 1m, 5m, 10m, 30m, 60m)
    String flushIntervalStr = configManager.getSdcardFlushInterval();
    FlushInterval flushInterval = stringToInterval(flushIntervalStr);
    unsigned long pauseMs = intervalToMs(flushInterval);
    if (pauseMs < SDCARD_DEFAULT_WRITE_PAUSE_MS) pauseMs = SDCARD_DEFAULT_WRITE_PAUSE_MS;
    
    // ⌛ Check periodically (every 500ms to reduce CPU load)
    if (now - lastCheck < 500) return;
    lastCheck = now;
    
    if (sdcardFreezeWrites) return;
    
    // 🕒 Update timer-expired flags
    // Timer expires when pauseMs has elapsed since last flush
    if (!sdcardTimerExpired_RS232 && (now - lastSdcardFlush_RS232) >= pauseMs) {
        sdcardTimerExpired_RS232 = true;
    }
    if (!sdcardTimerExpired_LPT && (now - lastSdcardFlush_LPT) >= pauseMs) {
        sdcardTimerExpired_LPT = true;
    }

    // 👌 Smart flush logic: Timer expired + first reception pause (200ms minimum)
    // Only swap when the flush buffer is empty to avoid overwriting pending data
    if (sdcardSwapLock(pdMS_TO_TICKS(5))) {
        auto* flushRS232 = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);
        auto* flushLPT = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);
        const bool rs232CanSwap = (sdcardActiveBytes_RS232 > 0) && flushRS232 && flushRS232->empty();
        const bool lptCanSwap = (sdcardActiveBytes_LPT > 0) && flushLPT && flushLPT->empty();

        // 📝 RS232: Flush if timer expired (force flush after pauseMs regardless of reception pauses)
        if (rs232CanSwap && sdcardTimerExpired_RS232) {
            const unsigned long pauseSinceLastByte = (sdcardLastByteTime_RS232 > 0) ? (now - sdcardLastByteTime_RS232) : pauseMs;
            
            // ✅ CRITICAL FIX: Flush after timeout even with continuous reception
            // If timer expired, accept any brief pause (even 50ms) to flush complete lines
            // This fixes issue where continuous data reception (e.g., every 4s) blocks SD writes
            const unsigned long minPauseForFlush = 50; // 50ms minimum pause (down from 200ms)
            
            if (pauseSinceLastByte >= minPauseForFlush) {
                auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
                
                if (activeRS232 && !activeRS232->empty()) {
                    int lastCompleteIdx = findLastCompleteLineInBuffer(activeRS232);
                    
                    // ✅ CRITICAL FIX: If no complete lines but timeout exceeded (30s absolute), force flush anyway
                    // This prevents data from staying in buffer indefinitely when commands lack newlines
                    const unsigned long ABSOLUTE_FLUSH_TIMEOUT_MS = 30000; // 30s absolute limit
                    const bool forceFlushNoNewline = (lastCompleteIdx < 0) && ((now - lastSdcardFlush_RS232) >= ABSOLUTE_FLUSH_TIMEOUT_MS);
                    
                    if (lastCompleteIdx >= 0 || forceFlushNoNewline) {
                        // Move complete lines (or ALL data if forcing) to flushBuffer
                        flushRS232->clear();
                        
                        if (forceFlushNoNewline) {
                            // No complete lines found, but timeout exceeded: flush ALL data
                            size_t activeBytes = sdcardActiveBytes_RS232.load(std::memory_order_acquire);
                            sendVirtualMessage(buildMessage("⚠️ SD RS232: Force-flushing %u bytes (no newlines, %lu sec timeout)", 
                                                              (unsigned)activeBytes, (now - lastSdcardFlush_RS232) / 1000), WARN);
                            for (size_t i = 0; i < activeRS232->size(); i++) {
                                flushRS232->push_back((*activeRS232)[i]);
                            }
                            activeRS232->clear();
                            sdcardActiveBytes_RS232 = 0;
                        } else {
                            // Normal case: flush complete lines only
                            for (int i = 0; i <= lastCompleteIdx; i++) {
                                flushRS232->push_back((*activeRS232)[i]);
                            }
                            
                            // Keep incomplete lines in activeBuffer
                            std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> remaining;
                            for (size_t i = lastCompleteIdx + 1; i < activeRS232->size(); i++) {
                                remaining.push_back((*activeRS232)[i]);
                            }
                            activeRS232->swap(remaining);
                            
                            // Update byte counter
                            size_t remainingBytes = 0;
                            for (const auto& item : *activeRS232) {
                                remainingBytes += item.dataLength();
                            }
                            sdcardActiveBytes_RS232 = remainingBytes;
                        }
                        
                        lastSdcardFlush_RS232 = now;
                        sdcardTimerExpired_RS232 = false; // Reset timer
                        
                        if (sdCardWriteTaskHandle) xTaskNotifyGive(sdCardWriteTaskHandle);
                    }
                }
            }
        }

        // 📝 LPT: Flush if timer expired (force flush after pauseMs regardless of reception pauses)
        if (lptCanSwap && sdcardTimerExpired_LPT) {
            const unsigned long pauseSinceLastByte = (sdcardLastByteTime_LPT > 0) ? (now - sdcardLastByteTime_LPT) : pauseMs;
            
            // ✅ CRITICAL FIX: Flush after timeout even with continuous reception
            // If timer expired, accept any brief pause (even 50ms) to flush complete lines
            // This fixes issue where continuous data reception (e.g., every 4s) blocks SD writes
            const unsigned long minPauseForFlush = 50; // 50ms minimum pause (down from 200ms)
            
            if (pauseSinceLastByte >= minPauseForFlush) {
                auto* activeLPT = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
                
                if (activeLPT && !activeLPT->empty()) {
                    int lastCompleteIdx = findLastCompleteLineInBuffer(activeLPT);
                    
                    // ✅ CRITICAL FIX: If no complete lines but timeout exceeded (30s absolute), force flush anyway
                    // This prevents data from staying in buffer indefinitely when commands lack newlines
                    const unsigned long ABSOLUTE_FLUSH_TIMEOUT_MS = 30000; // 30s absolute limit
                    const bool forceFlushNoNewline = (lastCompleteIdx < 0) && ((now - lastSdcardFlush_LPT) >= ABSOLUTE_FLUSH_TIMEOUT_MS);
                    
                    if (lastCompleteIdx >= 0 || forceFlushNoNewline) {
                        // Move complete lines (or ALL data if forcing) to flushBuffer
                        flushLPT->clear();
                        
                        if (forceFlushNoNewline) {
                            // No complete lines found, but timeout exceeded: flush ALL data
                            size_t activeBytes = sdcardActiveBytes_LPT.load(std::memory_order_acquire);
                            sendVirtualMessage(buildMessage("⚠️ SD LPT: Force-flushing %u bytes (no newlines, %lu sec timeout)", 
                                                              (unsigned)activeBytes, (now - lastSdcardFlush_LPT) / 1000), WARN);
                            for (size_t i = 0; i < activeLPT->size(); i++) {
                                flushLPT->push_back((*activeLPT)[i]);
                            }
                            activeLPT->clear();
                            sdcardActiveBytes_LPT = 0;
                        } else {
                            // Normal case: flush complete lines only
                            for (int i = 0; i <= lastCompleteIdx; i++) {
                                flushLPT->push_back((*activeLPT)[i]);
                            }
                            
                            // Keep incomplete lines in activeBuffer
                            std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> remaining;
                            for (size_t i = lastCompleteIdx + 1; i < activeLPT->size(); i++) {
                                remaining.push_back((*activeLPT)[i]);
                            }
                            activeLPT->swap(remaining);
                            
                            // Update byte counter
                            size_t remainingBytes = 0;
                            for (const auto& item : *activeLPT) {
                                remainingBytes += item.dataLength();
                            }
                            sdcardActiveBytes_LPT = remainingBytes;
                        }
                        
                        lastSdcardFlush_LPT = now;
                        sdcardTimerExpired_LPT = false; // Reset timer
                        
                        if (sdCardWriteTaskHandle) xTaskNotifyGive(sdCardWriteTaskHandle);
                    }
                }
            }
        }

        sdcardSwapUnlock();
    }

    sdAutoCleanupIfNeeded();
#endif
    flashAutoCleanupIfNeeded(); // no-op stub when FILESYSTEM is disabled
}
