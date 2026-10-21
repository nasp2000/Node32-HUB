// rs232_config_impl.h
// Included only from chatgpt.ino (single translation unit).

#include "heap_trace.h"
#include "littlefs_manager.h" // littlefsBegin() — no-SD crash-log fallback

#if ENABLE_RS232==1
// Function to update baudrate by index.
void setRS232BaudrateByIndex(int idx) {
    if (idx >= 0 && idx < RS232_BAUDRATES_COUNT) { // If the index is valid.
        rs232_baudrate_index = idx; // Set the new index.
        rs232_baudrate = RS232_BAUDRATES[idx]; // Set the new baudrate.
        if (rs232_enabled) { // Se o RS232 estiver ativado.
            RS232.updateBaudRate(rs232_baudrate); // Update baudrate.
            safeEnviarMensagemVirtual("RS232 baud=%d", INFO, rs232_baudrate);


        }
    } else { // Se o indice for invalido.
    safeEnviarMensagemVirtual("RS232 invalid idx: %d", ERROR, idx);
    }
}
#endif // ENABLE_RS232

// === RS232: apply runtime configuration (baud, framing, pins) ===
void applyRS232Config() { // Apply RS232 configs or perform startup if first run
    static bool _applying = false; // Reentrancy guard
    static bool _firstRun = true; // First-run flag
    // _rs232PortInitialized now declared at file scope (after lastRs232ConfigMillis)
    if(_applying){ sendVirtualMessage(F("applyRS232Config reentrant"), WARN); return; } // ? Mensagem compacta
    _applying = true; // Enter
    bool fullBoot = _firstRun; // Decide path
#if ENABLE_RS232==1
    // Baudrate coerce (uses RS232_BAUDRATES which is only defined when RS232 is enabled)
    if (rs232_baudrate_index >= 0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT) { rs232_baudrate = RS232_BAUDRATES[rs232_baudrate_index]; } else { rs232_baudrate_index = 3; rs232_baudrate = RS232_BAUDRATES[3]; sendVirtualMessage(F("Invalid baud idx -> 9600"), WARN); }
#endif // ENABLE_RS232
    if(fullBoot){ // Heavy sequence only at boot
        _firstRun = false; // Latch immediately to stop reentrant callers from triggering boot again
        // Serial.begin already called at the top of setup() - do not re-initialize here
        // WiFi.enableProv(false); // REMOVED: causes instability on Core 3.0.x if called early
    // Update startup banner to reflect v2.0
    sendVirtualMessage(F("Boot v2.0"), INFO); // compact message
        // REMOVED for Core 3.0.x: (esp_netif_init and esp_event_loop_create_default are already called during Arduino boot)
        // This avoids WDT/Crash in async_tcp due to LwIP re-initialization
        // esp_netif_init(); // TCP/IP stack
        // esp_event_loop_create_default();
        bootTime = millis(); // Record boot time
        // PSRAM detection with fallback to native Arduino ESP32 API
#if HAVE_PSRAM_API
        {
            size_t psram_total = (size_t)ESP.getPsramSize();
            size_t psram_free = (size_t)ESP.getFreePsram();
            size_t psram_used = psram_total > psram_free ? (psram_total - psram_free) : 0;
            if (psram_total == 0) {
                safeEnviarMensagemVirtual("PSRAM: not detected", WARN);
            } else {
                safeEnviarMensagemVirtual("PSRAM: %u KB total | %u KB free | %u KB used", INFO, (unsigned)(psram_total/1024), (unsigned)(psram_free/1024), (unsigned)(psram_used/1024));
            }
        }
#else
  // Fallback: try using Arduino ESP32 API (ESP.getPsramSize())
  #if defined(ESP32) || defined(ESP32S3)
        {
            size_t psram_total = ESP.getPsramSize();
            if (psram_total > 0) {
                size_t psram_free = ESP.getFreePsram();
                size_t psram_used = psram_total > psram_free ? (psram_total - psram_free) : 0;
                safeEnviarMensagemVirtual("PSRAM: %u KB total | %u KB free | %u KB used (API Arduino)", INFO, (unsigned)(psram_total/1024), (unsigned)(psram_free/1024), (unsigned)(psram_used/1024));
            } else {
                // More detailed information to aid diagnostics
                #if HAVE_PSRAM_FLAG && !HAVE_PSRAM_HEADERS
                    safeEnviarMensagemVirtual("PSRAM: partial support (using Arduino ESP32 API). ESP-IDF headers missing", INFO);
                #elif !HAVE_PSRAM_FLAG
                    safeEnviarMensagemVirtual("PSRAM: not detected. Add -DBOARD_HAS_PSRAM to platformio.ini if board has PSRAM", INFO);
                #else
                    safeEnviarMensagemVirtual("PSRAM: not detected", INFO);
                #endif
            }
        }
  #else
        safeEnviarMensagemVirtual("PSRAM: platform not supported", WARN);
  #endif
#endif
    nanoBridgeBegin(); // Start UART bridge with the Nano
        ensureExpectedNvsNamespaces(); // Create expected namespaces on fresh boards before read-only opens
        configManager.loadConfig(); // Load config from dual-storage (NVS + LittleFS)
        ntpManager.begin(
            configManager.getNtpServer(),
            configManager.getNtpTimezone(),
            configManager.getNtpSyncIntervalMinutes()
        );
        syncGlobalVariables(); // Sync globals
        setupLanguage(); // Apply stored language preference to active lang pack
        
        {
            String systemName = configManager.getSystemName();
            safeEnviarMensagemVirtual("System: '%s' | SD files: %s_DD-MM-YYYY.txt", INFO, systemName.c_str(), systemName.c_str());
        }
        
        // 📊 Load storage stats from NVS (write counters for NVS/Flash partitions)
        loadStorageStatsFromNVS();

        // Compact heap summary after config load
        {
            size_t dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            float frag = (dram_free > 0) ? (100.0f - ((float)dram_largest * 100.0f / (float)dram_free)) : 0.0f;
            safeEnviarMensagemVirtual("Heap: %u KB free, largest %u KB, frag %.0f%%", INFO,
                                     (unsigned)(dram_free/1024), (unsigned)(dram_largest/1024), frag);
        }

#if ENABLE_RS232==1
        // DIAGNOSTIC: Show loaded RS232 settings (full)
        char diagBuf[384];
        snprintf(diagBuf, sizeof(diagBuf), 
                 "RS232: Enabled=%s | Baud=%d (idx=%d) | Bits=%d%s%d | InvertPins=%s | XON/XOFF=%s | Mirror=%s",
                 rs232_enabled ? "YES" : "NO",
                 rs232_baudrate,
                 rs232_baudrate_index,
                 rs232_data_bits,
                 rs232_parity.c_str(),
                 rs232_stop_bits,
                 rs232_invert_pins ? "YES" : "NO",
                 configManager.getRs232UseXonXoff() ? "ON" : "OFF",
                 rs232_mirror_to_usb ? "YES" : "NO");
        sendVirtualMessage(String(diagBuf), INFO);
        
        // Active RS232 template (if configured)
        String activeTemplate = configManager.getRs232CurrentTemplate();
        if (!activeTemplate.isEmpty() && activeTemplate != "none") {
            char templateBuf[128];
            snprintf(templateBuf, sizeof(templateBuf), "RS232 Template: %s", activeTemplate.c_str());
            sendVirtualMessage(String(templateBuf), INFO);
        }
#endif // ENABLE_RS232

#if ENABLE_SDCARD==1
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: LittleFS FIRST (panel needs configs + WAV index); the TF/SPI
    // mount waits until after screenManager.begin() below, when the Board has
    // created CH422G and touch7SdAssertCs() can hold EXIO4 LOW. Mounting here
    // would run before the expander exists and SDMMC would hit SPI pins.
    // Stack size 12KB: loadFromLittleFS() + JSON parsing is stack-heavy.
    xTaskCreate(taskInitFlash, "flashInit", 12288, nullptr, 1, nullptr);
#else
    // SD init runs first; taskInitSD starts taskInitFlash when finished.
    // CRITICAL: SD and LittleFS must NOT initialize concurrently - both allocate PSRAM buffers
    // via malloc() (CONFIG_SPIRAM_USE_MALLOC=1). When interleaved, FatFS memset(FATFS*,0,...)
    // can zero the adjacent heap block header and trigger CORRUPT HEAP.
    xTaskCreate(taskInitSD, "sdInit", 8192, nullptr, 1, nullptr);
#endif
#elif ENABLE_FILESYSTEM==1
    // No SD: start LittleFS directly
    // Stack size 12KB: loadFromLittleFS() + JSON parsing is stack-heavy (see taskInitFlash)
    xTaskCreate(taskInitFlash, "flashInit", 12288, nullptr, 1, nullptr);
#endif

#if ENABLE_ETHERNET==1
    // Pin to Core 0: loopTask runs on Core 1; keeping ethInit on Core 0 prevents
    // Ethernet.begin() (DHCP, SPI) from starving loopTask and triggering the WDT.
    {
        BaseType_t _ethInitResult = xTaskCreatePinnedToCore(
            taskInitEthernet, "ethInit", 10240, nullptr, 1, nullptr, 0);
        if (_ethInitResult != pdPASS) {
            // Task creation failed (usually low heap). ethernetLoop() will retry
            // every 30 s via taskReinitEthernet once loop() starts.
            sendVirtualMessage(F("ETH: failed to create init task (low heap?)"), ERROR);
        }
    }
#endif

#if ENABLE_MODBUS==1
    // Modbus init: RTU (UART1) and/or TCP polling task.
    {
        JsonVariant mbSec = configManager.getSection("modbus");
        bool mbEn    = mbSec["enabled"].as<bool>();
        bool mbRtuEn = mbSec["rtu_enabled"].as<bool>();

        if (mbEn && mbRtuEn) {
            ModbusRtuConfig mbCfg;
            mbCfg.baud     = mbSec["rtu_baud"]      | (uint32_t)9600;
            mbCfg.dataBits = mbSec["rtu_data_bits"] | (uint8_t)8;
            const char* par = mbSec["rtu_parity"]   | "N";
            mbCfg.parity   = (par && par[0]) ? par[0] : 'N';
            mbCfg.stopBits = mbSec["rtu_stop_bits"] | (uint8_t)1;
            mbCfg.dePin    = mbSec["rtu_de_pin"]    | (int8_t)-1;
            modbusRtuBegin(mbCfg);
            safeEnviarMensagemVirtual("✅ Modbus RTU: UART1 @%u baud 8%c%d (auto-DE)", INFO,
                                      (unsigned)mbCfg.baud, mbCfg.parity, (int)mbCfg.stopBits);
        }

        if (mbEn) {
            // Load configured devices (RTU and/or TCP) and start the polling task.
            // This block runs for any Modbus-enabled config regardless of RTU/TCP mode.
            uint8_t devCount = modbusLoadDevicesFromConfig();
            if (devCount > 0) {
                // Wrapper: inject a logged line into the web RX ring buffer (bufferDados)
                // so poll values appear on the "Receção de Dados" page.
                modbusPollSetCallbacks(
                    addDataToSdcardDirectRaw,
                    [](const char* line, size_t len) { snifferInjectLine(line, len); }
                );
                modbusPollBegin(g_modbusDevices, devCount);
                safeEnviarMensagemVirtual("✅ Modbus poll task: %u device(s) (RTU=%s)", INFO,
                                         (unsigned)devCount, mbRtuEn ? "yes" : "no");
            } else {
                sendVirtualMessage(F("ℹ️ Modbus: no devices configured, poll task not started"), INFO);
            }
        }

#if ENABLE_MODBUS_SLAVE==1
        // Modbus slave mode: respond to external master requests with sensor readings.
        // RTU slave shares UART1 with the RTU master -- recommended to use TCP slave
        // when RTU master is also active, to avoid bus contention.
        bool mbSlaveEn = mbEn && mbSec["slave_enabled"].as<bool>();
        if (mbSlaveEn) {
            bool     slaveRtu = mbRtuEn;  // RTU slave mirrors RTU master setting
            bool     slaveTcp = mbSec["tcp_enabled"].as<bool>();
            uint8_t  slaveId  = mbSec["slave_id"]  | (uint8_t)1;
            int8_t   dePin    = mbSec["rtu_de_pin"]| (int8_t)-1;
            uint32_t slaveBaud= mbSec["rtu_baud"]  | (uint32_t)9600;
            modbusSlaveBegin(slaveId, slaveRtu, slaveTcp, dePin, slaveBaud, 502);
            safeEnviarMensagemVirtual("✅ Modbus slave started: ID=%u%s%s", INFO,
                                      (unsigned)slaveId,
                                      slaveRtu ? " RTU" : "",
                                      slaveTcp ? " TCP:502" : "");
            if (slaveRtu && mbRtuEn) {
                sendVirtualMessage(F("⚠️ Modbus: RTU slave + RTU master active -- avoid simultaneous bus access"), WARN);
            }
        }
#endif
    }
#endif

    // Create recursive mutex for recipient notification throttling
    recipientThrottleMutex = xSemaphoreCreateRecursiveMutex();
    if (recipientThrottleMutex == NULL) {
        sendVirtualMessage(F("❌ Failed to create recipient throttle mutex"), ERROR);
    }

    runtimeEmailInitBackgroundTasks();
    heapTraceMark("email");

    } else { syncGlobalVariables(false); } // Light reapply (ignore RS232)
    // Keep RS232 runtime settings stable at boot.
    // If current_template is missing/not found, do NOT overwrite live RS232 fields with Default,
    // otherwise saved manual baudrate may be lost after reboot.
    {
        String cur = configManager.getRs232CurrentTemplate();
        JsonArray arr = configManager.getRs232Templates();
        bool found = false;
        for (size_t i = 0; i < arr.size(); ++i) {
            JsonObject o = arr[i];
            if (o.containsKey("name") && String(o["name"].as<String>()) == cur) { found = true; break; }
        }
        if (!found) {
            // Clear invalid template marker in RAM, keep saved RS232 parameters unchanged.
            configManager.setRs232CurrentTemplateVolatile(String(""));
            sendVirtualMessage(F("RS232: current template invalid - keeping manual config"), WARN);
        }
    }
    if(fullBoot){ loadAccessPointConfig(); } // Only on boot

    // Start WiFi before NTP so the SNTP daemon has a network interface available
    // during the 10-second sync wait below. Without this, NTP always fails and the
    // wait wastes time, delaying RS232 init by ~10 s on every boot.
#if SCREEN_TYPE == 3
    T7LOG("boot: wifi...");
#endif
    if(fullBoot && ENABLE_WIFI==1){ connectToWiFi(); }
#if SCREEN_TYPE == 3
    T7LOG("boot: wifi done");
#endif

    if(fullBoot){ // NTP+Timezone only at boot to avoid repeated resets
        // Use configTzTime to apply timezone (with DST) and NTP server at once
        // Avoids local time -1h due to missing TZ applied to SNTP
        configTzTime(timezone.c_str(), ntpServer.c_str());
        lastNtpSyncMillis = millis(); // Mark initial sync
#if SCREEN_TYPE == 3
        T7LOG("boot: NTP wait...");
#endif
        
        // Wait for NTP sync (max 10 seconds)
        safeEnviarMensagemVirtual("NTP: synchronizing with %s...", INFO, ntpServer.c_str());
        int ntpRetries = 0;
        time_t now = 0;
        while(now < 100000 && ntpRetries < 20) { // Check for valid epoch (>100k = after 1970)
            esp_task_wdt_reset();
            delay(500);
            time(&now);
            ntpRetries++;
        }
        
        if(now > 100000) {
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
            safeEnviarMensagemVirtual("NTP OK: %s", INFO, buf);
            
            // ✅ Initialize crash logger AFTER NTP sync (ensures correct log filename)
            // Log file: /Log/YYYY-MM-DD.txt (e.g., /Log/2026-02-08.txt)
            #if ENABLE_SDCARD==1
            if (sdcardIsMounted()) {
                initCrashLogger();
                fixLogFilenameAfterNtp();  // Rename/merge pre-NTP log files (1970-01-01.txt)
                panicHookInit();
                panicContextReport();

                // Write the full system header (name + firmware + IP) now that WiFi IP is valid.
                // logWriteSystemInfo() prepends this to the log file, replacing the boot placeholder.
                // Retry up to 5 times (250 ms apart) to ensure DHCP has assigned a real IP.
                {
                    String sysHdr;
                    for (int _hdrTry = 0; _hdrTry < 5; _hdrTry++) {
                        IPAddress _logIp = WiFi.localIP();
                        #if ENABLE_ETHERNET==1
                        if (_logIp == IPAddress(0,0,0,0)) _logIp = ethernetLocalIP();
                        #endif
                        if (_logIp != IPAddress(0,0,0,0) || _hdrTry == 4) {
                            sysHdr = buildFileHeader("LOG");
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(250));
                    }
                    if (!sysHdr.isEmpty()) logWriteSystemInfo(sysHdr);
                    observabilityLogBootSnapshot(FIRMWARE_VERSION,
                                                 configManager.getSystemName().c_str(),
                                                 getCurrentIPForINO().c_str(),
                                                 configManager.getSdcardEnabled(),
                                                 sdcardIsMounted(),
                                                 psramFound(),
                                                 configManager.getRs232Enabled(),
                                                 configManager.getChartsPageEnabled(),
                                                 configManager.getEmailAutoSend());
                }

                debugWriterInit();  // Initialize debug writer (separate from crash logs)
                
                // ✅ AUTO-SYNC: If debugWriter was persisted as ON, auto-enable debug logs
                if (debugWriterIsEnabled()) {
                    enableDebugLogs = true;
                    Serial.println("✅ Debug logs auto-activated (DEBUG_WRITER is ON)");
                }
            }
#if SCREEN_TYPE == 3
            else if (littlefsBegin()) {
#else
            else if (littlefsBegin() && littlefsMounted()) {
#endif
                // No SD card: still init the crash logger. The boot/crash
                // event is persisted to LittleFS (/logs/boot.log) via
                // appendBootEventToLittleFS(), so a panic survives the reset
                // even with no SD (live .24 2026-10-14: crash forensics was
                // blind because this ran SD-only).
                initCrashLogger();
                panicHookInit();
                panicContextReport();
            }
            #endif
        } else {
            safeEnviarMensagemVirtual("NTP FAILED: invalid time after %d attempts", WARN, ntpRetries);
        }
#if SCREEN_TYPE == 3
        T7LOG("boot: NTP done");
#endif
    }
    
    // Configure RS232 (consolidated immediate application)
    // Redundant applyRS232Config call removed (already applied at startup)
    
    // connectToWiFi() already called above (before NTP block) so WiFi is up by now.

    if(fullBoot){ // Only during full boot
#if SCREEN_TYPE == 3
        // TouchMeteo-7: the RGB panel starts LAST (see below, after "Boot OK").
        // Its GDMA ISR memcpy's PSRAM, which panics on any concurrent SPI1
        // flash op — so every boot-time flash read (LittleFS, configs,
        // sensors, WiFi, NTP) must finish first. Proven 14-09-2026.
        #include "touch7_log.h"
        T7LOG("boot: fullBoot entry");
#else
        if (!screenManager.isInitialized()) { // Avoid re-initializing if already ready
            delay(150); // Wait slightly to stabilize I2C bus
            if (!screenManager.begin()) { // Try to initialize LCD
#ifndef QUIET_SCREEN_LOGS
                sendVirtualMessage(F("LCD not found (I2C)"), WARN);
#endif
            } else {
                sendVirtualMessage(F("LCD OK"), INFO);
            }
        }
#endif
        
        // Initialize environmental sensor (AHT20 + BMP280)
        #if ENABLE_AHT20_BMP280==1
#if SCREEN_TYPE == 3
        T7LOG("boot: AHT begin...");
#endif
        delay(100); // Wait for I2C bus stabilization
        if (environmentalSensor.begin()) {
            sendVirtualMessage(F("Environmental sensor OK"), INFO);
        } else {
            sendVirtualMessage(F("Environmental sensor not found — check I2C wiring"), WARN);
        }

        // Initialize sensor config manager (always required for history, SD flush, alarms).
        #if ENABLE_SENSOR_CONFIG_PAGE==1
#if SCREEN_TYPE == 3
        T7LOG("boot: sensorConfig begin...");
#endif
        if (sensorConfigManager.begin()) {
            sendVirtualMessage(F("Sensor config manager initialized"), INFO);
        } else {
            sendVirtualMessage(F("Sensor config manager failed to initialize"), WARN);
        }
        #endif
        #endif
        // TouchMeteo-7 pack modules (config/peers/weather/audio/PIR). The RGB
        // display itself is already up via screenManager.begin() above.
        #if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
        T7LOG("boot: touch7SystemBegin");
        touch7SystemBegin();
        sendVirtualMessage(F("TouchMeteo-7 modules initialized"), INFO);
        T7LOG("boot: touch7 modules ok");
        #endif
    }
    
    #if ENABLE_WIFI_WEB_SERVER==1
    if(fullBoot){
        // Initialize and start handlers and HTTP server
#if SCREEN_TYPE == 3
        T7LOG("boot: web server...");
#endif
        setupConfigManagerEndpoints();
        setupWebServer(server);
        delay(200); // Short wait to ensure TCP/IP stack & event loop are stable
        server.begin();
#if SCREEN_TYPE == 3
        T7LOG("boot: server begun");
#endif
        
        // ============================================================
        // HEAP DIAGNOSTIC: After web server initialization
        // ============================================================
        {
            size_t dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            
            safeEnviarMensagemVirtual("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", INFO);
            safeEnviarMensagemVirtual("HEAP DIAGNOSTIC (after web server init):", INFO);
            safeEnviarMensagemVirtual("DRAM Free: %u KB | PSRAM Free: %u KB", INFO, 
                                     (unsigned)(dram_free/1024), 
                                     (unsigned)(psram_free/1024));
            safeEnviarMensagemVirtual("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", INFO);
        }
        if (enableDebugLogs) {
            Serial.print("[DIAG] free_heap after server.begin(): ");
            Serial.println(ESP.getFreeHeap());
        }
        sendVirtualMessage(F("Web monitor ready"), INFO);
        lastStartupAction = millis();
        currentStartupState = STARTUP_TELNET_PENDING;
#if SCREEN_TYPE == 3
        T7LOG("boot: pre-screen");
#endif
#if SCREEN_TYPE == 3
        // TouchMeteo-7 RGB panel starts LAST: all boot flash I/O is done,
        // so the GDMA ISR can no longer race a boot-time SPI1 access.
        T7LOG("boot: screen begin (panel last)");
        if (!screenManager.isInitialized()) {
            if (!screenManager.begin()) {
                sendVirtualMessage(F("TOUCH7 display begin FAILED"), WARN);
            } else {
                sendVirtualMessage(F("TOUCH7 display OK"), INFO);
            }
        }
#if ENABLE_SDCARD==1
        // Panel up => Board/CH422G exists: mount the TF/SPI slot now in the
        // background (LittleFS is already up from the early flashInit task).
        sdcardBeginAsync();
#endif
#endif
        sendVirtualMessage(F("Boot OK"), INFO);
#if SCREEN_TYPE == 3
        T7LOG("boot: Boot OK, entering loop");
#endif

    } else
    #endif
    if (fullBoot) {
        lastStartupAction = millis();
        currentStartupState = STARTUP_TELNET_PENDING;
        sendVirtualMessage(F("WiFi web server disabled by build flag"), INFO);
        sendVirtualMessage(F("Boot OK"), INFO);

#if ENABLE_DB25==1 && ENABLE_NANO_REBOOT_ON_BOOT==1
        // Auto reboot the Nano on each ESP32 boot
        // Intent: ensure Nano boots cleanly and synchronizes with the ESP32
    sendVirtualMessage(F("NANO: reboot (auto at ESP32 boot)"), INFO);
    nanoBridgeReboot();             // Request immediate reboot to Nano (0xF5)
    delay(250);                     // Wait ~250 ms for the AVR to complete reset/boot stably
        // Reapply runtime defaults after reboot (reboot clears previous adjustments)
        nanoBridgeSetXonXoffEnable(true); // Respect XON/XOFF
        nanoBridgeSetAckWidth(20);        // ACK ~20 us
        nanoBridgeSetStrobeMinLow(4);     // Anti-glitch filter 4 us
        nanoBridgeSetPauseMode(1);        // Graceful pause
        nanoBridgeForceBusy(false);       // No forced BUSY
        nanoBridgeSetAckDelayBefore(0);   // No delay before ACK
        nanoBridgeSetBusyTail(0);         // No BUSY tail
        // No /ERROR control - simplified to core signals set
        nanoBridgeSetPauseGraceBytes(0);  // No grace after XOFF
#endif

    } else {
        sendVirtualMessage(F("Reapply OK"), INFO);
    }

    // === RS232: ensure physical initialization when rs232_enabled already TRUE in config ===
    // MOTIVATION: If rs232_enabled is true at startup, previously there was no RS232.begin() here, so RS232.available() remained 0 and nothing appeared on the monitor.
    // SOLUTION: Compute framing config (data bits / parity / stop bits) and do a safe begin() only if not initialized or parameters changed.
    // IMPLEMENTATION: Generate supported configuration code (fallback to SERIAL_8N1 for unsupported combinations) and start/update the port.

#if ENABLE_RS232==1
    auto buildSerialConfig = [&]() -> uint32_t { // Lambda to map parameters to Arduino constant
        // Notes: ESP32 Arduino defines macros SERIAL_{5,6,7,8}{N,E,O}{1,2}. Not all combinations are necessary, but we map the main ones.
        int db = rs232_data_bits; // target data bits
        String parity = rs232_parity; parity.toLowerCase(); // normalized parity
        int sb = rs232_stop_bits; // stop bits
        // Normalize bounds
        if (db < 5 || db > 8) db = 8; // out of range -> use 8 bits
        if (sb != 1 && sb != 2) sb = 1; // invalid stop -> 1
        // Select base by data bits
        // Structure: SERIAL_{db}{Parity}{Stop}
        // To reduce repeated code we create simple tables
        struct MapEntry { int db; char parity; int sb; uint32_t cfg; }; // map entry
        const MapEntry table[] = { // table of supported combinations
            {5,'n',1,SERIAL_5N1},{5,'n',2,SERIAL_5N2},{5,'e',1,SERIAL_5E1},{5,'e',2,SERIAL_5E2},{5,'o',1,SERIAL_5O1},{5,'o',2,SERIAL_5O2},
            {6,'n',1,SERIAL_6N1},{6,'n',2,SERIAL_6N2},{6,'e',1,SERIAL_6E1},{6,'e',2,SERIAL_6E2},{6,'o',1,SERIAL_6O1},{6,'o',2,SERIAL_6O2},
            {7,'n',1,SERIAL_7N1},{7,'n',2,SERIAL_7N2},{7,'e',1,SERIAL_7E1},{7,'e',2,SERIAL_7E2},{7,'o',1,SERIAL_7O1},{7,'o',2,SERIAL_7O2},
            {8,'n',1,SERIAL_8N1},{8,'n',2,SERIAL_8N2},{8,'e',1,SERIAL_8E1},{8,'e',2,SERIAL_8E2},{8,'o',1,SERIAL_8O1},{8,'o',2,SERIAL_8O2}
        };
        char p = 'n'; // Default parity
        if (parity == F("even")) p='e'; else if (parity == F("odd")) p='o'; else p='n'; // Map parity
        for (auto &e : table) { // Search for matching combination
            if (e.db == db && e.parity == p && e.sb == sb) return e.cfg; // Found
        }
        return SERIAL_8N1; // Fallback

    };

    if (rs232_enabled) { // Only initialize if enabled
        // Recalculate consistent baudrate (in case config changed externally)
        if (rs232_baudrate_index >=0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT) {
            rs232_baudrate = RS232_BAUDRATES[rs232_baudrate_index]; // Update baud
        }
        // N16R8 UART2 default pins: GPIO16=RX, GPIO17=TX.
        // invert_pins swaps RX and TX (useful when cable is wired backwards).
        if (rs232_invert_pins) { rs232_rx_pin = RS232_DEFAULT_TX_PIN; rs232_tx_pin = RS232_DEFAULT_RX_PIN; }
        else                   { rs232_rx_pin = RS232_DEFAULT_RX_PIN; rs232_tx_pin = RS232_DEFAULT_TX_PIN; }
        
        // EMI PROTECTION: enable internal pull-up on RX pin to avoid noise when disconnected
        pinMode(rs232_rx_pin, INPUT_PULLUP);
        
        uint32_t serialCfg = buildSerialConfig(); // Get config
        static uint32_t lastCfg = 0; // Tracks last applied framing config to detect changes
        static int lastRxPin = -1;
        static int lastTxPin = -1;
        if (!_rs232PortInitialized) { // First-time port startup
            RS232.setRxBufferSize(4096); // RX buffer 4KB (default 256) - max protection against overflows
            RS232.begin(rs232_baudrate, serialCfg, rs232_rx_pin, rs232_tx_pin); // Initialize UART2
            _rs232PortInitialized = true; // Mark initialized
            lastCfg = serialCfg; // Seed lastCfg so subsequent calls don't trigger a needless RECFG
            lastRxPin = rs232_rx_pin;
            lastTxPin = rs232_tx_pin;
            lastRs232ConfigMillis = millis(); // Mark reconfiguration
            sendVirtualMessage(String(F("RS232: baud=")) + String(rs232_baudrate) + F(" cfg=") + String((unsigned long)serialCfg,16) + F(" RX=") + String(rs232_rx_pin) + F(" TX=") + String(rs232_tx_pin), INFO); // Compact message
        } else { // Already initialized: apply baud and/or framing changes
            static uint32_t lastBaud = 0;
            bool baudChanged   = (rs232_baudrate != lastBaud);
            bool framingChanged = (lastCfg != serialCfg);
            bool pinChanged = (lastRxPin != rs232_rx_pin) || (lastTxPin != rs232_tx_pin);
            if (baudChanged || framingChanged || pinChanged) {
                // Set guard BEFORE touching the UART driver.
                // ESP32-S3 is dual-core: the AsyncWebServer task may call RS232.available()
                // concurrently; without this flag cleared first, uart_driver_delete() races
                // with uart_get_buffered_data_len() and emits "uart driver error".
                _rs232PortInitialized = false;
                RS232.end();
                RS232.setRxBufferSize(4096);
                RS232.begin(rs232_baudrate, serialCfg, rs232_rx_pin, rs232_tx_pin);
                _rs232PortInitialized = true;
                lastRs232ConfigMillis = millis();
                if (framingChanged || pinChanged) {
                    sendVirtualMessage(String(F("RS232 reconfig: baud=")) + String(rs232_baudrate) + F(" cfg=") + String((unsigned long)serialCfg,16) + F(" RX=") + String(rs232_rx_pin) + F(" TX=") + String(rs232_tx_pin), INFO);
                } else {
                    sendVirtualMessage(String(F("RS232 baud=")) + String(rs232_baudrate) + F(" RX=") + String(rs232_rx_pin) + F(" TX=") + String(rs232_tx_pin), INFO);
                }
            }
            lastCfg = serialCfg;
            lastBaud = rs232_baudrate;
            lastRxPin = rs232_rx_pin;
            lastTxPin = rs232_tx_pin;
        }

    #if ENABLE_SNIFFER==1
    // Sniffer is disabled while RS232 is active to avoid UART conflicts
    #endif
    } else { // RS232 disabled
        if (_rs232PortInitialized) { // Only close if it was active
            RS232.end(); // Close UART
            _rs232PortInitialized = false; // Clear flag
            sendVirtualMessage(F("RS232 OFF (apply)"), INFO); // Compact message
        }
        #if ENABLE_SNIFFER==1
        // If RS232 is OFF but the sniffer flag is active, initialize the sniffer
        // Instead of fixed defaults, inherit RS232 settings (baud, bits, parity, stop)
        // Get current parameters from ConfigManager (available even when RS232 disabled in build)
        int cfg_idx = configManager.getRs232BaudrateIndex(); // Baud index
        // Local map of baudrates for the sniffer (mirrors RS232_BAUDRATES)
        const int SNIFFER_BAUDS[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
        const int SNIFFER_BAUDS_COUNT = sizeof(SNIFFER_BAUDS) / sizeof(SNIFFER_BAUDS[0]);
        int snif_baud = (cfg_idx >=0 && cfg_idx < SNIFFER_BAUDS_COUNT) ? SNIFFER_BAUDS[cfg_idx] : SNIFFER_DEFAULT_BAUD; // Fallback 19200
        // Build configuration SERIAL_{db}{parity}{stop}
        int db = configManager.getRs232DataBits(); // 5..8
        String parity = configManager.getRs232Parity(); parity.toLowerCase(); // none/even/odd
        int sb = configManager.getRs232StopBits(); // 1/2
        if (db < 5 || db > 8) db = 8; // Sanitize
        if (sb != 1 && sb != 2) sb = 1; // Sanitize
        char p = 'n'; if (parity == F("even")) p='e'; else if (parity == F("odd")) p='o';
        struct MapEntry { int db; char parity; int sb; uint32_t cfg; };
        const MapEntry table[] = {
            {5,'n',1,SERIAL_5N1},{5,'n',2,SERIAL_5N2},{5,'e',1,SERIAL_5E1},{5,'e',2,SERIAL_5E2},{5,'o',1,SERIAL_5O1},{5,'o',2,SERIAL_5O2},
            {6,'n',1,SERIAL_6N1},{6,'n',2,SERIAL_6N2},{6,'e',1,SERIAL_6E1},{6,'e',2,SERIAL_6E2},{6,'o',1,SERIAL_6O1},{6,'o',2,SERIAL_6O2},
            {7,'n',1,SERIAL_7N1},{7,'n',2,SERIAL_7N2},{7,'e',1,SERIAL_7E1},{7,'e',2,SERIAL_7E2},{7,'o',1,SERIAL_7O1},{7,'o',2,SERIAL_7O2},

            {8,'n',1,SERIAL_8N1},{8,'n',2,SERIAL_8N2},{8,'e',1,SERIAL_8E1},{8,'e',2,SERIAL_8E2},{8,'o',1,SERIAL_8O1},{8,'o',2,SERIAL_8O2}
        };
        uint32_t serialCfg = SERIAL_8N1; // Fallback
        for (auto &e : table) { if (e.db==db && e.parity==p && e.sb==sb) { serialCfg = e.cfg; break; } }
        // Initialize sniffer with inherited parameters
        snifferBegin((uint32_t)snif_baud, serialCfg, snifferInjectLine);
        #endif
    }
#endif // ENABLE_RS232

    // When ENABLE_RS232==0 at compile time (forced by ENABLE_SNIFFER=1), the block above
    // is never compiled, so snifferBegin() would never be called. This block handles that case.
    // It also detects changes in baud/framing at runtime and reinitializes the sniffer accordingly.
#if ENABLE_RS232==0 && ENABLE_SNIFFER==1
    {
        const int SNIFFER_BAUDS[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
        const int SNIFFER_BAUDS_COUNT = (int)(sizeof(SNIFFER_BAUDS) / sizeof(SNIFFER_BAUDS[0]));
        int cfg_idx = configManager.getRs232BaudrateIndex();
        int snif_baud = (cfg_idx >= 0 && cfg_idx < SNIFFER_BAUDS_COUNT) ? SNIFFER_BAUDS[cfg_idx] : SNIFFER_DEFAULT_BAUD;
        int db = configManager.getRs232DataBits();
        String parity = configManager.getRs232Parity(); parity.toLowerCase();
        int sb = configManager.getRs232StopBits();
        if (db < 5 || db > 8) db = 8;
        if (sb != 1 && sb != 2) sb = 1;
        char p = 'n'; if (parity == F("even")) p = 'e'; else if (parity == F("odd")) p = 'o';
        struct MapEntry2 { int db; char parity; int sb; uint32_t cfg; };
        const MapEntry2 table2[] = {
            {5,'n',1,SERIAL_5N1},{5,'n',2,SERIAL_5N2},{5,'e',1,SERIAL_5E1},{5,'e',2,SERIAL_5E2},{5,'o',1,SERIAL_5O1},{5,'o',2,SERIAL_5O2},
            {6,'n',1,SERIAL_6N1},{6,'n',2,SERIAL_6N2},{6,'e',1,SERIAL_6E1},{6,'e',2,SERIAL_6E2},{6,'o',1,SERIAL_6O1},{6,'o',2,SERIAL_6O2},
            {7,'n',1,SERIAL_7N1},{7,'n',2,SERIAL_7N2},{7,'e',1,SERIAL_7E1},{7,'e',2,SERIAL_7E2},{7,'o',1,SERIAL_7O1},{7,'o',2,SERIAL_7O2},
            {8,'n',1,SERIAL_8N1},{8,'n',2,SERIAL_8N2},{8,'e',1,SERIAL_8E1},{8,'e',2,SERIAL_8E2},{8,'o',1,SERIAL_8O1},{8,'o',2,SERIAL_8O2}
        };
        uint32_t serialCfg = SERIAL_8N1;
        for (auto &e : table2) { if (e.db == db && e.parity == p && e.sb == sb) { serialCfg = e.cfg; break; } }

        // Track last applied config to detect changes and reinitialize when baud/framing is updated
        static uint32_t _lastSnifBaud = 0;
        static uint32_t _lastSnifCfg = 0;
        if (_lastSnifBaud != (uint32_t)snif_baud || _lastSnifCfg != serialCfg) {
            _lastSnifBaud = (uint32_t)snif_baud;
            _lastSnifCfg = serialCfg;
            snifferBegin((uint32_t)snif_baud, serialCfg, snifferInjectLine);
            sendVirtualMessage(String(F("SNIFFER: started baud=")) + String(snif_baud) +
                                  F(" RX1=GPIO") + String(SNIFFER1_RX_PIN) +
                                  F(" RX2=GPIO") + String(SNIFFER2_RX_PIN), INFO);
        }
    }
#endif // ENABLE_RS232==0 && ENABLE_SNIFFER==1

    // Initialize Google Email system (if enabled and configured)
    #if ENABLE_GOOGLE_EMAIL==1
    if (fullBoot) {
        // Only initialize if Google Email is configured or selected as method
        String emailMethod = configManager.getEmailMethod();
        String googleScriptUrl = configManager.getGoogleScriptUrl();
        
        if (emailMethod == "google" || googleScriptUrl.length() > 0) {
            googleEmailInit(); // Already prints "GOOGLE_EMAIL: Initialized" message
        } else {
            sendVirtualMessage(F("🔕 GOOGLE_EMAIL: Skipped (SMTP method selected, saving ~100 bytes heap)"), DEBUG);
        }
    }
    #endif

    _applying = false; // Release guard
}