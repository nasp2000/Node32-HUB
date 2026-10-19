// loop_impl.h
// Included only from chatgpt.ino (single TU).
// Arduino main loop: void loop()
// DO NOT add #pragma once - this is an impl-include file, not a shared header.

// Forward declaration: defined in logger_impl.h (included after this file in chatgpt.ino).
static void bootLogDrainQueue();

#include "loop_blocks.h"
#include "email_flash_rpc.h" // emailFlashServe(): runs PSRAM-task flash ops here
#include "log.h"             // logFlushIfDue(): moved off the PSRAM email task
#if SCREEN_TYPE == 3
#include "touch7_log.h" // T7LOG heartbeat (loop alive proof on UART0)
#endif

// loop() function - executed repeatedly
void loop() {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT trial (2026-10-11): unconditional first-instruction marker.
    // Distinguishes setup-tail wedge (never prints) from loop-entry wedge.
    // Literal only (no heap), immediate flush. Revert: delete these 5 lines.
    Serial.printf("LOOP:0\n"); Serial.flush();
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT trial markers (2026-10-11): last-gasp forensics for the silent
    // TG1WDT reset. Literals only (no heap alloc). First 30 iterations print
    // every handler entry; afterwards only a throttled top proves liveness.
    // Revert: delete this block, all S3MARK uses below, and the #undef at end.
    static unsigned long s3markIter = 0;
    ++s3markIter;
    bool s3mark = (s3markIter <= 30);
    if (s3mark || (s3markIter % 200) == 0) { Serial.printf("LOOP: %lu\n", s3markIter); Serial.flush(); }
#define S3MARK(tag) do { if (s3mark) { Serial.printf("LOOP: %s\n", tag); Serial.flush(); } } while (0)
#else
#define S3MARK(tag) do {} while (0)
#endif
    // ✅ UNIFIED STORAGE: Check and switch storage mode automatically (SD ↔ PSRAM ↔ NONE)
    // Runs every loop iteration to ensure immediate failover when SD becomes available/unavailable
    S3MARK("storage");
    checkAndSwitchStorageMode();

    // Stack-safe flash RPC: PSRAM-stacked email tasks publish their LittleFS ops
    // (open/read/seek/close/probe/write/list) here and this internal-stack loop
    // task executes them. Idle cost = one mux take (HISTORIC/HomeE32 (8)).
    emailFlashServe();
#if ENABLE_FILESYSTEM==1
    // LittleFS log drain: used to run on emailTaskFunction (PSRAM stack → flash
    // op → panic); now runs here on the loop's internal stack, throttled to 1Hz.
    {
        static unsigned long lastLogFlushMs = 0;
        unsigned long nowFlushMs = millis();
        if (nowFlushMs - lastLogFlushMs >= 1000) {
            lastLogFlushMs = nowFlushMs;
            logFlushIfDue();
        }
    }
#endif
    
    // 🔄 ASYNC SCHEDULER: Process pending non-blocking tasks (ESPHome-style)
    asyncScheduler.update();

#if ENABLE_BUZZER == 1
    buzzerLoop(); // Advance active beep pattern (non-blocking)
#endif
#if ENABLE_CAMERA==1
    cameraMaybeAutoStart(); // Task 3: boot auto-start when auto_mode != 0 (no-op otherwise)
    cameraLoop(); // Phase 2: motion polling and timelapse tick (Phase 1: no-op)
    healthModuleAlive(HEALTH_ID_CAMERA);
#endif
#if ENABLE_UNIKON == 1
    unikonUpdate(); // Drive timed polling and arrival detection
#endif
#if ENABLE_BRICON == 1
    briconUpdate(); // Drive Bricon poll flags + passive RX sniffer
#endif
#if ENABLE_IPG_LASER == 1
    ipgLaserUpdate();
#endif
    
    // ⚙️ DIAGNOSTIC: Loop heartbeat with configurable interval and detailed 5-line status
    static unsigned long lastHeartbeat = 0;
    static unsigned long loopIterations = 0;
    loopIterations++;
    unsigned long nowMs = millis();
    
    S3MARK("sensors");
    handleSensorReads();

    // Get heartbeat configuration
    bool heartbeatEnabled = configManager.getHeartbeatEnabled();
    uint32_t heartbeatInterval = configManager.getHeartbeatInterval();
    
    if (heartbeatEnabled && (nowMs - lastHeartbeat >= heartbeatInterval)) {
        // CRITICAL DIAGNOSTIC: Direct Serial output (bypass logging system to prevent blocking)
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minFreeHeap = ESP.getMinFreeHeap();
        uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        
        // Buffer sizes to identify leak source
#if ENABLE_RS232==1
        size_t rs232QueueSize = rs232SendQueue.size();
#else
        size_t rs232QueueSize = 0;
#endif
        size_t deferredLogsSize = deferredDirectSerialLogs.size();
        size_t printerBufSize = printerOutputBuffer.size();
        
        // Calculate uptime
        unsigned long uptimeSeconds = nowMs / 1000;
        
        // Retry PSRAM buffer sizes (use volatile counters published by writer task)
#if ENABLE_SDCARD==1
        float rs232RetryKB = sdcardRetryBytes_RS232 / 1024.0f;
        float lptRetryKB = sdcardRetryBytes_LPT / 1024.0f;
        
        // SD active buffer stats (use atomic counters)
        float sdActiveKB = (sdcardActiveBytes_RS232.load() + sdcardActiveBytes_LPT.load()) / 1024.0f;
        float sdRs232KB = sdcardActiveBytes_RS232.load() / 1024.0f;
        float sdLptKB = sdcardActiveBytes_LPT.load() / 1024.0f;
#else
        float rs232RetryKB = 0.0f, lptRetryKB = 0.0f;
        float sdActiveKB = 0.0f, sdRs232KB = 0.0f, sdLptKB = 0.0f;
#endif
        
        // Email state (use isEmailSendingActive global flag + streaming queue)
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        size_t emailQueueSize = runtimeEmailGetStreamQueueCount(); // Streaming email queue
        unsigned long lastEmailTime = 0;
        unsigned long lastStreamEmailSentTime = runtimeEmailGetLastStreamEmailSentTime();
        if (lastStreamEmailSentTime > 0) {
            lastEmailTime = (nowMs - lastStreamEmailSentTime) / 1000;
        }
        const char* emailState = (isEmailSendingActive ? "sending" : "idle");
#else
        size_t emailQueueSize = 0;
        unsigned long lastEmailTime = 0;
        const char* emailState = "disabled";
#endif
        
        // SD flush time (use most recent of RS232/LPT)
        unsigned long sdFlushTime = 0;
#if ENABLE_SDCARD==1
        unsigned long mostRecentFlush = max(lastSdcardFlush_RS232, lastSdcardFlush_LPT);
        if (mostRecentFlush > 0) {
            sdFlushTime = (nowMs - mostRecentFlush) / 1000;
        }
#endif
        
        // Line 1: iterations, uptime, heap, PSRAM
        Serial.printf("💓 HEARTBEAT: %lu iters | uptime %lus | heap %.1f/%.1fKB | PSRAM %.1fKB\n",
                      loopIterations, uptimeSeconds, freeHeap/1024.0f, minFreeHeap/1024.0f, freePsram/1024.0f);
        
        // Line 2: Queue sizes
        Serial.printf("📋 Queues: RS232-out %u | Logs %u | Print-buf %u bytes\n",
                      rs232QueueSize, deferredLogsSize, printerBufSize);
        
        // Line 3: Retry PSRAM buffers
        Serial.printf("🔄 Retry-PSRAM: RS232 %.2fKB | LPT %.2fKB\n",
                      rs232RetryKB, lptRetryKB);
        
        // Line 4: SD active buffers and flush time
        Serial.printf("💾 SD-Active: %.2fKB (RS232:%.2f LPT:%.2f) | flush: %lus ago\n",
                      sdActiveKB, sdRs232KB, sdLptKB, sdFlushTime);
        
        // Line 5: Email stats
        Serial.printf("📧 Email: %u queued | last: %lus ago | state: %s\n",
                      emailQueueSize, lastEmailTime, emailState);
        
        // Line 6: Object pools stats (ESPHome-style memory reuse)
        size_t poolHits, poolMisses, poolSize, poolCapacity;
        jsonPool256.getStats(poolHits, poolMisses, poolSize, poolCapacity);
        float hitRate = (poolHits + poolMisses > 0) ? (100.0f * poolHits / (poolHits + poolMisses)) : 0.0f;
        Serial.printf("Pools: JSON256 %u/%u (%.1f%% hit) | Tasks %u pending\n",
                      poolSize, poolCapacity, hitRate, asyncScheduler.getPendingCount());

        // Repeat the boot banner so users who open Serial Monitor after boot
        // can immediately confirm the correct COM port and that the firmware is alive.
        // (The banner is also printed at startup via setup_impl.h.)
        Serial.println(F("--- Node32-HUB alive | open this COM port in Serial Monitor ---"));

        lastHeartbeat = nowMs;
        loopIterations = 0;
        
        // PRIORITY: Check for stuck downloads and auto-release
        priorityManager().checkDownloadTimeout();
    }
    
    handleStorageStats();
    handleSdMutexWatchdog();
    
    // ? Reset watchdog at start of each loop iteration (critical for loopTask CPU 1)
    esp_task_wdt_reset();
    
    unsigned long currentMillis = millis();
    memoryPressureUpdateSample(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), currentMillis);
    
    handleHeapMonitoring();
    
    // Email test is now immediate (no polling needed)
    
    // ===== NEW: SMART HYBRID WIFI MANAGEMENT =====
    S3MARK("wifi");
    if (ENABLE_WIFI==1) {
        manageHybridWiFiMode();
    }
    // Prioritized Ethernet/WiFi coordinator
    runEthWifiCoordinator();
    healthModuleAlive(HEALTH_ID_NETWORK);
    
    // showDataSampleInWebMonitor(); // Temporariamente comentado
    
    // Processar salvamentos pendentes do ConfigManager
    configManager.loop();

    // Check P (presence watchdog) slots: fire absence alerts after silence timeout
    checkPresenceWatchdog();

    handleSdRotationAndWebCleanup();

    S3MARK("ntp");
    handleNtpResync();

    // ? Reset watchdog after NTP check (before scheduled command processing)
    esp_task_wdt_reset();

    handleSchedulerAndReboot();

    S3MARK("nano");
    handleNanoBridgeAndSniffer();

    // ===== Processar fila de envios RS232 =====
#if ENABLE_RS232==1
    // ? Reset watchdog before RS232 queue processing (can be slow with many commands)
    esp_task_wdt_reset();
    
    if (millis() - lastRs232QueueProcess >= RS232_QUEUE_PROCESS_INTERVAL_MS) {
        lastRs232QueueProcess = millis();
        // Light diagnostic: if items are pending but processing is blocked, emit spaced log
        {
            static unsigned long _lastQueueBlockedLog = 0;
            unsigned pendingCount = 0;
            bool hasPending = rs232QueueHasPending(&pendingCount);
            if (hasPending && (rs232_paused_by_controller || rs232_blockQueueForEmail)) {
                if (enableDebugLogs && (millis() - _lastQueueBlockedLog > 5000UL)) {
                    // Avoid expensive String concatenations: format on stack and send
                    {
                        char whyBuf[128];
                        int wpos = snprintf(whyBuf, sizeof(whyBuf), "DEBUG_QUEUE: bloqueada por ");
                        if (rs232_paused_by_controller && wpos < (int)sizeof(whyBuf)) {
                            wpos += snprintf(whyBuf + wpos, sizeof(whyBuf) - wpos, "XOFF/pausa");
                        }
                        if (rs232_paused_by_controller && rs232_blockQueueForEmail && wpos < (int)sizeof(whyBuf)) {
                            wpos += snprintf(whyBuf + wpos, sizeof(whyBuf) - wpos, " + ");
                        }
                        if (rs232_blockQueueForEmail && wpos < (int)sizeof(whyBuf)) {
                            wpos += snprintf(whyBuf + wpos, sizeof(whyBuf) - wpos, "email-cut");
                        }
                        if (wpos < (int)sizeof(whyBuf)) {
                            snprintf(whyBuf + wpos, sizeof(whyBuf) - wpos, "; pendentes=%u", pendingCount);
                        }
                        sendVirtualMessage(String(whyBuf), DEBUG);
                    }
                    _lastQueueBlockedLog = millis();
                }
            }
        }
    // Cold-start gate: discard any queued TX commands until 45 s after boot (system not yet stable).
    {
        static bool _rs232PortActiveLogged = false;
        bool inColdStart = (millis() - bootTime) < RS232_COLD_START_DELAY_MS;
        if (inColdStart) {
            String _discard;
            static unsigned long _lastColdWarnMs = 0;
            // Drain and discard all commands queued during the warmup window.
            while (rs232QueueTryPop(_discard)) {
                if (millis() - _lastColdWarnMs >= 10000UL) {
                    sendVirtualMessage(F("RS232: cmd ignored (warmup <45s)"), INFO);
                    _lastColdWarnMs = millis();
                }
            }
        } else if (!_rs232PortActiveLogged) {
            _rs232PortActiveLogged = true;
            sendVirtualMessage(F("RS232: port active — system ready"), INFO);
        }
    }
    bool _rs232InColdStart = (millis() - bootTime) < RS232_COLD_START_DELAY_MS;
    // Process sends even with active reception or buffers with data; only block for XOFF (controller pause)
    // Internal safeguards remain at other points (e.g., queue full, XON/XOFF) and independent inactivity triggers
    if (!_rs232InColdStart && !rs232_paused_by_controller && !rs232_blockQueueForEmail) {
            if (!rs232_sending) {
                // Pull next command atomically and send it.
                String payload;
                if (!rs232QueueTryPop(payload)) {
                    // Nothing to send.
                } else {
                String tpl = "";
                String cmdToSend = payload;
                String cmdType = "rs232"; // Default: RS232
                bool appliedTemplate = false;

                // ? Reset watchdog before JSON/template processing (can be slow)
                esp_task_wdt_reset();

                // Detect JSON-wrapped payloads (our enqueue may push a JSON string with {"template":{...},"command":"...","type":"..."})
                if (payload.length() > 0 && payload.charAt(0) == '{') {
                    SpiRamJsonDocument doc(512);
                    DeserializationError derr = deserializeJson(doc, payload);
                            if (!derr) {
                        // extract command
                        if (doc.containsKey("command")) cmdToSend = String((const char*)doc["command"]);
                        // Extract type (rs232 or system)
                        if (doc.containsKey("type")) {
                            cmdType = String((const char*)doc["type"]);
                        }
                        // if template object embedded, apply its fields directly
                        if (doc.containsKey("template") && doc["template"].is<JsonObject>()) {
                            JsonObject to = doc["template"].as<JsonObject>();
                            if (to.containsKey("name")) lastAppliedRs232TemplateName = String((const char*)to["name"]);
                            // Capture NVS-saved state as baseline for comparison.
                            // Using NVS values means: if the port was initialised from NVS at boot
                            // and the template matches, no hardware reconfiguration is needed.
                            int cur_idx = configManager.getRs232BaudrateIndex();
                            int cur_db  = configManager.getRs232DataBits();
                            String cur_par = configManager.getRs232Parity();
                            int cur_sb  = configManager.getRs232StopBits();
                            bool cur_inv = configManager.getRs232InvertPins();
                            // Calculate desired from template fields
                            int des_idx = cur_idx; int des_db = cur_db; String des_par = cur_par; int des_sb = cur_sb; bool des_inv = cur_inv;
                            if (to.containsKey("baudrate_index")) des_idx = to["baudrate_index"].as<int>();
                            if (to.containsKey("baudrate")) {
                                int nb = to["baudrate"].as<int>();
                                for (int ii = 0; ii < RS232_BAUDRATES_COUNT; ++ii) { if (RS232_BAUDRATES[ii] == nb) { des_idx = ii; break; } }
                            }
                            if (to.containsKey("data_bits")) des_db = to["data_bits"].as<int>();
                            if (to.containsKey("parity")) des_par = String((const char*)to["parity"]);
                            if (to.containsKey("stop_bits")) des_sb = to["stop_bits"].as<int>();
                            if (to.containsKey("invert_pins")) des_inv = to["invert_pins"].as<bool>();
                            // Logical fields (no port reopen needed)
                            int des_timeout = rs232_read_timeout_ms; bool des_xonxoff = rs232_template_use_xon_xoff;
                            if (to.containsKey("read_timeout_ms")) des_timeout = to["read_timeout_ms"].as<int>();
                            if (to.containsKey("use_xon_xoff")) des_xonxoff = to["use_xon_xoff"].as<bool>();

                            // Apply hardware only when template differs from NVS-saved config.
                            bool needsHardwareApply = (des_idx != cur_idx) || (des_db != cur_db) || (des_par != cur_par) || (des_sb != cur_sb) || (des_inv != cur_inv);

                            // Log humano-legivel do template alvo
                            {
                                String parityShort = (des_par == String(F("even")) ? String(F("E")) : (des_par == String(F("odd")) ? String(F("O")) : String(F("N"))));
                                String info = String(F("RS232 template alvo: ")) + lastAppliedRs232TemplateName;
                                info += String(F(" -> idx=")) + String(des_idx) + String(F(" (")) + String(des_db) + parityShort + String(des_sb) + String(F(")"));
                                if(!needsHardwareApply) info += String(F(" - igual ao estado atual, sem reconfig."));
                                sendVirtualMessage(info, INFO);
                            }

                            // Update only what does not require reopening (always safe)
                            rs232_read_timeout_ms = des_timeout;
                            rs232_template_use_xon_xoff = des_xonxoff;

                            if (needsHardwareApply) {
                                // Apply desired values to globals and reconfigure the port.
                                rs232_baudrate_index = des_idx; rs232_data_bits = des_db; rs232_parity = des_par; rs232_stop_bits = des_sb; rs232_invert_pins = des_inv;
                                syncGlobalVariables(false); applyRS232Config();
                                { // Short RX drain after reconfiguration
                                    unsigned long _drainStart = millis(); const unsigned long _DRAIN_RX_MS = 100; 
                                    while (millis() - _drainStart < _DRAIN_RX_MS) {
                                        if (_rs232PortInitialized) {
                                            while (RS232.available()) { (void)RS232.read(); lastReceptionTime = millis(); }
                                        }
                                        delay(1);
                                    }
                                }
                                appliedTemplate = true;
                                rs232TemplateDirty = true;
                                lastRs232TemplateChangeMillis = millis();
                            } else {
                                // Template matches NVS — port already correct, no dirty flag needed.
                                appliedTemplate = false;
                            }
                            // Update current template name in RAM only (no NVS write here).
                            configManager.setRs232CurrentTemplateVolatile(lastAppliedRs232TemplateName);
                        }
                        // If payload includes at_date/at_time, remember them in local vars for post-send removal
                        String queued_at_date = "";
                        String queued_at_time = "";
                        String queued_template_name = "";
                        if (doc.containsKey("at_date")) queued_at_date = String((const char*)doc["at_date"]);
                        if (doc.containsKey("at_time")) queued_at_time = String((const char*)doc["at_time"]);
                        if (doc.containsKey("template") && doc["template"].is<JsonObject>()) {
                            JsonObject to = doc["template"].as<JsonObject>();
                            if (to.containsKey("name")) queued_template_name = String((const char*)to["name"]);
                        } else if (doc.containsKey("template") && doc["template"].is<const char*>()) {
                            queued_template_name = String((const char*)doc["template"]);
                        }
                    }
                }

                // Fallback: legacy "templateName::command" format or plain command
                if (!appliedTemplate) {
                    int sep = payload.indexOf("::");
                    if (sep > 0) {
                        tpl = payload.substring(0, sep);
                        cmdToSend = payload.substring(sep + 2);
                    }
                    if (tpl.length()) {
                        JsonArray arr = configManager.getRs232Templates();
                        for (size_t i = 0; i < arr.size(); ++i) {
                            JsonObject o = arr[i];
                            if (o.containsKey("name") && String(o["name"].as<String>()) == tpl) {
                                // Use NVS-saved values as baseline (same logic as the JSON path).
                                int cur_idx = configManager.getRs232BaudrateIndex();
                                int cur_db  = configManager.getRs232DataBits();
                                String cur_par = configManager.getRs232Parity();
                                int cur_sb  = configManager.getRs232StopBits();
                                bool cur_inv = configManager.getRs232InvertPins();
                                // Desired values start from NVS baseline.
                                int des_idx = cur_idx; int des_db = cur_db; String des_par = cur_par; int des_sb = cur_sb; bool des_inv = cur_inv;
                                // Resolve baudrate from template (numeric preferred over index).
                                if (o.containsKey("baudrate")) {
                                    int nb = o["baudrate"].as<int>();
                                    for (int ii = 0; ii < RS232_BAUDRATES_COUNT; ++ii) { if (RS232_BAUDRATES[ii] == nb) { des_idx = ii; break; } }
                                } else if (o.containsKey("baudrate_index")) {
                                    des_idx = o["baudrate_index"].as<int>();
                                }
                                if (o.containsKey("data_bits")) des_db = o["data_bits"].as<int>();
                                if (o.containsKey("parity")) des_par = String(o["parity"].as<const char*>());
                                if (o.containsKey("stop_bits")) des_sb = o["stop_bits"].as<int>();
                                if (o.containsKey("invert_pins")) des_inv = o["invert_pins"].as<bool>();
                                // Logical fields
                                int des_timeout = rs232_read_timeout_ms; bool des_xonxoff = rs232_template_use_xon_xoff;
                                if (o.containsKey("read_timeout_ms")) des_timeout = o["read_timeout_ms"].as<int>();
                                if (o.containsKey("use_xon_xoff")) des_xonxoff = o["use_xon_xoff"].as<bool>();

                                // Reconfigure only if template differs from NVS-saved settings.
                                bool needsHardwareApply = (des_idx != cur_idx) || (des_db != cur_db) || (des_par != cur_par) || (des_sb != cur_sb) || (des_inv != cur_inv);

                                // Always update logical settings.
                                rs232_read_timeout_ms = des_timeout; rs232_template_use_xon_xoff = des_xonxoff;

                                if (needsHardwareApply) {
                                    rs232_baudrate_index = des_idx; rs232_data_bits = des_db; rs232_parity = des_par; rs232_stop_bits = des_sb; rs232_invert_pins = des_inv;
                                    syncGlobalVariables(false); applyRS232Config();
                                    {
                                        unsigned long _drainStart = millis(); const unsigned long _DRAIN_RX_MS = 100;
                                        while (millis() - _drainStart < _DRAIN_RX_MS) {
                                            if (_rs232PortInitialized) {
                                                while (RS232.available()) { (void)RS232.read(); lastReceptionTime = millis(); }
                                            }
                                            delay(1);
                                        }
                                    }
                                    appliedTemplate = true;
                                    rs232TemplateDirty = true;
                                    lastRs232TemplateChangeMillis = millis();
                                } else {
                                    // Template matches NVS config — port already correct, no dirty flag.
                                    appliedTemplate = false;
                                }
                                // Track template name and update RAM only (no NVS write here).
                                lastAppliedRs232TemplateName = tpl;
                                configManager.setRs232CurrentTemplateVolatile(lastAppliedRs232TemplateName);
                                break;
                            }
                        }
                    }
                }

                // No template applied: ensure the UART matches the settings saved in ConfigManager.
                // A previous template command may have left the port at a different baud/parity.
                // This satisfies Req 3: plain commands always use the config-page-saved settings.
                if (!appliedTemplate) {
                    // A Rambox module owns the port while selected: its profile (Unikon
                    // 8N1 / Bricon 8N2) must NOT be reverted to the RS232 page settings.
                    bool ramboxModuleActive = false;
#if ENABLE_UNIKON == 1
                    { extern bool unikonIsEnabled(); ramboxModuleActive = ramboxModuleActive || unikonIsEnabled(); }
#endif
#if ENABLE_BRICON == 1
                    { extern bool briconIsEnabled(); ramboxModuleActive = ramboxModuleActive || briconIsEnabled(); }
#endif
                    bool needsRestore = !ramboxModuleActive && (
                        rs232_baudrate_index != configManager.getRs232BaudrateIndex() ||
                        rs232_data_bits      != configManager.getRs232DataBits()      ||
                        rs232_parity         != configManager.getRs232Parity()        ||
                        rs232_stop_bits      != configManager.getRs232StopBits()      ||
                        rs232_invert_pins    != configManager.getRs232InvertPins());
                    if (needsRestore) {
                        // Restore to the config-page-saved settings (no NVS write — already saved there).
                        rs232_baudrate_index = configManager.getRs232BaudrateIndex();
                        if (rs232_baudrate_index >= 0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT)
                            rs232_baudrate = RS232_BAUDRATES[rs232_baudrate_index];
                        rs232_data_bits   = configManager.getRs232DataBits();
                        rs232_parity      = configManager.getRs232Parity();
                        rs232_stop_bits   = configManager.getRs232StopBits();
                        rs232_invert_pins = configManager.getRs232InvertPins();
                        applyRS232Config();
                        rs232TemplateDirty = false; // Restored — discard any deferred dirty
                        sendVirtualMessage(F("RS232: port restored to saved config"), INFO);
                    }
                }

                rs232_sending = true;
                // Explicit sequence log (template already applied in RAM at this point)
                if (lastAppliedRs232TemplateName.length()) {
                    char dbg[96];
                    // Ex.: "tpl:Name :: command"
                    snprintf(dbg, sizeof(dbg), "\xF0\x9F\xA7\xA0 tpl:%s :: %s", lastAppliedRs232TemplateName.c_str(), cmdToSend.c_str());
                    sendVirtualMessage(String(dbg), DEBUG);
                } else {
                    char dbg[96];
                    // Ex.: "no tpl :: command"
                    snprintf(dbg, sizeof(dbg), "\xF0\x9F\xA7\xA0 no tpl :: %s", cmdToSend.c_str());
                    sendVirtualMessage(String(dbg), DEBUG);
                }
                // If XON/XOFF active, request remote silence before stabilizing/applying
                bool _askedRemotePause = false;
                if (configManager.getRs232UseXonXoff() || rs232_template_use_xon_xoff) {
                    sendRs232XoffRaw();
                    _askedRemotePause = true;
                }

                // Wait after applying template to ensure physical stabilization before actual send
                // Warm-up after prolonged inactivity: inactivity > 60s => strengthen waits
                unsigned long _nowWU = millis();
                unsigned long _lastActWU = 0;
                _lastActWU = max(_lastActWU, lastReceptionTime);
                _lastActWU = max(_lastActWU, rs232LastTxMillis);
                bool _warmup = (_nowWU - _lastActWU) >= 60000UL; // 60s

                // Before sending: require a short continuous silence period on RX to reduce risk of cutting the start of a response
                {
                    const unsigned long PRE_TX_SILENCE_MS = _warmup ? 100 : 40;    // required silence period (reduced for responsiveness)
                    const unsigned long PRE_TX_MAX_WAIT_MS = 300;   // maximum budget to wait for silence (reduced)
                    unsigned long budgetStart = millis();
                    unsigned long lastByte = millis();
                    // Drain and measure silence
                    while (millis() - budgetStart < PRE_TX_MAX_WAIT_MS) {
                        bool got = false;
                        if (_rs232PortInitialized) {
                            while (RS232.available()) { (void)RS232.read(); lastReceptionTime = millis(); got = true; }
                        }
                        if (got) { lastByte = millis(); }
                        if (millis() - lastByte >= PRE_TX_SILENCE_MS) { break; }
                        delay(2);
                    }
                }
                
                // PROCESS BY TYPE
                if (cmdType == "system" || cmdType == "Sistema") {
                    // SYSTEM command (internal) - execute via executeCommand
                    sendVirtualMessage(String(F("System: ")) + cmdToSend, INFO);
                    executeCommand(cmdToSend); // Execute internal command
                    // Show on OLED
                    triggerScreenCommandSent(cmdToSend, "Sistema");
                } else {
                    // RS232 command (serial port) - send via enviarComandoRS232
                bool ok = enviarComandoRS232(cmdToSend);
                rs232_sending = false;

                    // Minimum wait after RS232 port initialization to ensure physical stability
                    // CRITICAL: After reboot, applyRS232Config() initializes Serial2 but it needs settle time
                    // If we send too soon, the first command may fail (port not fully ready)
                    const unsigned long MIN_CONFIG_SETTLE_MS = 200; // Minimum 200ms after config/init
                    unsigned long timeSinceConfig = millis() - lastRs232ConfigMillis;
                    if (timeSinceConfig < MIN_CONFIG_SETTLE_MS) {
                        unsigned long waitMs = MIN_CONFIG_SETTLE_MS - timeSinceConfig;
                        sendVirtualMessage(String(F("RS232 settle wait: ")) + String(waitMs) + F("ms"), DEBUG);
                        delay(waitMs);
                    }
                    
                    // Release the remote after sending
                    if (_askedRemotePause) {
                        // short wait to leave TX and guarantee return to IDLE
                        delay(5);
                        sendRs232XonRaw();
                    }

                // If no template was provided, nothing to persist; otherwise the template was applied and persisted above.

                if (!ok) {
                    char err[96];
                    snprintf(err, sizeof(err), "\xE2\x9D\x8C RS232 failure: %s", cmdToSend.c_str());
                    sendVirtualMessage(String(err), ERROR);
                } else {
                    // Reset the count of lines since last TX so that the 66-line XOFF
                    // only applies after receiving a full page of this response
                    char info[128];
                    char par = (rs232_parity == String(F("even"))) ? 'E' : ((rs232_parity == String(F("odd"))) ? 'O' : 'N');
                    if (lastAppliedRs232TemplateName.length()) {
                        // Ex.: "CMD OK (tpl:Nome -> 9600 baud, 8E1) [inv]"
                        if (rs232_invert_pins) snprintf(info, sizeof(info), "\xE2\x9C\x85 CMD OK (tpl:%s -> %d baud, %d%c%d) [inv]", lastAppliedRs232TemplateName.c_str(), rs232_baudrate, rs232_data_bits, par, rs232_stop_bits);
                        else snprintf(info, sizeof(info), "\xE2\x9C\x85 CMD OK (tpl:%s -> %d baud, %d%c%d)", lastAppliedRs232TemplateName.c_str(), rs232_baudrate, rs232_data_bits, par, rs232_stop_bits);
                    } else {
                        // Ex.: "CMD OK (cur:9600 baud, 8N1)"
                        snprintf(info, sizeof(info), "\xE2\x9C\x85 CMD OK (cur:%d baud, %d%c%d)", rs232_baudrate, rs232_data_bits, par, rs232_stop_bits);
                    }
                    // Rate limit success logs (1x/3min)
                    static unsigned long lastCmdOkLogMs = 0;
                    unsigned long nowOk = millis();
                    if (nowOk - lastCmdOkLogMs > 180000) { // 3 minutes
                        sendVirtualMessage(String(info), COMMAND_RESPONSE);
                        lastCmdOkLogMs = nowOk;
                    }
                    // Always trigger the temporary "RS232 Command Sent" screen on successful send
                    // Extract type of scheduled payload (if available) - assume RS232 by default
                    String cmdType = "RS232"; // Default
                    if (!payload.isEmpty()) {
                        SpiRamJsonDocument checkDoc(512);
                        if (deserializeJson(checkDoc, payload) == DeserializationError::Ok && checkDoc.containsKey("type")) {
                            String typeRaw = String((const char*)checkDoc["type"]);
                            if (typeRaw == "system" || typeRaw == "Sistema") cmdType = "Sistema";
                            else cmdType = "RS232";
                        }
                    }
                    triggerScreenCommandSent(cmdToSend, cmdType);
                    // If this queued payload carried an at_date/at_time, remove the persisted scheduled command(s)
                    // to ensure /commands-by-datetime no longer shows already-executed entries.
                    // We match by command text, date, time and template name when available.
                    // Deserialize payload again to extract any at_date/at_time if present (safe inexpensive op)
                    // Removed legacy cleanup logic for scheduled_commands (id / at_date / at_time)
                    // If needed in the future: implement removal of consumed sched_dt slots in another module
                }
                } // End of else block (RS232)
                } // payload available
            }
        } else {
            // While receiving, defer processing — optional: occasional log
        }
    }
#endif // ENABLE_RS232
    
    handleLcdUpdate();

#if SCREEN_TYPE == 3
    // TouchMeteo-7 loop heartbeat on UART0 (ets_printf survives CDC routing).
    {
        static unsigned long _t7hb = 0;
        unsigned long _now = millis();
        if (_now - _t7hb >= 15000) {
            _t7hb = _now;
            T7LOG("hb up=%lus heap=%u psram=%u", _now / 1000UL,
                  (unsigned)ESP.getFreeHeap() / 1024,
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024);
        }
    }
#endif

    S3MARK("startup-sm");
    handleStartupStateMachine();
    S3MARK("telnet");
    handleTelnet();

    // RS232: Read received data and send to bufferDados
#if ENABLE_RS232==1
    // ? Reset watchdog before RS232 data processing (can have many bytes in buffer)
    esp_task_wdt_reset();

    // Apply deferred RS232 config (set by web handlers to avoid race with UART driver).
    // Guard: do not apply while Unikon is actively using the UART — wait for next loop.
    if (_pendingRS232Apply) {
#if ENABLE_UNIKON == 1
        if (!unikonIsPolling()) {
            _pendingRS232Apply = false;
            applyRS232Config();
        }
#else
        _pendingRS232Apply = false;
        applyRS232Config();
#endif
    }

    // One-shot startup diagnostic: log RS232 gate state so we can verify the port was initialized.
    {
        static bool _rs232DiagDone = false;
        if (!_rs232DiagDone) {
            _rs232DiagDone = true;
            char dbg[80];
            snprintf(dbg, sizeof(dbg), "[RS232 DIAG] enabled=%d portInit=%d baudrate=%d rx=%d tx=%d",
                     (int)rs232_enabled, (int)_rs232PortInitialized, rs232_baudrate, rs232_rx_pin, rs232_tx_pin);
            sendVirtualMessage(String(dbg), INFO);
        }
    }

    if (rs232_enabled && _rs232PortInitialized) {
        static char rs232CommandInput[256];
        static size_t rs232CommandInput_len = 0;
        // Precompute ISQ20 active flag once for the RS232 handling scope ? evita problemas de escopo
        bool isq20Active = (configManager.getStructuresAutoDetectEnabled() || configManager.getAmadaISQ20MTF1Enabled() || configManager.getAmadaISQ20AWSEEnabled());
        int rs232ChunkLimit = 256; // Process up to 256 bytes per iteration to balance throughput vs responsiveness
#if ENABLE_UNIKON == 1
        // While Unikon is actively polling the RAMBOX, do not consume RS232 bytes —
        // the bytes belong to the RAMBOX protocol exchange, not the RS232 data stream.
        // Also block for a short window after the poll ends so late RAMBOX tail bytes
        // do not leak into bufferDados (flushRx has a fixed 100 ms window; RAMBOX can
        // send bytes slightly after that).
        bool _unikonBusy = unikonIsPolling() || unikonIsPostPollBlackout();
#else
        bool _unikonBusy = false;
#endif
        // [RX DIAG] Log once when the first byte arrives after the last TX — confirms UART delivery.
        // Also fires every loop after TX even with 0 bytes (to confirm we ARE reaching this code).
        static unsigned long _rxDiagLastTx = 0;
        static bool _rxDiagLogged = false;
        if (rs232LastTxMillis != _rxDiagLastTx) {
            _rxDiagLastTx = rs232LastTxMillis;
            _rxDiagLogged = false;
        }
        if (!_rxDiagLogged && rs232LastTxMillis > 0 && (millis() - rs232LastTxMillis) < 2000UL) {
            int _avail = RS232.available();
            if (_avail > 0 || (millis() - rs232LastTxMillis) > 100UL) {
                _rxDiagLogged = true;
                char _rxd[160];
                snprintf(_rxd, sizeof(_rxd), "[RX DIAG] avail=%d portInit=%d enabled=%d sinceLastTx=%lums pushFail=%lu lockFail=%lu seq=%lu",
                         _avail, (int)_rs232PortInitialized, (int)rs232_enabled,
                         (unsigned long)(millis() - rs232LastTxMillis),
                         (unsigned long)bufferDadosPushMutexFailCount,
                         (unsigned long)bufferDadosMutexFailCount,
                         (unsigned long)bufferDadosSeq);
                Serial.println(_rxd); // Direct to USB — bypasses logger buffer
                sendVirtualMessage(String(_rxd), WARN);
            }
        }

        while (!_unikonBusy && RS232.available() && rs232ChunkLimit-- > 0) {
            char rs232_in = RS232.read(); // Raw byte received from RS232 UART
            lastByteFromDb25 = false; // Source: RS232
            lastDataSourceForEmail = "RS232"; // Tracks source for SD Card logging
            rs232InactivityGraceUntil = 0; // Cancel TX grace window on any received byte

            // If the temporary protection expired or we already received more than the minimum lines, disable the guard
            if (rs232FirstResponseGuardActive) {
                if (millis() >= rs232FirstResponseGuardUntil || rs232LinesSinceLastTx > RS232_FIRST_RESPONSE_MIN_LINES) {
                    rs232FirstResponseGuardActive = false; // Guard completed
                }
            }

            // === Tratamento de controlo XON/XOFF recebido (ATUALIZADO) ===
            // Mandatory rules: when receiving XOFF/XON from the remote, we ALWAYS pause/resume our TX, regardless of config/template.
            // Auto-detection engages flow control to allow sending XOFF when needed.
            
            // Mirror reception to USB Monitor (for debug/visibility) if active
            // Placed BEFORE any processing or mode filtering
            if (rs232_mirror_to_usb) {
                // If printable byte, newline or tab, print as char. Hide binary bytes to clean the monitor.
                static bool rs232MonitorFirstByte = true;
                if (isprint(rs232_in) || rs232_in == '\n' || rs232_in == '\r' || rs232_in == '\t') {
                    if (rs232MonitorFirstByte) {
                        Serial.print("[RS232] "); // Prefix to identify source
                        rs232MonitorFirstByte = false;
                    }
                    Serial.write((uint8_t)rs232_in);
                    if (rs232_in == '\n') rs232MonitorFirstByte = true; // Reset at end of line
                }
            }

            if ((uint8_t)rs232_in == 0x13) { // Remote XOFF
                rs232RemoteSendsXonXoff = true;           // Auto-flow: saw remote XOFF
                rs232LastRemoteXoffMs = millis();         // Telemetry
                rs232RemoteXoffCount = rs232RemoteXoffCount + 1;                   // Counter
                if (!rs232_paused_by_controller) {        // Pause our local TX queue
                    rs232_paused_by_controller = true;    // Block send processing
                    // sendVirtualMessage(F("Remote XOFF: TX paused"), INFO); // Hidden on request
                }
                continue; // Do not insert control byte into bufferDados/commands
            } else if ((uint8_t)rs232_in == 0x11) { // Remote XON
                rs232RemoteSendsXonXoff = true;           // Auto-flow: saw remote XON
                rs232LastRemoteXonMs = millis();          // Telemetry
                rs232RemoteXonCount = rs232RemoteXonCount + 1;                    // Counter
                if (rs232_paused_by_controller) {         // Resume our local TX queue
                    rs232_paused_by_controller = false;   // Unblock processing
                    // sendVirtualMessage(F("Remote XON: TX resumed"), INFO); // Hidden on request
                }
                continue; // Do not store control byte
            }
#if ENABLE_IPG_LASER == 1
            ipgLaserOnRxByte(rs232_in);
#endif
                if (rs232_passthrough_raw) {
                // RAW: do not interpret as commands, only forward into the shared receive pipeline.
                // Keep the RAM buffer always filled for /monitor page and API access.
                // CHANGED: RAM buffer now always active regardless of SD/monitor state (fix: buffer not showing RS232 data)
                bool canFillRamBuffer = true;

                if (canFillRamBuffer) {
                    // Accumulate into line buffer for timestamp capture at line completion
                    if (ramLineAccumulator_RS232_len == 0) {
                        ramLineStartTimestamp_RS232 = millis(); // Capture timestamp at first byte
                    }
                    if (ramLineAccumulator_RS232_len < SDCARD_LINE_ACCUMULATOR_SIZE - 1) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len++] = (char)rs232_in;
                    }
                }
                
                    lastReceptionTime = millis();
                    lastReceiveActivityMillis = millis();
                    lastDataReceivedForInactivityAlert = millis(); // Update for inactivity alert (NEW data from RS232)
                    inactivityAlertSent = false; // Reset flag to allow new alerts after activity resumes
                    for (int _i = 0; _i < ConfigManager::ALERT_SLOT_COUNT; ++_i) inactivityAlertSentSlot[_i] = false;

                    // DIAGNOSTIC: Log inactivity timestamp update (rate-limited to avoid spam)
                    static unsigned long lastInactivityUpdateLog = 0;
                    if (millis() - lastInactivityUpdateLog > 15000) { // Log every 15s for better visibility
                        char logBuf[100];
                        snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by RS232 data (ts=%lu)", lastDataReceivedForInactivityAlert);
                        sendVirtualMessage(String(logBuf), INFO);
                        lastInactivityUpdateLog = millis();
                    }
                    
                    // Debounce: only mark isReceivingData if there is continuous flow (not isolated noise bytes)
                    unsigned long now = millis();
                    if (now - ghostDebounceStartMs > GHOST_DEBOUNCE_WINDOW_MS) {
                        // New window: reset counter
                        ghostDebounceStartMs = now;
                        ghostDebounceByteCount = 1;
                    } else {
                        // Same window: increment counter
                        ghostDebounceByteCount = ghostDebounceByteCount + 1;
                    }
                    
                    // Only mark as "receiving" if we have enough bytes (real stream, not noise)
                    if (ghostDebounceByteCount >= GHOST_DEBOUNCE_MIN_BYTES) {
                        isReceivingData = true;
                    }
                // IMPROVEMENT 3: Accumulate data by line in SD buffer with individual timestamps
                if (configManager.getSdcardEnabled()) {
                    // Accumulate byte into temporary line buffer
                    if (sdcardLineAccumulator_RS232_len == 0) {
                        sdcardLineStartTimestamp_RS232 = millis();
                    }
                    if (sdcardLineAccumulator_RS232_len < SDCARD_LINE_ACCUMULATOR_SIZE - 1) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len++] = (char)rs232_in;
                    }
                    lastSdcardDataReceived_RS232 = millis();
                }
                // (USB mirror code moved to loop start to cover all modes)
                // if (rs232_mirror_to_usb) { Serial.write((uint8_t)rs232_in); }

                // RS232 line counting for RAM email paging (66/90). No micro-pause; no XOFF on line limits.
                if (rs232_in == '\n') {
                    if (rs232SkipNextLF) {
                        // Line already counted on the previous CR (CRLF) - ignore this LF
                        rs232SkipNextLF = false;
                        rs232LastWasCR = false;
                        // Clear stale \n that was already added to the accumulators before this check
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                        // Do not recount lines nor re-evaluate thresholds
                        continue; // advance to the next byte
                    }
                    
                    // Flush accumulated RAM line to bufferDados with timestamp
                    if (canFillRamBuffer && ramLineAccumulator_RS232_len > 0) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len] = '\0';
                        snifferInjectLine(ramLineAccumulator_RS232, ramLineAccumulator_RS232_len);
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        bufferDadosSeq = bufferDadosSeq + 1; // Track sequence
                    }
                    
                    rs232LineCount++;
                    rs232LinesSinceLastTx++;
                    rs232LastWasCR = false; // CRLF: LF arrived, do not count the previous CR
                    bufferDadosLineCount++;
                    if (!configManager.getSdcardEnabled() && bufferDadosLineCount >= currentMaxEmailLines) {
                        autoEmailTriggeredByNewline = true;
                        lastTriggerFromDb25 = false;
                    }
                    
                    // SD: finalize line and enqueue one timestamped chunk (optimized: no temp String allocation).
                    if (configManager.getSdcardEnabled() && sdcardLineAccumulator_RS232_len > 0) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len] = '\0';
                        unsigned long lineTimestamp = (sdcardLineStartTimestamp_RS232 > 0) ? sdcardLineStartTimestamp_RS232 : millis();
                        addDataToSdcardBufferTimestampedRaw(true, sdcardLineAccumulator_RS232, sdcardLineAccumulator_RS232_len, lineTimestamp);
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                    }
                } else if (rs232_in == '\r') {
                    // Count CR as line when emitter is CR-only; if LF follows, we ignore that LF
                    
                    // FIX: Flush accumulated RAM line to bufferDados BEFORE counting (prevents loss when \r\n arrives)
                    if (canFillRamBuffer && ramLineAccumulator_RS232_len > 0) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len] = '\0';
                        snifferInjectLine(ramLineAccumulator_RS232, ramLineAccumulator_RS232_len);
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        bufferDadosSeq = bufferDadosSeq + 1; // Track sequence
                    }
                    
                    rs232LineCount++;
                    rs232LinesSinceLastTx++;
                    rs232SkipNextLF = true;  // o LF imediatamente a seguir n?o reconta
                    rs232LastWasCR = true;
                    bufferDadosLineCount++;
                    if (!configManager.getSdcardEnabled() && bufferDadosLineCount >= currentMaxEmailLines) {
                        autoEmailTriggeredByNewline = true;
                        lastTriggerFromDb25 = false;
                    }
                    
                    // SD: finalize line and enqueue one timestamped chunk (optimized: no temp String allocation).
                    if (configManager.getSdcardEnabled() && sdcardLineAccumulator_RS232_len > 0) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len] = '\0';
                        unsigned long lineTimestamp = (sdcardLineStartTimestamp_RS232 > 0) ? sdcardLineStartTimestamp_RS232 : millis();
                        addDataToSdcardBufferTimestampedRaw(true, sdcardLineAccumulator_RS232, sdcardLineAccumulator_RS232_len, lineTimestamp);
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                    }
                } else {
                    rs232LastWasCR = false;
                }
            } else {
                // Modo comando: acumula linha e processa
                // Always accumulate into RAM line buffer for monitor and API access
                // CHANGED: RAM buffer now always active regardless of SD/monitor state (fix: buffer not showing RS232 data)
                bool canFillRamBuffer = true;
                if (canFillRamBuffer) {
                    if (ramLineAccumulator_RS232_len == 0) {
                        ramLineStartTimestamp_RS232 = millis(); // Capture timestamp at first byte
                    }
                    if (ramLineAccumulator_RS232_len < SDCARD_LINE_ACCUMULATOR_SIZE - 1) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len++] = (char)rs232_in;
                    }
                }
                
                if (rs232_in == '\n' || rs232_in == '\r') {
                    // Trim whitespace from end
                    while (rs232CommandInput_len > 0 && 
                           (rs232CommandInput[rs232CommandInput_len-1] == ' ' || 
                            rs232CommandInput[rs232CommandInput_len-1] == '\t')) {
                        rs232CommandInput_len--;
                    }
                    if (rs232CommandInput_len > 0) {
                        rs232CommandInput[rs232CommandInput_len] = '\0';
                        processLine(String(rs232CommandInput), currentMillis);
                        // Sampling removed: do not store/push to the web monitor
                    }
                    rs232CommandInput_len = 0;
                } else {
                    if (rs232CommandInput_len < sizeof(rs232CommandInput) - 1) {
                        rs232CommandInput[rs232CommandInput_len++] = rs232_in;
                    }
                }
                // Note: bufferDados push now handled via line accumulator above
                lastReceptionTime = millis();
                lastReceiveActivityMillis = millis();
                lastDataReceivedForInactivityAlert = millis(); // Update for inactivity alert (NEW data from RS232)
                inactivityAlertSent = false; // Reset flag to allow new alerts after activity resumes
                for (int _i = 0; _i < ConfigManager::ALERT_SLOT_COUNT; ++_i) inactivityAlertSentSlot[_i] = false;

                // DIAGNOSTIC: Log inactivity timestamp update (rate-limited to avoid spam)
                static unsigned long lastInactivityUpdateLog2 = 0;
                if (millis() - lastInactivityUpdateLog2 > 15000) { // Log every 15s for better visibility
                    char logBuf[100];
                    snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by RS232 data (cmd mode) (ts=%lu)", lastDataReceivedForInactivityAlert);
                    sendVirtualMessage(String(logBuf), INFO);
                    lastInactivityUpdateLog2 = millis();
                }
                
                // Debounce: only mark isReceivingData if there is continuous flow (not isolated noise bytes)
                unsigned long now = millis();
                if (now - ghostDebounceStartMs > GHOST_DEBOUNCE_WINDOW_MS) {
                    // ? Nova janela: resetar contador
                    ghostDebounceStartMs = now;
                    ghostDebounceByteCount = 1;
                } else {
                        // Same window: increment counter
                }
                
                // Only mark as "receiving" if we have enough bytes (real stream, not noise)
                if (ghostDebounceByteCount >= GHOST_DEBOUNCE_MIN_BYTES) {
                    isReceivingData = true;
                }
                // Accumulate SD data
                if (configManager.getSdcardEnabled()) {
                    if (sdcardLineAccumulator_RS232_len == 0) {
                        sdcardLineStartTimestamp_RS232 = millis();
                    }
                    if (sdcardLineAccumulator_RS232_len < SDCARD_LINE_ACCUMULATOR_SIZE - 1) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len++] = (char)rs232_in;
                    }
                    lastSdcardDataReceived_RS232 = millis();
                }
                // Count lines also in command mode (incoming RS232 may still be printable data).
                if (rs232_in == '\n') {
                    if (rs232SkipNextLF) {
                        // Already counted on the previous CR - ignore LF
                        rs232SkipNextLF = false;
                        rs232LastWasCR = false;
                        // Clear stale \n that was already added to the accumulators before this check
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                        continue; // continue to the next byte
                    }
                    
                    // Flush accumulated RAM line to bufferDados with timestamp
                    if (canFillRamBuffer && ramLineAccumulator_RS232_len > 0) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len] = '\0';
                        snifferInjectLine(ramLineAccumulator_RS232, ramLineAccumulator_RS232_len);
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        bufferDadosSeq = bufferDadosSeq + 1;
                    }
                    
                    rs232LineCount++;
                    rs232LinesSinceLastTx++;
                    rs232LastWasCR = false;
                    bufferDadosLineCount++;
                    if (!configManager.getSdcardEnabled() && bufferDadosLineCount >= currentMaxEmailLines) {
                        autoEmailTriggeredByNewline = true;
                        lastTriggerFromDb25 = false;
                    }
                    
                    // SD: finalize line and enqueue one timestamped chunk (optimized: no temp String allocation).
                    if (configManager.getSdcardEnabled() && sdcardLineAccumulator_RS232_len > 0) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len] = '\0';
                        unsigned long lineTimestamp = (sdcardLineStartTimestamp_RS232 > 0) ? sdcardLineStartTimestamp_RS232 : millis();
                        addDataToSdcardBufferTimestampedRaw(true, sdcardLineAccumulator_RS232, sdcardLineAccumulator_RS232_len, lineTimestamp);
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                    }
                } else if (rs232_in == '\r') {
                    // Count CR as line (CR-only) and ignore following LF
                    
                    // Flush accumulated RAM line to bufferDados with timestamp
                    if (canFillRamBuffer && ramLineAccumulator_RS232_len > 0) {
                        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len] = '\0';
                        snifferInjectLine(ramLineAccumulator_RS232, ramLineAccumulator_RS232_len);
                        ramLineAccumulator_RS232_len = 0;
                        ramLineStartTimestamp_RS232 = 0;
                        bufferDadosSeq = bufferDadosSeq + 1;
                    }
                    
                    rs232LineCount++;
                    rs232LinesSinceLastTx++;
                    rs232SkipNextLF = true;
                    rs232LastWasCR = true;
                    bufferDadosLineCount++;
                    if (!configManager.getSdcardEnabled() && bufferDadosLineCount >= currentMaxEmailLines) {
                        autoEmailTriggeredByNewline = true;
                        lastTriggerFromDb25 = false;
                    }
                    
                    // SD: finalize line and enqueue one timestamped chunk (optimized: no temp String allocation).
                    if (configManager.getSdcardEnabled() && sdcardLineAccumulator_RS232_len > 0) {
                        sdcardLineAccumulator_RS232[sdcardLineAccumulator_RS232_len] = '\0';
                        unsigned long lineTimestamp = (sdcardLineStartTimestamp_RS232 > 0) ? sdcardLineStartTimestamp_RS232 : millis();
                        addDataToSdcardBufferTimestampedRaw(true, sdcardLineAccumulator_RS232, sdcardLineAccumulator_RS232_len, lineTimestamp);
                        sdcardLineAccumulator_RS232_len = 0;
                        sdcardLineStartTimestamp_RS232 = 0;
                    }
                } else {
                    rs232LastWasCR = false;
                }
            }
        }
    }

    // Idle flush: if bytes were accumulated but no \r/\n arrived, flush after 500ms of inactivity.
    // This ensures data from binary/raw streams (no newlines) still appears in the monitor.
    if (ramLineAccumulator_RS232_len > 0 && ramLineStartTimestamp_RS232 > 0 &&
        (millis() - ramLineStartTimestamp_RS232) > 500UL) {
        ramLineAccumulator_RS232[ramLineAccumulator_RS232_len] = '\0';
        snifferInjectLine(ramLineAccumulator_RS232, ramLineAccumulator_RS232_len);
        ramLineAccumulator_RS232_len = 0;
        ramLineStartTimestamp_RS232 = 0;
        bufferDadosSeq = bufferDadosSeq + 1;
    }
    healthModuleAlive(HEALTH_ID_RS232);
#endif // ENABLE_RS232

    handleBufferEmpty();

    handleInactivityAndHeap();

#if ENABLE_DB25==1 || ENABLE_RS232==1
    handleFlowControl();   // BUSY/XOFF flow control — only meaningful with DB25/RS232
#endif

    handleOverflowAndLogs();

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

    handleRs232TemplatePersist();

    // IMPROVEMENT 1+3: Batch transfer with lock-free (volatile indices)
    // Transfer 512 bytes at a time instead of 1 byte — 90% less overhead
    // ? Usa snapshot de writePos (lock-free) ? zero mutex contention
    if (!pauseInternalBufferTransfer) {
        static unsigned long lastBufWdtReset = 0;
        static uint8_t batchBuffer[BATCH_TRANSFER_SIZE]; // Stack buffer para batch
        
        while (true) {
            // IMPROVEMENT 3: Lock-free snapshot (volatile/atomic read)
            int snapshotWrite = writePos; // ? Atomic snapshot do write position
            int available = (snapshotWrite - readPos + BUFFER_RX_SIZE) % BUFFER_RX_SIZE;
            
            if (available == 0) break; // Nada para transferir
            
            // Limit batch to available size and buffer size
            int batchSize = (available > BATCH_TRANSFER_SIZE) ? BATCH_TRANSFER_SIZE : available;
            
            // Periodic watchdog reset (every ~100ms)
            if (millis() - lastBufWdtReset > 100) {
                esp_task_wdt_reset();
                lastBufWdtReset = millis();
            }
            
            // CRITICAL: Copy entire batch inside a short critical section
            noInterrupts();
            // Bounds checking for safety
            if (readPos >= BUFFER_RX_SIZE || snapshotWrite >= BUFFER_RX_SIZE) {
                readPos = 0;
                writePos = 0;
                interrupts();
                sendVirtualMessage(String(F("Buffer positions corrupted, reset performed")), ERROR);
                break;
            }
            
            // Copy batch to local buffer (fast, ~2ms for 512 bytes)
            for (int i = 0; i < batchSize; i++) {
                batchBuffer[i] = bufferRx[(readPos + i) % BUFFER_RX_SIZE];
            }
            readPos = (readPos + batchSize) % BUFFER_RX_SIZE;
            interrupts(); // Release interrupts QUICKLY
            
            // Process batch OUTSIDE the critical section (without blocking interrupts)
            lastByteFromDb25 = true; // Source: DB25/Nano
            lastDataSourceForEmail = "LPT"; // Tracks source for SD Card logging
            
            for (int i = 0; i < batchSize; i++) {
                uint8_t raw = batchBuffer[i];
                
                // LPT Serial Monitor removed (optimization - buffer eliminated)
                // LPT data now flows through main 128KB bufferDados
                
                // Processa o byte bruto (UTF-8 decoding)
                uint32_t decodedCodepoint = decodeUtf8Byte(raw);
                if (decodedCodepoint != 0) {
                    handleDecodedCodepoint(decodedCodepoint);
                }
            }
        }
    } else {
        if (((writePos - readPos + BUFFER_RX_SIZE) % BUFFER_RX_SIZE) > 0 && currentMillis - lastBusyLogTime > BUSY_LOG_INTERVAL) {
            lastBusyLogTime = currentMillis;
        }
    }

    const unsigned long SERIAL_COMMAND_TIMEOUT = 100;

    // LPT Serial Monitor flush removed (optimization - buffer eliminated)
    // LPT data now flows through main 128KB bufferDados

    // ? Reset watchdog before Serial reading (can have large input)
    static unsigned long lastSerialWdtReset = 0;
    while (Serial.available()) {
        // ? Periodic watchdog reset during large Serial input
        if (millis() - lastSerialWdtReset > 50) {
            esp_task_wdt_reset();
            lastSerialWdtReset = millis();
        }
        char inChar = Serial.read();
        lastSerialReadTime = currentMillis;
        if (inChar == '\n') {
            serialCommandComplete = true;
        } else if (inChar != '\r') {
            serialCommandInput += inChar;
        }
    }

    if (serialCommandComplete || (serialCommandInput.length() > 0 && currentMillis - lastSerialReadTime > SERIAL_COMMAND_TIMEOUT)) {
        String rawCommandLine = serialCommandInput;
        serialCommandInput = "";
        serialCommandComplete = false;

        rawCommandLine.trim();

        if (!rawCommandLine.isEmpty()) {
            // Ignorar mensagens de boot e lixo serial
            if (rawCommandLine.startsWith(String(F("ets Jul 29 2019"))) ||
                rawCommandLine.startsWith(String(F("rst:0x1"))) ||
                rawCommandLine.startsWith(String(F("mode:DIO"))) ||
                rawCommandLine.startsWith(String(F("load:0x"))) ||
                rawCommandLine.startsWith(String(F("entry 0x"))) ||
                rawCommandLine.startsWith(String(F("E ("))) ||
                rawCommandLine.startsWith(String(F("W ("))) ||
                rawCommandLine.startsWith(String(F("I ("))) ||
                (rawCommandLine.length() > 200 &&
                 !rawCommandLine.startsWith(String(F("SET_ALL_CONFIGS:"))) &&
                 !rawCommandLine.startsWith(String(F("GET_ALL_CONFIGS"))) &&
                 !rawCommandLine.startsWith(String(F("HELLO"))) &&
                 !rawCommandLine.startsWith(String(F("GETIP"))) &&

                 !rawCommandLine.startsWith(String(F("CLEAR_PRINTER_DATA"))) &&
                 !rawCommandLine.startsWith(String(F(" @@"))) && // Adicionado espa?o para o prefixo
                 !rawCommandLine.startsWith(String(F("HELP"))))
            ) {
                String ignoredMsg = String(F("Ignorando possivel mensagem de boot/lixo serial: '"));
                ignoredMsg += rawCommandLine;
                ignoredMsg += String(F("'"));
                sendVirtualMessage(ignoredMsg, DEBUG);
                sendVirtualMessage(String(F("DBG: ignorado por filtros")), DEBUG);
                // Serial.println(F("[DEBUG] Comando foi IGNORADO pelos filtros"));
            } else if (rawCommandLine.startsWith(String(F("']'")))) {
                sendVirtualMessage(String(F("']' solitario no inicio")), DEBUG);
                sendVirtualMessage(String(F("DBG: ignorado (])")), DEBUG);
                // Serial.println(F("[DEBUG] Comando foi IGNORADO (caractere ']')"));
            } else {
                sendVirtualMessage(String(F("DBG: processar")), DEBUG);
                // Serial.println(F("[DEBUG] Comando ser? PROCESSADO"));
                processLine(rawCommandLine, currentMillis);
            }
        }
    }

    // Logic to detect end of data reception (LPT/RS232) and display suppressed messages
    // Criterion: no active reception (inactivity) and there is a stock of suppressed messages
    bool isDataActiveForSuppression = true;
    {
        bool empty = false;
        bool hasData = true;
        if (bufferDadosTryIsEmpty(empty, 0)) hasData = !empty;
        isDataActiveForSuppression = (currentMillis - lastReceptionTime < DATA_INACTIVITY_FOR_SUPPRESSION_MS) || hasData || isReceivingData;
    }

    // Gating extra: avoid flushing system messages during the first RS232 response
    bool shouldDeferSystemFlush = rs232FirstResponseGuardActive || rs232_sending; // Do not release while guard is active or during TX
    static unsigned long _lastSystemFlushInfoAt = 0;           // Cooldown for flush messages
    static unsigned long _lastSystemFlushRececaoAt = 0;         // One-shot per reception period
    const unsigned long SYSTEM_FLUSH_INFO_COOLDOWN_MS = 10000;  // ? 10s
    
    // ? TEMPORARY WORKAROUND: Disable suppressed message flush to prevent crash
    // Root cause: CircularBuffer<String> has race condition even with mutex protection
    // TODO: Replace with safer queue implementation or redesign message suppression
    #if 0  // Set to 1 to re-enable (currently causes LoadProhibited @ 0x107)
    if (!isDataActiveForSuppression && !shouldDeferSystemFlush) {
        // Atomic flag protection for thread-safe buffer access
        // (Disabled code - kept for reference if re-enabled later)
        bool hasMsgs = false;
        if (logLock(pdMS_TO_TICKS(100))) {
            hasMsgs = !suppressedSystemMessagesBuffer.isEmpty();
            logUnlock();
        } else {
            goto skip_suppressed_flush; // Failed to acquire lock
        }
        
        if (hasMsgs) {
            if ((millis() - _lastSystemFlushInfoAt > SYSTEM_FLUSH_INFO_COOLDOWN_MS) && (_lastSystemFlushRececaoAt != lastReceptionTime)) {
                sendVirtualMessage(String(F("End of reception - pending messages")), INFO);
                _lastSystemFlushInfoAt = millis();
                _lastSystemFlushRececaoAt = lastReceptionTime;
            }
            
            // Flush with extended lock hold and careful iteration
            if (logLock(pdMS_TO_TICKS(100))) {
                int flushed = 0;
                // Safe iteration: use size() to avoid iterator invalidation
                size_t count = suppressedSystemMessagesBuffer.size();
                for (size_t i = 0; i < count && i < 8; i++) {
                    if (suppressedSystemMessagesBuffer.isEmpty()) break;
                    
                    // Critical: Create deep copy before shift
                    String msgCopy;
                    msgCopy.reserve(128);
                    msgCopy = String(suppressedSystemMessagesBuffer.first().c_str());
                    suppressedSystemMessagesBuffer.shift();
                    
                    // Release lock before calling sendVirtualMessage (recursive lock)
                    logUnlock();
                    sendVirtualMessage(msgCopy, INFO, true, true);
                    logLock(pdMS_TO_TICKS(100));
                    flushed++;
                }
                logUnlock();
                
                if (flushed > 0 && millis() - _lastSystemFlushInfoAt > 100) {
                    sendVirtualMessage(String(F("System messages displayed")), INFO);
                }
            }
        }
    }
skip_suppressed_flush:
    #endif
    // End of disabled flush block

    handleEmailAndBootLog();
    handleSampleTimer();
    handleDeferredWifiConnect();
    handleInactivityAlert();
    handleProximity();
    S3MARK("grbl");
    handleGrblAndUsbip();
    handleFinalWatchdog();
#undef S3MARK
}