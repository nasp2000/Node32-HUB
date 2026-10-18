// email_queues_impl.h
// Included only from chatgpt.ino (single TU).
// Email queue processors: processSdEmailQueue, processRamEmailQueue,
// triggerImmediateEmailQueueProcessing, endEmaildayFreezeCleanup.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
#include "flash_fallback_store.h" // no-SD LittleFS remap (stub when disabled)
#include "email_flash_rpc.h" // stack-safe LittleFS handles (PSRAM email tasks)
// Forward declarations for functions defined later in this file
static String getLabelForRule(const String &ruleName);
static String getFreqLabelForRule(const String &ruleName);
static String extractSmartNumber(const String &line);
// Pending SD-email jobs, for graceful-reboot gating in the scheduler TU
// (non-static: single definition here in the main TU, declared extern there).
size_t emailSdQueuePending() {
    return emailJobQueue.size();
}
void processSdEmailQueue() {
    // Reduce heartbeat noise: emit only when queue size changes, or periodically when pending jobs exist.
    static unsigned long lastLogMs = 0;
    static unsigned int lastLoggedQueueSize = 0xFFFFu;
    unsigned long now = millis();
    const unsigned int currentQueueSize = (unsigned)emailJobQueue.size();
    const bool queueSizeChanged = (currentQueueSize != lastLoggedQueueSize);
    const bool shouldLog = queueSizeChanged || (currentQueueSize > 0 && (now - lastLogMs > 30000UL));
    if (shouldLog) {
        sendVirtualMessage(buildMessage("🔄 processSdEmailQueue() called, queue size=%u",
                                          currentQueueSize), INFO);
        lastLogMs = now;
        lastLoggedQueueSize = currentQueueSize;
    }
    
    // PRIORITY EMAILDAY: Pause streaming if emailDay is active
    if (emailDayInProgress) {
        sendVirtualMessage(F("⏸️ processSdEmailQueue: PAUSED (EMAILDAY active)"), DEBUG);
        return;
    }

    // Heap protection: delay if memory is critical
    if (ESP.getFreeHeap() < 20000) {
        sendVirtualMessage(F("⚠️ processSdEmailQueue: PAUSED (low heap < 20KB)"), DEBUG);
        sendVirtualMessage(buildMessage("Email SD: low heap (%u), delaying...", ESP.getFreeHeap()), WARN);
        return;
    }

    // Monitor stack usage (diagnostic)
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (stackHighWaterMark < 2048) { // Less than 2KB free
        sendVirtualMessage(buildMessage("Email SD: low stack! %u bytes free", (unsigned)stackHighWaterMark), WARN);
    }
    
    // ? CRITICAL: Yield before mutex to reduce xTaskPriorityDisinherit crashes
    taskYIELD();
    
    // Check for pending jobs (mutex protected)
    if (xSemaphoreTakeRecursive(emailQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return; // Failed to acquire lock, try later
    }
    
    if (emailJobQueue.empty()) {
        xSemaphoreGiveRecursive(emailQueueMutex); // Release mutex
        static bool _sdQueueEmptyLogged = false;  // log only once per boot (avoids repeated noise)
        if (!_sdQueueEmptyLogged) {
            _sdQueueEmptyLogged = true;
            sendVirtualMessage(F("ℹ️ processSdEmailQueue: Queue EMPTY, nothing to process"), DEBUG);
        }
        return;
    }
    
    sendVirtualMessage(buildMessage("📧 Task email: PROCESSING queue (%u jobs)...", (unsigned)emailJobQueue.size()), INFO);
    
    // Copy queue for local processing and clear the global queue
    // This frees the mutex quickly so new writes do not block
    std::vector<PendingEmailJob> jobsToProcess = emailJobQueue;
    emailJobQueue.clear();
    emailJobQueue.shrink_to_fit(); // Release capacity (small optimization, queue limited to 10)
    xSemaphoreGiveRecursive(emailQueueMutex);

    // Block if a concurrent email operation (EMAILDAY or main state machine) is active on another task.
    // Re-queue jobs so they are not lost and will be retried on the next 30s cycle.
    if (!priorityManager().canStartEmail()) {
        sendVirtualMessage(F("⏸️ processSdEmailQueue: Waiting (another email active, will retry)"), WARN);
        if (xSemaphoreTakeRecursive(emailQueueMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            emailJobQueue.insert(emailJobQueue.begin(), jobsToProcess.begin(), jobsToProcess.end());
            xSemaphoreGiveRecursive(emailQueueMutex);
        }
        return;
    }
    priorityManager().startEmail();

    sendVirtualMessage(buildMessage("Email SD: Processing batch of %u jobs...", (unsigned)jobsToProcess.size()), INFO);

    // Group jobs by recipient (recipient string -> list of jobs)
    std::map<String, std::vector<PendingEmailJob>> jobsByRecipient;
    std::map<String, std::vector<String>> rulesByRecipient; // Rules found per group
    std::map<String, String> capturedValueByRecipient; // First valid numeric value found in matched line
    std::map<String, String> lineContentByRecipient;  // Full matched line for "Linha encontrada" row in email body
    bool sdActive = configManager.getSdcardEnabled();

    for (const auto &job : jobsToProcess) {
        
        bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
        bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled());
        bool usingFlashFile = false;
        String psramFolder, psramFilename;
        File file;
        int32_t efh = -1;   // flash handle (email_flash_rpc) when usingFlashFile

        // Open job file (order: SD first, then LittleFS fallback)
        if (usingSd) {
            sendVirtualMessage(buildMessage("📂 Opening file (SD): %s [%u-%u]", job.filepath.c_str(), 
                                              (unsigned)job.start_offset, (unsigned)job.end_offset), DEBUG);
            file = SD.open(job.filepath.c_str());
            if (!file) {
                sendVirtualMessage(buildMessage("❌ Email SD: Failed to open %s", job.filepath.c_str()), WARN);
                continue;
            }
            //  Seek to start
            if (!file.seek(job.start_offset)) {
                sendVirtualMessage(buildMessage("❌ Email SD: Failed to seek to %u", (unsigned)job.start_offset), WARN);
                file.close();
                continue;
            }
        } else if (usingPsram) {
            if (!splitPsramPath(job.filepath, psramFolder, psramFilename) || psramVFSGetFileSize(psramFolder, psramFilename) == 0) {
                 sendVirtualMessage(buildMessage("❌ Email PSRAM: invalid file %s", job.filepath.c_str()), WARN);
                 continue;
            }
            sendVirtualMessage(buildMessage("📂 Opening file (PSRAM): %s [%u-%u]", job.filepath.c_str(),
                                              (unsigned)job.start_offset, (unsigned)job.end_offset), DEBUG);
        } else if (!sdcardShouldUseSD()) {
            // Flash fallback (LittleFS): remap the job path, then open through
            // the stack-safe handle API — a PSRAM-stacked email task must never
            // hold a LittleFS File (HISTORIC/HomeE32 (8)).
            String flashPath;
            if (!flashFbFindFile(job.filepath, flashPath)) {
                 sendVirtualMessage(buildMessage("❌ Email Flash: file not found %s", job.filepath.c_str()), WARN);
                 continue;
            }
            sendVirtualMessage(buildMessage("📂 Opening file (Flash): %s [%u-%u]", flashPath.c_str(),
                                              (unsigned)job.start_offset, (unsigned)job.end_offset), DEBUG);
            efh = emailFlashOpenRead(flashPath);
            if (efh < 0) {
                sendVirtualMessage(buildMessage("❌ Email Flash: Failed to open %s", flashPath.c_str()), WARN);
                continue;
            }
            if (!emailFlashSeek(efh, job.start_offset)) {
                sendVirtualMessage(buildMessage("❌ Email Flash: Failed to seek to %u", (unsigned)job.start_offset), WARN);
                emailFlashClose(efh);
                efh = -1;
                continue;
            }
            usingFlashFile = true;
        } else {
             sendVirtualMessage(F("❌ Email: No storage available"), ERROR);
             continue;
        }

        sendVirtualMessage(buildMessage("🔍 Scanning %u bytes for recipients...", 
                                          (unsigned)(job.end_offset - job.start_offset)), DEBUG);

        //  Read and filter lines to find recipients
        const size_t LINE_BUF_SIZE = 1024;
        char lineBuffer[LINE_BUF_SIZE];
        size_t lineIdx = 0;
        size_t curPos = job.start_offset;
        bool jobMatched = false;
        
        // PSRAM Buffer state
        const size_t P_BUF_SZ = 512;
        uint8_t pBuf[P_BUF_SZ];
        size_t pBufLen = 0;
        size_t pBufPos = 0;
        size_t pTotalRead = 0; // Relative to start_offset for PSRAM refilling

        // Flash buffer state (handle-based emailFlashRead refills; same shape)
        const size_t F_BUF_SZ = 512;
        uint8_t fBuf[F_BUF_SZ];
        size_t fBufLen = 0;
        size_t fBufPos = 0;

        auto readNextByte = [&]() -> int {
            if (usingSd) return file.read();
            if (usingFlashFile) {
                if (fBufPos >= fBufLen) {
                    fBufLen = emailFlashRead(efh, fBuf, F_BUF_SZ);
                    fBufPos = 0;
                    if (fBufLen == 0) return -1;
                }
                return fBuf[fBufPos++];
            }
            // PSRAM Logic
            if (pBufPos >= pBufLen) {
                // Refill
                if (curPos >= job.end_offset) return -1;
                size_t remains = job.end_offset - curPos;
                size_t toReq = (remains > P_BUF_SZ) ? P_BUF_SZ : remains;
                pBufLen = psramVFSReadChunk(psramFolder, psramFilename, curPos, pBuf, toReq);
                pBufPos = 0;
                if (pBufLen == 0) return -1;
            }
            return pBuf[pBufPos++];
        };
        
        while (curPos < job.end_offset) {
            int c = readNextByte();
            if (c < 0) break; // EOF
            curPos++;
            
            if (c == '\n' || lineIdx >= LINE_BUF_SIZE - 1) {
                lineBuffer[lineIdx] = '\0';
                String line = String(lineBuffer);

                // Skip header and empty lines — they are metadata, not data.
                // Header format: "--- Node32-HUB v2.21 - YYYY-MM-DD ... ---"
                // The date token "-MM" in the header would be parsed as a negative colN value
                // and produce a false-positive colN match, which would prematurely set the
                // throttle and block real streaming detection for the entire frequency period.
                String lineLower = line;
                lineLower.toLowerCase();
                if (lineLower.length() == 0 || lineLower.startsWith("---")) {
                    lineIdx = 0;
                    continue;
                }

                // Find recipients in this data line
                std::vector<std::pair<String, String>> lineRecs;
                std::map<String,String> capturedByRule; // Numeric value found per rule name
                findRecipientsInLowerText(lineLower, lineRecs, sdActive, &capturedByRule);
                
                if (!lineRecs.empty()) {
                    sendVirtualMessage(buildMessage("🎯 processSdEmailQueue: Found %u recipient(s) in line: '%s'",
                                                       (unsigned)lineRecs.size(), line.c_str()), DEBUG);
                }
                if (!lineRecs.empty()) {
                    //  Build unique recipients key
                    String allRecipients;
                    std::vector<String> uniqueEmails;
                    for(auto &p : lineRecs) {
                        bool seen = false;
                        for(auto &e : uniqueEmails) if(e == p.second) { seen = true; break; }
                        if(!seen) uniqueEmails.push_back(p.second);
                    }
                    for(size_t i=0; i<uniqueEmails.size(); i++) {
                        if(i>0) allRecipients += ", ";
                        allRecipients += uniqueEmails[i];
                    }

                    //  Add job to the group and stop reading this job (we already know who it goes to)
                    jobsByRecipient[allRecipients].push_back(job);
                    
                    //  Store found rule names
                    for(auto &p : lineRecs) {
                        bool ruleExists = false;
                        for(const String &r : rulesByRecipient[allRecipients]) {
                            if(r == p.first) { ruleExists = true; break; }
                        }
                        if(!ruleExists) rulesByRecipient[allRecipients].push_back(p.first);
                    }

                    // Extract the numeric value for subject "Encontrei (value)", only once per recipient group.
                    // Priority: value from rule matching (exact position, correct column).
                    // Fallback: extractSmartNumber on the raw line (for token/equality rules).
                    if (capturedValueByRecipient.find(allRecipients) == capturedValueByRecipient.end()) {
                        String cv;
                        for (const auto &lp : lineRecs) {
                            if (capturedByRule.count(lp.first)) { cv = capturedByRule[lp.first]; break; }
                        }
                        if (cv.length() == 0) cv = extractSmartNumber(line);
                        bool cvHasDigit = false;
                        for (int _ci = 0; _ci < (int)cv.length(); _ci++) {
                            if (isDigit(cv.charAt(_ci))) { cvHasDigit = true; break; }
                        }
                        capturedValueByRecipient[allRecipients] = cvHasDigit ? cv : String();
                    }

                    // Store the matched line for the "Linha encontrada" row in the email body (first match wins).
                    if (lineContentByRecipient.find(allRecipients) == lineContentByRecipient.end()) {
                        lineContentByRecipient[allRecipients] = line;
                    }

                    jobMatched = true;
                    break; 
                }
                
                lineIdx = 0;
            } else if (c != '\r') {
                lineBuffer[lineIdx++] = (char)c;
            }
        }
        if (usingFlashFile) {
            emailFlashClose(efh);
            efh = -1;
        } else {
            file.close();
        }
        
        if (!jobMatched) {
            sendVirtualMessage(buildMessage("⚠️ No recipients found in file %s [%u-%u]", 
                                              job.filepath.c_str(), (unsigned)job.start_offset, (unsigned)job.end_offset), WARN);
        }
    }

    sendVirtualMessage(buildMessage("📊 Grouped into %u recipient group(s)", (unsigned)jobsByRecipient.size()), INFO);

    //  Process each group and send ONE combined email
    for (auto it = jobsByRecipient.begin(); it != jobsByRecipient.end(); ++it) {
        
        // PRIORITY EMAILDAY: Pause streaming between group sends
        // If it detects emailday, return remaining groups to the main queue and abort
        if (emailDayInProgress) {
            sendVirtualMessage(F("Email SD: Pause requested by EMAILDAY. Returning remaining jobs..."), WARN);
            
            if (xSemaphoreTakeRecursive(emailQueueMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Insert remaining jobs at the FRONT of the global queue (for priority on resume)
                // Rebuild vector of remaining jobs from the current iterator to the end
                std::vector<PendingEmailJob> leftoverJobs;
                for (auto resumeIt = it; resumeIt != jobsByRecipient.end(); ++resumeIt) {
                    // Add all jobs of this group
                    for (const auto &job : resumeIt->second) {
                        leftoverJobs.push_back(job);
                    }
                }
                
                // Inserir na frente da queue global
                if (!leftoverJobs.empty()) {
                    emailJobQueue.insert(emailJobQueue.begin(), leftoverJobs.begin(), leftoverJobs.end());
                    sendVirtualMessage(buildMessage("%u jobs returned to priority queue.", (unsigned)leftoverJobs.size()), INFO);
                }
                xSemaphoreGiveRecursive(emailQueueMutex);
            }
            priorityManager().endEmail(); // Release P3 lock before yielding to EMAILDAY
            return; // Exit the function to allow EMAILDAY to run
        }

        String recipients = it->first;
        std::vector<PendingEmailJob> &jobs = it->second;
        
        if (jobs.empty()) continue;

        // SMTP credentials (read once per group, same as processRamEmailQueue)
        String smtpServer     = configManager.getSmtpServer();
        int    smtpPort       = configManager.getSmtpPort();
        String smtpUser       = configManager.getSmtpUsername();
        String smtpPass       = configManager.getSmtpPassword();
        bool   smtpTlsInsecure = configManager.getSmtpTlsInsecure();

        // Build subject and inline body from the first matched line captured during scanning.
        // Send without file attachment — same format as processRamEmailQueue / streaming path.
        std::vector<String> &rules = rulesByRecipient[recipients];
        String primaryRule = rules.empty() ? String() : rules[0];
        String capturedVal = capturedValueByRecipient.count(recipients) ? capturedValueByRecipient[recipients] : String();
        String subject = buildRecipientEmailSubject(primaryRule, capturedVal);

        String srcLine = lineContentByRecipient.count(recipients) ? lineContentByRecipient[recipients] : String();
        String body = buildEmailInfoTable(
            "",
            primaryRule,
            getLabelForRule(primaryRule),
            capturedVal,
            getFreqLabelForRule(primaryRule),
            srcLine,
            capturedVal,
            String());

        // Skip if all matched rules are within their quiet period.
        // Prevents duplicate sends when the streaming path already marked the slots.
        {
            std::vector<String> &grpRules = rulesByRecipient[recipients];
            bool anyAllowed = grpRules.empty(); // No rules → always allow (exact-match recipient)
            for (const String &rn : grpRules) {
                String rnLow = rn; rnLow.toLowerCase();
                if (!isRecipientThrottled(rn, rnLow)) { anyAllowed = true; break; }
            }
            if (!anyAllowed) {
                sendVirtualMessage(buildMessage("⏭️ SD Email: all rules throttled, skipping [%s]", recipients.c_str()), DEBUG);
                for (const String &rn : grpRules) {
                    String rnLow = rn; rnLow.toLowerCase();
                    finalizeRecipientNotificationResult(rn, rnLow, false);
                }
                continue;
            }
        }

        sendVirtualMessage(buildMessage("📤 SD Email: Sending inline (no attachment) to [%s] - %.40s...",
                                           recipients.c_str(), srcLine.c_str()), INFO);

        std::vector<SmtpStreamFile> emptyFiles;
        bool success = smtpSendEmailStreamingSD(
            smtpUser,
            recipients,
            subject,
            body,
            String(F("")),
            emptyFiles,
            smtpServer,
            smtpPort,
            smtpUser,
            smtpPass,
            smtpTlsInsecure,
            String(),
            String()
        );

        if (success) {
            sendVirtualMessage(buildMessage("✅ Email SD enviado | %s → %s",
                                               subject.c_str(), recipients.c_str()), INFO);
            logEmailSent("SMTP", "DATA", subject, recipients);
            observabilityLogEmailPipeline("data-sd", "SMTP", subject.c_str(), recipients.c_str(), true, 1);
            #if ENABLE_LCD_SCREEN==1
            screenManager.showEmailSent();
            #endif
        } else {
            observabilityLogEmailPipeline("data-sd", "SMTP", subject.c_str(), recipients.c_str(), false, 1);
        }

        for (const String &ruleName : rules) {
            String ruleNameLower = ruleName;
            ruleNameLower.toLowerCase();
            finalizeRecipientNotificationResult(ruleName, ruleNameLower, success);
        }
    }

    priorityManager().endEmail(); // Release P3 lock after all groups are processed
}

// ============================================================================
// PROCESS RAM EMAIL QUEUE (recipient matching without SD)
// ============================================================================
// Returns an operator symbol string for a given numeric rule op code.
// Op codes match the internal numeric representation used by getNumericRulesJson().
static const char* opCodeToSymbol(int op) {
    switch (op) {
        case 0:  return "<";    // menor que
        case 1:  return ">";    // maior que
        case 2:  return "\xE2\x89\xA0"; // ≠ (diferente)
        case 4:  return "\xE2\x89\xA5"; // ≥ (maior ou igual)
        case 5:  return "\xE2\x89\xA4"; // ≤ (menor ou igual)
        case 6:  return "\xE2\x88\x88"; // ∈ (intervalo)
        case 7:  return "\xE2\x88\x89"; // ∉ (fora de intervalo)
        case 8:  return "\xCE\x94";     // Δ (variacao)
        case 9:  return "#";            // contagem
        case 10: return "\xF0\x9F\x91\x81"; // 👁 (presenca)
        case 11: return "\xE2\x8F\xB1"; // ⏱ (janela temporal)
        case 12: return "\xE3\x80\xB0"; // 〰 (histerese)
        default: return "=";
    }
}

// Returns a short human-readable label for a numeric op code.
static String opCodeToLabel(int op) {
    switch (op) {
        case 0:  return L(activeLang->opLessThan);
        case 1:  return L(activeLang->opGreaterThan);
        case 2:  return L(activeLang->opNotEqual);
        case 3:  return L(activeLang->opEqual);
        case 4:  return L(activeLang->opGreaterOrEqual);
        case 5:  return L(activeLang->opLessOrEqual);
        case 6:  return L(activeLang->opInterval);
        case 7:  return L(activeLang->opOutside);
        case 8:  return L(activeLang->opVariation);
        case 9:  return L(activeLang->opCount);
        case 10: return L(activeLang->opPresence);
        case 11: return L(activeLang->opTimeWindow);
        case 12: return L(activeLang->opHysteresis);
        default: return L(activeLang->opEqual);
    }
}

// Returns the op symbol for a named numeric rule (looks up the config).
// Returns "=" if the rule name is not found or is a plain-text (equal) rule.
static String getSymbolForRule(const String &ruleName) {
    JsonArray numericRules = configManager.getRecipientSlots();
    for (JsonObject r : numericRules) {
        bool enabled = r.containsKey("enabled") ? r["enabled"].as<bool>() : true;
        if (!enabled) continue;

        if (!r.containsKey("name") || !r.containsKey("op")) continue;
        if (r["name"].as<String>() == ruleName) {
            return String(opCodeToSymbol(opStrToInt(r["op"] | String(""))));
        }
    }
    return String("="); // Plain-text rule: equality match
}

// Returns the human-readable operator label for a named numeric rule.
// Returns opEqual label if not found (plain-text rule).
static String getLabelForRule(const String &ruleName) {
    JsonArray numericRules = configManager.getRecipientSlots();
    for (JsonObject r : numericRules) {
        bool enabled = r.containsKey("enabled") ? r["enabled"].as<bool>() : true;
        if (!enabled) continue;

        if (!r.containsKey("name") || !r.containsKey("op")) continue;
        if (r["name"].as<String>() == ruleName) {
            return opCodeToLabel(opStrToInt(r["op"] | String("")));
        }
    }
    return L(activeLang->opEqual); // Plain-text rule
}

// Extracts the first recognizable numeric value from a data line.
// Returns empty string if no number found.
static String extractFirstNumber(const String &line) {
    bool inNum = false;
    String numStr;
    for (int i = 0; i < (int)line.length(); i++) {
        char ch = line.charAt(i);
        if ((ch >= '0' && ch <= '9') || ch == '.' || (ch == '-' && !inNum)) {
            numStr += ch;
            inNum = true;
        } else if (inNum) {
            break; // End of number token
        }
    }
    return numStr;
}

// Extracts the best candidate numeric value from a data line for subject display.
// More reliable than extractFirstNumber for lines with timestamps or ID prefixes:
//   "18:51:30 018.79" -> "018.79"  (ignores HH:MM:SS parts terminated by ':')
//   "001 018.79"      -> "018.79"  (returns last number, skipping ID prefix)
//   "CO2: 450.23 ppm" -> "450.23"  (digit before ':' is skipped as label suffix)
//   "018.79"          -> "018.79"  (bare value passes through unchanged)
// Returns the last valid number token found that is NOT immediately followed by ':'.
static String extractSmartNumber(const String &line) {
    String lastNum;
    bool inNum = false;
    String current;
    int len = (int)line.length();
    for (int i = 0; i <= len; i++) {
        char ch = (i < len) ? line.charAt(i) : '\0';
        bool isNumChar = (ch >= '0' && ch <= '9') || ch == '.' || (ch == '-' && !inNum);
        if (isNumChar) {
            current += ch;
            inNum = true;
        } else if (inNum) {
            // Skip numbers terminated by ':' — they are timestamp components (HH:, MM:, SS:)
            // or label suffixes ("CO2:"). Keeping only numbers with neutral/space terminators.
            if (ch != ':') {
                lastNum = current; // Accept this number as a data candidate
            }
            current = "";
            inNum = false;
        }
    }
    return lastNum;
}

// Returns an emoji string matching the rule name based on keyword heuristics.
// Checks the rule name (lowercased) for known sensor/domain keywords.
static String getRuleEmoji(const String &ruleName) {
    String rn = ruleName;
    rn.toLowerCase();
    if (rn.indexOf("temp") >= 0)                                                    return String(F("\xF0\x9F\x8C\xA1\xEF\xB8\x8F")); // 🌡️
    if (rn.indexOf("hum") >= 0)                                                     return String(F("\xF0\x9F\x92\xA7"));             // 💧
    if (rn.indexOf("press") >= 0)                                                   return String(F("\xF0\x9F\x8C\x8A"));             // 🌊
    if (rn.indexOf("lux") >= 0 || rn.indexOf("luz") >= 0 ||
        rn.indexOf("light") >= 0 || rn.indexOf("solar") >= 0)                      return String(F("\xE2\x98\x80\xEF\xB8\x8F"));    // ☀️
    if (rn.indexOf("co2") >= 0 || rn.indexOf("gas") >= 0 ||
        rn.indexOf("gaz") >= 0 || rn.indexOf("smoke") >= 0 ||
        rn.indexOf("fumo") >= 0)                                                    return String(F("\xF0\x9F\x8C\xBF"));             // 🌿
    if (rn.indexOf("alarm") >= 0 || rn.indexOf("alerta") >= 0 ||
        rn.indexOf("alert") >= 0)                                                   return String(F("\xF0\x9F\x9A\xA8"));             // 🚨
    if (rn.indexOf("water") >= 0 || rn.indexOf("agua") >= 0 ||
        rn.indexOf("chuva") >= 0 || rn.indexOf("rain") >= 0)                       return String(F("\xF0\x9F\x92\xA6"));             // 💦
    if (rn.indexOf("volt") >= 0 || rn.indexOf("amper") >= 0 ||
        rn.indexOf("power") >= 0 || rn.indexOf("watt") >= 0)                       return String(F("\xE2\x9A\xA1"));                 // ⚡
    if (rn.indexOf("bat") >= 0 || rn.indexOf("bater") >= 0)                        return String(F("\xF0\x9F\x94\x8B"));             // 🔋
    return String(F("\xF0\x9F\x93\xA9"));                                                                                             // 📩 default
}

// Extracts the threshold value(s) from a rule name string.
// Supports CSV format "key, threshold" (returns the part after ", ") and
// legacy embedded-number format "Name35" (returns the first numeric substring).
// Returns empty string if no threshold is found.
static String getThresholdForRule(const String &ruleName) {
    // CSV format "key, threshold" — return the part after ", "
    int csvSep = ruleName.indexOf(", ");
    if (csvSep >= 1) {
        String thresholdStr = ruleName.substring(csvSep + 2);
        thresholdStr.trim();
        return thresholdStr;
    }
    // Interval rule: "20.00..45.00" — return the full range as-is
    // The legacy digit-scanner would stop at the first '.' before the second dot,
    // returning only the lower bound. Instead, return the whole string from the
    // first digit to the end (trimmed), which preserves "20.00..45.00".
    int dotsPos = ruleName.indexOf("..");
    if (dotsPos > 0) {
        // Find where the numeric part starts (skip any leading prefix/label)
        int startPos = 0;
        for (int i = 0; i < dotsPos; i++) {
            if (isDigit(ruleName.charAt(i)) || ruleName.charAt(i) == '-') {
                startPos = i;
                break;
            }
        }
        String rangeStr = ruleName.substring(startPos);
        rangeStr.trim();
        return rangeStr;
    }
    // Legacy: scan for the first digit sequence (with optional decimal point)
    for (int i = 0; i < (int)ruleName.length(); i++) {
        if (isDigit(ruleName.charAt(i))) {
            int j = i;
            bool seenDot = false;
            while (j < (int)ruleName.length() &&
                   (isDigit(ruleName.charAt(j)) || (!seenDot && ruleName.charAt(j) == '.'))) {
                if (ruleName.charAt(j) == '.') seenDot = true;
                j++;
            }
            return ruleName.substring(i, j);
        }
    }
    return String();
}

// Converts a frequency in seconds to a concise human-readable label.
// 0 (always) -> "always", <60 -> "Xs", <3600 -> "Xmin", <86400 -> "Xh", else "Xd".
static String freqSecondsToLabel(int freqSec) {
    if (freqSec <= 0)    return L(activeLang->emailAlways);
    if (freqSec < 60)    return String(freqSec) + String(F("s"));
    if (freqSec < 3600)  return String(freqSec / 60) + String(F("min"));
    if (freqSec < 86400) return String(freqSec / 3600) + String(F("h"));
    return String(freqSec / 86400) + String(F("d"));
}

// Returns the send-frequency label for the named rule by looking up its recipient slot.
// Falls back to "sempre" if the slot is not found.
// Applies the same 180s minimum as shouldThrottleRecipient so the subject label
// always matches the actual throttle behaviour (slots without the freq field stored
// return 0 from getRecipientSlotFrequency, which would incorrectly show "imediata").
static String getFreqLabelForRule(const String &ruleName) {
    String rn = ruleName;
    rn.toLowerCase();
    int slotIdx = configManager.findRecipientSlotIndexByName(rn);
    if (slotIdx < 0) return L(activeLang->emailAlways);
    int freqSec = configManager.getRecipientSlotFrequency(slotIdx);
    if (freqSec < 180) freqSec = 180; // Mirror shouldThrottleRecipient minimum
    return freqSecondsToLabel(freqSec);
}

// Builds the email subject for a recipient-triggered alert email.
// Format: "emoji - SistemaNome - Regras - ruleName - opLabel - Encontrei (capturedValue)"
// ruleName and opLabel mirror the primary rule shown in the body.
// capturedValue may be empty (omits the "- Encontrei (value)" segment).
static String buildRecipientEmailSubject(const String &ruleName,
                                          const String &capturedValue) {
    String emoji = ruleName.isEmpty() ? String(F("\xF0\x9F\x93\xA9")) : getRuleEmoji(ruleName);
    String opLabel = ruleName.isEmpty() ? String() : getLabelForRule(ruleName);
    String subject = emoji + String(F(" - ")) + configManager.getSystemName() + L(activeLang->emailRulesSuffix);
    if (ruleName.length() > 0) {
        subject += String(F(" - ")) + ruleName;
    }
    if (opLabel.length() > 0) {
        subject += String(F(" - ")) + opLabel;
    }
    if (capturedValue.length() > 0) {
        subject += L(activeLang->emailFoundPrefix) + capturedValue + String(F(")"));
    }
    return subject;
}

// ============================================================================
// Processes RAM-based email jobs created by recipient matching when SD is inactive.
// Each job contains a line copy + matched recipients; sends separate emails.
// Thread-safe: uses ramEmailQueueMutex for concurrent access protection.
void processRamEmailQueue() {
    // Skip email sending for the first 90 seconds after boot.
    // During this window the SD card, NVS config and WiFi are still initialising;
    // any match that fires here would send an attachment-less, possibly mis-labelled email.
    // Jobs queued during this window are kept and sent on the next 30s cycle.
    static const unsigned long BOOT_WARMUP_MS = 90000UL;
    if (millis() < BOOT_WARMUP_MS) {
        static bool _ramWarmupLogged = false;  // log once (repeats every cycle during warmup)
        if (!_ramWarmupLogged) {
            _ramWarmupLogged = true;
            sendVirtualMessage(F("⏳ processRamEmailQueue: boot warmup active, deferring RAM emails"), DEBUG);
        }
        return;
    }

    // Check if there are pending RAM jobs (protected by mutex)
    if (!ramEmailQueueMutex) return; // Mutex not initialized
    
    if (xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return; // Failed to acquire lock, try later
    }
    
    if (ramEmailJobQueue.empty()) {
        xSemaphoreGiveRecursive(ramEmailQueueMutex);
        return; // Nothing to process
    }
    
    sendVirtualMessage(buildMessage("📧 RAM Email: Processing %u pending jobs...", 
                                      (unsigned)ramEmailJobQueue.size()), INFO);
    
    // Copy queue for local processing and clear the global queue
    // This releases the mutex quickly so new writes don't block
    std::vector<PendingRamEmailJob> jobsToProcess = ramEmailJobQueue;
    ramEmailJobQueue.clear();
    ramEmailJobQueue.shrink_to_fit();
    xSemaphoreGiveRecursive(ramEmailQueueMutex);
    
    // SMTP configuration
    String smtpServer = configManager.getSmtpServer();
    int smtpPort = configManager.getSmtpPort();
    String smtpUser = configManager.getSmtpUsername();
    String smtpPass = configManager.getSmtpPassword();
    bool smtpTlsInsecure = configManager.getSmtpTlsInsecure();
    
    // Verify configuration
    if (smtpServer.isEmpty() || smtpUser.isEmpty() || smtpPass.isEmpty()) {
        sendVirtualMessage(F("❌ RAM Email: SMTP config incomplete, aborting"), ERROR);
        return;
    }

    // Block if a concurrent email operation (EMAILDAY or main state machine) is active on another task.
    // Re-queue jobs so they are not lost and will be retried on the next 30s cycle.
    if (!priorityManager().canStartEmail()) {
        sendVirtualMessage(F("⏸️ processRamEmailQueue: Waiting (another email active, will retry)"), WARN);
        if (xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ramEmailJobQueue.insert(ramEmailJobQueue.begin(), jobsToProcess.begin(), jobsToProcess.end());
            xSemaphoreGiveRecursive(ramEmailQueueMutex);
        }
        return;
    }
    priorityManager().startEmail();

    // Process each job individually
    for (const auto &job : jobsToProcess) {
        
        // PRIORITY: Pause if EMAILDAY is active
        if (emailDayInProgress) {
            sendVirtualMessage(F("⏸️ RAM Email: PAUSED (EMAILDAY active), re-queueing..."), WARN);
            
            // Return remaining jobs to queue
            if (xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ramEmailJobQueue.push_back(job);
                xSemaphoreGiveRecursive(ramEmailQueueMutex);
            }
            priorityManager().endEmail(); // Release P3 lock before yielding to EMAILDAY
            return;
        }
        
        // Heap protection
        if (ESP.getFreeHeap() < 20000) {
            sendVirtualMessage(buildMessage("⚠️ RAM Email: Low heap (%u bytes), delaying...", ESP.getFreeHeap()), WARN);
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2s
            
            // Re-queue remaining jobs
            if (xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ramEmailJobQueue.push_back(job);
                xSemaphoreGiveRecursive(ramEmailQueueMutex);
            }
            priorityManager().endEmail(); // Release P3 lock before exiting early
            return;
        }
        
        // Check frequency throttle — skip job if all matched rules are within their quiet window.
        // Iterate the "|"-separated matchedRules list; if every rule slot is throttled, skip.
        // A slot index < 0 means the rule was not found in config — treat as always allowed.
        {
            bool anyAllowed = false;
            String rulesStr = job.matchedRules;
            int rStart = 0;
            while (rStart <= (int)rulesStr.length()) {
                int comma = rulesStr.indexOf('|', rStart);
                if (comma < 0) comma = (int)rulesStr.length();
                String rule = rulesStr.substring(rStart, comma);
                rule.trim();
                if (rule.length() > 0) {
                    String ruleLower = rule;
                    ruleLower.toLowerCase();
                    // isRecipientThrottled resolves the slot index internally from the name
                    if (!isRecipientThrottled(rule, ruleLower)) {
                        anyAllowed = true;
                        break;
                    }
                }
                rStart = comma + 1;
            }
            if (!anyAllowed) {
                sendVirtualMessage(buildMessage("⏭️ RAM Email: throttled [%s] rules=[%.30s]",
                                                   job.recipients.c_str(), job.matchedRules.c_str()), DEBUG);
                finalizeMatchedRulesResult(job.matchedRules, false);
                continue; // All rules are within their quiet period — skip job
            }
        }

        // Prepare email content
        // Subject mirrors the same primary rule/operator shown in the body.
        // When multiple rules matched, use the first rule as the primary context.
        // Rules are "|" separated (not "," which collides with CSV rule names like "key, threshold").
        String primaryRule = job.matchedRules;
        int pipeIdx = primaryRule.indexOf('|');
        if (pipeIdx > 0) primaryRule = primaryRule.substring(0, pipeIdx);
        primaryRule.trim();

        // Use the value captured during rule matching (exact, correct column in multi-value lines).
        // Falls back to extractSmartNumber on the raw line if no captured value was stored.
        String capturedNum = job.capturedValue;
        if (capturedNum.length() == 0) capturedNum = extractSmartNumber(job.lineContent);
        // Guard: discard if no digit (safety check against stray '-' or '.')
        {
            bool hasDigit = false;
            for (int _i = 0; _i < (int)capturedNum.length(); _i++) {
                if (isDigit(capturedNum.charAt(_i))) { hasDigit = true; break; }
            }
            if (!hasDigit) capturedNum = "";
        }
        String subject = buildRecipientEmailSubject(primaryRule, capturedNum);

        // Body: no attachment line for Regras emails.
        String body = buildEmailInfoTable(
            "",
            primaryRule,
            getLabelForRule(primaryRule),
            capturedNum,
            getFreqLabelForRule(primaryRule),
            job.lineContent,
            capturedNum,
            job.timestamp);
        
        sendVirtualMessage(buildMessage("📤 RAM Email: Sending to [%s] - %.40s...", 
                                          job.recipients.c_str(), 
                                          job.lineContent.c_str()), INFO);
        
        // Send email using existing SMTP streaming function (no attachments)
// Empty file vector = text-only email (unless we attach text below)
        std::vector<SmtpStreamFile> emptyFiles;

        // Rules emails: never attach — the matched line is already shown in the
        // "Linha:" row of buildEmailInfoTable. Attachments are only for EMAILDAY.
        bool success = smtpSendEmailStreamingSD(
            smtpUser,              // from
            job.recipients,        // recipients
            subject,               // subject
            body,                  // bodyText
            String(F("")),         // folderPath (empty)
            emptyFiles,            // files (empty = no attachments)
            smtpServer,            // server
            smtpPort,              // port
            smtpUser,              // user
            smtpPass,              // pass
            smtpTlsInsecure,       // tlsInsecure
            String(),              // attachName: empty (no attachment)
            String()               // attachData: empty
        );
        
        if (success) {
            sendVirtualMessage(buildMessage("✅ Email RAM enviado | %s → %s",
                                               subject.c_str(), job.recipients.c_str()), INFO);
            logEmailSent("SMTP", "DATA", subject, job.recipients);
            observabilityLogEmailPipeline("data-ram", "SMTP", subject.c_str(), job.recipients.c_str(), true, 0);
            #if ENABLE_LCD_SCREEN==1
            screenManager.showEmailSent();
            #endif
        } else {
            sendVirtualMessage(buildMessage("❌ RAM Email: FAILED to [%s]", job.recipients.c_str()), ERROR);
            observabilityLogEmailPipeline("data-ram", "SMTP", subject.c_str(), job.recipients.c_str(), false, 0);
        }

        finalizeMatchedRulesResult(job.matchedRules, success);
        
        // Yield between emails and feed watchdog
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
    }
    
    priorityManager().endEmail(); // Release P3 lock after all jobs are processed
    sendVirtualMessage(F("✅ RAM Email: Queue processing complete"), INFO);
}

// ⚡ MANUAL TRIGGER: Force immediate email queue processing (for testing/debug)
// THREAD-SAFE: Can be called from web handlers or serial commands
// SECURITY: This should ONLY be exposed to authenticated admin users
void triggerImmediateEmailQueueProcessing() {
    sendVirtualMessage(F("⚡ MANUAL TRIGGER: Processing email queue NOW..."), INFO);
    
    // Process both queues immediately (thread-safe)
    processSdEmailQueue();      // SD+Email mode (recipient-matched jobs)
    processEmailQueue();         // Normal streaming email queue
    
    sendVirtualMessage(F("✅ Manual email queue processing complete"), INFO);
}

// Quick helper to check if data contains recipient matches (streaming)
// Retorna true se encontrar pelo menos 1 destinatario configurado (regras Igual + Numericas)
// Accumulation buffer: matched data is stored for 30s before sending
// OPTIMIZATION: Increased from 8KB to 64KB and moved to PSRAM (no heap fragmentation)
static const size_t MAX_STREAMING_BUFFER_SIZE = 64 * 1024;  // 64KB maximum in PSRAM
static std::vector<char, PsramAllocatorStd<char>> streamingEmailBuffer; // PSRAM buffer
static unsigned long firstMatchTimestamp = 0;         // Timestamp do primeiro match
static bool streamingEmailPending = false;            // Flag: an email is pending
// Accumulated matched rule names ("|"-separated) for the current streaming window.
// Populated by hasRecipientMatchStreaming on each match during the window.
// Passed directly to enqueueStreamingEmail so collectMatchedRulesFromContent is not needed.
static String streamingMatchedRulesAccum;

// Append "|"-separated rule names from newRules into accum, deduplicating.
static void appendRulesToAccum(const String &newRules, String &accum) {
    int s = 0;
    while (s <= (int)newRules.length()) {
        int sep = newRules.indexOf('|', s);
        if (sep < 0) sep = (int)newRules.length();
        String ruleName = newRules.substring(s, sep);
        ruleName.trim();
        if (ruleName.length() > 0) {
            bool already = false;
            int s2 = 0;
            while (s2 <= (int)accum.length()) {
                int sep2 = accum.indexOf('|', s2);
                if (sep2 < 0) sep2 = (int)accum.length();
                if (accum.substring(s2, sep2) == ruleName) { already = true; break; }
                if (sep2 >= (int)accum.length()) break;
                s2 = sep2 + 1;
            }
            if (!already) {
                if (accum.length() > 0) accum += "|";
                accum += ruleName;
            }
        }
        if (sep >= (int)newRules.length()) break;
        s = sep + 1;
    }
}

// Helper: convert vector buffer to String for email sending
static inline String streamingBufferToString() {
    if (streamingEmailBuffer.empty()) return "";
    return String(streamingEmailBuffer.data(), streamingEmailBuffer.size());
}

// Helper: append String data to PSRAM vector buffer
static inline void streamingBufferAppend(const String &data) {
    if (data.length() == 0) return;
    size_t oldSize = streamingEmailBuffer.size();
    streamingEmailBuffer.resize(oldSize + data.length());
    memcpy(streamingEmailBuffer.data() + oldSize, data.c_str(), data.length());
}

// Helper: clear buffer and release PSRAM
static inline void streamingBufferClear() {
    streamingEmailBuffer.clear();
    streamingEmailBuffer.shrink_to_fit(); // Release PSRAM immediately
}

void endEmaildayFreezeCleanup() {
#if ENABLE_SDCARD==1
    sdcardFreezeWrites = false;

    if (sdcardSwapLock(pdMS_TO_TICKS(500))) {
        if (!sdcardFreezeBuffer_RS232.empty()) {
            sendVirtualMessage(buildMessage("🔁 Transferring %u RS232 chunks from freeze buffer",
                                              (unsigned)sdcardFreezeBuffer_RS232.size()), DEBUG);
            for (auto &chunk : sdcardFreezeBuffer_RS232) {
                auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
                if (activeRS232) activeRS232->push_back(std::move(chunk));
            }
            sdcardActiveBytes_RS232 += sdcardFreezeBytes_RS232;
            sdcardFreezeBuffer_RS232.clear();
            sdcardFreezeBuffer_RS232.shrink_to_fit();
            sdcardFreezeBytes_RS232 = 0;
        }

        if (!sdcardFreezeBuffer_LPT.empty()) {
            sendVirtualMessage(buildMessage("🔁 Transferring %u LPT chunks from freeze buffer",
                                              (unsigned)sdcardFreezeBuffer_LPT.size()), DEBUG);
            for (auto &chunk : sdcardFreezeBuffer_LPT) {
                auto* activeLPT = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
                if (activeLPT) activeLPT->push_back(std::move(chunk));
            }
            sdcardActiveBytes_LPT += sdcardFreezeBytes_LPT;
            sdcardFreezeBuffer_LPT.clear();
            sdcardFreezeBuffer_LPT.shrink_to_fit();
            sdcardFreezeBytes_LPT = 0;
        }

        sdcardSwapUnlock();
    }

    sdcardFreezeCutoffSeq_RS232 = 0;
    sdcardFreezeCutoffSeq_LPT = 0;
#endif // ENABLE_SDCARD

    // Always disable PSRAM snapshot when emailday ends (runs in both SD and PSRAM-only builds).
    if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled() && psramVFSIsSnapshotActive()) {
        sendVirtualMessage(F("🔓 EMAILDAY: Force-disabling PSRAM snapshot mode"), WARN);
        psramVFSDisableSnapshot();
    }
}