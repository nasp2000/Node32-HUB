// wifi_config_impl.h
// Included only from chatgpt.ino (single translation unit).
// WiFi Scan Async Helper (saves 10-20KB + eliminates 2-5s blocking calls)
// Returns cached results if valid (<60s), otherwise starts async scan and returns when complete
int performCachedWiFiScan(bool forceNew = false) {
    unsigned long currentTime = millis();
    
    // Check if cache is still valid and not forcing new scan
    if (!forceNew && wifiScanCache && cachedScanCount > 0 && 
        (currentTime - lastScanTime) < WIFI_SCAN_CACHE_DURATION) {
        // Cache is valid, return cached count
        return cachedScanCount;
    }
    
    // Check if async scan is in progress
    int16_t scanResult = WiFi.scanComplete();
    
    if (scanResult == WIFI_SCAN_RUNNING) {
        // Scan in progress, return 0 to indicate not ready yet
        return 0;
    }
    
    if (scanResult >= 0) {
        // Scan completed, update cache
        cachedScanCount = scanResult;
        
        // Free old cache if exists
        if (wifiScanCache) {
            // FIX for memory corruption: If we malloc'd it, we MUST free it!
            // Cannot use delete[] on heap_caps_malloc memory.
            // Since CachedWiFiScanResult contains Strings, we must call destructors!
            if (psramFound()) {
                for (int i = 0; i < cachedScanCount; i++) {
                    wifiScanCache[i].~CachedWiFiScanResult();
                }
                heap_caps_free(wifiScanCache);
            } else {
                delete[] wifiScanCache;
            }
            wifiScanCache = nullptr;
        }
        
        // Allocate new cache in PSRAM if available
        if (cachedScanCount > 0) {
            if (psramFound()) {
                void* ptr = heap_caps_malloc(cachedScanCount * sizeof(CachedWiFiScanResult), MALLOC_CAP_SPIRAM);
                if (ptr) {
                    // Use placement new to initialize Strings properly
                    wifiScanCache = static_cast<CachedWiFiScanResult*>(ptr);
                    for (int i = 0; i < cachedScanCount; i++) {
                        new (&wifiScanCache[i]) CachedWiFiScanResult();
                    }
                }
            } else {
                wifiScanCache = new CachedWiFiScanResult[cachedScanCount];
            }
            
            if (wifiScanCache) {
                // Copy results to cache
                for (int i = 0; i < cachedScanCount; i++) {
                    wifiScanCache[i].ssid = WiFi.SSID(i);
                    wifiScanCache[i].rssi = WiFi.RSSI(i);
                    wifiScanCache[i].encryption = WiFi.encryptionType(i);
                }
                lastScanTime = currentTime;
            } else {
                // Memory allocation failed
                cachedScanCount = 0;
            }
        }
        
        WiFi.scanDelete(); // Clear ESP32 scan results (we have them cached)
        wifiScanInProgress = false;
        return cachedScanCount;
    }
    
    // No scan in progress and no valid results, start new async scan
    if (!wifiScanInProgress) {
        WiFi.scanNetworks(true, false, false, 300); // Async, show hidden, all channels, 300ms/ch
        wifiScanInProgress = true;
    }
    
    // Return 0 to indicate scan in progress
    return 0;
}

// Get cached scan result (call after performCachedWiFiScan() returns > 0)
bool getCachedScanResult(int index, String& ssid, int32_t& rssi, wifi_auth_mode_t& encryption) {
    if (!wifiScanCache || index < 0 || index >= cachedScanCount) {
        return false;
    }
    ssid = wifiScanCache[index].ssid;
    rssi = wifiScanCache[index].rssi;
    encryption = wifiScanCache[index].encryption;
    return true;
}

// void sendChipInfo(); // REMOVED: information now provided in get_all_configs()
// void sendMemoryInfo(LogLevel level); // REMOVED: information now provided in get_all_configs()

// Definition of connectToWiFi function - Smart Hybrid System
void connectToWiFi() {
    startHybridWiFiConnection();
}

// Function to set the printer emulation mode
void setPrinterEmulationMode(int mode) {
    if (mode >= 0 && mode < PRINTER_EMULATION_MODES_COUNT) {
        printerEmulationMode = mode;
        configManager.setPrinterEmulationMode(mode);
        sendVirtualMessage(String(F("Emulation mode=")) + String(mode), INFO);
    } else {
        sendVirtualMessage(String(F("Invalid emulation mode: ")) + String(mode), WARN);
    }
}

// NEW FUNCTION: Synchronize global variables with ConfigManager
void syncGlobalVariables(bool includeRS232) {
    // Synchronize WiFi settings
    ssid = configManager.getWifiSSID();
    password = configManager.getWifiPassword();
    
    // Synchronize SMTP settings
    smtpServer = configManager.getSmtpServer();
    smtpPort = String(configManager.getSmtpPort());
    smtpUser = configManager.getSmtpUsername(); // Fixed: getSmtpUsername
    smtpPass = configManager.getSmtpPassword();
    emailDest = configManager.getEmailDestination();
    autoSendEmail = configManager.getEmailAutoSend(); // Fixed: getEmailAutoSend
    
    // Synchronize RS232 (optional)
    if (includeRS232) {
#if ENABLE_RS232==1
        rs232_enabled = configManager.getRs232Enabled(); // Fixed: getRs232Enabled
        rs232_baudrate_index = configManager.getRs232BaudrateIndex(); // Fixed: use index

        // Validate baudrate index before use
        if (rs232_baudrate_index >= 0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT) {
            rs232_baudrate = RS232_BAUDRATES[rs232_baudrate_index]; // Compute baudrate from index
        } else {
            rs232_baudrate_index = 3; // Default value (9600)
            rs232_baudrate = RS232_BAUDRATES[3];
            sendVirtualMessage(F("Invalid baudrate index; using 9600"), WARN);
        }
        
        rs232_invert_pins = configManager.getRs232InvertPins(); // Fixed: getRs232InvertPins
        // New RS232 parameters
        rs232_data_bits = configManager.getRs232DataBits();
        rs232_parity = configManager.getRs232Parity();
        rs232_stop_bits = configManager.getRs232StopBits();
        rs232_read_timeout_ms = configManager.getRs232ReadTimeoutMs();
        // Mirror-to-USB setting (shows raw RS232 bytes on Serial USB when in RAW passthrough)
        rs232_mirror_to_usb = configManager.getRs232MirrorToUsb();
#else
    // RS232 disabled in build: ignore reading and force off at runtime
    (void)rs232_enabled; // avoid warnings if declared under a different guard block
#endif
    }
    
    // Synchronize email settings
    minEmailContentLength = configManager.getEmailMinContentLength(); // Fixed: getEmailMinContentLength
    // Update current max email lines from ConfigManager to reflect at runtime and in the API
    currentMaxEmailLines = configManager.getEmailMaxLines();
    autoEmailEnabledInactivityTimeout = configManager.getTimeoutAutoEmailEnabled(); // Fixed
    autoEmailDisabledInactivityTimeout = configManager.getTimeoutAutoEmailDisabled(); // Fixed
    
    // Synchronize NTP settings
    ntpServer = configManager.getNtpServer();
    timezone = configManager.getNtpTimezone(); // Fixed: getNtpTimezone
    
    // Synchronize Telnet settings (only if active)
#if ENABLE_TELNET==1
    telnetUser = configManager.getTelnetUsername(); // Fixed: getTelnetUsername
    telnetPass = configManager.getTelnetPassword(); // Fixed: getTelnetPassword
#endif
    
    // Sincronizar modos de impressora (FALTAVA) ? necess?rio para que /api/configs devolva valores atualizados
    printerEmulationMode = configManager.getPrinterEmulationMode();
    charsetMode = configManager.getPrinterCharsetMode();

    // Scheduled recipients and commands are now used directly via ConfigManager (no extra RAM duplication).

    sendVirtualMessage(F("DBG: Variables synchronized with ConfigManager"), DEBUG);
}

// Vers?o por omiss?o (compat?vel): inclui RS232
void syncGlobalVariables() { syncGlobalVariables(true); }

void prepareRs232ForAccessPointStart() {
    bool warmup = false;
    applyRS232Config();
    const unsigned long rs232PostConfigSettleMs = 500;
    const unsigned long extraWarmupMs = 300;
    unsigned long requiredSettle = rs232PostConfigSettleMs + (warmup ? extraWarmupMs : 0);
    unsigned long waitStart = millis();
#if ENABLE_RS232==1
    while (millis() - waitStart < requiredSettle) {
        if (_rs232PortInitialized) {
            while (RS232.available()) {
                (void)RS232.read();
                lastReceptionTime = millis();
            }
        }
        delay(1);
    }
#else
    while (millis() - waitStart < requiredSettle) {
        delay(1);
    }
#endif
    delay(200);
}

// --- RS232 template dirty-write optimization ---
// Keep template changes in RAM and only persist to NVS after IDLE_PERSIST_MS of no changes
// Note: rs232TemplateDirty definition moved to the top (single declaration/definition)
static unsigned long lastRs232TemplateChangeMillis = 0; // last time template was changed/applied
// Max ~2 writes/day: only flush template changes to NVS after 12h of idle.
const unsigned long RS232_TEMPLATE_PERSIST_DELAY_MS = 12UL * 60UL * 60UL * 1000UL; // 12 hours


// Function to update NTP/timezone at runtime
void atualizarNTPTimezone() {
    // Apply timezone (including DST rules) and NTP server
    configTzTime(timezone.c_str(), ntpServer.c_str());
    lastNtpSyncMillis = millis();

    // Reduce serial monitor spam:
    // - INFO only when time becomes valid (first sync) or config changes.
    // - Periodic INFO heartbeat is rate-limited.
    // - While waiting for first valid epoch, keep logs at DEBUG only.
    static bool s_prevTimeValid = false;
    static unsigned long s_lastInfoLogMs = 0;
    static char s_lastTimezone[96] = {0};
    static char s_lastNtpServer[96] = {0};

    const unsigned long nowMs = millis();
    const unsigned long infoHeartbeatMs = 15UL * 60UL * 1000UL; // 15 min max INFO cadence

    bool tzChanged = (strncmp(s_lastTimezone, timezone.c_str(), sizeof(s_lastTimezone) - 1) != 0);
    bool ntpChanged = (strncmp(s_lastNtpServer, ntpServer.c_str(), sizeof(s_lastNtpServer) - 1) != 0);

    time_t nowTs = time(nullptr);
    bool timeValid = (nowTs > 1000000000UL);

    bool becameValid = (!s_prevTimeValid && timeValid);
    bool heartbeatDue = (s_lastInfoLogMs == 0) || (nowMs - s_lastInfoLogMs >= infoHeartbeatMs);

    if (timeValid) {
        if (becameValid || tzChanged || ntpChanged || heartbeatDue) {
            safeEnviarMensagemVirtual("NTP/TZ synced: %s", INFO, timezone.c_str());
            s_lastInfoLogMs = nowMs;
        }
    } else if (tzChanged || ntpChanged) {
        // Configuration was applied, but SNTP has not produced a valid epoch yet.
        safeEnviarMensagemVirtual("NTP/TZ applied (awaiting sync): %s", DEBUG, timezone.c_str());
    }

    // Persist comparison state for next call.
    strncpy(s_lastTimezone, timezone.c_str(), sizeof(s_lastTimezone) - 1);
    s_lastTimezone[sizeof(s_lastTimezone) - 1] = '\0';
    strncpy(s_lastNtpServer, ntpServer.c_str(), sizeof(s_lastNtpServer) - 1);
    s_lastNtpServer[sizeof(s_lastNtpServer) - 1] = '\0';
    s_prevTimeValid = timeValid;
}

// Inactivity alert functions
static void updateInactivityAlertTimestamp() {
    lastDataReceivedForInactivityAlert = millis();
    inactivityAlertSent = false; // Legacy global flag
    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        inactivityAlertSentSlot[i] = false; // Reset all per-slot flags
    }
}

static void checkAndSendInactivityAlert() {
    // Quick exit: at least one slot must be enabled
    bool anyEnabled = false;
    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        if (configManager.getAlertSlotEnabled(i)) { anyEnabled = true; break; }
    }
    if (!anyEnabled) return;

    static unsigned long lastCheck = 0;
    static int lastResetDay = -1;
    unsigned long now = millis();
    if (now - lastCheck < 10000) return;
    lastCheck = now;

    // Reset ALL daily counters if day changed (shared across slots)
    time_t nowTime = time(nullptr);
    if (nowTime > 1000) {
        struct tm timeinfo;
        localtime_r(&nowTime, &timeinfo);
        int currentDay = timeinfo.tm_mday;

        if (inactivityAlertLastDay != currentDay) {
            inactivityAlertLastDay = currentDay;
            inactivityAlertsToday = 0; // Legacy global
            inactivityAlertSent = false; // Legacy per-period flag
            for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
                inactivityAlertsTodaySlot[i] = 0;
                inactivityAlertSentSlot[i] = false; // Allow new alerts after day change
            }
        }
    }

    // Wait for first data before starting timer
    if (lastDataReceivedForInactivityAlert == 0) {
        // No data received yet — initialize to current time so timer starts counting
        lastDataReceivedForInactivityAlert = millis();
    }

    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        if (!configManager.getAlertSlotEnabled(i)) continue;

        // Per-slot daily limit
        int maxPerDay = configManager.getAlertSlotMaxPerDay(i);
        if (inactivityAlertsTodaySlot[i] >= maxPerDay) continue;

        // Per-slot timeout check against global lastDataReceived timestamp
        int timeoutMinutes = configManager.getAlertSlotTimeout(i);
        unsigned long timeoutMs = (unsigned long)timeoutMinutes * 60UL * 1000UL;
        if ((now - lastDataReceivedForInactivityAlert) < timeoutMs) continue;

        // Per-slot period flag
        if (inactivityAlertSentSlot[i]) continue;

        // Get recipient emails for this slot
        String emails = configManager.getAlertSlotEmails(i);
        if (emails.length() == 0) continue;

        // Build alert email subject
        String subject = String(F("\xF0\x9F\x9A\xA8 - ")) + configManager.getSystemName() + String(F(" - ")) + L(activeLang->emailInactivitySubject);

        // Build human-readable timeout description
        String timeoutDesc;
        if (timeoutMinutes < 60) {
            timeoutDesc = String(timeoutMinutes) + " " + L(activeLang->timeMinutes);
        } else if (timeoutMinutes == 60) {
            timeoutDesc = "1 " + L(activeLang->timeHour);
        } else if (timeoutMinutes < 1440) {
            timeoutDesc = String(timeoutMinutes / 60) + " " + L(activeLang->timeHours);
        } else {
            timeoutDesc = "24 " + L(activeLang->timeHours);
        }

        String body;
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        body = buildEmailInfoTable(
            L(activeLang->emailInactivityPrefix) + timeoutDesc +
            L(activeLang->emailInactivityAlert) + String(inactivityAlertsTodaySlot[i] + 1) + String(F("/")) + String(maxPerDay) + String(F(")")),
            "", "", "",
            L(activeLang->emailMaxAlertPrefix) + String(maxPerDay) + L(activeLang->emailMaxAlertSuffix));
#endif

        #if ENABLE_SMTP==1
        extern bool startInactivityAlertAsync(const String &recipients, const String &subject, const String &body);
        bool started = startInactivityAlertAsync(emails, subject, body);
        #else
        bool started = false;
        #endif

        if (started) {
            inactivityAlertsTodaySlot[i]++;
            inactivityAlertsToday++; // Legacy global counter
            inactivityAlertSentSlot[i] = true;
            inactivityAlertSent = true; // Legacy global flag
            sendVirtualMessage(F("⚠️ [INACTIVITY] Alert sent"), WARN);
        }
    }
}
