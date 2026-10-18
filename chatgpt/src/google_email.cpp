// =============================================================
// 📧 google_email.cpp — Google Apps Script Email Implementation
// =============================================================
#include "feature_flags.h"

#if ENABLE_GOOGLE_EMAIL==1

#include "google_email.h"
#include "structures.h" // Shared structures (StorageFileInfo)
#include "sd_layout.h" // standard SD layout: flat day-files, dated subdirs, aggregates
#include "ConfigManager.h"
#include "commands.h" // For LogLevel enum and sendVirtualMessage
#include "email_transport.h"
#include "psram_vfs.h" // Shared PSRAM VFS
#include "sdcard.h" // sdcardShouldUseSD()
#include "flash_fallback_store.h" // no-SD LittleFS email merge (stub when disabled)
#include "email_flash_rpc.h" // stack-safe LittleFS handles (PSRAM email tasks)
#include "littlefs_manager.h" // littlefsBegin/Mounted for the Flash gate
#include <base64.h>
#include <esp_task_wdt.h>
#include "sd_lib.h" // SD backend shim (SDMMC or SPI)

// Wrappers for esp_task_wdt_add/delete that tolerate IDF 5.x "already subscribed"
// errors (ESP_ERR_INVALID_STATE). In IDF 4.x these were silent; in IDF 5.x they
// return an error code which would normally trigger an assert in debug builds.
static inline void wdt_add_current_task() {
    esp_err_t err = esp_task_wdt_add(NULL);
    // ESP_ERR_INVALID_STATE = already subscribed; not an error for our purposes.
    (void)err;
}
static inline void wdt_delete_current_task() {
    esp_err_t err = esp_task_wdt_delete(NULL);
    (void)err;
}
// Use SD_MMC.xxx() directly — no #define alias to avoid type conflict with SD.h

// External references
extern bool sdcardIsMounted();
extern SemaphoreHandle_t sdcardMutex;
extern bool sdMutexLock(TickType_t timeoutTicks);
extern void sdMutexUnlock();

// =============================================================
// 🔐 GLOBAL STATE
// =============================================================

SemaphoreHandle_t googleEmailMutex = nullptr;
GoogleEmailSession activeSession = {"", "", 0, 0, false};

static bool isAllowedEmailDayAttachment(const String &fileName) {
    // .chart binaries NEVER ride emails (graphs/OEE charts only) — same rule
    // as the SMTP sender (live .24 2026-10-14). Applies to every pack.
    String lowerName = fileName;
    lowerName.toLowerCase();
    return lowerName.endsWith(F(".txt")) ||
           lowerName.endsWith(F(".csv")) ||
           lowerName.endsWith(F(".bin")) ||
           lowerName.endsWith(F(".log"));
}

// =============================================================
// 🔐 MUTEX HELPERS
// =============================================================

bool googleEmailLock(TickType_t timeoutTicks) {
    if (!googleEmailMutex) return true;
    return xSemaphoreTake(googleEmailMutex, timeoutTicks) == pdTRUE;
}

void googleEmailUnlock() {
    if (googleEmailMutex) xSemaphoreGive(googleEmailMutex);
}

// =============================================================
// 🚀 INITIALIZATION
// =============================================================

bool googleEmailInit() {
    if (!googleEmailMutex) {
        googleEmailMutex = xSemaphoreCreateMutex();
        if (!googleEmailMutex) {
            sendVirtualMessage(F("❌ GOOGLE_EMAIL: Failed to create mutex"), ERROR);
            return false;
        }
    }
    
    sendVirtualMessage(F("✅ GOOGLE_EMAIL: Initialized"), INFO);
    return true;
}

// =============================================================
// 🧠 AVAILABILITY CHECK
// =============================================================

bool googleEmailIsAvailable() {
    String url = googleEmailGetScriptUrl();
    if (url.length() == 0) {
        return false;
    }
    
    // Basic URL validation
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        return false;
    }

    return true;
}

// =============================================================
// 🛠️ HELPER: Get Script URL
// =============================================================

String googleEmailGetScriptUrl() {
    return configManager.getGoogleScriptUrl();
}

// =============================================================
// 🛠️ HELPER: Build Recipients
// =============================================================

String googleEmailBuildRecipients(const String &primary, const String &extra) {
    String result;
    
    if (primary.length() > 0 && extra.length() > 0) {
        result = primary + ", " + extra;
    } else if (primary.length() > 0) {
        result = primary;
    } else {
        result = extra;
    }
    
    return result;
}

// =============================================================
// 🛠️ HELPER: Base64 Encode (Heap-Efficient)
// =============================================================

String googleEmailBase64Encode(const uint8_t* data, size_t length) {
    return base64::encode(data, length);
}

// =============================================================
// 🛠️ HELPER: HTTP POST with Retry
// =============================================================

bool googleEmailHttpPost(const String &url, const String &payload, String &response, int maxRetries) {
    if (!emailTransportCanSendEmail()) {
        sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: ")) + emailTransportUnavailableReason(), ERROR);
        return false;
    }

    HTTPClient http;
    
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(GOOGLE_EMAIL_TIMEOUT_MS);
        // FORCE redirect: follow 302 as GET, not POST — prevents GAS from running twice
        // (STRICT_FOLLOW_REDIRECTS re-POSTs to the redirect URL, triggering the script a second time)
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        
        int httpCode = http.POST(payload);
        
        if (httpCode == 200 || httpCode == 201) {
            response = http.getString();
            http.end();
            return true;
        }
        
        if (attempt < maxRetries) {
            sendVirtualMessage(String(F("⚠️ GOOGLE_EMAIL: HTTP ")) + String(httpCode) + 
                                String(F(" (retry ")) + String(attempt) + String(F("/")) + 
                                String(maxRetries) + String(F(")")), WARN);
            delay(1000 * attempt); // Exponential backoff
        } else {
            sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: HTTP failed after ")) + 
                                String(maxRetries) + String(F(" retries: ")) + String(httpCode), ERROR);
        }
        
        http.end();
    }
    
    return false;
}

// =============================================================
// 📧 SESSION MANAGEMENT
// =============================================================

String googleEmailStartSession(const String &filename, const String &to, 
                                const String &subject, const String &body) {
    if (!googleEmailIsAvailable()) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Script URL not configured"), ERROR);
        return "";
    }
    
    String url = googleEmailGetScriptUrl() + "?action=start";
    
    String json = "{";
    json += "\"filename\":\"" + filename + "\",";
    json += "\"to\":\"" + to + "\",";
    json += "\"subject\":\"" + subject + "\",";
    json += "\"body\":\"" + body + "\"";
    json += "}";
    
    String response;
    if (googleEmailHttpPost(url, json, response, GOOGLE_EMAIL_MAX_RETRIES)) {
        sendVirtualMessage(String(F("✅ GOOGLE_EMAIL: Session started: ")) + response, DEBUG);
        return response;
    }
    
    return "";
}

bool googleEmailUploadChunk(const String &sessionId, int chunkNum, 
                             const uint8_t* data, size_t dataLen) {
    if (!googleEmailIsAvailable()) return false;
    
    String url = googleEmailGetScriptUrl() + "?action=chunk";
    
    String base64Data = googleEmailBase64Encode(data, dataLen);
    
    String json = "{";
    json += "\"sessionId\":\"" + sessionId + "\",";
    json += "\"chunkNum\":" + String(chunkNum) + ",";
    json += "\"data\":\"" + base64Data + "\"";
    json += "}";
    
    String response;
    bool success = googleEmailHttpPost(url, json, response, GOOGLE_EMAIL_MAX_RETRIES);
    
    if (success) {
        sendVirtualMessage(String(F("✅ GOOGLE_EMAIL: Chunk ")) + String(chunkNum) + 
                            String(F(" uploaded (")) + String(dataLen) + String(F(" bytes)")), DEBUG);
    }
    
    return success;
}

bool googleEmailFinishSession(const String &sessionId) {
    if (!googleEmailIsAvailable()) return false;
    
    String url = googleEmailGetScriptUrl() + "?action=finish";
    
    String json = "{\"sessionId\":\"" + sessionId + "\"}";
    
    String response;
    bool success = googleEmailHttpPost(url, json, response, GOOGLE_EMAIL_MAX_RETRIES);
    
    if (success) {
        sendVirtualMessage(F("✅ GOOGLE_EMAIL: Email sent successfully"), INFO);
    }
    
    return success;
}

void googleEmailCancelSession(const String &sessionId) {
    if (!googleEmailIsAvailable()) return;
    
    String url = googleEmailGetScriptUrl() + "?action=cancel";
    String json = "{\"sessionId\":\"" + sessionId + "\"}";
    
    String response;
    googleEmailHttpPost(url, json, response, 1); // Single attempt for cleanup
    
    sendVirtualMessage(F("🗑️ GOOGLE_EMAIL: Session cancelled"), DEBUG);
}

// =============================================================
// 📧 SIMPLE EMAIL (No Attachments)
// =============================================================

bool googleEmailSendSimple(const String &to, const String &subject, const String &body) {
    if (!googleEmailIsAvailable()) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Not available, cannot send simple email"), ERROR);
        return false;
    }
    
    if (!googleEmailLock(pdMS_TO_TICKS(10000))) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Mutex timeout (simple email)"), ERROR);
        return false;
    }
    
    String url = googleEmailGetScriptUrl() + "?action=simple";
    
    String json = "{";
    json += "\"to\":\"" + to + "\",";
    json += "\"subject\":\"" + subject + "\",";
    json += "\"body\":\"" + body + "\"";
    json += "}";
    
    String response;
    // maxRetries=1: send exactly once — GAS side-effects (email sent) happen on contact,
    // so retrying would duplicate emails even if the HTTP response was unexpected.
    bool success = googleEmailHttpPost(url, json, response, 1);
    
    googleEmailUnlock();
    
    return success;
}

// =============================================================
// 📧 EMAIL WITH FILE (Chunked Upload)
// =============================================================

bool googleEmailSendWithFile(const String &to, const String &subject, const String &body,
                              const String &filepath, const String &attachmentName) {
    if (!googleEmailIsAvailable()) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Not available"), ERROR);
        return false;
    }
    
    // Subscribe to watchdog for long operation
    wdt_add_current_task();
    
    // ✅ UNIFIED STORAGE: Check storage availability
    extern volatile StorageMode currentStorageMode;
    extern bool sdcardIsMounted();
    
    bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    // Flash fallback (LittleFS) resolves per file below, after the mode
    // backends miss (order per file: SD -> PSRAM -> Flash).
    bool usingFlash = false;
    String flashPath;

    if (!usingSd && !usingPsram && !(!sdcardShouldUseSD() && littlefsBegin() && littlefsMounted())) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: No storage available"), ERROR);
        wdt_delete_current_task();
        return false;
    }

    if (!googleEmailLock(pdMS_TO_TICKS(60000))) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Mutex timeout"), ERROR);
        wdt_delete_current_task();
        return false;
    }

    size_t fileSize = 0;

    // 1. Get File Size
    if (usingSd) {
        if (!sdMutexLock(pdMS_TO_TICKS(30000))) {
            sendVirtualMessage(F("❌ GOOGLE_EMAIL: SD mutex timeout"), ERROR);
            googleEmailUnlock();
            wdt_delete_current_task();
            return false;
        }
        File file = SD_MMC.open(filepath.c_str(), FILE_READ);
        if (!file) {
            sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: File not found (SD): ")) + filepath, ERROR);
            sdMutexUnlock();
            googleEmailUnlock();
            wdt_delete_current_task();
            return false;
        }
        fileSize = file.size();
        file.close();
        sdMutexUnlock();
    } else if (usingPsram) {
        // Extract folder and filename from path
        String folder = filepath.substring(0, filepath.lastIndexOf('/'));
        String fname = filepath.substring(filepath.lastIndexOf('/') + 1);

        // 🧠 Optimized: Get size first without loading content
        fileSize = psramVFSGetFileSize(folder, fname);
        if (fileSize == 0) {
             // File not found or empty: fall through to the Flash remap below.
        }
    }
    if (!usingSd && fileSize == 0 && !sdcardShouldUseSD()) {
        // Flash fallback (LittleFS): explicit path first, then SD-path remap.
        // emailFlash*() locks each op itself (direct vs loop-served RPC per the
        // caller stack) — no caller-held lfsMutex (would deadlock vs serve).
        flashPath = filepath;
        if (!emailFlashExists(flashPath)) {
            if (!flashFbFindFile(filepath, flashPath)) flashPath = "";
        }
        if (flashPath.length() > 0) {
            size_t probeSize = 0;
            if (emailFlashProbeSize(flashPath, probeSize) && probeSize > 0) {
                fileSize = probeSize;
                usingFlash = true;
            }
        }
    }
    
    sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Sending file ")) + attachmentName + 
                        String(F(" (")) + String(fileSize) + String(F(" bytes)")), INFO);
    
    // Allocate PSRAM buffer for entire file
    uint8_t* fileBuffer = nullptr;
    bool fileBufferInPsram = false; // CRITICAL: Track actual allocation type
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    if (psramFound() && fileSize > 0) {
        fileBuffer = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
        if (fileBuffer) {
            fileBufferInPsram = true; // Track allocation success
        }
    }
    #endif
    // Fallback to heap if no PSRAM or allocation failed (only for small files)
    if (!fileBuffer && fileSize > 0) {
        if (fileSize < 20000) { // Limit RAM usage
            fileBuffer = (uint8_t*)malloc(fileSize);
            fileBufferInPsram = false; // Track allocation type
        }
    }
    
    if (!fileBuffer && fileSize > 0) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Failed to allocate buffer"), ERROR);
        googleEmailUnlock();
        wdt_delete_current_task();
        return false;
    }
    
    // 2. Read File Content to Buffer
    size_t totalRead = 0;
    
    if (usingSd) {
        if (sdMutexLock(pdMS_TO_TICKS(10000))) {
            File file = SD_MMC.open(filepath.c_str(), FILE_READ);
            if (file) {
                 while (file.available() && totalRead < fileSize) {
                    size_t bytesRead = file.read(fileBuffer + totalRead, fileSize - totalRead);
                    if (bytesRead == 0) break;
                    totalRead += bytesRead;
                }
                file.close();
            }
            sdMutexUnlock();
        }
    } else if (usingFlash) {
        // Stack-safe read via emailFlash* handles, chunked (one op cap 64KB).
        int32_t efh = emailFlashOpenRead(flashPath);
        if (efh >= 0) {
            while (totalRead < fileSize) {
                size_t want = fileSize - totalRead;
                if (want > 16384) want = 16384;
                size_t bytesRead = emailFlashRead(efh, fileBuffer + totalRead, want);
                if (bytesRead == 0) break;
                totalRead += bytesRead;
            }
            emailFlashClose(efh);
        } else {
            sendVirtualMessage(F("GOOGLE_EMAIL: flash open failed for read"), WARN);
        }
    } else if (usingPsram) {
        // 🧠 Optimized: Read directly to buffer (zero extra allocation)
        String folder = filepath.substring(0, filepath.lastIndexOf('/'));
        String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
        
        // CRITICAL: Validate file exists and get size BEFORE attempting read
        size_t actualFileSize = psramVFSGetFileSize(folder, fname);
        if (actualFileSize == 0) {
            sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: PSRAM file not found or empty: ")) + fname, ERROR);
            if (fileBuffer) { if (fileBufferInPsram) heap_caps_free(fileBuffer); else free(fileBuffer); fileBuffer = nullptr; }
            googleEmailUnlock();
            wdt_delete_current_task();
            return false;
        }
        
        if (actualFileSize != fileSize) {
            char tmpMsg[256];
            snprintf(tmpMsg, sizeof(tmpMsg), "⚠️ GOOGLE_EMAIL: Size mismatch (expected=%u, actual=%u) for %s",
                     (unsigned)fileSize, (unsigned)actualFileSize, fname.c_str());
            sendVirtualMessage(String(tmpMsg), WARN);
            fileSize = actualFileSize;  // Use actual size
        }
        
        {
            char tmpMsg[256];
            snprintf(tmpMsg, sizeof(tmpMsg), "📖 GOOGLE_EMAIL: Reading %u bytes from PSRAM: %s", 
                     (unsigned)fileSize, fname.c_str());
            sendVirtualMessage(String(tmpMsg), DEBUG);
        }
        
        size_t bytesRead = psramVFSReadChunk(folder, fname, 0, fileBuffer, fileSize);
        
        if (bytesRead > 0) {
            totalRead = bytesRead;
            char tmpMsg[256];
            snprintf(tmpMsg, sizeof(tmpMsg), "✅ GOOGLE_EMAIL: Read %u/%u bytes from PSRAM", 
                     (unsigned)bytesRead, (unsigned)fileSize);
            sendVirtualMessage(String(tmpMsg), DEBUG);
        } else {
            sendVirtualMessage(F("❌ GOOGLE_EMAIL: PSRAM Read error (zero bytes returned)"), ERROR);
        }
    }
    
    if (totalRead != fileSize && fileSize > 0) {
        sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: Read mismatch (")) + 
                            String(totalRead) + String(F("/")) + String(fileSize) + String(F(")")), ERROR);
        if (fileBuffer) { if (fileBufferInPsram) heap_caps_free(fileBuffer); else free(fileBuffer); fileBuffer = nullptr; }
        googleEmailUnlock();
        wdt_delete_current_task();
        return false;
    }
    
    sendVirtualMessage(String(F("✅ GOOGLE_EMAIL: File loaded to buffer")), DEBUG);
    
    // Start session
    taskYIELD(); 
    vTaskDelay(pdMS_TO_TICKS(100)); 
    String sessionId = googleEmailStartSession(attachmentName, to, subject, body);
    if (sessionId.length() == 0) {
        if (fileBuffer) { if (fileBufferInPsram) heap_caps_free(fileBuffer); else free(fileBuffer); fileBuffer = nullptr; }
        googleEmailUnlock();
        wdt_delete_current_task();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    taskYIELD();
    
    // Upload chunks from PSRAM buffer
    int chunkNum = 0;
    bool success = true;
    size_t offset = 0;
    
    while (offset < fileSize && success) {
        size_t chunkSize = (fileSize - offset > GOOGLE_EMAIL_CHUNK_SIZE) ? 
                           GOOGLE_EMAIL_CHUNK_SIZE : (fileSize - offset);
        
        taskYIELD(); 
        if (!googleEmailUploadChunk(sessionId, chunkNum, fileBuffer + offset, chunkSize)) {
            success = false;
            break;
        }
        
        offset += chunkSize;
        chunkNum++;
        esp_task_wdt_reset(); 
        yield();
    }
    
    // Free file buffer using the correct allocator
    if (fileBuffer) { if (fileBufferInPsram) heap_caps_free(fileBuffer); else free(fileBuffer); fileBuffer = nullptr; }
    
    // Finish or cancel
    taskYIELD(); 
    vTaskDelay(pdMS_TO_TICKS(50));
    if (success) {
        success = googleEmailFinishSession(sessionId);
    } else {
        googleEmailCancelSession(sessionId);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    taskYIELD();
    
    googleEmailUnlock();
    wdt_delete_current_task();
    
    return success;
}

// =============================================================
// 📧 EMAIL WITH PARTIAL FILE (Chunked Upload with Byte Range)
// =============================================================

// Stream [startOffset, startOffset+dataSize) from a LittleFS file in chunks.
// Session must already be started; chunkNum is updated. Leaves session open.
static bool googleEmailUploadFlashRange(const String& sessionId, const String& flashPath,
                                        size_t startOffset, size_t dataSize, uint8_t* buffer,
                                        int& chunkNum) {
    // Stack-safe: handle-based reads (direct/RPC per the caller's stack); the
    // abstraction locks each op itself — no caller-held lfsMutex (deadlock vs
    // the loop serve side). GOOGLE_EMAIL_CHUNK_SIZE (50KB) fits the op cap.
    int32_t efh = emailFlashOpenRead(flashPath);
    if (efh < 0) {
        sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: File not found: ")) + flashPath, ERROR);
        return false;
    }
    bool ok = true;
    if (!emailFlashSeek(efh, startOffset)) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Seek failed"), ERROR);
        ok = false;
    }
    {
        size_t remaining = dataSize;
        while (remaining > 0 && ok) {
            size_t toRead = min(remaining, (size_t)GOOGLE_EMAIL_CHUNK_SIZE);
            size_t bytesRead = emailFlashRead(efh, buffer, toRead);
            if (bytesRead != toRead) {
                sendVirtualMessage(F("❌ GOOGLE_EMAIL: Read error"), ERROR);
                ok = false;
                break;
            }
            taskYIELD();
            if (!googleEmailUploadChunk(sessionId, chunkNum, buffer, bytesRead)) {
                ok = false;
                break;
            }
            remaining -= bytesRead;
            chunkNum++;
            esp_task_wdt_reset();
            yield();
        }
    }
    emailFlashClose(efh);
    return ok;
}

bool googleEmailSendWithFilePartial(const String &to, const String &subject, const String &body,
                                     const String &filepath, size_t startOffset, size_t endOffset,
                                     const String &attachmentName) {
    if (!googleEmailIsAvailable()) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Not available"), ERROR);
        return false;
    }
    
    // Subscribe to watchdog for long operation
    wdt_add_current_task();
    
    // ✅ UNIFIED STORAGE: Check storage availability
    extern volatile StorageMode currentStorageMode;
    extern bool sdcardIsMounted();
    
    bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    bool usingFlash = false;
    String flashPath = filepath;

    if (!usingSd && !usingPsram && !sdcardShouldUseSD()) {
        // Flash fallback (LittleFS): explicit path first, then SD-path remap.
        // emailFlash*() handles locking + direct/RPC dispatch internally.
        if (!emailFlashExists(flashPath)) {
            if (!flashFbFindFile(filepath, flashPath)) flashPath = "";
        }
        if (flashPath.length() > 0) {
            size_t probeSize = 0;
            if (emailFlashProbeSize(flashPath, probeSize) && probeSize > 0) {
                usingFlash = true;
            }
        }
    }

    if (!usingSd && !usingPsram && !usingFlash) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: No storage available"), ERROR);
        wdt_delete_current_task();
        return false;
    }
    
    if (!googleEmailLock(pdMS_TO_TICKS(60000))) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Mutex timeout"), ERROR);
        wdt_delete_current_task();
        return false;
    }
    
    size_t dataSize = endOffset - startOffset;
    sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Sending partial ")) + filepath + 
                        String(F(" [")) + String(startOffset) + String(F("-")) + 
                        String(endOffset) + String(F("] (")) + String(dataSize) + String(F(" bytes)")), INFO);
    
    // Allocate buffer for the range (partial content)
    uint8_t* buffer = nullptr;
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    if (psramFound()) {
        buffer = (uint8_t*)heap_caps_malloc(GOOGLE_EMAIL_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    }
    #endif
    if (!buffer) {
        buffer = (uint8_t*)malloc(GOOGLE_EMAIL_CHUNK_SIZE);
    }
    
    if (!buffer) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Failed to allocate range buffer"), ERROR);
        googleEmailUnlock();
        wdt_delete_current_task();
        return false;
    }
    
    String sessionId;
    
    // START SESSION
    taskYIELD(); 
    vTaskDelay(pdMS_TO_TICKS(100)); 
    sessionId = googleEmailStartSession(attachmentName, to, subject, body);
    if (sessionId.length() == 0) {
        if (buffer) {
            #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
            heap_caps_free(buffer);
            #else
            free(buffer);
            #endif
        }
        googleEmailUnlock();
        wdt_delete_current_task();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); 
    taskYIELD();
    
    // READ AND UPLOAD LOOP
    // Different strategy for SD vs PSRAM vs Flash (LittleFS fallback)
    // SD/Flash: Seek and read chunks
    // PSRAM: Read full file to string (sadly), then substring/memcpy

    bool success = true;
    int chunkNum = 0;
    bool tryFlashAfterVfsMiss = false;

    if (usingFlash) {
        // Pre-resolved Flash path (VFS disabled case); validated again on open.
        success = googleEmailUploadFlashRange(sessionId, flashPath, startOffset,
                                              dataSize, buffer, chunkNum);
    } else if (usingSd) {
        if (!sdMutexLock(pdMS_TO_TICKS(30000))) {
            sendVirtualMessage(F("❌ GOOGLE_EMAIL: SD mutex timeout"), ERROR);
            success = false;
        } else {
            File file = SD_MMC.open(filepath.c_str(), FILE_READ);
            if (!file) {
                sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: File not found: ")) + filepath, ERROR);
                success = false;
            } else {
                if (!file.seek(startOffset)) {
                     sendVirtualMessage(F("❌ GOOGLE_EMAIL: Seek failed"), ERROR);
                     success = false;
                } else {
                    size_t remaining = dataSize;
                    while (remaining > 0 && success) {
                        size_t toRead = min(remaining, (size_t)GOOGLE_EMAIL_CHUNK_SIZE);
                        size_t bytesRead = file.read(buffer, toRead);
                        
                        if (bytesRead != toRead) {
                            sendVirtualMessage(F("❌ GOOGLE_EMAIL: Read error"), ERROR);
                            success = false;
                            break;
                        }
                        
                        taskYIELD(); 
                        if (!googleEmailUploadChunk(sessionId, chunkNum, buffer, bytesRead)) {
                            success = false;
                            break;
                        }
                        
                        remaining -= bytesRead;
                        chunkNum++;
                        esp_task_wdt_reset();
                        yield();
                    }
                }
                file.close();
            }
            sdMutexUnlock();
        }
    } else if (usingPsram) {
        // PSRAM Strategy: Read chunks directly from VFS (no full file load)
        String folder = filepath.substring(0, filepath.lastIndexOf('/'));
        String fname = filepath.substring(filepath.lastIndexOf('/') + 1);
        
        size_t currentOffset = startOffset;
        size_t remaining = dataSize;
        
        // Sanity check size
        size_t totalSize = psramVFSGetFileSize(folder, fname);
        if (totalSize == 0 || totalSize < endOffset) {
            // VFS miss: the Flash mirror may hold it — fall through below.
            tryFlashAfterVfsMiss = true;
            sendVirtualMessage(String(F("📦 GOOGLE_EMAIL: PSRAM miss, trying Flash: ")) + fname, DEBUG);
        } else {
            char tmpMsg[256];
            snprintf(tmpMsg, sizeof(tmpMsg), "📖 GOOGLE_EMAIL: Reading PSRAM range [%u-%u] from %s (total=%u)",
                     (unsigned)startOffset, (unsigned)endOffset, fname.c_str(), (unsigned)totalSize);
            sendVirtualMessage(String(tmpMsg), DEBUG);
            
            while (remaining > 0 && success) {
                size_t toRead = min(remaining, (size_t)GOOGLE_EMAIL_CHUNK_SIZE);
                
                // Read chunk directly to upload buffer
                size_t bytesRead = psramVFSReadChunk(folder, fname, currentOffset, buffer, toRead);
                
                if (bytesRead != toRead) {
                    char tmpMsg[256];
                    snprintf(tmpMsg, sizeof(tmpMsg), "❌ GOOGLE_EMAIL: PSRAM chunk mismatch (got=%u, need=%u @ offset=%u)",
                             (unsigned)bytesRead, (unsigned)toRead, (unsigned)currentOffset);
                    sendVirtualMessage(String(tmpMsg), ERROR);
                    success = false;
                    break;
                }
                
                taskYIELD(); 
                if (!googleEmailUploadChunk(sessionId, chunkNum, buffer, bytesRead)) {
                    success = false;
                    break;
                }
                
                currentOffset += bytesRead;
                remaining -= bytesRead;
                chunkNum++;
                esp_task_wdt_reset();
                yield();
            }
            
            if (success) {
                char tmpMsg[256];
                snprintf(tmpMsg, sizeof(tmpMsg), "✅ GOOGLE_EMAIL: PSRAM range read complete (%u chunks)",
                         (unsigned)chunkNum);
                sendVirtualMessage(String(tmpMsg), DEBUG);
            }
        }
    }
    if (tryFlashAfterVfsMiss && success && !sdcardShouldUseSD()) {
        // VFS missed: the Flash mirror may hold it — same range upload.
        String fp = filepath;
        if (!emailFlashExists(fp) && !flashFbFindFile(filepath, fp)) fp = "";
        if (fp.length() > 0) {
            success = googleEmailUploadFlashRange(sessionId, fp, startOffset,
                                                  dataSize, buffer, chunkNum);
        } else {
            sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: file not found on PSRAM/Flash: ")) + filepath, ERROR);
            success = false;
        }
    }

    // Free buffer
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    heap_caps_free(buffer);
    #else
    free(buffer);
    #endif
    
    // Finish or cancel
    taskYIELD(); 
    vTaskDelay(pdMS_TO_TICKS(50));
    if (success) {
        success = googleEmailFinishSession(sessionId);
    } else {
        googleEmailCancelSession(sessionId);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    taskYIELD();
    
    googleEmailUnlock();
    wdt_delete_current_task();
    
    return success;
}

// =============================================================
// 📧 EMAILDAY (Folder with Multiple Files)
// =============================================================

bool googleEmailSendEmailDay(const String &folderPath, const String &subject, 
                              const String &bodyText, size_t maxBytesPerEmail,
                              const String &shiftEmails) {
    if (!googleEmailIsAvailable()) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: Not available for EMAILDAY"), ERROR);
        return false;
    }
    
    // ✅ UNIFIED STORAGE: Check storage availability (SD or PSRAM)
    extern volatile StorageMode currentStorageMode;
    extern bool sdcardIsMounted();
    
    bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    // Flash fallback mirror covers no-SD without PSRAM (checked again at listing).
    bool flashMayHaveFiles = !usingSd && littlefsBegin() && littlefsMounted();

    if (!usingSd && !usingPsram && !flashMayHaveFiles) {
        sendVirtualMessage(F("❌ GOOGLE_EMAIL: No storage available (SD/PSRAM/Flash)"), ERROR);
        return false;
    }
    
    sendVirtualMessage(String(F("📧 GOOGLE_EMAIL EMAILDAY: ")) + folderPath + 
                         (usingPsram ? F(" [PSRAM]") : F(" [SD]")), INFO);
    
    // List files in folder using unified storage API
    std::vector<StorageFileInfo> files;

    // Parse "@" date filter: folderPath may be "/{base}@DD-MM-YYYY".
    // SD listing expands one level of date-matching source subfolders; PSRAM uses folderPath directly.
    String actualFolderPath = folderPath;
    String dateSuffixFilter = "";
    {
        int atPos = folderPath.indexOf('@');
        if (atPos >= 0) {
            actualFolderPath = folderPath.substring(0, atPos);
            dateSuffixFilter = folderPath.substring(atPos + 1);
        }
    }

    // Single-file pre-probe (explicit path, e.g. SHIFTDAY csvPath): resolve on
    // the first tier holding it. The SD probe below handles the mounted case;
    // VFS/Flash are covered here. Sets fileMode to skip folder listing.
    bool emaildayFileMode = false;
    if (dateSuffixFilter.isEmpty() && !usingSd) {
        String pname = actualFolderPath;
        int psl = pname.lastIndexOf('/');
        String pbase = (psl >= 0) ? pname.substring(psl + 1) : pname;
        String pdir = (psl > 0) ? pname.substring(0, psl) : String("/");
        if (isAllowedEmailDayAttachment(pbase)) {
            size_t psz = 0;
            uint8_t pbe = 0;
            String ppath;
            if (psramVFSIsEnabled()) {
                psz = psramVFSGetFileSize(pdir, pbase);
                if (psz > 0) pbe = STORAGE_BACKEND_PSRAM;
            }
            if (psz == 0 && flashMayHaveFiles) {
                String fp;
                if (flashFbFindFile(actualFolderPath, fp)) {
                    size_t flashSize = 0;
                    if (emailFlashProbeSize(fp, flashSize) && flashSize > 0) {
                        psz = flashSize;
                        pbe = STORAGE_BACKEND_FLASH;
                        ppath = fp;
                    }
                }
            }
            if (psz > 0) {
                StorageFileInfo info;
                info.name = pbase;
                info.size = psz;
                info.fullPath = ppath;
                info.backend = pbe;
                files.push_back(info);
                if (pbe == STORAGE_BACKEND_FLASH && ppath.length() > 0) {
                    actualFolderPath = ppath.substring(0, ppath.lastIndexOf('/'));
                } else {
                    actualFolderPath = pdir;
                }
                emaildayFileMode = true;
                sendVirtualMessage(String(F("GOOGLE_EMAIL: single-file mode: ")) + pbase, INFO);
            }
        }
    }

    // ============================================================================
    // UNIFIED STORAGE: List files from SD, PSRAM or Flash (order SD -> PSRAM -> Flash)
    // ============================================================================
    if (usingSd && !emaildayFileMode) {
        // Caller already owns SD mutex; do not re-acquire.

        // FILE MODE (standard flat day-files, e.g. /{sys}/PROXIMITY_DD-MM-YYYY.csv):
        // an explicit path resolving to a FILE is attached directly, bypassing the
        // directory listing below. Folders keep the legacy listing untouched.
        // Only without "@" (callers pass "@" solely for folder listings).
        if (dateSuffixFilter.isEmpty()) {
            File probe = SD_MMC.open(actualFolderPath.c_str());
            if (probe) {
                if (!probe.isDirectory()) {
                    size_t psize = probe.size();
                    String pname = actualFolderPath;
                    int sl = pname.lastIndexOf('/');
                    String base = (sl >= 0) ? pname.substring(sl + 1) : pname;
                    if (isAllowedEmailDayAttachment(base)) {
                        StorageFileInfo info;
                        info.name = base;
                        info.size = psize;
                        files.push_back(info);
                        actualFolderPath = (sl > 0) ? pname.substring(0, sl) : String("/");
                        emaildayFileMode = true;
                        sendVirtualMessage(String(F("GOOGLE_EMAIL: single-file mode: ")) + base, INFO);
                    } else {
                        sendVirtualMessage(String(F("GOOGLE_EMAIL: unsupported file type: ")) + base, ERROR);
                    }
                }
                probe.close();
            }
        }
        size_t skippedFiles = 0;
        if (!emaildayFileMode) {
            File dir = SD_MMC.open(actualFolderPath.c_str());
            if (!dir || !dir.isDirectory()) {
                sendVirtualMessage(String(F("GOOGLE_EMAIL: Folder not found: ")) + actualFolderPath, ERROR);
                if (dir) dir.close();
                return false;
            }

            File file;
            while ((file = dir.openNextFile())) {
                if (!file) break;

                String fname = String(file.name());
                size_t fsize = file.size();
                if (file.isDirectory()) {
                    // Expand one level: include files from date-matching source subfolders.
                    // e.g. "RS232_26-03-2026" -> stored as "RS232_26-03-2026/{file}".
                    bool matchesFilter = dateSuffixFilter.isEmpty() || (fname.indexOf(dateSuffixFilter) >= 0);
                    if (matchesFilter) {
                        String subPath = actualFolderPath + "/" + fname;
                        File subDir = SD_MMC.open(subPath.c_str());
                        if (subDir && subDir.isDirectory()) {
                            File sf;
                            while ((sf = subDir.openNextFile())) {
                                if (!sf) break;
                                String sfName = String(sf.name());
                                size_t sfSize = sf.size();
                                if (!sf.isDirectory() && isAllowedEmailDayAttachment(sfName)) {
                                    StorageFileInfo info;
                                    info.name = fname + "/" + sfName;
                                    info.size = sfSize;
                                    files.push_back(info);
                                }
                                sf.close();
                            }
                            subDir.close();
                        }
                    } else {
                        skippedFiles++;
                    }
                } else if (isAllowedEmailDayAttachment(fname) &&
                           (dateSuffixFilter.isEmpty() || fname.indexOf(dateSuffixFilter) >= 0 ||
                            sd_layout::isAggregate(fname))) {
                    StorageFileInfo info;
                    info.name = fname;
                    info.size = fsize;
                    files.push_back(info);
                } else {
                    skippedFiles++;
                }
                file.close();
            }
            dir.close();
        } // !emaildayFileMode

        if (skippedFiles > 0) {
            sendVirtualMessage(String(F("GOOGLE_EMAIL: Skipped ")) + String(skippedFiles) + String(F(" unsupported file(s)")), INFO);
        }
    } else if (usingPsram && !emaildayFileMode) {
        // PSRAM Mode: Use unified storage API (defined in structures.h).
        // storageListFiles already merges the Flash fallback mirror.
        std::vector<StorageFileInfo> listedFiles = storageListFiles(folderPath);
        size_t skippedFiles = 0;
        for (const auto &fi : listedFiles) {
            if (isAllowedEmailDayAttachment(fi.name)) {
                files.push_back(fi);
            } else {
                skippedFiles++;
            }
        }

        if (skippedFiles > 0) {
            sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Skipped ")) + String(skippedFiles) + String(F(" unsupported PSRAM file(s)")), INFO);
        }
    } else if (flashMayHaveFiles && !emaildayFileMode) {
        // Flash fallback mirror only (no SD, no PSRAM).
        std::vector<StorageFileInfo> flashFiles;
        flashFbCollectEmailFiles(actualFolderPath, dateSuffixFilter, flashFiles, true);
        size_t skippedFiles = 0;
        for (const auto &fi : flashFiles) {
            if (isAllowedEmailDayAttachment(fi.name)) {
                files.push_back(fi);
            } else {
                skippedFiles++;
            }
        }
        if (!flashFiles.empty()) {
            sendVirtualMessage(String(F("💾 GOOGLE_EMAIL: Flash: ")) + String(files.size()) + String(F(" file(s) from LittleFS")), INFO);
        }
        if (skippedFiles > 0) {
            sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Skipped ")) + String(skippedFiles) + String(F(" unsupported Flash file(s)")), INFO);
        }
    }
    
    if (files.empty()) {
        sendVirtualMessage(F("⚠️ GOOGLE_EMAIL: No files found"), WARN);
        return false;
    }
    
    sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Found ")) + String(files.size()) + String(F(" files")), INFO);
    
    // Split large files and batch files
    std::vector<StorageFileInfo> largeFiles;
    std::vector<StorageFileInfo> normalFiles;
    
    for (const auto &fi : files) {
        if (fi.size > maxBytesPerEmail) {
            largeFiles.push_back(fi);
        } else {
            normalFiles.push_back(fi);
        }
    }
    
    // Send large files as individual parts
    for (const auto &fi : largeFiles) {
        size_t parts = (fi.size + maxBytesPerEmail - 1) / maxBytesPerEmail;
        
        for (size_t part = 0; part < parts; part++) {
            size_t startOffset = part * maxBytesPerEmail;
            size_t endOffset = min(startOffset + maxBytesPerEmail, fi.size);
            
            String partSubject = subject + " (" + fi.name + " part " + String(part + 1) + "/" + String(parts) + ")";
            String fullPath = (fi.backend == STORAGE_BACKEND_FLASH && fi.fullPath.length() > 0)
                ? fi.fullPath : actualFolderPath + "/" + fi.name;
            String attachmentName = fi.name + ".part" + String(part + 1);
            
            // Get recipients (command-specific extras + SHIFTDAY addresses pre-merged into shiftEmails)
            String dest = configManager.getEmailDestination();
            String extra = shiftEmails;
            String allRecipients = googleEmailBuildRecipients(dest, extra);
            
            if (!googleEmailSendWithFilePartial(allRecipients, partSubject, bodyText, 
                                                fullPath, startOffset, endOffset, attachmentName)) {
                sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: Failed part ")) + String(part + 1) + 
                                    String(F("/")) + String(parts), ERROR);
                return false;
            }
            
            delay(200); // Small delay between emails
        }
    }
    
    // Batch normal files
    std::vector<std::vector<StorageFileInfo>> batches;
    std::vector<StorageFileInfo> currentBatch;
    size_t currentBatchSize = 0;
    
    for (const auto &fi : normalFiles) {
        if (currentBatchSize + fi.size > maxBytesPerEmail && !currentBatch.empty()) {
            batches.push_back(currentBatch);
            currentBatch.clear();
            currentBatchSize = 0;
        }
        currentBatch.push_back(fi);
        currentBatchSize += fi.size;
    }
    
    if (!currentBatch.empty()) {
        batches.push_back(currentBatch);
    }
    
    sendVirtualMessage(String(F("📧 GOOGLE_EMAIL: Sending ")) + String(batches.size()) + String(F(" batches")), INFO);
    
    // Send each batch
    for (size_t batchNum = 0; batchNum < batches.size(); batchNum++) {
        String batchSubject = subject;
        if (batches.size() > 1) {
            batchSubject += " (" + String(batchNum + 1) + "/" + String(batches.size()) + ")";
        }
        
        // Get recipients (command-specific extras + SHIFTDAY addresses pre-merged into shiftEmails)
        String dest = configManager.getEmailDestination();
        String extra = shiftEmails;
        String allRecipients = googleEmailBuildRecipients(dest, extra);
        
        // For multiple files in batch, we need to send them as ZIP or separate emails
        // For now, send each file separately (simplest approach)
        for (const auto &fi : batches[batchNum]) {
            String fullPath = (fi.backend == STORAGE_BACKEND_FLASH && fi.fullPath.length() > 0)
                ? fi.fullPath : actualFolderPath + "/" + fi.name;

            if (!googleEmailSendWithFile(allRecipients, batchSubject, bodyText, fullPath, fi.name)) {
                sendVirtualMessage(String(F("❌ GOOGLE_EMAIL: Failed ")) + fi.name, ERROR);
                return false;
            }
        }
        
        delay(200); // Small delay between batches
    }
    
    sendVirtualMessage(F("✅ GOOGLE_EMAIL: EMAILDAY completed"), INFO);
    
    // 🔓 PSRAM MODE: Disable snapshot AFTER reading all files (prevents data corruption)
    // This MUST be done here, not in emailDayTask(), to avoid reallocing vectors while reading
    extern volatile StorageMode currentStorageMode;
    
    if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled() && psramVFSIsSnapshotActive()) {
        sendVirtualMessage(F("📸 EMAILDAY: Disabling PSRAM snapshot (all files read)"), INFO);
        
        size_t pendingCount = 0, pendingBytes = 0;
        psramVFSGetSnapshotStats(pendingCount, pendingBytes);
        sendVirtualMessage(String(F("   📦 Flushing pending writes: ")) + 
                             String(pendingCount) + String(F(" items, ")) + 
                             String(pendingBytes / 1024) + String(F(" KB")), INFO);
        
        psramVFSDisableSnapshot();  // Safe to flush now - all file reading complete
        sendVirtualMessage(F("   ✅ PSRAM snapshot disabled, pending writes flushed"), DEBUG);
    }
    
    return true;
}

#endif // ENABLE_GOOGLE_EMAIL
