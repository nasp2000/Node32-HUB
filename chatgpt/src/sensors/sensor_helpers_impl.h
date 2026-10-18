// sensor_helpers_impl.h
// Included only from chatgpt.ino (single translation unit).
// Daily storage stats (defined in sd_tasks_impl.h)
void storNoteSdWrite(uint64_t bytes);

// =============================================================
// Sensor Data Injection (Option C)
// =============================================================
// Sensor readings are injected into:
//   1. bufferDados  — display on /monitor page (alongside RS232/LPT data, no tag)
//   2. ramEmailJobQueue — recipient/rule matching for email alerts
// They are NEVER written to SD/PSRAM storage files; persistence is handled
// exclusively by SensorHistory (PSRAM ring buffer + periodic CSV flush).
// Guard covers AHT20/BMP280, MQ135, proximity and Unikon — any module can call this function.
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1 || ENABLE_PROXIMITY==1 || ENABLE_UNIKON==1
void injectDataToRXBuffer(const String &data, const String &source,
                          bool resetInactivity = false) {
    if (data.length() == 0) return;

    // --- STEP 1: Write to visualization buffer (bufferDados) for /monitor page ---
    // Same path as RS232/LPT so both sources are interleaved in display order.
    bool didLock = false;
    if (bufferDadosLock(pdMS_TO_TICKS(50), didLock)) {
        size_t capacity = bufferDados.capacity();
        size_t currentSize = bufferDados.size();
        const size_t freeSlots = (capacity > currentSize) ? (capacity - currentSize) : 0;
        if (capacity > 0 && freeSlots >= data.length()) {
            // Ensure data starts on its own line (avoids merging with previous LPT/RS232 content)
            if (currentSize > 0 && bufferDados[currentSize - 1] != '\n') {
                bufferDados.push((uint8_t)'\n');
            }
            for (size_t i = 0; i < data.length(); i++) {
                char c = data[i];
                if (c == '\r') continue; // normalise line endings
                bufferDados.push((uint8_t)c);
            }
            bufferDadosSeq = bufferDadosSeq + 1; // trigger web refresh
        } else if (capacity > 0) {
            // AUTO-TRIM: buffer is full (usually flooded by RS232 data).
            // Evict oldest bytes to make room so sensor data is never silently dropped.
            bufferDadosTrimOldest(data.length() + 1);
            size_t updatedSize = bufferDados.size();
            if (updatedSize > 0 && bufferDados[updatedSize - 1] != '\n') {
                bufferDados.push((uint8_t)'\n');
            }
            for (size_t i = 0; i < data.length(); i++) {
                char c = data[i];
                if (c == '\r') continue;
                bufferDados.push((uint8_t)c);
            }
            bufferDadosSeq = bufferDadosSeq + 1;
        }
        bufferDadosUnlock(didLock);
    } else {
        bufferDadosPushMutexFailCount = bufferDadosPushMutexFailCount + 1;
    }

    // On boards WITHOUT RS232/LPT (e.g. Waveshare 8DI8DO), proximity is the
    // ONLY real production data source — generic sensors (HX711, AHT20, etc.)
    // must NOT reset the inactivity timer, otherwise they would mask a real
    // machine stop (the timer would be reset every sensor tick). Proximity
    // handles the reset itself in proximity_manager.cpp.
    // On boards WITH RS232/LPT, honor the user "Sensors count for inactivity"
    // checkbox so RX data still resets as before.
#if ENABLE_RS232==0 && ENABLE_DB25==0
    // On boards without RS232/LPT (e.g. Waveshare 8DI8DO), proximity is
    // the ONLY production data source. Allow sensor buffer injections to
    // reset the timer — without this, the timer would never start.
    const bool _resetInactivitySH = true;
#else
    const bool _resetInactivitySH = configManager.getInactivityAlertSensorsResetTimer();
#endif
    if (resetInactivity && _resetInactivitySH) {
        lastDataReceivedForInactivityAlert = millis();
        inactivityAlertSent = false; // Reset flag when optional sensor activity is enabled
        for (int _i = 0; _i < 6; ++_i) inactivityAlertSentSlot[_i] = false;

        static unsigned long lastInactivityUpdateLogSensor = 0;
        if (millis() - lastInactivityUpdateLogSensor > 15000) {
            char logBuf[128];
            const char *activitySource = source.length() > 0 ? source.c_str() : "sensor buffer injection";
            snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by %s (ts=%lu)", activitySource, lastDataReceivedForInactivityAlert);
            sendVirtualMessage(String(logBuf), INFO);
            lastInactivityUpdateLogSensor = millis();
        }
    }

    // --- STEP 2: Recipient / rule matching (line by line) ---
    // Reuses the same ramEmailJobQueue and logic as the RS232 RAM path.
    // SD card is never involved — sensor data is never written to storage files.
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    bool sdActive = configManager.getSdcardEnabled();
    std::vector<PendingRamEmailJob> matchedJobs;

    int startIdx = 0;
    while (startIdx < (int)data.length()) {
        int endIdx = data.indexOf('\n', startIdx);
        if (endIdx == -1) endIdx = data.length();

        String line = data.substring(startIdx, endIdx);
        line.trim();

        if (line.length() > 0) {
            String lineLower = line;
            lineLower.toLowerCase();
            std::vector<std::pair<String, String>> lineRecipients;
            std::map<String,String> capturedByRule;
            findRecipientsInLowerText(lineLower, lineRecipients, sdActive, &capturedByRule);

            if (!lineRecipients.empty()) {
                PendingRamEmailJob job;
                job.lineContent = line;
                job.source      = "SENSOR";
                job.processed   = false;
                for (const auto &rec : lineRecipients) {
                    if (capturedByRule.count(rec.first)) { job.capturedValue = capturedByRule[rec.first]; break; }
                }
                if (job.capturedValue.length() == 0) job.capturedValue = extractSmartNumber(line);

                std::vector<String> uniqueEmails, uniqueRules;
                for (const auto &rec : lineRecipients) {
                    bool seen = false;
                    for (const auto &e : uniqueEmails) if (e == rec.second) { seen = true; break; }
                    if (!seen) uniqueEmails.push_back(rec.second);
                    seen = false;
                    for (const auto &r : uniqueRules) if (r == rec.first) { seen = true; break; }
                    if (!seen) uniqueRules.push_back(rec.first);
                }
                for (size_t i = 0; i < uniqueEmails.size(); i++) {
                    if (i > 0) job.recipients += ", ";
                    job.recipients += uniqueEmails[i];
                }
                for (size_t i = 0; i < uniqueRules.size(); i++) {
                    if (i > 0) job.matchedRules += "|";
                    job.matchedRules += uniqueRules[i];
                }

                time_t nowTs = time(nullptr);
                struct tm tInfo;
                localtime_r(&nowTs, &tInfo);
                char tsBuf[10];
                snprintf(tsBuf, sizeof(tsBuf), "%02d:%02d:%02d",
                         tInfo.tm_hour, tInfo.tm_min, tInfo.tm_sec);
                job.timestamp = String(tsBuf);

                matchedJobs.push_back(job);
            }
        }
        startIdx = endIdx + 1;
    }

    if (!matchedJobs.empty()) {
        sendVirtualMessage(buildMessage("📋 Sensor match: %u line(s) → email queue",
                                          (unsigned)matchedJobs.size()), INFO);
        if (ramEmailQueueMutex &&
            xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            for (const auto &newJob : matchedJobs) {
                if (ramEmailJobQueue.size() >= MAX_RAM_EMAIL_JOBS) {
                    for (auto it = ramEmailJobQueue.begin(); it != ramEmailJobQueue.end(); ++it) {
                        if (!it->processed) {
                            finalizeMatchedRulesResult(it->matchedRules, false);
                            ramEmailJobQueue.erase(it);
                            break;
                        }
                    }
                    if (ramEmailJobQueue.size() >= MAX_RAM_EMAIL_JOBS)
                    {
                        finalizeMatchedRulesResult(ramEmailJobQueue.front().matchedRules, false);
                        ramEmailJobQueue.erase(ramEmailJobQueue.begin());
                    }
                }
                ramEmailJobQueue.push_back(newJob);
                if (newJob.matchedRules.length() > 0) markMatchedRulesPending(newJob.matchedRules);
            }
            xSemaphoreGiveRecursive(ramEmailQueueMutex);
        }
    }
#endif

    // Debug preview (suppressed in release builds)
    #if ENABLE_DEBUG_LOGS==1
    String preview = data.substring(0, min((int)data.length(), 80));
    preview.replace("\n", " "); preview.replace("\r", "");
    sendVirtualMessage(String(F("[SENSOR] ")) + preview, DEBUG);
    #endif
}

// Queue sensor alarm email (stub for alarm system)
void enqueueSensorAlarmEmail(const String &subject, const String &body) {
    // Simple implementation: use existing email infrastructure
    // Future enhancement: create dedicated alarm email queue with priority
    
    #if ENABLE_SMTP==1
    // Log alarm
    sendVirtualMessage(String(F("Sensor alarm: ")) + subject, WARN);
    
    // Queue alarm email using existing system
    // This will be sent via the next scheduled email check
    // For now, just log the alarm - full email integration requires extending email queue
    Serial.println("[SENSOR_ALARM] Subject: " + subject);
    Serial.println("[SENSOR_ALARM] Body preview: " + body.substring(0, 100));

    // T24: body .txt file (ALARM, send time), SD or LittleFS,
    // padrao T8b turno_data. Registo local mesmo sem fila de envio.
    {
        extern bool emailArchiveSaveBody(const String &tipo, const String &bodyText);
        emailArchiveSaveBody(F("ALARM"), body);
    }
    
    // TODO: Integrate with email queue when email system supports priority messages
    // For Phase 1, alarms are logged and can be seen in serial/telnet monitors
    #endif
}
#else
void injectDataToRXBuffer(const String &data, const String &source,
                          bool resetInactivity = false) {
    if (data.length() == 0) return;

    // Fallback path when optional sensor modules are disabled.
    // Keep monitor visibility for GRBL/USB and any other producer.
    bool didLock = false;
    if (bufferDadosLock(pdMS_TO_TICKS(50), didLock)) {
        size_t currentSize = bufferDados.size();
        if (currentSize > 0 && bufferDados[currentSize - 1] != '\n') {
            bufferDados.push((uint8_t)'\n');
        }
        for (size_t i = 0; i < data.length(); i++) {
            char c = data[i];
            if (c == '\r') continue;
            bufferDados.push((uint8_t)c);
        }
        bufferDadosSeq = bufferDadosSeq + 1;
        bufferDadosUnlock(didLock);
    } else {
        bufferDadosPushMutexFailCount = bufferDadosPushMutexFailCount + 1;
    }

    (void)source;
}

void enqueueSensorAlarmEmail(const String &subject, const String &body) {
    (void)subject;
    (void)body;
}
#endif

// =============================================================
// IP Detection Helper (for log file headers)
// =============================================================
static String getCurrentIPForINO() {
    return getCurrentNetworkIPString();
}

// =============================================================
// LPT Copy to SD: Save .txt backup of each LPT email send
// =============================================================
#if ENABLE_SDCARD==1
static bool saveLptCopyToSD(const String &emailContent) {
    // Save a .txt copy of LPT email content to SD Card (persistent backup)
    // PSRAM is NOT used: backups must be persistent (PSRAM is volatile)
    // Creates files in /LPT_COPIES/YYYY-MM-DD_HHMMSS.txt
    
    if (emailContent.length() == 0) {
        return false; // No content to save
    }
    
    // Check SD availability
    if (!sdcardIsMounted()) {
        Serial.println("saveLptCopyToSD: SD card not mounted");
        return false; // SD not available
    }
    
    // Get current timestamp for filename
    struct tm timeinfo;
    char filename[64];
    
    if (!getLocalTime(&timeinfo, 100)) {
        // Fallback to millis-based naming if NTP not synced
        unsigned long ms = millis();
        snprintf(filename, sizeof(filename), "/LPT_COPIES/lpt_%lu.txt", ms);
    } else {
        // Create filename with timestamp: /LPT_COPIES/2025-01-21_224530.txt
        strftime(filename, sizeof(filename), "/LPT_COPIES/%Y-%m-%d_%H%M%S.txt", &timeinfo);
    }
    
    // Create directory only if it does not yet exist.
    if (!SD_MMC.exists("/LPT_COPIES")) SD_MMC.mkdir("/LPT_COPIES");
    
    // Open file for writing
    File f = SD_MMC.open(filename, FILE_WRITE);
    if (!f) {
        Serial.printf("saveLptCopyToSD: Failed to create %s\n", filename);
        return false;
    }
    
    // Write IP header if file is new (empty)
    size_t fileSize = f.size();
    if (fileSize == 0) {
        String ipHeader = "Device IP: " + getCurrentIPForINO() + "\n";
        size_t headerWritten = f.print(ipHeader);
        if (headerWritten > 0) {
            f.flush(); // Ensure header is written immediately
            Serial.printf("[LPT_COPIES] New file with IP header: %s (IP: %s)\n", 
                         filename, getCurrentIPForINO().c_str());
        }
    }
    
    // Write content
    size_t written = f.print(emailContent);
    size_t endSize = f.size();
    f.close();
    storNoteSdWrite(endSize > fileSize ? (size_t)(endSize - fileSize) : written);
    
    if (written == emailContent.length()) {
        Serial.printf("✅ LPT copy saved (SD): %s (%u bytes)\n", filename, written);
        return true;
    } else {
        Serial.printf("⚠️ LPT copy incomplete (SD): %s (%u/%u bytes)\n", filename, written, emailContent.length());
        return false;
    }
}
#else
static inline bool saveLptCopyToSD(const String &) { return false; }
#endif

// =============================================================
// Arduino entry point: setup
// =============================================================