// loop_blocks.h
// Extracted inline code blocks from loop_impl.h as named static functions.
// Included by chatgpt.ino before loop_impl.h.

#pragma once

#include "network_mdns.h"

#include "feature_flags.h"
#if ENABLE_SCHEDULER==1 && (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
#include "day_report_fallback.h"
#endif
#include "flash_fallback_cleanup.h" // LittleFS fallback cleanup (stub when disabled)
void processLine(const String& line, unsigned long currentMillis);

static void handleSensorReads() {
    // 🌡️ SENSOR CONFIG: Automatic reading and buffer injection based on configured intervals
    #if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    if (environmentalSensor.isInitialized() && sensorConfigManager.shouldAutoRead()) {
        if (sensorConfigManager.performReading()) {
            sensorConfigManager.markReadingDone();
            // injectToBuffer() is already called inside performReading() — do not call again here
        }
    }
    // Periodic persistence: SD flush at the configured MicroSD interval (default
    // 30m; page-configurable 30m/20m/10m/...), LittleFS config save every 8h
#if !(defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7))
    // TOUCH7-OFF: any LittleFS write races the always-on RGB VSYNC ISR
    // (flash-hosted GDMA restart path) -> cache panic. Persistence happens
    // on explicit user POSTs (atomic .tmp+rename: safe, may reboot). T7-22.
    sensorConfigManager.periodicPersist();
#endif

    #endif

    #if ENABLE_HX711==1
    if (hx711ConfigManager.isInitialized() && hx711ConfigManager.shouldAutoRead()) {
        hx711ConfigManager.performReading();
    }
    hx711ConfigManager.periodicPersist();
    #endif

    #if ENABLE_INA226==1
    if (ina226ConfigManager.isInitialized() && ina226ConfigManager.shouldAutoRead()) {
        ina226ConfigManager.performReading();
    }
#if !(defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7))
    // TOUCH7-OFF: any LittleFS write races the always-on RGB VSYNC ISR
    // (flash-hosted GDMA restart path) -> cache panic. Persistence happens
    // on explicit user POSTs (atomic .tmp+rename: safe, may reboot). T7-22.
    ina226ConfigManager.periodicPersist();
#endif

    #endif

    #if ENABLE_BH1750==1
    if (bh1750ConfigManager.isInitialized() && bh1750ConfigManager.shouldAutoRead()) {
        bh1750ConfigManager.performReading();
    }
#if !(defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7))
    // TOUCH7-OFF: any LittleFS write races the always-on RGB VSYNC ISR
    // (flash-hosted GDMA restart path) -> cache panic. Persistence happens
    // on explicit user POSTs (atomic .tmp+rename: safe, may reboot). T7-22.
    bh1750ConfigManager.periodicPersist();
#endif

    #endif

    #if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    touch7SystemTick(); // LVGL pump + PIR/audio/weather/peers (self-throttled)
    #endif

    #if ENABLE_SOUND_SENSOR==1
    if (soundConfigManager.isInitialized() && soundConfigManager.shouldAutoRead()) {
        soundConfigManager.performReading();
    }
    soundConfigManager.periodicPersist();
    #endif

    #if ENABLE_MAX31865==1
    if (max31865ConfigManager.isInitialized() && max31865ConfigManager.shouldAutoRead()) {
        max31865ConfigManager.performReading();
    }
    max31865ConfigManager.periodicPersist();
    #endif

    #if ENABLE_UDA1334A==1
    // Drive MP3 decode/stream output for PACK_RADIO runtime playback.
    radioAudioRuntimeLoop();
    #endif
    ensureMdns();
    healthModuleAlive(HEALTH_ID_SENSORS);
}

static void handleStorageStats() {
    unsigned long _now = millis();
    // 📊 Periodic save of storage stats to NVS (every 12h like microSD)
    static unsigned long lastStorageStatsSave = 0;
    if (_now - lastStorageStatsSave >= STORAGE_STATS_SAVE_INTERVAL_MS) {
        // Merge session counters into persistent totals before save
        _nvsWriteCountTotal += _nvsWriteCount.exchange(0);
        _nvsBytesWrittenTotal += _nvsBytesWritten.exchange(0);
        _flashWriteCountTotal += _flashWriteCount.exchange(0);
        _flashBytesWrittenTotal += _flashBytesWritten.exchange(0);
        
        if (saveStorageStatsToNVS()) {
            Serial.println("💾 Storage stats saved to NVS");
        }
        lastStorageStatsSave = _now;
    }
    healthModuleAlive(HEALTH_ID_CONFIG);
}

static void handleSdMutexWatchdog() {
    unsigned long _now = millis();
    // 🛡️ SD MUTEX WATCHDOG: Emergency deadlock recovery
    // Monitors mutex held duration and forces reset if >60s (indicates file.close() hang)
    #if ENABLE_SDCARD==1
    static unsigned long lastMutexCheck = 0;
    if (_now - lastMutexCheck >= 5000) { // Check every 5 seconds (faster monitoring)
        lastMutexCheck = _now;
        
        extern volatile const char* sdMutexHolder;
        extern volatile unsigned long sdMutexAcquiredAt;
        extern TaskHandle_t sdMutexOwnerTask;
        extern SemaphoreHandle_t sdMutex;
        
        if (sdMutexHolder && sdMutexAcquiredAt > 0 && sdMutexOwnerTask) {
            unsigned long heldDuration = _now - sdMutexAcquiredAt;
            
            // CRITICAL: If mutex held >60s (1 minute), force emergency recovery
            if (heldDuration > 60000) {
                char panicMsg[250];
                snprintf(panicMsg, sizeof(panicMsg), 
                         "🚨 SD MUTEX DEADLOCK: held by '%s' for %lums (task %p). FORCING UNLOCK!",
                         sdMutexHolder, heldDuration, sdMutexOwnerTask);
                Serial.println(panicMsg);
                sendVirtualMessage(panicMsg, ERROR);
                
                // SAFE RECOVERY: Force-unlock mutex WITHOUT deleting it
                // Problem: vSemaphoreDelete() with blocked tasks causes memory corruption
                // Solution: Force recursive Give() to unwind mutex depth + reset tracking
                
                if (sdMutex != NULL) {
                    // Force unlock up to 10 times (handles recursive locks)
                    for (int i = 0; i < 10; i++) {
                        if (xSemaphoreGiveRecursive(sdMutex) != pdTRUE) {
                            break; // Mutex fully released
                        }
                        delay(10); // Small delay between unlocks
                    }
                }
                
                // Reset tracking via public API (sdcard.cpp handles internal struct)
                extern TaskHandle_t sdMutexOwnerTask;
                extern uint32_t sdMutexDepth;
                sdMutexOwnerTask = NULL;
                sdMutexDepth = 0;
                
                // Reset legacy diagnostic variables
                sdMutexHolder = nullptr;
                sdMutexAcquiredAt = 0;
                
                sendVirtualMessage(F("✅ SD mutex force-unlocked. System may be unstable - reboot recommended."), WARN);
            }
            // WARNING: If mutex held >30s, log warning (but don't force reset yet)
            else if (heldDuration > 30000 && (heldDuration % 15000) < 5000) { // Log every 15s after 30s threshold
                char warnMsg[200];
                snprintf(warnMsg, sizeof(warnMsg), 
                         "⚠️ SD mutex held long: '%s' for %lums (task %p)",
                         sdMutexHolder, heldDuration, sdMutexOwnerTask);
                sendVirtualMessage(warnMsg, WARN);
            }
        }
    }
    #endif
    healthModuleAlive(HEALTH_ID_CONFIG);
}

static void handleHeapMonitoring() {
    unsigned long _cur = millis();
    // 🔴 CRITICAL HEAP MONITORING - detect dangerous low heap
    static unsigned long lastHeapCheck = 0;
    if (_cur - lastHeapCheck >= 10000) { // Check every 10s
        lastHeapCheck = _cur;
        size_t freeHeap = gMemoryPressureState.freeHeap ? gMemoryPressureState.freeHeap : ESP.getFreeHeap();
        size_t maxAllocHeap = gMemoryPressureState.maxAllocHeap ? gMemoryPressureState.maxAllocHeap : ESP.getMaxAllocHeap();
        MemoryPressureLevel heapLevel = memoryPressureCurrentLevel();
        if (heapLevel >= MEMORY_PRESSURE_WARN) {
            char heapWarn[128];
            snprintf(heapWarn, sizeof(heapWarn), 
                     "\u26a0\ufe0f Heap pressure=%s free=%uKB max_alloc=%uKB", 
                     memoryPressureLevelName(heapLevel),
                     (unsigned)(freeHeap / 1024),
                     (unsigned)(maxAllocHeap / 1024));
            sendVirtualMessage(heapWarn, WARN);
            
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
            // If heap is critically low, warn about any pending email test work.
            if (heapLevel == MEMORY_PRESSURE_CRITICAL && emailTestInProgress) {
                sendVirtualMessage(F("⚠️ Low heap during email test - may cause issues"), ERROR);
                // emailTestInProgress will be cleared by the task itself when it completes
                if (emailTestTaskHandle) {
                    sendVirtualMessage(F("⚠️ Email test task still running with low heap"), WARN);
                }
            }
#endif
        }
    }
    healthModuleAlive(HEALTH_ID_HEARTBEAT);
}

static void handleSdRotationAndWebCleanup() {
    unsigned long _cur = millis();

    // Periodic cleanup for web single-client inactivity (keep light and infrequent)
    static unsigned long lastWebClientCleanup = 0;
    if (_cur - lastWebClientCleanup >= 5000UL) { // every 5s
        lastWebClientCleanup = _cur;
        #if ENABLE_WIFI_WEB_SERVER==1
        webclientCleanup();
        #endif
    }

    // Light heap maintenance during inactivity periods
    idleHeapMaintenanceIfIdle(_cur);

    // Lightweight maintenance tasks (non-blocking)
#if ENABLE_ETHERNET==1
    ethernetLoop();
#endif
#if ENABLE_SDCARD==1
    sdcardLoop();
    sdcardCheckRecovery(); // Check if SD can recover from failover mode
    flushSdcardBuffers(); // Periodic flush of log buffers
    debugWriterTask();    // Periodic flush of debug writer buffer

    // Email queue processed by dedicated background task (Core 0)
    #endif
    flashAutoCleanupIfNeeded(); // LittleFS fallback cleanup (only acts without SD)
    healthModuleAlive(HEALTH_ID_CONFIG);
}

static void handleNtpResync() {
    unsigned long _cur = millis();
    // Periodic NTP re-sync (configurable via ConfigManager: ntp.sync_interval_minutes).
    // If time is still invalid (pre-epoch), retry faster every 30s so Serial Monitor
    // does not stay stuck at 00:00:xx for up to 60 minutes after Ethernet comes up.
    int ntpIntervalMin = configManager.getNtpSyncIntervalMinutes();
    if (ntpIntervalMin > 0) {
        unsigned long ntpIntervalMs = (unsigned long)ntpIntervalMin * 60UL * 1000UL;
        const unsigned long ntpFastRetryMs = 30000UL;
        static bool prevNtpNetworkReady = false;

        bool networkReady = false;
#if ENABLE_WIFI==1
        if (WiFi.status() == WL_CONNECTED) {
            networkReady = true;
        }
#endif
#if ENABLE_ETHERNET==1
        if (ethernetStatus() == 0) {
            networkReady = true;
        }
#endif

        time_t nowTs = time(nullptr);
        bool hasValidTime = (nowTs > 1000000000UL);
        unsigned long targetIntervalMs = hasValidTime ? ntpIntervalMs : ntpFastRetryMs;

        // Force an immediate NTP re-apply on network-online edge.
        // This avoids waiting for the next interval when Ethernet/WiFi comes up later.
        if (networkReady && !prevNtpNetworkReady) {
            lastNtpSyncMillis = _cur - targetIntervalMs;
        }
        prevNtpNetworkReady = networkReady;

        if (lastNtpSyncMillis == 0) {
            // Allow immediate retry on first pass when network is already ready.
            lastNtpSyncMillis = _cur - targetIntervalMs;
        }

        if (networkReady && (_cur - lastNtpSyncMillis >= targetIntervalMs)) {
            atualizarNTPTimezone(); // re-apply configTime/configTzTime
            lastNtpSyncMillis = _cur;
        }
    }
    healthModuleAlive(HEALTH_ID_NETWORK);
}

static void handleSchedulerAndReboot() {
    // ? Reset watchdog after NTP check (before scheduled command processing)
    esp_task_wdt_reset();

    // Process scheduled RS232 commands (checks every 1s internally)
    // Lazy migration from legacy array to slots (while legacy exists)
    static bool didSlotMigration = false; // Ensures single execution
    if(!didSlotMigration){ // First pass
    // Removed migration call scheduled_commands -> slots (deprecated function)
        didSlotMigration = true; // Marked
    }
    // Slot-based executors
    // Reset watchdog before scheduled commands (can have many scheduled items)
    esp_task_wdt_reset();
    
    processSchedCmdSlots(); // Interval slots
    processSchedDtSlots();  // Date/time slots
    
    // Reset watchdog after scheduled commands (can be many and slow)
    esp_task_wdt_reset();
    
    processDailyRebootSlots();

#if ENABLE_SCHEDULER==1 && (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
    // Day-report fallback: retry EMAILDAY/LOGDAY/SENSORDAY/SHIFTDAY ~20min later
    // if the scheduled send failed or a reboot/panic skipped it. Throttled to
    // ~15s; per-entry spacing (20min) and idempotency are enforced in the journal.
    {
        static unsigned long _lastDayRepCheck = 0;
        unsigned long _nowMs = millis();
        if (_nowMs - _lastDayRepCheck >= 15000UL) {
            _lastDayRepCheck = _nowMs;
            String _retryCmd;
            int _retrySlot = -1;
            if (dayReportNextRetry(_retryCmd, _retrySlot)) {
                dayReportAutoEnter();
                processLine(_retryCmd, _nowMs);
                dayReportAutoExit();
            }
        }
    }
#endif
    
    // ? Reset watchdog after daily reboot check (can be slow with parsing)
    esp_task_wdt_reset();
    
    // Legacy still called for compatibility while old UI exists
    // Legacy calls processScheduledCommands / processScheduledDateTimeCommands removed (slot-based executors active)
    healthModuleAlive(HEALTH_ID_SCHEDULER);
}

static void handleNanoBridgeAndSniffer() {
    // Reset watchdog before UART drain (can be slow with heavy traffic)
    esp_task_wdt_reset();
    
    nanoBridgePoll();

    // Sniffer (if enabled by compile-time flag)
    #if ENABLE_SNIFFER==1
    snifferPoll();
    // Flush accumulated sniffer data to LittleFS every 30 seconds
    {
        static unsigned long _lastSniffFlush = 0;
        if (millis() - _lastSniffFlush >= 30000UL) {
            _lastSniffFlush = millis();
            snifferFlushToLittleFS();
        }
    }
    #endif
}

static void handleLcdUpdate() {
    unsigned long _cur = millis();
    // LCD OPTIMIZATION: update only when necessary
    static unsigned long lastLcdUpdate = 0;
    static unsigned long lastBufferSize = 0;
    static bool lastWifiStatus = false;
    static bool forceNextUpdate = false;
    // Use a small positive interval for fast updates. Avoid 0 or negative values for unsigned types.
    // Base interval in milliseconds for LCD updates (tune: 5..50). 10ms is a good starting point.
    const unsigned long LCD_UPDATE_INTERVAL_MS = 10UL; // 10 ms base for fast scroll
    
    // Detect significant changes that justify updating the LCD
    unsigned long currentBufferSize = lastBufferSize;
    {
        size_t sizeTmp = 0;
        if (bufferDadosTryGetSize(sizeTmp, 0)) currentBufferSize = (unsigned long)sizeTmp;
    }
    bool currentWifiStatus = (WiFi.status() == WL_CONNECTED);
    bool hasChanges = (currentBufferSize != lastBufferSize) || 
                      (currentWifiStatus != lastWifiStatus) ||
                      forceNextUpdate;
    
    // System busy? Reduce LCD updates further
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    bool systemBusy = (currentEmailSendingState != EMAIL_IDLE) || 
                     (currentBufferSize > 100) ||
                     (currentWifiConnectionState != WIFI_CONNECTED);
#else
    bool systemBusy = (currentBufferSize > 100) ||
                     (currentWifiConnectionState != WIFI_CONNECTED);
#endif
    
    unsigned long lcdInterval = systemBusy ? (LCD_UPDATE_INTERVAL_MS * 2UL) : LCD_UPDATE_INTERVAL_MS;
    
    // Atualizar LCD apenas se: (h? mudan?as importantes OU passou o intervalo adaptativo)
    if (hasChanges || (_cur - lastLcdUpdate >= lcdInterval)) {
        updateScreen();
        lastLcdUpdate = _cur;
        lastBufferSize = currentBufferSize;
        lastWifiStatus = currentWifiStatus;
        forceNextUpdate = false;
    }
}

static void handleStartupStateMachine() {
    unsigned long _cur = millis();
    // Handle delayed startup phases to prevent memory crashes
    switch (currentStartupState) {
        case STARTUP_TELNET_PENDING:
            if (_cur - lastStartupAction > STARTUP_DELAY_MS) {
#if ENABLE_TELNET==1
                if (configManager.getTelnetEnabled()) {
                    sendVirtualMessage(String(F("Post-boot: Telnet...")), INFO);
                    startTelnetServer();
                } else {
                    sendVirtualMessage(String(F("Telnet OFF (config)")), INFO);
                }
#else
                sendVirtualMessage(String(F("Telnet: disabled in build (ENABLE_TELNET=0)")), INFO);
#endif
                currentStartupState = STARTUP_WIFI_STATUS_PENDING;
                lastStartupAction = _cur;
            }
            break;
            
        case STARTUP_WIFI_STATUS_PENDING:
            if (_cur - lastStartupAction > STARTUP_DELAY_MS) {
                if (!startupMessagesShown) {
                    sendVirtualMessage(String(F("Post-boot: WiFi final")), INFO);
                    sendWifiStatusAndRssi(INFO, _cur);
                    sendVirtualMessage(String(F("=====")), INFO);
                    sendVirtualMessage(String(F("System initialized")), INFO);
                    sendVirtualMessage(String(F("10s -> WebServer")), INFO);
                    safeEnviarMensagemVirtual("Heap disponivel: %u bytes", INFO, (unsigned)ESP.getFreeHeap());
                    // Remind the user whether firmware-to-SD logging is active.
                    if (configManager.getBootLogEnabled()) {
                        sendVirtualMessage(F("BOOT-LOG: ON — firmware logs saved to SD (use BOOT-LOG OFF to disable)"), INFO);
                    } else {
                        sendVirtualMessage(F("BOOT-LOG: OFF — use BOOT-LOG ON to save firmware logs to SD"), INFO);
                    }
                    startupMessagesShown = true;
                }
                // Avan?a para o pr?ximo estado ap?s mostrar as mensagens
                currentStartupState = STARTUP_COMPLETE;
                lastStartupAction = _cur;
            }
            break;
    }
}

static void handleTelnet() {
    unsigned long _cur = millis();
    // Only handle Telnet after startup is complete (and if enabled in config)
    if (currentStartupState == STARTUP_COMPLETE) {
#if ENABLE_TELNET==1
        if (configManager.getTelnetEnabled()) {
            handleTelnetClient(_cur);
        }
#endif
    }
}

static void handleBufferEmpty() {
    // L?gica para limpar o buffer de dados se estiver vazio e notificar a UI
    {
        bool empty = false;
        if (bufferDadosTryIsEmpty(empty, 0)) {
            if (empty && !bufferClearedOnInactivity) {
                clearPrinterDataBufferAndNotifyUI();
                // ? Reset contadores RS232 quando buffer fica vazio
                rs232LineCount = 0;
            } else if (!empty) {
                bufferClearedOnInactivity = false; // Se houver dados, n?o est? "limpo por inatividade"
            }
        }
    }
}

static void handleInactivityAndHeap() {
    // Clear isReceivingData flag if no reception happens for a while
    const unsigned long RECEIVE_INACTIVITY_TO_CLEAR_MS = 1500; // ? 1.5s after last byte (aumentado para dar tempo ao debounce e evitar flicker)
    if (isReceivingData && (millis() - lastReceiveActivityMillis) > RECEIVE_INACTIVITY_TO_CLEAR_MS) {
        isReceivingData = false;
        ghostDebounceByteCount = 0; // Clear debounce counter on expiry
        
        // Heap recovery: force cleanup when reception stops to reduce fragmentation
        static unsigned long lastHeapRecoveryAttempt = 0;
        const unsigned long HEAP_RECOVERY_INTERVAL_MS = 5000; // Only try every 5s to avoid overhead
        if ((millis() - lastHeapRecoveryAttempt) > HEAP_RECOVERY_INTERVAL_MS) {
            lastHeapRecoveryAttempt = millis();
            
            size_t freeHeapBefore = ESP.getFreeHeap();
            size_t maxAllocBefore = ESP.getMaxAllocHeap();
            
            // Force PSRAM buffer cleanup - shrink capacity to release unused PSRAM
#if ENABLE_SDCARD==1
            if (sdcardActiveBuffer_RS232 != nullptr) {
                auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
                if (activeRS232) activeRS232->shrink_to_fit();
            }
            if (sdcardActiveBuffer_LPT != nullptr) {
                auto* activeLPT = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
                if (activeLPT) activeLPT->shrink_to_fit();
            }
#endif
            
            // Yield to allow FreeRTOS idle task to run (heap consolidation happens in idle)
            vTaskDelay(1); // 1 tick = ~10ms, allows idle task to compact heap
            
            size_t freeHeapAfter = ESP.getFreeHeap();
            size_t maxAllocAfter = ESP.getMaxAllocHeap();
            
            // Calculate fragmentation percentage: 100% = severely fragmented, 0% = no fragmentation
            double fragBefore = (freeHeapBefore > 0) ? (1.0 - ((double)maxAllocBefore / (double)freeHeapBefore)) * 100.0 : 0.0;
            double fragAfter = (freeHeapAfter > 0) ? (1.0 - ((double)maxAllocAfter / (double)freeHeapAfter)) * 100.0 : 0.0;
            
            if (freeHeapAfter > freeHeapBefore + 5000 || (fragAfter < fragBefore - 5.0)) { // Recovered at least 5KB OR reduced fragmentation by 5%
                sendVirtualMessage("[HEAP RECOVERY] Free: " + String(freeHeapBefore) + " -> " + String(freeHeapAfter) + 
                                      " | MaxAlloc: " + String(maxAllocBefore) + " -> " + String(maxAllocAfter) + 
                                      " | Fragmentation: " + String((int)fragBefore) + "% -> " + String((int)fragAfter) + "%", INFO);
            } else if (fragAfter > 40.0) { // Fragmentation above 40% is concerning
                sendVirtualMessage("[HEAP WARNING] Fragmentation at " + String((int)fragAfter) + "% (free=" + String(freeHeapAfter) + 
                                      ", maxAlloc=" + String(maxAllocAfter) + "). Consider reboot if critical.", WARN);
            }
        }
    }
}

static void handleFlowControl() {
    // BUSY/Ready logic
    // New architecture: ESP32 no longer drives BUSY physically on DB25.
    // Instead, we propagate BUSY to the Nano via XOFF (pause) and XON (resume).
    // The physical DB25 BUSY state is controlled by the Nano according to received commands.
    // Here we calculate the desired state and ask the Nano to pause/resume if necessary.
    size_t bufferDadosUsed = 0;
    if (!bufferDadosTryGetSize(bufferDadosUsed, 0)) {
        bufferDadosUsed = BUSY_ACTIVATION_THRESHOLD_BYTES; // Fail-safe: assume high usage.
    }

    // DB25 BUSY is driven by the large shared RAM buffer (PSRAM-backed).
    // The raw DB25 RX ring buffer should be drained quickly into bufferDados.
    bool desiredBusyState = (emulatorShouldBeBusy)
        || (bufferDadosUsed >= (size_t)BUSY_ACTIVATION_THRESHOLD_BYTES);
    bool currentBusyState = isPrinterBusy; // estado l?gico atual

    if (desiredBusyState != currentBusyState) {
        setBusyPinState(desiredBusyState);
    }

    // Heartbeat for BUSY/XOFF: re-assert physical pause periodically while BUSY is active
    if (isPrinterBusy) {
        static unsigned long _lastBusyHb = 0;
        unsigned long nowHb = millis();
        if (nowHb - _lastBusyHb >= 50UL) { // ~20 Hz
            nanoBridgeSendXoff();      // idempotent resend
            nanoBridgeForceBusy(true); // ensure physical BUSY high on the Nano
            _lastBusyHb = nowHb;
        }
    }

#if ENABLE_RS232==1
    // RS232 flow control: pause/resume by shared RAM buffer high-water mark.
    if (rs232_enabled && isFlowControlEngaged()) {
        const size_t ACTIVATE_BYTES = (size_t)BUSY_ACTIVATION_THRESHOLD_BYTES;
        const size_t DEACTIVATE_BYTES = (ACTIVATE_BYTES > 8192) ? (ACTIVATE_BYTES - 8192) : (ACTIVATE_BYTES / 2);
        size_t used = 0;
        if (bufferDadosTryGetSize(used, 0)) {
            if (!rs232PausedByHighWater && used >= ACTIVATE_BYTES) {
                sendRs232Xoff();
                rs232PausedByHighWater = true;
                sendVirtualMessage(String(F("RS232: XOFF (buffer high-water)")), WARN);
            } else if (rs232PausedByHighWater && used <= DEACTIVATE_BYTES) {
                sendRs232Xon();
                rs232PausedByHighWater = false;
                sendVirtualMessage(String(F("RS232: XON (buffer below water)")), INFO);
            }
        }
    }
#endif
}

static void handleOverflowAndLogs() {
    if (overflowCount > 0) {
    safeEnviarMensagemVirtual("Buffer de recepcao cheio! Descartados %u bytes.", WARN, (unsigned)overflowCount);
        overflowCount = 0;
    }

    // Flush deferred direct logs when the system has been idle >= 500ms
    flushDeferredDirectLogsIfIdle();

    observabilityTick(WiFi.status() == WL_CONNECTED,
                      apActive,
                      ethernetHasIp(),
                      WiFi.SSID().c_str(),
                      getCurrentIPForINO().c_str(),
                      ESP.getFreeHeap(),
                      ESP.getMaxAllocHeap(),
                      ESP.getFreePsram(),
                      sdcardIsMounted(),
                      configManager.getSdcardEnabled());
    healthModuleAlive(HEALTH_ID_HEARTBEAT);
}

static void handleRs232TemplatePersist() {
#if ENABLE_RS232==1
    // --- Deferred RS232 template persistence (max ~2x/day) ---
    // Template changes are applied immediately to RAM; NVS is only written after
    // RS232_TEMPLATE_PERSIST_DELAY_MS (12h) of no further changes to limit flash wear.
    // If RAM already matches NVS (same template applied again), no write occurs at all.
    if (rs232TemplateDirty) {
        unsigned long _now = millis();
        if ((_now - lastRs232TemplateChangeMillis) >= RS232_TEMPLATE_PERSIST_DELAY_MS) {
            // Only write if RAM state differs from the value already stored in NVS.
            String stored = configManager.getRs232CurrentTemplate();
            bool sameAsStored =
                (stored == lastAppliedRs232TemplateName) &&
                (rs232_baudrate_index == configManager.getRs232BaudrateIndex()) &&
                (rs232_data_bits      == configManager.getRs232DataBits())      &&
                (rs232_parity         == configManager.getRs232Parity())        &&
                (rs232_stop_bits      == configManager.getRs232StopBits())      &&
                (rs232_invert_pins    == configManager.getRs232InvertPins())    &&
                (rs232_read_timeout_ms== configManager.getRs232ReadTimeoutMs()) &&
                (rs232_template_use_xon_xoff == configManager.getRs232UseXonXoff());
            if (!sameAsStored && lastAppliedRs232TemplateName.length()) {
                configManager.setRs232CurrentTemplate(lastAppliedRs232TemplateName);
                configManager.setRs232BaudrateIndex(rs232_baudrate_index);
                configManager.setRs232DataBits(rs232_data_bits);
                configManager.setRs232Parity(rs232_parity);
                configManager.setRs232StopBits(rs232_stop_bits);
                configManager.setRs232InvertPins(rs232_invert_pins);
                configManager.setRs232ReadTimeoutMs(rs232_read_timeout_ms);
                configManager.setRs232UseXonXoff(rs232_template_use_xon_xoff);
                // AUTO-SAVE DISABLED: RS232 changes persist only via the RS232 page Save.
                sendVirtualMessage(F("RS232: runtime template applied (not persisted — use page Save)"), INFO);
            } else {
                sendVirtualMessage(F("RS232: template already in NVS — no write"), DEBUG);
            }
            rs232TemplateDirty = false;
        }
    }
#endif // ENABLE_RS232
}

static void handleEmailAndBootLog() {
    // ? Reset watchdog before email management (can trigger heavy processing)
    esp_task_wdt_reset();
    
    // Gerenciar o envio de emails (estado da m?quina de estados de email)
    manageEmailSending();

    // ? Reset watchdog after email management
    esp_task_wdt_reset();
    
    manageSerialOutput();
    // Drain deferred boot-log entries to the SD daily log file.
    // Written here (shallow stack) to avoid stack overflow inside sendVirtualMessageInternal.
    bootLogDrainQueue();
    healthModuleAlive(HEALTH_ID_EMAIL);
}

static void handleSampleTimer() {
    unsigned long _cur = millis();
    if (_cur - lastSampleTimer >= 60000) {
        lastSampleTimer = _cur;
    }
}

static void handleDeferredWifiConnect() {
    unsigned long _cur = millis();
    // ===== Executar job diferido de conex?o WiFi sem bloqueio =====
    if (wifiConnectJobPending) {
        switch (wifiConnectJobPhase) {
            case 0: { // Preparation
                sendVirtualMessage(String(F("JOB WiFi: ")) + wifiJobSsid, INFO);
                // If in pure AP mode (minimal config page), disconnect AP immediately
                // so the client sees the transition to STA. The HTTP response was
                // already delivered by the time this loop iteration runs.
                if (WiFi.getMode() == WIFI_AP) {
                    directLogPrintln("WiFi: AP-only mode, shutting down AP for STA switch");
                    WiFi.softAPdisconnect(true);
                    delay(50);
                }
                WiFi.mode(WIFI_AP_STA);
                wifiConnectJobPhase = 1;
                break;
            }
            case 1: { // Iniciar begin()
                sendVirtualMessage(F("JOB WiFi: begin"), INFO);
                // ? Reset watchdog before WiFi operations
                esp_task_wdt_reset();
                WiFi.disconnect(true);
                delay(50);
                WiFi.begin(wifiJobSsid.c_str(), wifiJobPass.c_str());
                wifiConnectJobStart = _cur;
                wifiConnectJobPhase = 2;
                break;
            }
            case 2: { // Aguardar at? 8s sem bloquear
                // ? Reset watchdog during WiFi wait (can take up to 8s)
                esp_task_wdt_reset();
                if (WiFi.status() == WL_CONNECTED) {
                    sendVirtualMessage(String(F("JOB WiFi: IP ")) + WiFi.localIP().toString(), INFO);
                    // Desliga AP para modo STA puro
                    WiFi.softAPdisconnect(true);
                    delay(50);
                    WiFi.mode(WIFI_STA);
                    apActive = false;
                    wifiConnectJobPhase = 3;
                } else if (_cur - wifiConnectJobStart > 20000) {
                    sendVirtualMessage(F("JOB WiFi: timeout -> AP"), WARN);
                    wifiConnectJobPhase = 3; // Finaliza mantendo AP
                }
                break;
            }
            case 3: { // Finalizar
                sendVirtualMessage(F("JOB WiFi: ok"), INFO);
                wifiConnectJobPending = false;
                break;
            }
        }
    }
}

static void handleInactivityAlert() {
    // Check for inactivity alerts (periodic check)
    checkAndSendInactivityAlert();
#if ENABLE_PROXIMITY==1
    // OEE alerts (alertas OEE tab) — same loop slot, throttled internally.
    proximityOeeAlertTick();
#endif
#if ENABLE_SMTP==1
    // EMAILDAY stuck-send fallback — releases sends wedged past the timeout
    // so one dead email can never block the system (checked every loop, cheap).
    emailDayWatchdogTick();
#endif
}

static void handleProximity() {
#if ENABLE_PROXIMITY==1
    proximityTick();
#if ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1
    proximityModbusTick();
#endif
#endif
    healthModuleAlive(HEALTH_ID_PROXIMITY);
}

static void handleGrblAndUsbip() {
#if ENABLE_GRBL == 1
    grblSenderUpdate(); // GRBL G-code streaming pump (character-counting flow control)
    healthModuleAlive(HEALTH_ID_GRBL);
#endif

#if ENABLE_USBIP_SERVER == 1
    usbipServerUpdate();
    healthModuleAlive(HEALTH_ID_USBIP);
#endif
}

static void handleFinalWatchdog() {
    // Module health check: log warning if any essential module has not reported life.
    static unsigned long lastHealthWarn = 0;
    unsigned long _now = millis();
    if (_now - lastHealthWarn >= 30000UL) {
        lastHealthWarn = _now;
        for (uint8_t i = 0; i < HEALTH_ID_COUNT; i++) {
            if (gModuleHealth.essential[i] && !healthModuleIsAlive((ModuleHealthId)i, _now)) {
                char warn[96];
                snprintf(warn, sizeof(warn), "HEALTH: module %u stale (last alive %lu ms ago)",
                         (unsigned)i,
                         (unsigned long)(_now - gModuleHealth.lastAliveMs[i]));
                sendVirtualMessage(warn, WARN);
            }
        }
    }

    // Reset watchdog only if all essential modules are healthy — prevents masking a hung module.
    if (healthModulesCheckAll(_now) || _now < 30000UL) {
        esp_task_wdt_reset();
    }
    
    // ? CRITICAL: delay(1) gives FreeRTOS scheduler time without causing priority inheritance bugs
    // yield() alone caused race conditions (xTaskPriorityDisinherit assertion)
    delay(1); // 1ms breathing room for Core 0 tasks (async_tcp, EmailDay)
}
