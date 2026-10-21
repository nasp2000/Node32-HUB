// config_sync_impl.h
// Included only from chatgpt.ino (single TU).
// Config persistence and UI sync: saveAllConfigs, loadAllConfigs,
// enviarTodasConfiguracoesParaUI, sendWifiStatusAndRssi.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
void saveAllConfigs() {
    // Check memory before creating large JSON documents.
    size_t freeHeap = ESP.getFreeHeap();
    const size_t SAFE_JSON_HEAP_THRESHOLD = 100 * 1024; // Avoid heavy serialization with heap <= 100KB
    if (freeHeap <= SAFE_JSON_HEAP_THRESHOLD) {
        sendVirtualMessage(String(F("Low heap (")) + String(freeHeap) + String(F(" bytes). Deferring save")), WARN);
        return; // Avoid large allocations when memory is low
    }
    sendVirtualMessage(F("DBG: saveAllConfigs() start"), DEBUG);
    
    preferences.begin("all_configs", false); // Use 'preferences' object
    // Size JSON document dynamically based on heap
    size_t docCapacity = (freeHeap >= 300 * 1024) ? 8192 : (freeHeap >= 160 * 1024 ? 4096 : 3072);
    SpiRamJsonDocument doc(docCapacity); //  Capacidade adaptativa

    doc["ssid"] = ssid;
    doc["pass"] = password;
    doc["smtpServer"] = smtpServer;
    doc["smtpPort"] = smtpPort;
    doc["smtpUser"] = smtpUser;
    doc["smtpPass"] = smtpPass;
    doc["emailDest"] = emailDest;
    doc["autoSendEmail"] = configManager.getEmailAutoSend(); //  Use value persisted in ConfigManager for consistency across reboots
    doc["currentMaxEmailLines"] = currentMaxEmailLines;
#if ENABLE_TELNET==1
    doc["telnetUser"] = telnetUser; //  Telnet username
    doc["telnetPass"] = telnetPass; //  Telnet password
#endif
    doc["charsetMode"] = charsetMode;
    doc["printerEmulationMode"] = printerEmulationMode; // Save emulation mode

    doc["autoEmailEnabledTimeout"] = autoEmailEnabledInactivityTimeout;
    doc["autoEmailDisabledTimeout"] = autoEmailDisabledInactivityTimeout;
    doc["minEmailContentLength"] = minEmailContentLength;

    doc["ntpServer"] = ntpServer;
    doc["timezone"] = timezone;
    doc["enableDebugLogs"] = enableDebugLogs;

    // RS232 configs
    doc["rs232_enabled"] = rs232_enabled;
    doc["rs232_invert_pins"] = rs232_invert_pins;
    doc["rs232_baudrate_index"] = rs232_baudrate_index;

    // Removed legacy recipients export (use slots endpoints for debug if needed)

    // Removed legacy scheduled_commands export (use slots endpoints for debug if needed)

    // Add quick status snapshot (like /api/status) for offline diagnosis
    JsonObject quick = doc.createNestedObject("quick_status");
    quick["wifi_connected"] = (WiFi.status()==WL_CONNECTED);
    quick["network_connected"] = hasNetworkConnectivity();
    if (WiFi.status()==WL_CONNECTED) {
        quick["ip"] = WiFi.localIP().toString();
        quick["rssi"] = WiFi.RSSI();
        quick["ssid"] = WiFi.SSID();
    } else if (hasNetworkConnectivity()) {
        quick["ip"] = getCurrentNetworkIPString();
        quick["link"] = "ethernet";
    }
    quick["heap"] = ESP.getFreeHeap();
    quick["firmware_version"] = FIRMWARE_VERSION; // Base version (for cache-busting)
    quick["firmware_build"] = FIRMWARE_BUILD;     // Full version (with date)

    // Serialize in one shot into a fixed buffer to avoid repeated String reallocations.
    size_t jsonCapacity = measureJson(doc) + 1;
    std::vector<char> jsonBuf;
    jsonBuf.resize(jsonCapacity);
    size_t jsonBytes = serializeJson(doc, jsonBuf.data(), jsonBuf.size());
    if (jsonBytes == 0) {
        preferences.end();
        sendVirtualMessage(F("saveAllConfigs: serializeJson failed"), ERROR);
        return;
    }
    jsonBuf[jsonBuf.size() - 1] = '\0';
    preferences.putString("data", jsonBuf.data()); // Use 'preferences' object
    preferences.end(); // Use 'preferences' object
    safeEnviarMensagemVirtual("Configs saved (%u bytes).", INFO, (unsigned)jsonBytes);
    sendVirtualMessage(F("DBG: saveAllConfigs() completed"), DEBUG);

    String debugMsg = String(F("Heap after save: "));
    debugMsg += String(ESP.getFreeHeap());
    debugMsg += String(F(" bytes"));
    sendVirtualMessage(debugMsg, DEBUG);
}

void loadAllConfigs() {
    preferences.begin("all_configs", true); // Use 'preferences' object
    String jsonStr = preferences.isKey("data") ? preferences.getString("data", "{}") : String("{}"); // Use 'preferences' object
    preferences.end(); // Use 'preferences' object

    // Size dynamically per free heap
    size_t freeHeap = ESP.getFreeHeap();
    size_t docCapacity = (freeHeap >= 300 * 1024) ? 8192 : (freeHeap >= 160 * 1024 ? 3072 : 2048);
    SpiRamJsonDocument doc(docCapacity);
    DeserializationError error = deserializeJson(doc, jsonStr);

    String debugMsgLoaded = String(F("Heap before loading configs: "));
    debugMsgLoaded += String(ESP.getFreeHeap());
    debugMsgLoaded += String(F(" bytes"));
    sendVirtualMessage(debugMsgLoaded, DEBUG);

    if (error) {
    String errorMsg = String(F("Error loading configurations: "));
        errorMsg += String(error.c_str());
        sendVirtualMessage(errorMsg, ERROR);
        return;
    }

    ssid = doc["ssid"] | "";
    password = doc["pass"] | "";
    smtpServer = doc["smtpServer"] | "";
    smtpPort = doc["smtpPort"] | "";
    smtpUser = doc["smtpUser"] | "";
    smtpPass = doc["smtpPass"] | "";
    emailDest = doc["emailDest"] | "";
    autoSendEmail = doc["autoSendEmail"] | false;
    currentMaxEmailLines = doc["currentMaxEmailLines"] | MAX_LINES_6_LPI;
#if ENABLE_TELNET==1
    telnetUser = doc["telnetUser"] | "root"; //  Telnet username
    telnetPass = doc["telnetPass"] | "root"; //  Telnet password
#endif
    charsetMode = doc["charsetMode"] | 0;
    printerEmulationMode = doc["printerEmulationMode"] | 0; // Load emulation mode

    autoEmailEnabledInactivityTimeout = doc["autoEmailEnabledTimeout"] | 10000;
    autoEmailDisabledInactivityTimeout = doc["autoEmailDisabledTimeout"] | 120000;
    minEmailContentLength = doc["minEmailContentLength"] | 3;
    autoBufferClearInactivityTimeout = autoEmailDisabledInactivityTimeout + 1000;

    ntpServer = doc["ntpServer"] | "pool.ntp.org";
    timezone = doc["timezone"] | "WET0WEST,M3.5.0/1,M10.5.0";
    enableDebugLogs = doc["enableDebugLogs"] | false;

#if ENABLE_RS232==1
    // RS232 configs
    rs232_enabled = doc["rs232_enabled"] | false;
    rs232_invert_pins = doc["rs232_invert_pins"] | false;
    rs232_baudrate_index = doc["rs232_baudrate_index"] | 3;
    setRS232BaudrateByIndex(rs232_baudrate_index);
#endif // ENABLE_RS232

    String debugHeap = String(F("Heap after loading basics: "));
    debugHeap += String(ESP.getFreeHeap());
    debugHeap += String(F(" bytes"));
    sendVirtualMessage(debugHeap, DEBUG);

    if (doc.containsKey("recipients")) {
        JsonObject destObj = doc["recipients"].as<JsonObject>();
        configManager.clearAllRecipients();
        int count=0;
        for (JsonPair kv : destObj) {
            String key = String(kv.key().c_str());
            key.toLowerCase();
            String value = kv.value().as<String>();
            configManager.addRecipient(key, value);
            count++;
        }
        sendVirtualMessage(String(F("Loaded ")) + String(count) + String(F(" recipients")), INFO);
    }

    // Removed legacy scheduled_commands loading (migration completed)
    sendVirtualMessage(String(F("Configurations loaded.")), INFO);
    
    String finalHeap = String(F("Final heap after loading: "));
    finalHeap += String(ESP.getFreeHeap());
    finalHeap += String(F(" bytes"));
    sendVirtualMessage(finalHeap, DEBUG);
}

// NEW: Function to get all unified information in JSON
SpiRamJsonDocument get_all_configs() { // Renamed to get_all_configs
    // Use PSRAM free space to size the document — SpiRamJsonDocument allocates in PSRAM,
    // so checking getFreeHeap() (DRAM) was wrong for sizing. With 8MB PSRAM available,
    // we can afford a much larger document without impacting DRAM pressure.
    size_t freeForDoc = psramFound() ? ESP.getFreePsram() : ESP.getFreeHeap();
    size_t docCapacity = (freeForDoc >= 2 * 1024 * 1024) ? 32768 :
                         (freeForDoc >= 512 * 1024)       ? 24576 :
                         (freeForDoc >= 160 * 1024)       ? 16384 :
                         (freeForDoc >= 64 * 1024)        ? 8192  : 4096;
    SpiRamJsonDocument doc(docCapacity); // Main document (PSRAM when available)

    // 1. Get full ConfigManager settings
    SpiRamJsonDocument configDoc = configManager.getFullConfig(true); // With masked passwords
    doc["config"] = configDoc;

    // 2. Chip information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE == 1
    static const char* s3ModelName = "ESP32 S3 Waveshare 8DI8DO";
#else
    static const char* s3ModelName = "ESP32-S3";
#endif
    JsonObject chip = doc.createNestedObject("chip_info");
    chip["model"] = (chip_info.model == CHIP_ESP32) ? "ESP32" :
                    (chip_info.model == CHIP_ESP32S2) ? "ESP32-S2" :
                    (chip_info.model == CHIP_ESP32S3) ? s3ModelName :
                    (chip_info.model == CHIP_ESP32C3) ? "ESP32-C3" :
                    (chip_info.model == CHIP_ESP32C2) ? "ESP32-C2" :
                    (chip_info.model == CHIP_ESP32C6) ? "ESP32-C6" :
                    (chip_info.model == CHIP_ESP32H2) ? "ESP32-H2" :
#ifdef CHIP_ESP32P4
                    // CHIP_ESP32P4 defined in ESP-IDF >= 5.3 (arduino-esp32 >= 3.2.0)
                    (chip_info.model == CHIP_ESP32P4) ? "ESP32-P4 (Waveshare)" :
#endif
                    "Unknown";
    chip["revision"] = chip_info.revision;
    chip["cores"] = chip_info.cores;
    chip["embedded_flash"] = (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? true : false;
    chip["external_flash_size_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
    chip["flash_speed_mhz"] = ESP.getFlashChipSpeed() / (1000 * 1000);
    chip["sdk_version"] = ESP.getSdkVersion();

    // 3. Firmware information
    doc["firmware_version"] = FIRMWARE_VERSION; // Base
    doc["firmware_build"] = FIRMWARE_BUILD;     // Full

    // 4. Network information
    JsonObject network = doc.createNestedObject("network_info");
    network["mac_address"] = WiFi.macAddress();
    network["network_connected"] = hasNetworkConnectivity();
    network["active_ip"] = hasNetworkConnectivity() ? getCurrentNetworkIPString() : String("N/A");
    network["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED";
    IPAddress ethIp = ethernetLocalIP();
    bool ethConnected = (ethIp != IPAddress(0,0,0,0));
    network["ethernet_status"] = ethConnected ? "CONNECTED" : "DISCONNECTED";
    network["ethernet_ip"] = ethConnected ? ethIp.toString() : String("N/A");
    if (WiFi.status() == WL_CONNECTED) {
        network["ip_address"] = WiFi.localIP().toString();
        network["rssi"] = WiFi.RSSI();
#if ENABLE_TELNET==1
        network["telnet_server_status"] = "ACTIVE"; //  Telnet ativo
        network["telnet_port"] = TELNET_PORT; //  Telnet port
#else
        network["telnet_server_status"] = "DISABLED"; //  Telnet desativado
        network["telnet_port"] = 23; //  Default port (for reference)
#endif
    } else {
        network["ip_address"] = ethConnected ? ethIp.toString() : String("N/A");
        network["rssi"] = "N/A";
#if ENABLE_TELNET==1
        network["telnet_server_status"] = "INACTIVE"; //  Telnet inativo
        network["telnet_port"] = TELNET_PORT; //  Configured port
#else
        network["telnet_server_status"] = "DISABLED"; //  Telnet desativado
        network["telnet_port"] = 23; //  Default port (for reference)
#endif
    }

    // 5. Memory information
    JsonObject memory = doc.createNestedObject("memory_info");
    JsonObject buffer_email = memory.createNestedObject("email_buffer");
    buffer_email["total_size_bytes"] = BUFFER_DATA_SIZE;
    {
        size_t used = 0;
        if (bufferDadosTryGetSize(used, 0)) {
            buffer_email["used_bytes"] = (uint32_t)used;
            buffer_email["free_bytes"] = (uint32_t)(bufferDados.capacity() - used);
        } else {
            // Buffer is being accessed by another task; report unknown for a consistent JSON shape.
            buffer_email["used_bytes"] = -1;
            buffer_email["free_bytes"] = -1;
        }
    }
    buffer_email["lines_count"] = bufferDadosLineCount;
    buffer_email["busy_threshold_bytes"] = BUSY_ACTIVATION_THRESHOLD_BYTES;

    JsonObject buffer_rx = memory.createNestedObject("reception_buffer");
    buffer_rx["total_size_bytes"] = BUFFER_RX_SIZE;
    buffer_rx["used_bytes"] = (writePos - readPos + BUFFER_RX_SIZE) % BUFFER_RX_SIZE;
    buffer_rx["free_bytes"] = BUFFER_RX_SIZE - ((writePos - readPos + BUFFER_RX_SIZE) % BUFFER_RX_SIZE) - 1;
    buffer_rx["overflows"] = overflowCount;
    
    // LPT Serial Monitor telemetry removed (optimization - buffer eliminated)
    // LPT data now flows through main 128KB bufferDados

    const esp_partition_t *nvs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    JsonObject nvs_part = memory.createNestedObject("nvs_partition");
    if (nvs_partition) {
        nvs_part["found"] = true;
        nvs_part["total_size_bytes"] = nvs_partition->size;
        
        // Calculate approximate usage based on our stored configs.
        // On TOUCH7 this must NOT read NVS here: the RGB VSYNC ISR runs
        // flash-hosted code, so a flash op on the web task panics the board.
        // touch7SystemBegin() caches the size at boot (pre-panel).
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
        extern size_t touch7NvsAllConfigsBytes;
        size_t used_bytes_estimate = touch7NvsAllConfigsBytes + 500; // + NVS overhead
#else
        Preferences tempPrefs;
        tempPrefs.begin("all_configs", true);
        String jsonStr = tempPrefs.isKey("data") ? tempPrefs.getString("data", "{}") : String("{}");
        tempPrefs.end();
        size_t used_bytes_estimate = jsonStr.length() + 500; // Add overhead for NVS structure
#endif
        nvs_part["used_bytes_estimate"] = used_bytes_estimate;
        nvs_part["free_bytes_estimate"] = nvs_partition->size - used_bytes_estimate;
        nvs_part["usage_percentage"] = (float)(used_bytes_estimate * 100) / nvs_partition->size;
        nvs_part["usage_kb"] = (float)used_bytes_estimate / 1024.0;
        nvs_part["total_size_kb"] = (float)nvs_partition->size / 1024.0;
        nvs_part["free_kb"] = (float)(nvs_partition->size - used_bytes_estimate) / 1024.0;
    } else {
        nvs_part["found"] = false;
    }
    
    memory["total_flash_size_bytes"] = ESP.getFlashChipSize();
    memory["free_heap_bytes"] = ESP.getFreeHeap();
    memory["min_free_heap_bytes"] = ESP.getMinFreeHeap();

    // 5. Saved settings (masking passwords) - UNIFIED WITH NVS
    JsonObject configs = doc.createNestedObject("saved_configs");
    configs["system_name"] = configManager.getSystemName();
    configs["system_language"] = configManager.getSystemLanguage();
    configs["unified_password"] = "********"; // 🔑 Senha Unificada (Mascarada)
    configs["wifi_ssid"] = ssid;
    configs["wifi_pass"] = "********"; // Mascarado
    configs["smtp_server"] = smtpServer;
    configs["smtp_port"] = smtpPort;
    configs["smtp_user"] = smtpUser;
    configs["smtp_pass"] = "********"; // Mascarado
    configs["email_destination"] = configManager.getEmailDestination(); //  Usa sempre valor atual do ConfigManager (legacy plano)
    // Legacy email object removed - canonical email object created below with all fields
    configs["auto_send_email"] = configManager.getEmailAutoSend(); //  Exportar estado de auto envio a partir do ConfigManager (persistente)
    configs["current_max_email_lines"] = configManager.getEmailMaxLines(); //  Garantir valor persistente atualizado
    configs["min_email_content_length"] = minEmailContentLength;
#if ENABLE_TELNET==1
    configs["telnet_user"] = telnetUser; //  Telnet username
    configs["telnet_pass"] = "********"; //  Telnet password (masked)
#else
    configs["telnet_user"] = ""; //  Telnet desativado
    configs["telnet_pass"] = ""; //  Telnet desativado
#endif
    configs["charset_mode"] = charsetMode;
    configs["printer_emulation_mode"] = printerEmulationMode;
    
    // CORRIGIDO: Usar mesmas chaves do NVS (camelCase)
    configs["autoEmailEnabledTimeout"] = autoEmailEnabledInactivityTimeout;
    configs["autoEmailDisabledTimeout"] = autoEmailDisabledInactivityTimeout;
    configs["auto_buffer_clear_inactivity_timeout_ms"] = autoBufferClearInactivityTimeout;

    configs["ntp_server"] = ntpServer;
    configs["timezone"] = timezone;
    configs["debug_logs"] = enableDebugLogs;
    // Expose email config flags useful for UI (mask sensitive fields already above)
    JsonObject emailObj = configs.createNestedObject("email"); //  Canonical single section for email
    emailObj["method"] = configManager.getEmailMethod(); //  Send method (auto/smtp/google)
    emailObj["destination"] = configManager.getEmailDestination(); //  Normalized destination(s)
    emailObj["auto_send"] = configManager.getEmailAutoSend(); //  Auto-send flag
    emailObj["max_lines"] = configManager.getEmailMaxLines(); //  Line limit
    emailObj["min_content_length"] = configManager.getEmailMinContentLength(); //  Minimum characters for sending
    emailObj["subject"] = configManager.getEmailSubject(); //  Configured subject
    emailObj["dont_copy_sender"] = configManager.getEmailDontCopySender(); //  Flag: do not send copy to sender
    //  Expose .txt attachment flag so the Envios page preload reflects the state correctly
    emailObj["attach_txt"] = configManager.getEmailAttachTxt(); // Include attach_txt in saved_configs
    emailObj["include_timestamp"] = configManager.getEmailIncludeTimestamp(); //  Include include_timestamp in saved_configs
    //  Export only recipient count here for lightness (full list via dedicated endpoint)
    // Export only slot count here for efficiency (full list via dedicated endpoint)
    emailObj["recipients_count"] = (int)configManager.getRecipientSlots().size();

    // Inactivity alert config (6-slot system)
    {
        JsonObject inactObj = configs.createNestedObject("inactivity_alerts");
        inactObj["sensors_reset_timer"] = configManager.getInactivityAlertSensorsResetTimer();
        JsonArray slotsArr = inactObj.createNestedArray("slots");
        for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
            JsonObject slot = slotsArr.createNestedObject();
            slot["enabled"]         = configManager.getAlertSlotEnabled(i);
            slot["timeout_minutes"] = configManager.getAlertSlotTimeout(i);
            slot["max_per_day"]     = configManager.getAlertSlotMaxPerDay(i);
            slot["emails"]          = configManager.getAlertSlotEmails(i);
        }
    }

#if ENABLE_RS232==1
    // RS232 mode: read from ConfigManager to avoid dependency on global visibility across TUs.
    // Globals (rs232_data_bits etc.) mirror ConfigManager after syncGlobalVariables(); using
    // getters here is functionally equivalent and avoids scope issues in impl-include files.
    configs["rs232_enabled"] = configManager.getRs232Enabled();
    configs["rs232_invert_pins"] = configManager.getRs232InvertPins();
    configs["rs232_baudrate_index"] = configManager.getRs232BaudrateIndex();
    configs["rs232_data_bits"] = configManager.getRs232DataBits();
    configs["rs232_parity"] = configManager.getRs232Parity();
    configs["rs232_stop_bits"] = configManager.getRs232StopBits();
    configs["rs232_read_timeout"] = configManager.getRs232ReadTimeoutMs();
    configs["rs232_use_xon_xoff"] = configManager.getRs232UseXonXoff();
    { JsonArray baudrate_list = configs.createNestedArray("rs232_baudrate_list");
      for (int i = 0; i < RS232_BAUDRATES_COUNT; ++i) baudrate_list.add(RS232_BAUDRATES[i]); }
#elif ENABLE_SNIFFER==1
    // Sniffer mode: RS232 globals do not exist; read directly from ConfigManager.
    // The sniffer uses the same config fields as RS232 for its UART parameters.
    configs["rs232_enabled"] = false;
    configs["rs232_invert_pins"] = configManager.getRs232InvertPins();
    configs["rs232_baudrate_index"] = configManager.getRs232BaudrateIndex();
    configs["rs232_data_bits"] = configManager.getRs232DataBits();
    configs["rs232_parity"] = configManager.getRs232Parity();
    configs["rs232_stop_bits"] = configManager.getRs232StopBits();
    configs["rs232_read_timeout"] = configManager.getRs232ReadTimeoutMs();
    configs["rs232_use_xon_xoff"] = configManager.getRs232UseXonXoff();
    configs["sniffer_mode"] = true; // Web UI uses this flag to show sniffer status
    { const int snBauds[] = {1200,2400,4800,9600,19200,38400,57600,115200};
      JsonArray baudrate_list = configs.createNestedArray("rs232_baudrate_list");
      for (int i = 0; i < 8; ++i) baudrate_list.add(snBauds[i]); }
#endif // ENABLE_RS232 / ENABLE_SNIFFER

    {
        JsonObject webObj = configs.createNestedObject("web");
        webObj["max_sessions"]    = configManager.getWebMaxSessions();
        webObj["idle_timeout_min"] = configManager.getWebIdleTimeoutMin();
        webObj["abs_timeout_min"] = configManager.getWebAbsTimeoutMin();
        webObj["username"]        = configManager.getWebUsername();
        webObj["password"]        = "********"; // masked
        webObj["auth_enabled"]    = configManager.getWebAuthEnabled();
        webObj["viewer_enabled"]  = configManager.getWebViewerEnabled();
        webObj["viewer_username"] = configManager.getWebViewerUsername();
        webObj["viewer_password"] = "********"; // masked
    }

    JsonArray emulation_modes_list = configs.createNestedArray("printer_emulation_modes_list");
    for (int i = 0; i < PRINTER_EMULATION_MODES_COUNT; ++i) {
        emulation_modes_list.add(PRINTER_EMULATION_MODES[i]);
    }

    //  Predefined structures — expose simple flags used by the frontend
    {
        //  Moved structure fill to dedicated modular file
        #include "src/structures.h" //  Include structures utility (fills configs)

#include "psram_helpers.h"
        fillStructuresInConfigs(configs, configManager); // Export MTF1/AWSE flags
    }

    // Recipients: export as array of slot objects {name, op, emails, freq}
    JsonArray recipientsArr = configs.createNestedArray("recipients");
    JsonArray slotsForExport = configManager.getRecipientSlots();
    for (JsonObject slot : slotsForExport) {
        JsonObject obj = recipientsArr.createNestedObject();
        obj["name"]   = slot["name"]   | String("");
        obj["op"]     = slot["op"]     | String("I");
        obj["emails"] = slot["emails"] | String("");
        obj["freq"]   = slot["freq"]   | 0;
    }
    // Export numeric_rules (if any)
    JsonArray numericArr = configs.createNestedArray("numeric_rules");
    JsonArray nr = configManager.getNumericRulesJson();
    for (JsonObject o : nr) {
        JsonObject nn = numericArr.createNestedObject();
        if (o.containsKey("name")) nn["name"] = o["name"].as<String>();
        if (o.containsKey("keyword")) nn["keyword"] = o["keyword"].as<String>();
        if (o.containsKey("op")) nn["op"] = o["op"].as<int>();
        if (o.containsKey("emails")) nn["emails"] = o["emails"].as<String>();
    }

    //  Removed scheduled_commands inclusion in /api/configs (slots are the official source)

    // 6. Access Point settings
    JsonObject access_point = configs.createNestedObject("access_point");
    access_point["ssid"] = configManager.getAccessPointSSID();
    access_point["password"] = "********"; // Masked for safety
    access_point["active"] = apActive;
    access_point["retry_delay_ms"] = AP_RETRY_DELAY;
    access_point["max_failures"] = MAX_AP_FAILURES;
    access_point["failure_pause_ms"] = AP_FAILURE_PAUSE;
    access_point["activation_delay_ms"] = AP_ACTIVATION_DELAY;
    access_point["current_failure_count"] = apFailureCount;
    access_point["last_attempt_time"] = lastAPAttemptTime;
    if (apActive) {
        access_point["ip_address"] = WiFi.softAPIP().toString();
        access_point["connected_clients"] = WiFi.softAPgetStationNum();
    } else {
        access_point["ip_address"] = "N/A";
        access_point["connected_clients"] = 0;
    }

    return doc;
}

void enviarTodasConfiguracoesParaUI() {
    const size_t SAFE_HEAP_THRESHOLD = 100 * 1024; // Align with global 100 KB policy
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < SAFE_HEAP_THRESHOLD) {
        // Avoid serializing full JSON when heap is low; send short hint instead
    String warn = String(F("Low heap (")) + String(freeHeap / 1024) + String(F(" KB). Full JSON will not be sent. Use /api/configs in browser or free heap."));
        sendVirtualMessage(warn, WARN);
        return;
    }

    SpiRamJsonDocument doc = get_all_configs();
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    String configMsg = String(F("ALL_CONFIGS_DATA:###CONFIG_START###"));
    configMsg += jsonOutput;
    configMsg += String(F("###CONFIG_END###"));
    sendVirtualMessage(configMsg, COMMAND_RESPONSE);
    sendVirtualMessage(String(F("Configurations sent to UI.")), COMMAND_RESPONSE);
}

void sendWifiStatusAndRssi(LogLevel level, unsigned long currentMillis) {
    if (WiFi.status() == WL_CONNECTED) {
        String ipAddress = WiFi.localIP().toString();
    String ipMsg = String(F("WIFI_OK:"));
        ipMsg += ipAddress;
        sendVirtualMessage(ipMsg, level);
        heapTraceMark("wifi");

    String rssiMsg = String(F("RSSI:"));
        rssiMsg += String(WiFi.RSSI());
        sendVirtualMessage(rssiMsg, level);
        //  Only advertise Telnet as "Active" if really enabled in configs
        if (configManager.getTelnetEnabled()) {
#if ENABLE_TELNET==1
            String telnetMsg = String(F("Telnet Server: Active on port "));
            telnetMsg += String(TELNET_PORT); //  Telnet port
            telnetMsg += String(F(" on IP: "));
            telnetMsg += ipAddress;
            sendVirtualMessage(telnetMsg, level);
#else
            sendVirtualMessage(String(F("Telnet Server: Disabled in build")), level);
#endif
        } else {
            // If Telnet is off in config, make it clear in the log
            sendVirtualMessage(String(F("Telnet Server: Inactive (disabled)")), level);
        }
    } else if (ethernetHasIp()) {
        String ipAddress = getCurrentNetworkIPString();
        String ipMsg = String(F("ETH_OK:"));
        ipMsg += ipAddress;
        sendVirtualMessage(ipMsg, level);
        sendVirtualMessage(String(F("RSSI:N/A (Ethernet)")), level);
        if (configManager.getTelnetEnabled()) {
#if ENABLE_TELNET==1
            String telnetMsg = String(F("Telnet Server: Active on port "));
            telnetMsg += String(TELNET_PORT);
            telnetMsg += String(F(" on IP: "));
            telnetMsg += ipAddress;
            sendVirtualMessage(telnetMsg, level);
#else
            sendVirtualMessage(String(F("Telnet Server: Disabled in build")), level);
#endif
        } else {
            sendVirtualMessage(String(F("Telnet Server: Inactive (disabled)")), level);
        }
    } else {
    sendVirtualMessage(String(F("NET_DISCONNECTED")), level);
    sendVirtualMessage(String(F("RSSI:N/A")), level);
    sendVirtualMessage(String(F("Telnet Server: Inactive (network disconnected)")), level);
    }
}