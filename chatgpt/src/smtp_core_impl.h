// smtp_core_impl.h
// Included only from chatgpt.ino (single TU).
// SMTP core streaming functions: smtpSendEmailStreamingSD, smtpSendEmail,
// enviarEmailComAnexosSD, smtpSendEmailStreamingSDPartial, etc.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
#include "flash_fallback_store.h" // needed by SMTP funcs below (also included again at v1.118 block)
#include "email_flash_rpc.h" // stack-safe LittleFS (email tasks run with PSRAM stacks)
// Send email with SD attachments using AUTONOMOUS chunked SMTP streaming
// Does not use ReadyMail library - own TLS connection, doesn't accumulate MIME in RAM
// This function does everything: connects, authenticates, sends, disconnects
// TLS client allocated dynamically (~15KB) - freed after sending
static bool smtpSendEmailStreamingSD(
    const String &from,
    const String &recipients,
    const String &subject,
    const String &bodyText,
    const String &folderPath,
    const std::vector<SmtpStreamFile> &files, // name (MIME basename), size, path (Flash override)
    const String &server,
    int port,
    const String &user,
    const String &pass,
    bool tlsInsecure,
    const String &ramAttachmentName = "",
    const String &ramAttachmentContent = ""
) {
    // Reset watchdog at function entry (critical SMTP streaming operation)
    esp_task_wdt_reset();
    
    sendVirtualMessage(buildMessage("SMTP STREAM: %d file(s)", (int)files.size()), INFO);
    
    // ====================================================================
    // PHASE 0: Allocate and connect TLS
    // ====================================================================
    
    // Allocate TLS client dynamically (saves heap when idle)
    // Timeout 30s (not 120s): connect/read stall must stay well under the
    // 120s task watchdog — a single blocking client->connect() never feeds
    // the WDT, so 120s stalled SMTP == TWDT panic ~2min after trigger.
    // Same 30s the other streaming call sites already use.
    size_t h1 = ESP.getFreeHeap();
    Client* client = emailTransportCreateClient(tlsInsecure, 30000, 60);
    size_t h2 = ESP.getFreeHeap();
    sendVirtualMessage(buildMessage("SMTP: client alloc delta: %u bytes", h1 - h2), DEBUG);

    if (!client) {
        String reason = emailTransportUnavailableReason();
        if (reason.length() > 0) {
            sendVirtualMessage(String(F("SMTP: transport unavailable: ")) + reason, ERROR);
        } else {
            sendVirtualMessage(F("SMTP: failed to allocate network client"), ERROR);
        }
        return false;
    }
    
    // Note: setBufferSizes() removed (doesn't exist in ESP32 core 3.x / NetworkClientSecure)
    // The ~30-50KB RAM consumption is normal for SSL buffers (RX/TX + structures)
    
    sendVirtualMessage(buildMessage("SMTP: connect %s:%d...", server.c_str(), port), INFO);
    
    // Reset watchdog before TLS handshake (can take up to 60s)
    esp_task_wdt_reset();
    
    // CRITICAL: Stabilize scheduler before TLS connect (long blocking operation)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow priority inheritance to settle
    
    if (!client->connect(server.c_str(), port)) {
        sendVirtualMessage(buildMessage("SMTP: TLS connect failed %s:%d", server.c_str(), port), ERROR);
        delete client; // Free heap
        return false;
    }
    
    size_t h3 = ESP.getFreeHeap();
    sendVirtualMessage(buildMessage("SMTP: SSL connect OK (heap: %u)", (unsigned)h3), INFO);

    // Reset watchdog after successful connect
    esp_task_wdt_reset();
    
    // CRITICAL: Stabilize after TLS connect (settle priority inheritance)
    vTaskDelay(pdMS_TO_TICKS(100));
    taskYIELD();
    
    // Read server greeting (220).
    char response[256];
    int code = smtpWaitForCode(client, 220, /*multiline=*/false, 5000, response, sizeof(response));
    sendVirtualMessage(buildMessage("SMTP GREETING: %s", response), INFO);
    
    if (code != 220) {
        sendVirtualMessage(buildMessage("SMTP: invalid greeting (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // ====================================================================
    // PHASE 0.5: EHLO
    // ====================================================================
    smtpRawSendF(client, F("EHLO ESP32\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Read EHLO reply (250-... then 250 ...).
    response[0] = 0;
    code = smtpWaitForCode(client, 250, /*multiline=*/true, 3000, response, sizeof(response));
    
    if (code != 250) {
        sendVirtualMessage(buildMessage("SMTP: EHLO failed (%s)", response), ERROR);
    } else {
        sendVirtualMessage(F("SMTP: EHLO OK"), INFO);
    }
    
    // ====================================================================
    // PHASE 0.7: AUTH LOGIN
    // ====================================================================
    esp_task_wdt_reset(); // Reset before AUTH (network I/O)
    
    // CRITICAL: Stabilize before AUTH (network I/O)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    smtpRawSendF(client, F("AUTH LOGIN\r\n"));
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitForCode(client, 334, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 334) {
        sendVirtualMessage(buildMessage("SMTP: AUTH LOGIN rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // Send username/password using Base64 without large temporary Strings.
    char authLine[256];
    auto sendB64Line = [&](const String &in) -> bool {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const char *src = in.c_str();
        size_t len = in.length();
        size_t out = 0;
        for (size_t i = 0; i < len; i += 3) {
            uint8_t a = (uint8_t)src[i];
            uint8_t b = (i + 1 < len) ? (uint8_t)src[i + 1] : 0;
            uint8_t c = (i + 2 < len) ? (uint8_t)src[i + 2] : 0;
            if (out + 6 >= sizeof(authLine)) return false;
            authLine[out++] = b64[a >> 2];
            authLine[out++] = b64[((a & 3) << 4) | (b >> 4)];
            authLine[out++] = (i + 1 < len) ? b64[((b & 15) << 2) | (c >> 6)] : '=';
            authLine[out++] = (i + 2 < len) ? b64[c & 63] : '=';
        }
        if (out + 3 >= sizeof(authLine)) return false;
        authLine[out++] = '\r';
        authLine[out++] = '\n';
        authLine[out] = 0;
        return smtpRawSendC(client, authLine);
    };
    if (!sendB64Line(user)) {
        sendVirtualMessage(F("STREAM: failed to send Base64 username"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitForCode(client, 334, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 334) {
        sendVirtualMessage(buildMessage("SMTP: username rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    if (!sendB64Line(pass)) {
        sendVirtualMessage(F("STREAM: failed to send Base64 password"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Verify auth reply (235).
    response[0] = 0;
    code = smtpWaitForCode(client, 235, /*multiline=*/false, 5000, response, sizeof(response));
    sendVirtualMessage(buildMessage("SMTP AUTH: %s", response), INFO);
    
    if (code != 235) {
        sendVirtualMessage(F("SMTP: authentication failed"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    sendVirtualMessage(F("SMTP: authenticated"), INFO);
    
    // Generate MIME boundary
    String boundary = makeMimeBoundary();
    
    // ====================================================================
    // PHASE 1: Send SMTP envelope (MAIL FROM, RCPT TO)
    // ====================================================================
    
    // Reset watchdog before MAIL FROM (SMTP command, network I/O)
    esp_task_wdt_reset();
    
    // CRITICAL: Stabilize before MAIL FROM (SMTP envelope)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // MAIL FROM
    char line[512];
    snprintf(line, sizeof(line), "MAIL FROM:<%s>\r\n", from.c_str());
    smtpRawSendC(client, line);
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitFor2xx(client, 5000, response, sizeof(response));
    if (code < 200 || code >= 300) {
        sendVirtualMessage(buildMessage("SMTP: MAIL FROM rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // Reset watchdog before RCPT TO (SMTP command for each recipient, network I/O)
    esp_task_wdt_reset();
    
    // RCPT TO (for each recipient)
    int pos = 0;
    while (pos < (int)recipients.length()) {
        int sep1 = recipients.indexOf(',', pos);
        int sep2 = recipients.indexOf(';', pos);
        int sep = -1;
        if (sep1 < 0) sep = sep2;
        else if (sep2 < 0) sep = sep1;
        else sep = min(sep1, sep2);
        if (sep < 0) sep = recipients.length();
        
        String oneAddr = recipients.substring(pos, sep);
        oneAddr.trim();
        if (oneAddr.length() > 0 && oneAddr.indexOf('@') > 0) {
            // Reset watchdog for each RCPT TO (multiple recipients can take time)
            esp_task_wdt_reset();
            
            if (oneAddr.startsWith("<") && oneAddr.endsWith(">")) {
                oneAddr = oneAddr.substring(1, oneAddr.length() - 1);
            }
            snprintf(line, sizeof(line), "RCPT TO:<%s>\r\n", oneAddr.c_str());
            sendVirtualMessage(buildMessage("SMTP RCPT: %s", oneAddr.c_str()), INFO);
            smtpRawSendC(client, line);
            vTaskDelay(pdMS_TO_TICKS(50));
            response[0] = 0;
            code = smtpWaitFor2xx(client, 5000, response, sizeof(response));
            if (code < 200 || code >= 300) {
                sendVirtualMessage(buildMessage("SMTP: RCPT rejected (%s)", response), ERROR);
                client->stop();
                delete client;
                return false;
            }
        }
        pos = sep + 1;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to stabilize
    
    // DATA
    sendVirtualMessage(F("SMTP: sending DATA"), DEBUG);
    esp_task_wdt_reset(); // Reset before DATA (file streaming can be long)
    
    // CRITICAL: Stabilize before DATA (file streaming)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    if (!smtpRawSendF(client, F("DATA\r\n"))) {
         sendVirtualMessage(F("SMTP: failed to send DATA"), ERROR);
         return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Wait for 354 (go ahead).
    response[0] = 0;
    code = smtpWaitForCode(client, 354, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 354) {
        sendVirtualMessage(F("SMTP: DATA rejected (expected 354)"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // ====================================================================
    // PHASE 2: Send email headers
    // ====================================================================
    snprintf(line, sizeof(line), "From: <%s>\r\n", from.c_str());
    smtpRawSendC(client, line);

    snprintf(line, sizeof(line), "To: %s\r\n", recipients.c_str());
    smtpRawSendC(client, line);

    snprintf(line, sizeof(line), "Subject: %s\r\n", subject.c_str());
    smtpRawSendC(client, line);

    smtpRawSendF(client, F("MIME-Version: 1.0\r\n"));

    snprintf(line, sizeof(line), "Content-Type: multipart/mixed; boundary=\"%s\"\r\n", boundary.c_str());
    smtpRawSendC(client, line);
    
    smtpRawSendF(client, F("\r\n")); // End of headers
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // ====================================================================
    // PHASE 3: Send MIME body in streaming
    // ====================================================================
    
    // Preamble
    smtpRawSendF(client, F("This is a multi-part message in MIME format.\r\n"));
    
    // Parte 1: corpo de texto
    smtpRawSendF(client, F("--"));
    smtpRawSend(client, boundary);
    smtpRawSendF(client, F("\r\nContent-Type: text/plain; charset=UTF-8\r\n"));
    smtpRawSendF(client, F("Content-Transfer-Encoding: 7bit\r\n\r\n"));
    smtpRawSend(client, bodyText);
    smtpRawSendF(client, F("\r\n"));
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Reset watchdog before file streaming loop (critical multi-file operation)
    esp_task_wdt_reset();
    
    // Partes de anexos: streaming directo do SD, PSRAM ou Flash (LittleFS)
    for (const auto &fileInfo : files) {
        // Reset watchdog at start of EACH file (streaming can take minutes per file)
        esp_task_wdt_reset();

        const String &fileName = fileInfo.name;
        size_t fileSize = fileInfo.size;

        // Resolve the open path: explicit Flash path wins, else folder + name.
        String fullPath;
        if (fileInfo.path.length() > 0) {
            fullPath = fileInfo.path;
        } else if (folderPath.endsWith("/")) {
            fullPath.reserve(folderPath.length() + fileName.length());
            fullPath = folderPath + fileName;
        } else {
            fullPath.reserve(folderPath.length() + fileName.length() + 1);
            fullPath = folderPath + "/" + fileName;
        }
        
        // ============================================================================
        // UNIFIED STORAGE: per-file cascade, order always SD -> PSRAM -> Flash.
        // (PSRAM stays the hot tier; Flash is the persistent fallback.)
        // ============================================================================
        bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());

        File file;  // For SD/Flash mode (unused in PSRAM mode)
        String flashPath; // Flash backend path (fileSrc==3) — streaming is path-based,
                          // no LittleFS File ever lives on this task's stack
        bool fileValid = false;
        uint8_t fileSrc = 0; // 1 = SD, 2 = PSRAM, 3 = Flash

        if (usingSd) {
#if ENABLE_SDCARD==1
            // SD mutex for open/close operations (non-recursive, multi-core safe).
            bool sdLocked = false;
            extern bool sdMutexLock(TickType_t timeoutTicks);
            if (sdMutex != NULL) {
                if (!sdMutexLock(pdMS_TO_TICKS(5000))) {
                    sendVirtualMessage(F("STREAM: SD mutex timeout before open"), ERROR);
                    sdMutexDumpState();
                    client->stop();
                    delete client;
                    return false;
                }
                sdLocked = true;
                sdMutexTrackAcquire("SMTP_FILE_OPEN");
            }

            file = SD_MMC.open(fullPath);
            
            // Release mutex immediately after open. Streaming will manage SD locking per read chunk.
            extern void sdMutexUnlock();
            if (sdLocked) {
                sdMutexUnlock(); // Tracking automatic
                sdLocked = false;
            }

            if (!file) {
                sendVirtualMessage(String(F("STREAM: failed to open ")) + fileName, WARN);
                continue;
            }
            fileValid = true;
            fileSrc = 1;
#endif // ENABLE_SDCARD
        }
        if (!fileValid && psramVFSIsEnabled()) {
            // PSRAM: Validate file exists without loading it (streaming will access directly)
            size_t testSize = 0;
            const char* testPtr = psramVFSGetFileDataPtr(folderPath, fileName, &testSize);
            if (testPtr && testSize > 0) {
                fileValid = true;
                fileSrc = 2;
                sendVirtualMessage(buildMessage("📦 PSRAM: will stream %s (%u bytes) directly from memory",
                                                  fileName.c_str(), (unsigned)testSize), DEBUG);
            }
        }
        if (!fileValid && !sdcardShouldUseSD()) {
            // Flash fallback (LittleFS): explicit path first, then SD-path remap.
            // All metadata goes through emailFlash*() — NO caller-held lfsMutex
            // here: the abstraction locks each op itself (direct on an internal
            // stack, loop-served RPC on the PSRAM email tasks — holding lfsMutex
            // across an RPC would deadlock against the loop serve side). Busy or
            // timeout surfaces as probe failure -> candidate skipped, same as
            // the old contention skip (torn bytes beat a panic).
            String candPath = (fileInfo.path.length() > 0) ? fileInfo.path : fullPath;
            bool candIsFlash = (fileInfo.path.length() > 0) && emailFlashExists(candPath);
            if (!candIsFlash) candIsFlash = flashFbFindFile(candPath, flashPath);
            else flashPath = candPath;
            if (candIsFlash) {
                size_t flashSize = 0;
                if (emailFlashProbeSize(flashPath, flashSize) && flashSize > 0) {
                    fileValid = true;
                    fileSrc = 3;
                    sendVirtualMessage(String(F("💾 Flash: will stream ")) + fileName, DEBUG);
                }
            }
        }
        if (!fileValid) {
            sendVirtualMessage(String(F("STREAM: ")) + fileName + String(F(" not found on SD/PSRAM/Flash")), WARN);
            continue;
        }
        
        // Reset watchdog after opening file (before streaming starts)
        esp_task_wdt_reset();
        
        // sendVirtualMessage(buildMessage("STREAMING: %s (%u bytes)...", fileName.c_str(), (unsigned)fileSize), INFO); // Hidden
        
        // Attachment Part Header
        smtpRawSendF(client, F("--"));
        smtpRawSend(client, boundary);
        smtpRawSendF(client, F("\r\nContent-Type: application/octet-stream; name=\""));
        smtpRawSend(client, fileName);
        smtpRawSendF(client, F("\"\r\nContent-Disposition: attachment; filename=\""));
        smtpRawSend(client, fileName);
        smtpRawSendF(client, F("\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"));

        // STREAMING: read from SD, PSRAM or Flash and send Base64 directly to socket
        bool streamOk = false;

        if (fileSrc == 1) {
            // SD mode: thread-safe streaming with mutex (lock/unlock each chunk)
            streamOk = smtpStreamFileAsBase64(client, file, fileSize);
            
            // Reset watchdog after streaming each file (critical checkpoint)
            esp_task_wdt_reset();
            
            // CRITICAL FIX: Close file WITHOUT mutex (file.close() is thread-safe)
            // Background: Holding mutex during file.close() causes 60s deadlock if SD hangs
            // Solution: Close FIRST (no mutex), then release mutex if it was held for streaming
            // Rationale: file.close() is idempotent and internally synchronized by SD_MMC driver
            
            // WATCHDOG-GUARDED file.close() with timeout detection
            unsigned long closeStart = millis();
            
            // CRITICAL: Feed watchdog BEFORE close (prevents reset during slow operations)
            esp_task_wdt_reset();
            
            // Close file (may block if SD controller hung, but won't deadlock other operations)
            file.close();
            
            unsigned long closeDuration = millis() - closeStart;
            
            // ALERT: Log slow close operations (>1s is abnormal, >5s is critical)
            if (closeDuration > 5000) {
            char critMsg[150];
            snprintf(critMsg, sizeof(critMsg), "🚨 CRITICAL: SD close took %lums (file: %s) - possible controller hang", 
                     closeDuration, fileName.c_str());
            sendVirtualMessage(critMsg, ERROR);
            
            // If close took >5s, SD card may be in bad state
            // Log diagnostic info for postmortem analysis
            extern void sdMutexDumpState();
            sdMutexDumpState();
            
            // Recommend immediate reboot to reset SD controller
            sendVirtualMessage(F("⚠️ SD controller may be hung. Reboot recommended after email completes."), WARN);
        } else if (closeDuration > 1000) {
            char slowMsg[120];
            snprintf(slowMsg, sizeof(slowMsg), "⚠️ SLOW SD close: %lu ms (file: %s)", 
                     closeDuration, fileName.c_str());
            sendVirtualMessage(slowMsg, WARN);
        }
        // Mutex no longer held during close - streaming function released it after last read
        
        } else if (fileSrc == 2) {
            // PSRAM mode: stream directly from PSRAM memory (no String allocation for 512KB files)
            // No mutex needed - PSRAM is single-core safe, no controller blocking
            // CRITICAL FIX: Use direct pointer streaming instead of String allocation
            streamOk = smtpStreamPsramFileAsBase64(client, folderPath, fileName, fileSize);

            // Reset watchdog after PSRAM streaming
            esp_task_wdt_reset();

            // sendVirtualMessage(String(F("PSRAM: ")) + fileName + String(F(" streamed")), DEBUG);
        } else if (fileSrc == 3) {
            // Flash fallback (LittleFS): path-based streaming — reads go through
            // emailFlashRead() (direct/RPC per caller stack), no File on this task.
            streamOk = smtpStreamFlashAsBase64(client, flashPath, fileSize);
            esp_task_wdt_reset();
        } else {
            sendVirtualMessage(F("STREAM: no valid storage mode"), ERROR);
            streamOk = false;
        }

        if (!streamOk) {
            sendVirtualMessage(String(F("STREAM: failed at ")) + fileName, ERROR);
            client->stop();
            delete client;
            return false;
        }
        
        smtpRawSendF(client, F("\r\n")); // End of attachment Base64 section
        // sendVirtualMessage(String(F("STREAM: ")) + fileName + String(F(" sent")), INFO); // Hidden
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Optional RAM Attachment (e.g. for SD rule matches lines)
    if (ramAttachmentName.length() > 0 && ramAttachmentContent.length() > 0) {
        esp_task_wdt_reset();
        smtpRawSendF(client, F("--"));
        smtpRawSend(client, boundary);
        smtpRawSendF(client, F("\r\nContent-Type: text/plain; charset=UTF-8; name=\""));
        smtpRawSend(client, ramAttachmentName);
        smtpRawSendF(client, F("\"\r\nContent-Disposition: attachment; filename=\""));
        smtpRawSend(client, ramAttachmentName);
        smtpRawSendF(client, F("\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"));
        
        String b64;
        appendBase64Wrapped(b64, ramAttachmentContent);
        // Ensure no stray line breaks at the end to mess up MIME formatting if it doesn't end cleanly, 
        // appendBase64Wrapped adds CRLF correctly.
        smtpRawSend(client, b64);
        smtpRawSendF(client, F("\r\n"));
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Close MIME
    smtpRawSendF(client, F("--"));
    smtpRawSend(client, boundary);
    smtpRawSendF(client, F("--\r\n"));
    
    // ====================================================================
    // PHASE 4: Finish DATA
    // ====================================================================
    smtpRawSendF(client, F("\r\n.\r\n"));
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Verify end-of-DATA response (250).
    response[0] = 0;
    code = smtpWaitForCode(client, 250, /*multiline=*/false, 8000, response, sizeof(response));
    bool success = (code >= 200 && code < 300);
    // sendVirtualMessage(buildMessage("STREAM: %s (resp: %s)", success ? "OK" : "FAIL", response.c_str()), success ? INFO : ERROR); // Hidden
    
    // ====================================================================
    // PHASE 5: QUIT and disconnect
    // ====================================================================
    
    // Reset watchdog before QUIT (SMTP close command)
    esp_task_wdt_reset();
    
    smtpRawSendF(client, F("QUIT\r\n"));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // CRITICAL CLEANUP: Free socket before delete
    client->stop();
    delay(100); // Wait for complete socket closure
    client->flush();
    
    // Log heap before delete (diagnostic for ESP_ERR_NO_MEM)
    size_t heapBefore = ESP.getFreeHeap();
    delete client;
    size_t heapAfter = ESP.getFreeHeap();
    
    sendVirtualMessage(buildMessage("SMTP cleanup: heap %u -> %u (delta +%d)", 
                                      (unsigned)heapBefore, (unsigned)heapAfter, 
                                      (int)(heapAfter - heapBefore)), DEBUG);
    
    return success;
}

// Heap and email concurrency protection
volatile bool isEmailSending = false;
// CRITICAL: Use FreeRTOS mutex with priority inheritance to prevent deadlock
volatile bool emailBusy = false;
static SemaphoreHandle_t logMutex = NULL; // Recursive mutex for log protection with priority inheritance

static inline bool logLock(TickType_t timeoutTicks) {
    if (logMutex == NULL) {
        logMutex = xSemaphoreCreateRecursiveMutex(); // Create recursive mutex on first use
        if (logMutex == NULL) return false; // Failed to create
    }
    return xSemaphoreTakeRecursive(logMutex, timeoutTicks) == pdTRUE;
}

static inline void logUnlock() {
    if (logMutex != NULL) {
        xSemaphoreGiveRecursive(logMutex);
    }
}

bool checkHeapSafety(const char* context, size_t required = 15000) {
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < required) {
        sendVirtualMessage(String(F("CRITICAL HEAP at ")) + String(context) + F(": ") + String(freeHeap), ERROR);
        return false;
    }
    return true;
}

// Send email with partial SD file section using SMTP streaming
// Reads only from startOffset to endOffset - ideal for partial/chunked sends
// AUTONOMOUS: Makes own TLS connection, doesn't use ReadyMail
// Helper to split full path for PSRAM VFS
static bool splitPsramPath(const String& fullPath, String& folder, String& filename) {
    int lastSlash = fullPath.lastIndexOf('/');
    if (lastSlash < 0) return false;
    folder = fullPath.substring(0, lastSlash); 
    // If root (e.g. /file.txt), folder is empty string or just /? 
    // psramVFS expects folder like /2024-02-17.
    // fullPath usually /2024/02/17/file.txt -> folder /2024/02/17
    filename = fullPath.substring(lastSlash + 1);
    return true;
}

static bool smtpSendEmailStreamingSDPartial_Internal(
    const String &from,
    const String &recipients,
    const String &subject,
    const String &bodyText,
    const String &filePath,
    size_t startOffset,
    size_t endOffset,
    const String &attachmentName,
    const String &server,
    int port,
    const String &user,
    const String &pass,
    bool tlsInsecure) {
    
    size_t dataSize = endOffset - startOffset;
    sendVirtualMessage(buildMessage("AUTONOMOUS PARTIAL STREAM: %s [%u-%u] (%u bytes)", 
                                      filePath.c_str(), (unsigned)startOffset, (unsigned)endOffset, (unsigned)dataSize), INFO);
    
    // ====================================================================
    // PHASE 0: Allocate and connect TLS
    // ====================================================================
    
    // Allocate TLS client dynamically (saves heap when idle)
    // Timeout 30s (not 120s): see smtpSendEmailStreamingSD — blocking I/O
    // must stay well under the 120s task watchdog (TWDT panic otherwise).
    Client* client = emailTransportCreateClient(tlsInsecure, 30000, 60);
    if (!client) {
        String reason = emailTransportUnavailableReason();
        if (reason.length() > 0) {
            sendVirtualMessage(String(F("PARTIAL STREAM: transport unavailable: ")) + reason, ERROR);
        } else {
            sendVirtualMessage(F("PARTIAL STREAM: failed to allocate network client"), ERROR);
        }
        return false;
    }
    
    sendVirtualMessage(buildMessage("PARTIAL STREAM: connect %s:%d...", server.c_str(), port), DEBUG);
    
    // CRITICAL: Stabilize scheduler before TLS connect (long blocking operation)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow priority inheritance to settle
    
    if (!client->connect(server.c_str(), port)) {
        sendVirtualMessage(F("PARTIAL STREAM: failed TLS connection"), ERROR);
        delete client;
        return false;
    }
    
    // CRITICAL: Stabilize after TLS connect (settle priority inheritance)
    vTaskDelay(pdMS_TO_TICKS(100));
    taskYIELD();
    
    // Read server greeting (220).
    char response[256];
    int code = smtpWaitForCode(client, 220, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 220) {
        sendVirtualMessage(F("SMTP PARTIAL: invalid greeting"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // ====================================================================
    // EHLO + AUTH
    // ====================================================================
    smtpRawSendF(client, F("EHLO ESP32\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Read EHLO reply (250-... then 250 ...).
    response[0] = 0;
    code = smtpWaitForCode(client, 250, /*multiline=*/true, 3000, response, sizeof(response));
    if (code != 250) {
        sendVirtualMessage(buildMessage("SMTP PARTIAL: EHLO failed (%s)", response), WARN);
    }
    
    // AUTH LOGIN
    // CRITICAL: Stabilize before AUTH (network I/O)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    smtpRawSendF(client, F("AUTH LOGIN\r\n"));
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitForCode(client, 334, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 334) {
        sendVirtualMessage(buildMessage("SMTP PARTIAL: AUTH LOGIN rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // Username/password Base64 (avoid building large temporary Strings).
    char authLine[256];
    auto sendB64Line = [&](const String &in) -> bool {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const char *src = in.c_str();
        size_t len = in.length();
        size_t out = 0;
        for (size_t i = 0; i < len; i += 3) {
            uint8_t a = (uint8_t)src[i];
            uint8_t b = (i + 1 < len) ? (uint8_t)src[i + 1] : 0;
            uint8_t c = (i + 2 < len) ? (uint8_t)src[i + 2] : 0;
            if (out + 6 >= sizeof(authLine)) return false;
            authLine[out++] = b64[a >> 2];
            authLine[out++] = b64[((a & 3) << 4) | (b >> 4)];
            authLine[out++] = (i + 1 < len) ? b64[((b & 15) << 2) | (c >> 6)] : '=';
            authLine[out++] = (i + 2 < len) ? b64[c & 63] : '=';
        }
        if (out + 3 >= sizeof(authLine)) return false;
        authLine[out++] = '\r';
        authLine[out++] = '\n';
        authLine[out] = 0;
        return smtpRawSendC(client, authLine);
    };
    if (!sendB64Line(user)) {
        sendVirtualMessage(F("PARTIAL STREAM: failed to send Base64 username"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitForCode(client, 334, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 334) {
        sendVirtualMessage(buildMessage("SMTP PARTIAL: username rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    if (!sendB64Line(pass)) {
        sendVirtualMessage(F("PARTIAL STREAM: failed to send Base64 password"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Verify auth reply (235).
    response[0] = 0;
    code = smtpWaitForCode(client, 235, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 235) {
        sendVirtualMessage(F("SMTP PARTIAL: auth failed"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // ====================================================================
    // PHASE 1: Send SMTP commands
    // ====================================================================
    
    // Reset watchdog before MAIL FROM (SMTP command, network I/O)
    esp_task_wdt_reset();
    
    // CRITICAL: Stabilize before MAIL FROM (SMTP envelope)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    char line[512];

    // MAIL FROM
    snprintf(line, sizeof(line), "MAIL FROM:<%s>\r\n", from.c_str());
    smtpRawSendC(client, line);
    vTaskDelay(pdMS_TO_TICKS(100));
    response[0] = 0;
    code = smtpWaitFor2xx(client, 5000, response, sizeof(response));
    if (code < 200 || code >= 300) {
        sendVirtualMessage(buildMessage("SMTP PARTIAL: MAIL FROM rejected (%s)", response), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // Reset watchdog before RCPT TO (SMTP command for each recipient, network I/O)
    esp_task_wdt_reset();
    
    // RCPT TO - process multiple recipients
    String recipientsCopy = recipients;
    recipientsCopy.replace(" ", "");
    int startIdx = 0;
    int commaIdx;
    while ((commaIdx = recipientsCopy.indexOf(',', startIdx)) != -1) {
        String recipient = recipientsCopy.substring(startIdx, commaIdx);
        recipient.trim();
        if (recipient.length() > 0) {
            // Reset watchdog for each RCPT TO (multiple recipients can take time)
            esp_task_wdt_reset();
            
            snprintf(line, sizeof(line), "RCPT TO:<%s>\r\n", recipient.c_str());
            smtpRawSendC(client, line);
            vTaskDelay(pdMS_TO_TICKS(50));
            response[0] = 0;
            code = smtpWaitFor2xx(client, 5000, response, sizeof(response));
            if (code < 200 || code >= 300) {
                sendVirtualMessage(buildMessage("SMTP PARTIAL: RCPT rejected (%s)", response), ERROR);
                client->stop();
                delete client;
                return false;
            }
        }
        startIdx = commaIdx + 1;
    }
    // Last (or single) recipient
    String lastRecipient = recipientsCopy.substring(startIdx);
    lastRecipient.trim();
    if (lastRecipient.length() > 0) {
        // Reset watchdog for last RCPT TO
        esp_task_wdt_reset();
        
        snprintf(line, sizeof(line), "RCPT TO:<%s>\r\n", lastRecipient.c_str());
        smtpRawSendC(client, line);
        vTaskDelay(pdMS_TO_TICKS(100));
        response[0] = 0;
        code = smtpWaitFor2xx(client, 5000, response, sizeof(response));
        if (code < 200 || code >= 300) {
            sendVirtualMessage(buildMessage("SMTP PARTIAL: RCPT rejected (%s)", response), ERROR);
            client->stop();
            delete client;
            return false;
        }
    }
    
    // DATA
    esp_task_wdt_reset(); // Reset before DATA (partial stream can also be long)
    
    // CRITICAL: Stabilize before DATA (file streaming)
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    smtpRawSendF(client, F("DATA\r\n"));
    vTaskDelay(pdMS_TO_TICKS(200));

    // Wait for 354 (go ahead).
    response[0] = 0;
    code = smtpWaitForCode(client, 354, /*multiline=*/false, 5000, response, sizeof(response));
    if (code != 354) {
        sendVirtualMessage(F("SMTP PARTIAL: DATA rejected (expected 354)"), ERROR);
        client->stop();
        delete client;
        return false;
    }
    
    // ====================================================================
    // PHASE 2: Email headers
    // ====================================================================
    String boundary = makeMimeBoundary();

    snprintf(line, sizeof(line), "From: <%s>\r\n", from.c_str());
    smtpRawSendC(client, line);

    snprintf(line, sizeof(line), "To: %s\r\n", recipients.c_str());
    smtpRawSendC(client, line);

    snprintf(line, sizeof(line), "Subject: %s\r\n", subject.c_str());
    smtpRawSendC(client, line);

    smtpRawSendF(client, F("MIME-Version: 1.0\r\n"));

    snprintf(line, sizeof(line), "Content-Type: multipart/mixed; boundary=\"%s\"\r\n", boundary.c_str());
    smtpRawSendC(client, line);
    
    smtpRawSendF(client, F("\r\n")); // End of headers
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // ====================================================================
    // PHASE 3: MIME body streaming
    // ====================================================================
    
    // Preamble
    smtpRawSendF(client, F("This is a multi-part message in MIME format.\r\n"));
    
    // Parte 1: corpo de texto
    smtpRawSendF(client, F("--"));
    smtpRawSend(client, boundary);
    smtpRawSendF(client, F("\r\nContent-Type: text/plain; charset=UTF-8\r\n"));
    smtpRawSendF(client, F("Content-Transfer-Encoding: 7bit\r\n\r\n"));
    smtpRawSend(client, bodyText);
    smtpRawSendF(client, F("\r\n"));
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Part 2: attachment ? partial streaming from an SD file.
    bool sdLocked = false;
#if ENABLE_SDCARD==1
    extern bool sdMutexLock(TickType_t timeoutTicks);
    if (sdMutex != NULL) {
        if (!sdMutexLock(pdMS_TO_TICKS(30000))) {
            sendVirtualMessage(F("STREAM PARTIAL: SD mutex timeout before open"), ERROR);
            sdMutexDumpState();
            smtpRawSendF(client, F(".\r\n"));
            client->stop();
            delete client;
            return false;
        }
        sdLocked = true;
        sdMutexTrackAcquire("SMTP_PARTIAL_OPEN");
    }
#endif // ENABLE_SDCARD

    File file;
    bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    bool usingFlash = false;
    String flashPath; // hoisted: flash range streaming is path-based (no File here)
    String psramFolder, psramFile;

    if (usingSd) {
        file = SD_MMC.open(filePath.c_str());
    } else if (usingPsram) {
        if (!splitPsramPath(filePath, psramFolder, psramFile)) {
            sendVirtualMessage(F("STREAM PARTIAL: PSRAM path invalid"), ERROR);
        }
    }
    if (!usingSd && !usingPsram && !sdcardShouldUseSD()) {
        // Flash fallback (LittleFS): explicit path first, then SD-path remap.
        // emailFlash*() locks each op itself (direct vs loop-served RPC per the
        // caller stack) — no caller-held lfsMutex (would deadlock vs serve).
        flashPath = filePath;
        if (!emailFlashExists(flashPath)) {
            if (!flashFbFindFile(filePath, flashPath)) flashPath = "";
        }
        if (flashPath.length() > 0) {
            size_t flashSize = 0;
            if (emailFlashProbeSize(flashPath, flashSize) && flashSize > 0) {
                usingFlash = true;
            }
        }
    }

#if ENABLE_SDCARD==1
    extern void sdMutexUnlock();
    if (sdLocked) {
        sdMutexUnlock(); // Tracking automatic
        sdLocked = false;
    }
#endif // ENABLE_SDCARD
    
    if (usingSd && !file) {
        sendVirtualMessage(String(F("PARTIAL STREAM: failed to open ")) + filePath, ERROR);
        smtpRawSendF(client, F(".\r\n"));
        client->stop();
        delete client;
        return false;
    } else if (usingPsram && (psramFolder.isEmpty() || psramVFSGetFileSize(psramFolder, psramFile) == 0)) {
        sendVirtualMessage(String(F("PARTIAL STREAM: PSRAM file not found ")) + filePath, ERROR);
        smtpRawSendF(client, F(".\r\n"));
        client->stop();
        delete client;
        return false;
    } else if (!usingSd && !usingPsram && !usingFlash) {
        sendVirtualMessage(F("PARTIAL STREAM: No storage available"), ERROR);
        smtpRawSendF(client, F(".\r\n"));
        client->stop();
        delete client;
        return false;
    }
    
    // Attachment header
    smtpRawSendF(client, F("--"));
    smtpRawSend(client, boundary);
    smtpRawSendF(client, F("\r\n"));
    smtpRawSendF(client, F("Content-Type: text/plain; name=\""));
    smtpRawSend(client, attachmentName);
    smtpRawSendF(client, F("\"\r\n"));
    smtpRawSendF(client, F("Content-Disposition: attachment; filename=\""));
    smtpRawSend(client, attachmentName);
    smtpRawSendF(client, F("\"\r\n"));
    smtpRawSendF(client, F("Content-Transfer-Encoding: base64\r\n\r\n"));
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Partial range streaming: read from SD/PSRAM (specific offset range) and send Base64
    bool streamOk = false;
    
    if (usingSd) {
        streamOk = smtpStreamFileRangeAsBase64(client, file, startOffset, endOffset);
        // CRITICAL FIX: Close file WITHOUT mutex (file.close() is thread-safe)
        file.close();
    } else if (usingPsram) {
        streamOk = smtpStreamPsramBase64Internal(client, psramFolder, psramFile, startOffset, endOffset - startOffset);
    } else if (usingFlash) {
        // Path-based flash range streaming (emailFlashRead; direct/RPC per stack).
        streamOk = smtpStreamFlashRangeAsBase64(client, flashPath, startOffset, endOffset);
    }
    
    if (!streamOk) {
        sendVirtualMessage(F("PARTIAL STREAM: failed to encode Base64"), ERROR);
        smtpRawSendF(client, F(".\r\n"));
        client->stop();
        delete client;
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1));
    
    // Close MIME
    smtpRawSendF(client, F("--"));
    smtpRawSend(client, boundary);
    smtpRawSendF(client, F("--\r\n"));
    
    // ====================================================================
    // PHASE 4: Finish DATA
    // ====================================================================
    smtpRawSendF(client, F("\r\n.\r\n"));
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Verify end-of-DATA response (250).
    response[0] = 0;
    code = smtpWaitForCode(client, 250, /*multiline=*/false, 8000, response, sizeof(response));
    bool success = (code >= 200 && code < 300);
    sendVirtualMessage(buildMessage("SMTP PARTIAL: %s (resp: %s)", success ? "OK" : "FAIL", response), success ? INFO : ERROR);
    
    // ====================================================================
    // PHASE 5: QUIT and disconnect
    // ====================================================================
    
    // Reset watchdog before QUIT (SMTP close command)
    esp_task_wdt_reset();
    
    smtpRawSendF(client, F("QUIT\r\n"));
    vTaskDelay(pdMS_TO_TICKS(100));
    client->stop();
    delete client;
    
    return success;
}

// Send a simple plain-text email (no attachments) via autonomous SMTP/TLS.
// Uses the same low-level TLS approach as smtpSendEmailStreamingSD().
// Called by emailTestTask and inactivityAlertTask for simple alerts.
bool smtpSendEmail(
    const String &from,
    const String &to,
    const String &subject,
    const String &body,
    const String &server,
    int port,
    const String &user,
    const String &pass,
    bool tlsInsecure,
    bool isHtml)
{
    esp_task_wdt_reset();

    Client* client = emailTransportCreateClient(tlsInsecure, 30000, 60);
    if (!client) {
        String reason = emailTransportUnavailableReason();
        if (reason.length() > 0) {
            sendVirtualMessage(String(F("SMTP: transport unavailable: ")) + reason, ERROR);
        } else {
            sendVirtualMessage(F("SMTP: failed to allocate network client"), ERROR);
        }
        return false;
    }

    sendVirtualMessage(buildMessage("SMTP: connecting to %s:%d", server.c_str(), port), DEBUG);
    if (!client->connect(server.c_str(), port)) {
        sendVirtualMessage(F("SMTP: connect failed"), ERROR);
        delete client;
        return false;
    }
    esp_task_wdt_reset();

    // Read server greeting
    char lineBuf[256];
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 10000) || lineBuf[0] != '2') {
        sendVirtualMessage(F("SMTP: no greeting"), ERROR);
        client->stop(); delete client; return false;
    }

    // EHLO
    smtpRawSendC(client, "EHLO esp32\r\n");
    // Read until end of EHLO response (multi-line 250)
    for (int i = 0; i < 20; i++) {
        if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000)) break;
        if (strncmp(lineBuf, "250 ", 4) == 0) break; // Last line has space after code
    }
    esp_task_wdt_reset();

    // AUTH LOGIN
    smtpRawSendC(client, "AUTH LOGIN\r\n");
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000) || lineBuf[0] != '3') {
        sendVirtualMessage(F("SMTP: AUTH LOGIN rejected"), ERROR);
        client->stop(); delete client; return false;
    }

    // Username (base64 - no line wrapping needed for credentials)
    String userB64;
    appendBase64Wrapped(userB64, user);
    userB64.replace("\r\n", ""); // Remove line breaks (not allowed in SMTP AUTH)
    smtpRawSend(client, userB64 + "\r\n");
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000) || lineBuf[0] != '3') {
        sendVirtualMessage(F("SMTP: username rejected"), ERROR);
        client->stop(); delete client; return false;
    }

    // Password (base64 - no line wrapping needed for credentials)
    String passB64;
    appendBase64Wrapped(passB64, pass);
    passB64.replace("\r\n", ""); // Remove line breaks
    smtpRawSend(client, passB64 + "\r\n");
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 10000) || lineBuf[0] != '2') {
        sendVirtualMessage(F("SMTP: auth failed"), ERROR);
        client->stop(); delete client; return false;
    }
    esp_task_wdt_reset();

    // MAIL FROM
    smtpRawSend(client, String(F("MAIL FROM:<")) + from + ">\r\n");
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000) || lineBuf[0] != '2') {
        sendVirtualMessage(F("SMTP: MAIL FROM rejected"), ERROR);
        client->stop(); delete client; return false;
    }

    // RCPT TO (may be comma-separated)
    String rcptList = to;
    rcptList.replace(" ", "");
    int start = 0;
    while (start < (int)rcptList.length()) {
        int comma = rcptList.indexOf(',', start);
        String rcpt = (comma < 0) ? rcptList.substring(start) : rcptList.substring(start, comma);
        if (rcpt.length() > 0) {
            smtpRawSend(client, String(F("RCPT TO:<")) + rcpt + ">\r\n");
            if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000) || lineBuf[0] != '2') {
                sendVirtualMessage(buildMessage("SMTP: RCPT TO rejected: %s", rcpt.c_str()), ERROR);
                client->stop(); delete client; return false;
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    esp_task_wdt_reset();

    // DATA
    smtpRawSendC(client, "DATA\r\n");
    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 5000) || lineBuf[0] != '3') {
        sendVirtualMessage(F("SMTP: DATA rejected"), ERROR);
        client->stop(); delete client; return false;
    }

    // Headers + body
    smtpRawSend(client, String(F("From: ")) + from + "\r\n");
    smtpRawSend(client, String(F("To: ")) + to + "\r\n");
    smtpRawSend(client, String(F("Subject: ")) + subject + "\r\n");
    smtpRawSendC(client, "MIME-Version: 1.0\r\n");
    smtpRawSendC(client, isHtml ? "Content-Type: text/html; charset=UTF-8\r\n"
                                : "Content-Type: text/plain; charset=UTF-8\r\n");
    if (isAscii7(body)) {
        smtpRawSendC(client, "\r\n");
        smtpRawSend(client, body);
    } else {
        // Non-ASCII body (e.g. PT accents as UTF-8 bytes): base64, otherwise
        // MTAs treat the missing CTE as 7bit and mangle the accents. Same rule
        // as the multipart builder (buildMultipartMixedPlainText).
        smtpRawSendC(client, "Content-Transfer-Encoding: base64\r\n\r\n");
        String b64;
        b64.reserve(((body.length() + 2) / 3) * 4 + 64);
        appendBase64Wrapped(b64, body);
        smtpRawSend(client, b64);
    }
    smtpRawSendC(client, "\r\n.\r\n"); // End of DATA
    esp_task_wdt_reset();

    if (!smtpReadLine(client, lineBuf, sizeof(lineBuf), 15000) || lineBuf[0] != '2') {
        sendVirtualMessage(F("SMTP: message rejected"), ERROR);
        client->stop(); delete client; return false;
    }

    smtpRawSendC(client, "QUIT\r\n");
    smtpReadLine(client, lineBuf, sizeof(lineBuf), 3000);

    client->stop();
    delete client;
    sendVirtualMessage(F("SMTP: simple email sent OK"), INFO);
    return true;
}

// Wrapper with Heap and Mutex protection
static bool smtpSendEmailStreamingSDPartial(
    const String &from,
    const String &recipients,
    const String &subject,
    const String &bodyText,
    const String &filePath,
    size_t startOffset,
    size_t endOffset,
    const String &attachmentName,
    const String &server,
    int port,
    const String &user,
    const String &pass,
    bool tlsInsecure) {

    // 1. Check Heap before trying
    if (!checkHeapSafety("smtpSendEmailStreamingSDPartial", 25000)) return false;

    // 2. Adquirir lock via atomic flag (NO semaphores to avoid priority inheritance bugs)
    unsigned long lockStart = millis();
    while (emailBusy) {
        if (millis() - lockStart > 10000) { // 10s timeout
            sendVirtualMessage(F("Email busy (timeout)"), WARN);
            return false;
        }
        delay(50); // Yield CPU
    }
    emailBusy = true; // Acquire lock

    // 3. Mark global flag (so other parts of the system know)
    isEmailSending = true;

    // 4. Run real send
    bool res = smtpSendEmailStreamingSDPartial_Internal(
        from, recipients, subject, bodyText, filePath, startOffset, endOffset, 
        attachmentName, server, port, user, pass, tlsInsecure
    );

    // 5. Clear flag and release lock
    isEmailSending = false;
    emailBusy = false; // Release lock
    
    return res;
}
// ====================================================================================
// ENUM AND STRUCTURE DEFINITIONS
// ====================================================================================

// Log levels defined at file top; see enum LogLevel.

// Forward declaration (default values defined in commands.h)
// Many parts of code invoke sendVirtualMessage(msg, level) without the extra flags;
//    default values are declared in commands.h to avoid conflict
void sendVirtualMessage(const String &msg, int level, bool alreadyFormatted, bool bypassDedup);

// REMOVED: duplicate struct SerialMessage (now defined in structures.h with fixed buffers)

struct ScheduledCommand {
    String command; // The command to be executed
    int interval; // The time interval
    String unit; // The time unit (seconds, minutes, hours)
    String days; // The days of the week for execution
    bool enabled; // If the command is enabled
    unsigned long lastExecuted; // The time of last execution
};

// Ethernet/WiFi coordination is passive-only: keep transport sync without changing legacy WiFi/AP behavior.
static void runEthWifiCoordinator(){
    syncTelnetNetworkBindings();
}

// ===== FORWARD DECLARATIONS FOR WEBPAGE.H =====
void connectToWiFi(); // WiFi connect function declaration
void atualizarNTPTimezone(); // NTP timezone update function
// Ethernet/WiFi coordinator (declaration of periodic execution function)
static void runEthWifiCoordinator();

// Functions for printer emulation
void setPrinterEmulationMode(int mode);

// TEST: Using simplified web server for diagnostics
// Forward declarations needed by webpage.h handlers
void applyRS232Config();

// Forward declarations for functions defined later (needed for compilation)
static void addDataToSdcardBuffer(const String &source, const String &data);
static void addDataToSdcardBufferTimestamped(const String &source, const String &data, unsigned long timestamp);
void sendRs232Xoff();
void sendRs232Xon();
void sendRs232XoffRaw();
void sendRs232XonRaw();

// #include "webpage/webpage.h" // Temporarily commented
// Helper to perform a safe reboot (saving stats first)
void safeReboot(const char* reason = "UNKNOWN") {
    extern ConfigManager configManager;
    Serial.printf("🔄 Rebooting... Reason: %s\n", reason);

    // Force pending config flush before any reboot path (manual/API/OTA/scheduler).
    // This protects all settings that use markDirtyAndScheduleSave() from being lost
    // if reboot happens before the debounce window.
    bool cfgSaved = configManager.save(true);
    storNoteCfgSaveForced();
    if (!cfgSaved) {
        Serial.println(F("[REBOOT] ⚠️ configManager.save(true) failed before reboot"));
    }
    
    // 1. Save NVS/Flash storage stats (critical for counters)
    saveStorageStatsToNVS();

    // 2. Flush sensor history to SD (or PSRAM if SD absent) and save sensor config to LittleFS
    #if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
    Serial.println("[REBOOT] Flushing sensor history...");
    bool _rbFlushed = sensorConfigManager.getHistory().periodicFlushToSD(sensorConfigManager.getConfig());
    if (!_rbFlushed) {
        sensorConfigManager.getHistory().periodicFlushToPSRAM(sensorConfigManager.getConfig());
    }
    Serial.println("[REBOOT] Saving sensor config to LittleFS...");
    sensorConfigManager.save();
    #endif

    // 3. Flush log audit buffer to SD (blocking — ensures last entries survive the reboot)
    #if ENABLE_SDCARD==1
    Serial.println("[REBOOT] Flushing log audit buffer to SD...");
    logFlushNow();
    #endif

    // 3b. Flush coalesced LittleFS fallback buffers (same pre-reboot guarantee
    // as the SD path: no-SD data accumulated in PSRAM is persisted now).
    #if ENABLE_FILESYSTEM==1
    Serial.println("[REBOOT] Flushing LittleFS fallback buffers...");
    flashFbFlushAllSlots();
    #endif

    // 4. Unmount SD card (saves SD stats to NVS)
    #if ENABLE_SDCARD==1
    if (sdcardIsMounted()) {
        sdcardUnmount(); 
    }
    #endif

    // 5. Unmount LittleFS to flush any pending journal entries before reset.
    // ESP.restart() does not call filesystem destructors; calling littlefsEnd() here
    // ensures LittleFS writes (config saves, sensor data, etc.) are fully committed.
    #if ENABLE_FILESYSTEM==1
    extern bool littlefsEnd();
    littlefsEnd();
    #endif

    // 6. Flush serial buffer
    Serial.flush();
    delay(100);
    
    // 5. Restart
    ESP.restart();
}

// ====================================================================================
// FIRMWARE VERSION CONSTANTS (must be before webpage.h include)
// ====================================================================================

// Single source of truth for the version number.
// Changing only this line keeps FW, Build, and Build-datetime all in sync.
#define FIRMWARE_VERSION_STR "v1.120"
const char* FIRMWARE_VERSION          = FIRMWARE_VERSION_STR;
// Build identifier (aligned with "LAST GLOBAL CHANGE")
// Date/time generated AUTOMATICALLY on each compilation (uses __DATE__ and __TIME__ macros)
const char* FIRMWARE_BUILD_DATETIME   = __DATE__ " " __TIME__ " WET"; // Build date/time for diagnostics
// Complete version string ready to display (do not use for auto.js cache-busting)
const char* FIRMWARE_BUILD            = FIRMWARE_VERSION_STR " - " __DATE__ " " __TIME__ " WET";

#include "src/webpages/webpage.h" // Web server (includes forward declaration of activeHtmlSessions)
#include "sd_layout.h" // standard SD layout: flat day-files, dated subdirs, aggregates
#include "flash_fallback_store.h" // no-SD LittleFS email merge (stub when disabled)

// ====================================================================================
// GLOBAL VARIABLE AND MACRO DECLARATIONS
// ====================================================================================

// Default limit for BUSY activation based on buffer size
// Increased to ~50KB (supported by 128KB PSRAM buffer)
#ifndef BUSY_ACTIVATION_THRESHOLD_BYTES
#define BUSY_ACTIVATION_THRESHOLD_BYTES 98304
#endif

// Line hysteresis for DB25: delay BUSY/XOFF and effective trigger
//    until there is margin of +N lines beyond budget (66/90).
//    Avoids cuts at 65/89 caused by bytes in transit at threshold.
#ifndef DB25_LINE_HYST
#define DB25_LINE_HYST 2
#endif
#ifndef DB25_LINE_HYST_TIMEOUT_MS
#define DB25_LINE_HYST_TIMEOUT_MS 250
#endif

// Global reception buffer (fed by Nano UART bridge)
// OPTIMIZATION: Moved to PSRAM (128KB) via dynamic allocation in setup()
// Note: Removed 'volatile' keyword from pointer itself to avoid 'not declared' compilation errors,
//          but pointed content continues to be treated as volatile where needed.
uint8_t* bufferRx = nullptr; // Reception buffer (pointer to PSRAM)
volatile int writePos = 0; // Write position in bufferRx
volatile int readPos = 0;   // Read position in bufferRx
volatile unsigned long overflowCount = 0; // Buffer overflow count (volatile for ISR)

// IMPROVEMENT 2: LPT Serial Monitor REMOVED (optimization)
// LPT data now flows through the main 128KB bufferDados (same as RS232)
// Old lptMonitorBuffer (8KB heap) eliminated - saves heap and simplifies code

// Legacy system log message buffer — stored in PSRAM to save DRAM
// Uses PsramQueue (lazy PSRAM allocation; safe as global — allocates on first push, after PSRAM init)
#include "src/psram_queue.h"
PsramQueue<SerialMessage> serialMonitorBuffer(64); // 64 slots in PSRAM

// IMPROVEMENT 1: Batch Transfer Buffer (512 bytes)
// Stack-local buffer to transfer blocks instead of 1 byte at a time
#define BATCH_TRANSFER_SIZE 512
// Removed MCP/DB25 counters and BUSY thresholds - not needed in Nano bridge

// Printer state variables
volatile bool isPrinterBusy = false; // Flag to indicate if printer is busy
volatile bool isReceivingData = false; // Flag to indicate we are receiving data
unsigned long lastActivityTime = 0; // Last time there was activity (reception or sending)

// Ghost debounce variables (EMI/RFI noise rejection on floating pins)
static volatile unsigned long ghostDebounceStartMs = 0; // Debounce window start timestamp
static volatile int ghostDebounceByteCount = 0;         // Bytes received in current window
static const int GHOST_DEBOUNCE_MIN_BYTES = 8;          // Minimum 8 bytes in 100ms to validate (EMI noise filter)
static const unsigned long GHOST_DEBOUNCE_WINDOW_MS = 100; // 100ms window

// Counters
unsigned long lineCount = 0; // Line count
unsigned long charCount = 0; // Character count


// NVS Preferences
Preferences preferences; // Object to interact with NVS
const char* NVS_NAMESPACE = "printer_configs"; // Namespace name in NVS

// Structure to store all configurations (kept for reference, but globals are used)
struct AppConfig {
    String wifiSsid; // WiFi SSID
    String wifiPass; // WiFi password
    String smtpServer; // SMTP server
    int smtpPort; // SMTP port
    String smtpUser; // SMTP user
    String smtpPass; // SMTP password
    String emailDest; // Destination email
    bool autoSendEmail; // Automatic email send
    // ... other legacy fields
    std::map<String, String> destinatarios; // Name -> email map
    String emailSubject; // Email subject
    int emailLineLimit; // Line limit per email
    unsigned long emailInactivityTimeout; // Inactivity timeout for email send
    int telnetPort; // Telnet port
    unsigned long telnetInactivityTimeout; // Telnet inactivity timeout
};
// Removed invalid block that referenced saved_configs out of context

// AppConfig appConfig; // No longer used directly, global variables are.

#if ENABLE_SMTP // If SMTP is enabled
Client *smtpClient = nullptr; // Pointer to secure WiFi client (dynamically allocated)
SMTPClient *smtp = nullptr; // Pointer to SMTP client instance (dynamically allocated)

// Allocate SMTP resources only if needed
void ensureSmtpAllocated() {
    if (!smtpClient) {
        // Point 3: SSL/PSRAM optimization & verification (Core 3.x)
        // Dynamic allocation on heap (SSL buffers may be placed in PSRAM on OPI).
        // READYMAIL CHECK: PSRAM is considered safe, but requires tolerant timeouts.
        // Raised setHandshakeTimeout to 60s to compensate latency in heavy handshakes.
        smtpClient = emailTransportCreateClient(true, 30000, 60);
        if (!smtpClient) {
            String reason = emailTransportUnavailableReason();
            if (reason.length() > 0) {
                sendVirtualMessage(String(F("❌ SMTP transport unavailable: ")) + reason, ERROR);
            } else {
                sendVirtualMessage(buildMessage("❌ Failed to allocate network client (heap: %u)", 
                                                   (unsigned)ESP.getFreeHeap()), ERROR);
            }
        }
    }
    if (!smtp && smtpClient) {
        smtp = new SMTPClient(*smtpClient);
        if (!smtp) {
            sendVirtualMessage(buildMessage("❌ Failed to allocate SMTPClient (heap: %u)", 
                                               (unsigned)ESP.getFreeHeap()), ERROR);
        }
    }
}

// Free SMTP resources to save RAM
void releaseSmtpResources() {
    if (smtp) {
        delete smtp;
        smtp = nullptr;
    }
    if (smtpClient) {
        delete smtpClient;
        smtpClient = nullptr;
    }
}
#endif // ENABLE_SMTP

// Thresholds and failure protection for the email/SMTP subsystem
#if ENABLE_SMTP // Only relevant when SMTP is enabled
// Minimum heap required to start an SMTP/TLS connection (avoids severe fragmentation)
static const size_t EMAIL_MIN_HEAP_TO_CONNECT = 28 * 1024; // ~28KB
// Number of consecutive failures before firing a small "circuit breaker"
static const uint8_t EMAIL_FAILS_BEFORE_BREAKER = 3;
// Wait window when the breaker fires (ms)
static const unsigned long EMAIL_BREAKER_COOLDOWN_MS = 60UL * 1000UL; // 60s
// Breaker state
static uint8_t emailConsecutiveFailures = 0; // Consecutive failures
static unsigned long emailBreakerUntil = 0;  // Until when not to attempt

// Local helper: robust teardown of SMTP/TLS client between attempts
static inline void smtpTeardown(){
    if (!smtp || !smtpClient) return;
    smtp->getMessage().clear();
    smtpClient->stop();
    smtpClient->flush();
    delay(50);
}

// Helper: robust preparation of TLS client before a new SMTP connection
// Must be called before smtp->connect() to ensure clean socket/TLS state.
static inline bool smtpClientPrepare(bool tlsInsecure, uint32_t timeoutMs = 30000, uint32_t handshakeTimeoutSeconds = 60) {
    ensureSmtpAllocated();

    if (!smtp || !smtpClient) {
        sendVirtualMessage(F("SMTP prepare: allocation failed"), ERROR);
        return false;
    }

    if (!emailTransportCanSendEmail()) {
        sendVirtualMessage(String(F("SMTP prepare: transport unavailable: ")) + emailTransportUnavailableReason(), ERROR);
        return false;
    }

    smtpClient->stop();
    delay(50);
    smtpClient->flush();
    delay(100);

    if (!emailTransportConfigureClient(*smtpClient, tlsInsecure, timeoutMs, handshakeTimeoutSeconds)) {
        sendVirtualMessage(String(F("SMTP prepare: failed to configure network client: ")) + emailTransportUnavailableReason(), ERROR);
        return false;
    }

    delay(50);
    return true;
}

#else
// Keep symbols available for non-SMTP builds where shared code paths still compile.
static const size_t EMAIL_MIN_HEAP_TO_CONNECT = 28 * 1024;
static const uint8_t EMAIL_FAILS_BEFORE_BREAKER = 3;
static const unsigned long EMAIL_BREAKER_COOLDOWN_MS = 60UL * 1000UL;
static uint8_t emailConsecutiveFailures = 0;
static unsigned long emailBreakerUntil = 0;

// Local helper: robust teardown of SMTP/TLS client between attempts
static inline void smtpTeardown(){
    // SMTP disabled: no-op.
}

// Helper: Robust preparation of TLS client before new SMTP connection
// CRITICAL: Must be called BEFORE smtp->connect() to ensure clean state
// NOTE: Uses ensureSmtpAllocated() to ensure objects exist
static inline bool smtpClientPrepare(bool tlsInsecure, uint32_t timeoutMs = 30000, uint32_t handshakeTimeoutSeconds = 60) {
    (void)tlsInsecure;
    (void)timeoutMs;
    (void)handshakeTimeoutSeconds;
    return false;
}

#endif // ENABLE_SMTP

// Synchronous test email send (blocking) used by the "Test Send Email" button
// Returns true if the email was sent successfully, false otherwise
// Email test task is now created immediately in startEmailTestAsync(), following EMAILDAY pattern

// NOTE: executeScheduledEmailTest() removed - no longer needed with immediate task creation
// Email test task is now created immediately in startEmailTestAsync(), following EMAILDAY pattern

// ============================================================================
// INACTIVITY ALERT - Async Email Send (NO attachments, simple body-only email)
// ============================================================================
// Throttling for REBOOT command (minimum 1 hour between reboots).
// Defined here (outside SDCARD guard) because command_executor.cpp references it unconditionally.
unsigned long lastRebootCommandTime = 0;
// REBOOT_COOLDOWN_MS defined as constexpr in commands.h

#if ENABLE_SDCARD==1
// Throttling for EMAILDAY (minimum 180s between commands)
static unsigned long lastEmailDayTime = 0;
static const unsigned long EMAILDAY_COOLDOWN_MS = 180UL * 1000UL; // 180 segundos

// Forward declaration for the partial streaming sender (used to split large attachments).
static bool smtpSendEmailStreamingSDPartial(
    const String &from,
    const String &recipients,
    const String &subject,
    const String &bodyText,
    const String &filePath,
    size_t startOffset,
    size_t endOffset,
    const String &attachmentName,
    const String &server,
    int port,
    const String &user,
    const String &pass,
    bool tlsInsecure);

// Send email with ALL files from the day's folder
// EXCEPTION CRITICAL: Always send, even with active SD writes (manual command)
// Group files into emails of up to 5MB each (multiple emails if needed)
// Optimized for heap: Base64 streaming, without loading complete files into RAM
bool enviarEmailComAnexosSD(const String &folderPath, const String &subject, const String &bodyText, size_t maxBytesPerEmail, const String &shiftEmails) {
    // .chart binary files NEVER ride emails (any type, any pack): they only
    // feed the graphs/OEE charts — the human-readable CSVs carry the data
    // (live .24 2026-10-14: the .chart attachment is the prime suspect for
    // the SENSORDAY send PANIC; LOGDAY .txt sends clean).
    auto isAllowedEmailDayAttachment = [](const String &fileName) -> bool {
        String lowerName = fileName;
        lowerName.toLowerCase();
        return lowerName.endsWith(F(".txt")) ||
               lowerName.endsWith(F(".csv")) ||
               lowerName.endsWith(F(".bin")) ||
               lowerName.endsWith(F(".log"));
    };

    // ? CRITICAL: Yield before heavy operations to let scheduler stabilize priorities
    // This reduces xTaskPriorityDisinherit crashes caused by FreeRTOS async_tcp + mutex interaction
    taskYIELD();
    vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay to let pending operations complete
    
    // ACTION 1: Register this task with watchdog (CRITICAL before any esp_task_wdt_reset calls)
    esp_task_wdt_add(NULL);
    
    // ACTION 2: Reset watchdog at function start
    esp_task_wdt_reset();
    
    // Acquire SMTP mutex (protects against concurrent operations, e.g. auto-email).
    // Timeout is 60s to allow an in-flight auto-email to finish.
    if (!smtpLock(pdMS_TO_TICKS(60000))) {
        sendVirtualMessage(F("❌ EMAILDAY: timeout obtaining SMTP mutex (another operation in progress)"), ERROR);
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    sendVirtualMessage(F("🔒 EMAILDAY: SMTP mutex acquired"), INFO);
    
    // CRITICAL: Protect against heap maintenance during send
    isEmailSendingActive = true;
    
    // Check if we're already within EMAILDAY context (avoids double-lock deadlock)
    bool isWithinEmailday = priorityManager().isEmaildayActive();
    
    // Track if SD mutex was acquired (to ensure cleanup releases it)
    bool sdMutexAcquiredByUs = false;
    
    // Helper lambda: cleanup on error (to avoid code duplication)
    auto cleanupOnError = [isWithinEmailday, &sdMutexAcquiredByUs](bool releaseEmail = false) {
        // CRITICAL: Release SD mutex if we acquired it
        if (sdMutexAcquiredByUs) {
            extern void sdMutexUnlock();
            sdMutexUnlock();
            sdMutexAcquiredByUs = false;
            sendVirtualMessage(F("🔓 CLEANUP: SD mutex released"), DEBUG);
        }
        // Only release email lock if we acquired it (not if EMAILDAY already owns it)
        if (releaseEmail && !isWithinEmailday) priorityManager().endEmail();
        isEmailSendingActive = false;
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove task from watchdog before returning
    };

    // Reset automatic triggers (manual takes precedence and avoids state conflicts)
    rs232MicroPauseArmed = false;
    autoEmailTriggeredByNewline = false;
    shouldSendEmailImmediately = false;
    formFeedTriggered = false;
    
    // Check circuit breaker (protection against consecutive failures)
    unsigned long now = millis();
    if (emailBreakerUntil && now < emailBreakerUntil) {
        unsigned long waitMs = emailBreakerUntil - now;
        sendVirtualMessage(buildMessage("⚠️ EMAIL_BREAKER active. Wait %lu seconds.", waitMs / 1000), WARN);
        isEmailSendingActive = false; // Release flag before returning
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    
    // Throttling: check if 60s passed since last EMAILDAY
    if (lastEmailDayTime != 0 && (now - lastEmailDayTime) < EMAILDAY_COOLDOWN_MS) {
        unsigned long remaining = (EMAILDAY_COOLDOWN_MS - (now - lastEmailDayTime)) / 1000;
        sendVirtualMessage(buildMessage("⚠️ EMAILDAY throttled. Wait %lu seconds.", remaining), WARN);
        isEmailSendingActive = false; // Release flag before returning
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    
    // Initial checks
    if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(F("❌ Email not configured or WiFi disabled."), ERROR);
        isEmailSendingActive = false; // Release flag before returning
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    
    // Check storage availability: SD, PSRAM or Flash fallback must be available
    bool hasSdStorage = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool hasPsramStorage = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    bool hasFlashStorage = !hasSdStorage && littlefsBegin() && littlefsMounted();

    if (!hasSdStorage && !hasPsramStorage && !hasFlashStorage) {
        sendVirtualMessage(F("❌ No storage available (SD not mounted, PSRAM not enabled, Flash not mounted)."), ERROR);
        isEmailSendingActive = false; // Release flag before returning
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    
    // Check available heap BEFORE starting TLS connection
    size_t freeHeap = ESP.getFreeHeap();
    const size_t EMAILDAY_MIN_HEAP = 40000; // 40KB m?nimo (mais alto que email normal)
    if (freeHeap < EMAILDAY_MIN_HEAP) {
        sendVirtualMessage(buildMessage("🧪 Heap insufficient for EMAILDAY: %u < %u bytes. Wait for recovery.", 
                                          (unsigned)freeHeap, (unsigned)EMAILDAY_MIN_HEAP), ERROR);
        isEmailSendingActive = false; // Release flag before returning
        smtpUnlock();
        esp_task_wdt_delete(NULL); // Remove from watchdog
        return false;
    }
    
    sendVirtualMessage(buildMessage("🧪 Initial EMAILDAY heap: %u bytes (minimum: %u)",
                                      (unsigned)freeHeap, (unsigned)EMAILDAY_MIN_HEAP), INFO);
    
    // PRIORITY: Mark email send as active (P3 priority)
    // Skip if already within EMAILDAY context (lock already held by parent EMAILDAY task)
    if (!isWithinEmailday) {
        if (!priorityManager().canStartEmail()) {
            sendVirtualMessage(F("⚠️ Email blocked by higher priority operation"), WARN);
            isEmailSendingActive = false;
            smtpUnlock();
            esp_task_wdt_delete(NULL); // Remove from watchdog
            return false;
        }
        priorityManager().startEmail();
        sendVirtualMessage(F("? Email send started (P3 priority)"), DEBUG);
    } else {
        sendVirtualMessage(F("? Email send within EMAILDAY context (lock already held)"), DEBUG);
    }
    
    // List ALL files in the day's folder
    struct FileInfo {
        String name;
        size_t size;
        String path;      // backend-usable path (Flash); empty = folderPath + "/" + name
        uint8_t backend = 0; // 0 = SD, 1 = PSRAM, 2 = Flash (LittleFS)
    };
    std::vector<FileInfo> files;
    size_t totalSize = 0;
    bool emaildayFileMode = false; // explicit file path: attach directly, skip listing

    // Parse "@" date filter: folderPath may be "/{base}@DD-MM-YYYY" (encoded by sdcardGetTodayFolder).
    // SD listing expands one level of source subdirs matching the date; PSRAM uses folderPath directly.
    String actualFolderPath = folderPath;
    String dateSuffixFilter = "";
    {
        int atPos = folderPath.indexOf('@');
        if (atPos >= 0) {
            actualFolderPath = folderPath.substring(0, atPos);
            dateSuffixFilter = folderPath.substring(atPos + 1);
        }
    }

    // ============================================================================
    // UNIFIED STORAGE: List files from SD, PSRAM or Flash (order SD -> PSRAM -> Flash)
    // ============================================================================
    bool usingSdStorage = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    bool usingPsramStorage = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
    // Flash fallback needs no mode flag: it is consulted whenever SD is out.
    bool flashMayHaveFiles = !usingSdStorage && littlefsBegin() && littlefsMounted();

    if (!usingSdStorage && !usingPsramStorage && !flashMayHaveFiles) {
        sendVirtualMessage(F("❌ No storage available (SD, PSRAM or Flash)"), ERROR);
        cleanupOnError(true);
        return false;
    }

    // Single-file pre-probe (explicit path, e.g. SHIFTDAY csvPath): resolve on
    // the first tier holding it, bypassing folder listing entirely. The SD
    // probe below handles the mounted case; VFS/Flash are covered here.
    // (emaildayFileMode declared above.)
    if (dateSuffixFilter.isEmpty() && !usingSdStorage && !emaildayFileMode) {
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
                // Flash metadata probe via emailFlash*() — the abstraction does
                // the locking/direct-vs-RPC itself; a caller-held lfsMutex here
                // would deadlock against the loop serve side. Failure (busy,
                // timeout, missing) just skips the probe — same as before.
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
                FileInfo fi;
                fi.name = pbase;
                fi.size = psz;
                fi.path = ppath;
                fi.backend = pbe;
                files.push_back(fi);
                totalSize += psz;
                actualFolderPath = (pbe == STORAGE_BACKEND_FLASH && ppath.length() > 0)
                    ? ppath.substring(0, ppath.lastIndexOf('/')) : pdir;
                emaildayFileMode = true;
                sendVirtualMessage(String(F("EMAILDAY: single-file mode: ")) + pbase, INFO);
            }
        }
    }
    
    if (usingSdStorage) {
        // SD mutex for listing (recursive)
        // Increased timeout to 30s to avoid failures during heavy reception (SPI contention)
        // Note: SD write task is suspended at this point, so mutex should be available
        extern bool sdMutexLock(TickType_t timeoutTicks);
        if (sdMutexLock(pdMS_TO_TICKS(30000))) {
            sdMutexAcquiredByUs = true; // Track that we acquired it
            sdMutexTrackAcquire("EMAILDAY_LISTING"); // Track holder
        } else {
            sendVirtualMessage(F("? EMAILDAY: timeout SD Mutex (listagem)"), ERROR);
            sdMutexDumpState(); // Debug: who holds it?
            cleanupOnError(true); // Release email priority lock
            return false;
        }
    
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
                        files.push_back({base, psize});
                        totalSize += psize;
                        actualFolderPath = (sl > 0) ? pname.substring(0, sl) : String("/");
                        emaildayFileMode = true;
                        sendVirtualMessage(String(F("EMAILDAY: single-file mode: ")) + base, INFO);
                    } else {
                        sendVirtualMessage(String(F("EMAILDAY: unsupported file type: ")) + base, ERROR);
                    }
                }
                probe.close();
            }
        }
        size_t skippedFiles = 0;
        if (!emaildayFileMode) {
            File dir = SD_MMC.open(actualFolderPath.c_str());
            if (!dir || !dir.isDirectory()) {
                sendVirtualMessage(String(F("SD Folder not found: ")) + actualFolderPath, ERROR);
                if (dir) dir.close();
                extern void sdMutexUnlock();
                if (sdMutexAcquiredByUs) {
                    sdMutexUnlock(); // Tracking automatic
                    sdMutexAcquiredByUs = false;
                }
                cleanupOnError(true); // Release email priority lock
                return false;
            }
        
            File f = dir.openNextFile(); 
            int filesListed = 0; 
            while (f) {
                String fname = String(f.name());
                size_t fsize = f.size();
                if (f.isDirectory()) {
                    // Expand one level: include files from date-matching source subfolders.
                    // e.g. "RS232_26-03-2026" -> stored as "RS232_26-03-2026/{file}" for correct full path building.
                    bool matchesFilter = dateSuffixFilter.isEmpty() || (fname.indexOf(dateSuffixFilter) >= 0);
                    if (matchesFilter) {
                        String subPath = actualFolderPath + "/" + fname;
                        File subDir = SD_MMC.open(subPath.c_str());
                        if (subDir && subDir.isDirectory()) {
                            File sf = subDir.openNextFile();
                            while (sf) {
                                String sfName = String(sf.name());
                                size_t sfSize = sf.size();
                                if (!sf.isDirectory() && isAllowedEmailDayAttachment(sfName)) {
                                    files.push_back({fname + "/" + sfName, sfSize});
                                    totalSize += sfSize;
                                }
                                sf.close();
                                sf = subDir.openNextFile();
                                esp_task_wdt_reset();
                            }
                            subDir.close();
                        }
                    } else {
                        skippedFiles++;
                    }
                } else if (isAllowedEmailDayAttachment(fname) &&
                           (dateSuffixFilter.isEmpty() || fname.indexOf(dateSuffixFilter) >= 0 ||
                            sd_layout::isAggregate(fname))) {
                    files.push_back({fname, fsize});
                    totalSize += fsize;
                } else {
                    skippedFiles++;
                }
                f.close();
                f = dir.openNextFile();
                esp_task_wdt_reset(); // Reset watchdog during listing (prevents timeout on large folders)
            
                // Release mutex every 5 files to allow write task to drain retry queue
                // Critical for concurrent RS232 reception during EMAILDAY
                filesListed++;
                if (filesListed % 5 == 0) {
                    extern void sdMutexUnlock();
                    if (sdMutexAcquiredByUs) {
                        sdMutexUnlock(); // Tracking automatic
                        sdMutexAcquiredByUs = false;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10)); // Give write task time to acquire mutex
                    extern bool sdMutexLock(TickType_t timeoutTicks);
                    if (sdMutexLock(pdMS_TO_TICKS(30000))) {
                        sdMutexAcquiredByUs = true;
                        sdMutexTrackAcquire("EMAILDAY_LISTING"); // Track reacquire
                    } else {
                        sendVirtualMessage(F("⚠️ EMAILDAY: mutex reacquire timeout during listing"), WARN);
                        sdMutexDumpState(); // Debug
                        dir.close();
                        cleanupOnError(true); // Release email priority lock (SD mutex already released)
                        return false;
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1)); // Yield for watchdog
                }
            }
        
            // Close directory after listing
            dir.close();
        } // !emaildayFileMode
        
        // Release SD mutex after listing (files list is now stable in RAM)
        extern void sdMutexUnlock();
        if (sdMutexAcquiredByUs) {
            sdMutexUnlock();
            sdMutexAcquiredByUs = false;
            sendVirtualMessage(F("🔓 SD mutex released (listing complete)"), DEBUG);
        }

        if (skippedFiles > 0) {
            sendVirtualMessage(buildMessage("EMAILDAY: skipped %u unsupported file(s)", (unsigned)skippedFiles), INFO);
        }
    } else if (usingPsramStorage && !emaildayFileMode) {
        // PSRAM Virtual Filesystem - direct memory access (no mutex needed)
        sendVirtualMessage(F("📦 PSRAM VFS: listing files from volatile storage"), INFO);
        
        std::vector<String> fileNames = psramVFSListFiles(folderPath);
        if (fileNames.empty()) {
            // VFS empty: fall through to the Flash fallback mirror merge below
            // (no-SD packs persist data on LittleFS, not PSRAM). If neither
            // holds data, the final files.empty() check reports it.
            sendVirtualMessage(String(F("📁 PSRAM folder empty or not found: ")) + folderPath + String(F(" — falling back to Flash mirror")), INFO);
            fileNames.clear();
        }
        
        size_t skippedFiles = 0;
        for (const String& fname : fileNames) {
            if (!isAllowedEmailDayAttachment(fname)) {
                skippedFiles++;
                continue;
            }
            // Size without loading the file: psramVFSGetFileSize is O(1) and
            // avoids duplicating the whole attachment in a String on the
            // EmailDay task heap (OOM/reboot risk before TLS alloc).
            size_t fsize = psramVFSGetFileSize(folderPath, fname);
            if (fsize == 0) {
                skippedFiles++;
                continue;
            }
            FileInfo fi;
            fi.name = fname;
            fi.size = fsize;
            fi.path = ""; // PSRAM: resolved as folderPath + "/" + name at stream time
            fi.backend = STORAGE_BACKEND_PSRAM;
            files.push_back(fi);
            totalSize += fsize;
            esp_task_wdt_reset();
        }

        if (skippedFiles > 0) {
            sendVirtualMessage(buildMessage("EMAILDAY: skipped %u unsupported PSRAM file(s)", (unsigned)skippedFiles), INFO);
        }
        
        sendVirtualMessage(buildMessage("📦 PSRAM VFS: %d files listed (%u bytes total)",
                                          (int)files.size(), (unsigned)totalSize), INFO);
    }

    // Flash fallback mirror (no-SD only): merge persisted LittleFS files.
    // Bulk overlap is skipped once the hot tier yielded files (/oee never overlaps).
    // Skipped in single-file mode (SHIFTDAY explicit path): remap handles it.
    // flashFbCollectEmailFiles lists via emailFlash*() — locking + direct/RPC
    // dispatch happen inside; no caller-held lfsMutex (deadlock vs serve), and
    // busy/timeout just yields no flash merge (same outcome as the old skip).
    if (!emaildayFileMode && !sdcardShouldUseSD()) {
        std::vector<StorageFileInfo> flashFiles;
        flashFbCollectEmailFiles(actualFolderPath, dateSuffixFilter, flashFiles, files.empty());
        for (auto& fi : flashFiles) {
            if (!isAllowedEmailDayAttachment(fi.name)) continue;
            bool dup = false;
            for (auto& e : files) {
                if (e.name == fi.name) { dup = true; break; }
            }
            if (dup) continue;
            FileInfo out;
            out.name = fi.name;
            out.size = fi.size;
            out.path = fi.fullPath;
            out.backend = STORAGE_BACKEND_FLASH;
            files.push_back(out);
            totalSize += fi.size;
        }
        if (!flashFiles.empty()) {
            sendVirtualMessage(buildMessage("💾 Flash: %d file(s) merged from LittleFS",
                                              (int)flashFiles.size()), INFO);
        }
    }
    
    if (files.empty()) {
        sendVirtualMessage(buildMessage("⚠️ No files found in: %s", actualFolderPath.c_str()), WARN);
        cleanupOnError(true); // Release email priority lock (SD already released)
        return false;
    }
    
    // Split oversized files into 1MB ranges (sent as multiple emails) to limit peak resource usage.
    std::vector<FileInfo> largeFiles;
    std::vector<FileInfo> normalFiles;
    normalFiles.reserve(files.size());
    for (const auto &fi : files) {
        if (fi.size > maxBytesPerEmail) largeFiles.push_back(fi);
        else normalFiles.push_back(fi);
    }
    files.swap(normalFiles);

    sendVirtualMessage(buildMessage("Found %d file(s) + %d large file(s), total: %u bytes", (int)files.size(), (int)largeFiles.size(), (unsigned)totalSize), INFO);
    
    // Split files into batches of up to maxBytesPerEmail
    std::vector<std::vector<FileInfo>> batches;
    std::vector<FileInfo> currentBatch;
    size_t currentBatchSize = 0;
    
    for (const auto &file : files) {
        // If adding this file exceeds the limit AND there are already files in the current batch, close the batch
        if (currentBatchSize + file.size > maxBytesPerEmail && !currentBatch.empty()) {
            batches.push_back(currentBatch);
            currentBatch.clear();
            currentBatchSize = 0;
        }
        currentBatch.push_back(file);
        currentBatchSize += file.size;
        
        // OPTION 2: Reset watchdog every 100 files
        static int fileCounter = 0;
        if (++fileCounter % 100 == 0) {
            esp_task_wdt_reset();
        }
    }
    if (!currentBatch.empty()) {
        batches.push_back(currentBatch);
    }
    
    sendVirtualMessage(buildMessage("📧 Split into %d email(s)", batches.size()), INFO);
    
    // Small delay to ensure SD finished flushing recent writes
    // IMPORTANT: If you just wrote to the SD, the controller may have pending buffers
    delay(100); // 100ms to let SD finalize write operations
    yield();
    
    // Load SMTP credentials once — used by both the batch loop and the large-files loop below
    String dest   = configManager.getEmailDestination();
    String server = configManager.getSmtpServer();
    int    port   = configManager.getSmtpPort();
    String user   = configManager.getSmtpUsername();
    String pass   = configManager.getSmtpPassword();

    // Send each batch
    int batchNum = 0;
    for (const auto &batch : batches) {
        batchNum++;
        
        // OPTION 2: Reset watchdog at the start of each batch
        esp_task_wdt_reset();
        
        // CRITICAL PROTECTION: Check heap BEFORE each batch
        size_t freeHeapNow = ESP.getFreeHeap();
        if (freeHeapNow < EMAIL_MIN_HEAP_TO_CONNECT) {
            sendVirtualMessage(buildMessage("Batch %d: insufficient heap (%u < %u bytes). Aborting.",
                                              batchNum, (unsigned)freeHeapNow, (unsigned)EMAIL_MIN_HEAP_TO_CONNECT), ERROR);
            cleanupOnError(true); // Release email priority lock
            return false;
        }
        
        sendVirtualMessage(buildMessage("Batch %d: available heap: %u bytes", 
                                          batchNum, (unsigned)freeHeapNow), DEBUG);
        
        // Prepare subject with suffix if multiple emails
        String emailSubject = subject;
        if (batches.size() > 1) {
            char batchSuffix[32];
            snprintf(batchSuffix, sizeof(batchSuffix), " (%d/%d)", batchNum, (int)batches.size());
            emailSubject += batchSuffix;
        }
        
                // Preserve the caller-provided body so scheduled sends keep their configured cadence text.
                String emailBody = bodyText.length() > 0
                    ? bodyText
                    : buildEmailInfoTable(
                          String(F("Os seus dados est\xC3\xA3o em anexo!")),
                          "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
        
        // Prepare MIME email
        size_t freeHeap = ESP.getFreeHeap();
        sendVirtualMessage(buildMessage("Heap available: %u bytes (minimum: %u)", 
                                          (unsigned)freeHeap, (unsigned)EMAIL_MIN_HEAP_TO_CONNECT), DEBUG);
        if (freeHeap < EMAIL_MIN_HEAP_TO_CONNECT) {
            sendVirtualMessage(buildMessage("🧪 Heap insufficient: %u < %u bytes. Wait for recovery.", 
                                              (unsigned)freeHeap, (unsigned)EMAIL_MIN_HEAP_TO_CONNECT), ERROR);
            cleanupOnError(true); // Release email priority lock
            return false;
        }
        
        // Build extra email list: command-specific extras + SHIFTDAY-specific addresses (both pre-merged into shiftEmails)
        String extraEmails = shiftEmails;
        extraEmails.trim();
        
        // Build full recipients list
        String allRecipients;
        if (dest.length() > 0 && extraEmails.length() > 0) {
            allRecipients = dest + ", " + extraEmails;
        } else if (dest.length() > 0) {
            allRecipients = dest;
        } else {
            allRecipients = extraEmails;
        }
        
        sendVirtualMessage(buildMessage("Config SMTP: server=%s, port=%d, user=%s, dest=%s", 
                                          server.c_str(), port, user.c_str(), allRecipients.c_str()), DEBUG);
        
        // Send email (AUTONOMOUS STREAMING, does not use ReadyMail)
        sendVirtualMessage(String(F("📤 Sending email ")) + String(batchNum) + String(F("/")) + String(batches.size()) + String(F("...")), INFO);
        
        // SMTP streaming currently resolves to the WiFi-backed TLS transport only.
        if (!emailTransportCanSendEmail()) {
            sendVirtualMessage(String(F("❌ Email transport unavailable: ")) + emailTransportUnavailableReason(), ERROR);
            cleanupOnError(true); // Release email priority lock
            return false;
        }
        
        // ? Reset watchdog before long network operation (FINAL reset before streaming)
        esp_task_wdt_reset();
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Yield for watchdog (increased)
        
        // ====================================================================
        // STREAMING CHUNKED AUTONOMOUS: Send email directly to the socket
        // ====================================================================
        // Instead of accumulating MIME in RAM, send everything streaming to the socket
        // TLS connection is managed internally by this function (does not use ReadyMail!)
        sendVirtualMessage(buildMessage("STREAMING: %d file(s), heap: %u bytes", 
                                          (int)batch.size(), (unsigned)ESP.getFreeHeap()), INFO);
        
        // Convert batch to format expected by streaming (keeps Flash paths)
        std::vector<SmtpStreamFile> filesForStream;

        // ? Reset watchdog before processing batch files (can iterate many times)
        esp_task_wdt_reset();

        for (const auto &fi : batch) {
            SmtpStreamFile sf;
            sf.name = fi.name;
            sf.size = fi.size;
            sf.path = fi.path;
            filesForStream.push_back(sf);
        }
        
        // Reset watchdog before long streaming operation
        esp_task_wdt_reset();
        
        // Send using autonomous streaming (own TLS connection, does not accumulate in RAM!)
        bool success = smtpSendEmailStreamingSD(
            user,                               // from
            allRecipients,                      // recipients
            emailSubject,                       // subject
            emailBody,                          // body text
            actualFolderPath,                    // folder path (decoded from "@" encoding)
            filesForStream,                     // files
            server,                             // SMTP server
            port,                               // SMTP port
            user,                               // SMTP username
            pass,                               // SMTP password
            configManager.getSmtpTlsInsecure()  // TLS insecure mode
        );
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Watchdog timeout guard after send
        
        // Cleanup (streaming already closed the connection, but ensure mutex)
        delay(100);
        
        taskYIELD(); // Explicit yield
        
        if (!success) {
            sendVirtualMessage(buildMessage("❌ Failed to send email %d (smtp.send() returned false, heap=%u)", 
                                              batchNum, (unsigned)ESP.getFreeHeap()), ERROR);
            observabilityLogEmailPipeline("emailday-batch", "SMTP", emailSubject.c_str(), allRecipients.c_str(), false, (int)filesForStream.size());
            sendVirtualMessage(F("Possible causes:"), ERROR);
            sendVirtualMessage(F("  1) Gmail: wrong password or blocking app (use App Password)"), ERROR);
            sendVirtualMessage(F("  2) SMTP server rejected recipients/message"), ERROR);
            sendVirtualMessage(F("  3) Transmission timeout (message too large)"), ERROR);
            sendVirtualMessage(F("  4) Unstable/interrupted TLS connection"), ERROR);
            sendVirtualMessage(F("  5) Out of memory during MIME construction"), ERROR);
            
            // Increment failure counter
            emailConsecutiveFailures++;
            if (emailConsecutiveFailures >= EMAIL_FAILS_BEFORE_BREAKER) {
                emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                sendVirtualMessage(buildMessage("⚠️ EMAIL_BREAKER: pause %lu seconds.", 
                                                  (unsigned)(EMAIL_BREAKER_COOLDOWN_MS / 1000)), WARN);
            }
            
            // CRITICAL: Use cleanupOnError to release ALL locks (including SD mutex)
            cleanupOnError(true); // This will release SD mutex, email priority, SMTP, and watchdog
            return false;
        }
        
        // Success — reset failure counter
        emailConsecutiveFailures = 0;
        
        // OPTION 2: Reset watchdog after success
        esp_task_wdt_reset();
        
        // AGGRESSIVE CLEANUP: Force heap release between batches
        emailSubject = ""; // Clear temporary strings
        emailBody = "";
        filesForStream.clear(); // Limpar vetor
        std::vector<SmtpStreamFile>().swap(filesForStream); // For?ar shrink
        delay(50); // Wait for heap to settle
        
        size_t heapAfterBatch = ESP.getFreeHeap();
        sendVirtualMessage(buildMessage("Heap after batch %d: %u bytes", 
                                          batchNum, (unsigned)heapAfterBatch), DEBUG);
        
        // sendVirtualMessage(buildMessage("Email %d sent successfully!", batchNum), INFO); // Hidden
        
        // Small delay between emails (if more)
        if (batchNum < batches.size()) {
            delay(2000);
            esp_task_wdt_reset(); // ? Reset durante delay
            taskYIELD(); // Explicit yield between batches
        }
    } // closes for (const auto &batch : batches)
    
    // ? End of batch loop

    // Send oversized files as multiple 1MB ranges.
    if (!largeFiles.empty()) {
        esp_task_wdt_reset(); // OPTION 2: Reset before processing large files
        
        for (const auto &fi : largeFiles) {
            const size_t parts = (fi.size + maxBytesPerEmail - 1) / maxBytesPerEmail;
            
            for (size_t part = 0; part < parts; ++part) {
                esp_task_wdt_reset(); // Reset on each part
                
                size_t startOffset = part * maxBytesPerEmail;
                size_t endOffset = min(fi.size, startOffset + maxBytesPerEmail);

                String partSubject = subject;
                partSubject += String(F(" ("));
                partSubject += fi.name;
                partSubject += String(F(" "));
                partSubject += String((int)(part + 1));
                partSubject += String(F("/"));
                partSubject += String((int)parts);
                partSubject += String(F(")"));

                String attachmentName = fi.name;
                int dot = attachmentName.lastIndexOf('.');
                if (dot > 0) {
                    attachmentName = attachmentName.substring(0, dot) + String(F("_part")) + String((int)(part + 1)) + attachmentName.substring(dot);
                } else {
                    attachmentName += String(F("_part")) + String((int)(part + 1));
                }

                String fullPath = fi.path;
                if (fullPath.isEmpty()) {
                    fullPath = folderPath;
                    if (!fullPath.endsWith("/")) fullPath += "/";
                    fullPath += fi.name;
                }

                // Build recipients: command-specific extras + SHIFTDAY-specific addresses (both pre-merged into shiftEmails)
                String extraEmails = shiftEmails;
                extraEmails.trim();

                String allRecipients;
                if (dest.length() > 0 && extraEmails.length() > 0) allRecipients = dest + ", " + extraEmails;
                else if (dest.length() > 0) allRecipients = dest;
                else allRecipients = extraEmails;

                bool success = smtpSendEmailStreamingSDPartial(
                    user,
                    allRecipients,
                    partSubject,
                    String(F("See attached log part.")),
                    fullPath,
                    startOffset,
                    endOffset,
                    attachmentName,
                    server,
                    port,
                    user,
                    pass,
                    configManager.getSmtpTlsInsecure()
                );

                if (!success) {
                    sendVirtualMessage(buildMessage("? EMAILDAY: failed sending %s part %d/%d", fi.name.c_str(), (int)(part + 1), (int)parts), ERROR);
                    cleanupOnError(true); // Release email priority lock
                    return false;
                }

                vTaskDelay(pdMS_TO_TICKS(200));
                taskYIELD();
            }
        }
    }
    
    // sendVirtualMessage(buildMessage("Sending complete: %d email(s).", (int)batches.size()), COMMAND_RESPONSE); // Hidden
    
    #if ENABLE_LCD_SCREEN==1
    // Visual notification on the OLED screen
    screenManager.showEmailSent();
    #endif
    
    // Record timestamp of the last EMAILDAY
    lastEmailDayTime = millis();
    
    // FINAL CLEANUP: Free all vectors and strings to prevent ESP_ERR_NO_MEM
    files.clear();
    std::vector<FileInfo>().swap(files);
    batches.clear();
    std::vector<std::vector<FileInfo>>().swap(batches);
    largeFiles.clear();
    std::vector<FileInfo>().swap(largeFiles);
    
    // Log final heap
    size_t finalHeap = ESP.getFreeHeap();
    sendVirtualMessage(buildMessage("📤 EMAILDAY completed. Final heap: %u bytes", 
                                      (unsigned)finalHeap), INFO);
    
    // 🔓 PSRAM MODE: Disable snapshot AFTER reading all files (prevents data corruption)
    // This MUST be done here, not in emailDayTask(), to avoid reallocing vectors while reading
    if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled() && psramVFSIsSnapshotActive()) {
        sendVirtualMessage(F("📸 EMAILDAY: Disabling PSRAM snapshot (all files read)"), INFO);
        
        size_t pendingCount = 0, pendingBytes = 0;
        psramVFSGetSnapshotStats(pendingCount, pendingBytes);
        sendVirtualMessage(buildMessage("   📦 Flushing pending writes: %u items, %u KB", 
                                          (unsigned)pendingCount, (unsigned)(pendingBytes / 1024)), INFO);
        
        psramVFSDisableSnapshot();  // Safe to flush now - all file reading complete
        sendVirtualMessage(F("   ✅ PSRAM snapshot disabled, pending writes flushed"), DEBUG);
    }
    
    // PRIORITY: Release email lock (P3 priority complete)
    // Only release if we acquired it (not if EMAILDAY already owns it)
    if (!isWithinEmailday) {
        priorityManager().endEmail();
        sendVirtualMessage(F("? Email send completed, priority released"), DEBUG);
    } else {
        sendVirtualMessage(F("? Email send completed (lock held by EMAILDAY)"), DEBUG);
    }
    
    // CRITICAL: Release SD mutex if still held (should be released after listing, but safety check)
    if (sdMutexAcquiredByUs) {
        extern void sdMutexUnlock();
        sdMutexUnlock();
        sdMutexAcquiredByUs = false;
        sendVirtualMessage(F("⚠️ SD mutex released (safety cleanup at exit)"), WARN);
    }
    
    isEmailSendingActive = false; // Release flag on successful finish
    smtpUnlock();
    esp_task_wdt_delete(NULL); // Remove task from watchdog after completion
    return true;
}
#else
// SDCARD disabled: provide stub so runtime_email.cpp can link (it calls this unconditionally).
bool enviarEmailComAnexosSD(const String&, const String&, const String&, size_t, const String&) { return false; }
#endif // ENABLE_SDCARD
