// process_line_impl.h
// Included only from chatgpt.ino (single TU).
// Main data line processor: processLine().
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
#include "sensor_day_report.h"  // T23: ShiftEnd-style SensorDay body (pure, native-tested)
void processLine(const String &line, unsigned long currentMillis) {
    String debugRawInput = String(F("DEBUG_RAW_COMMAND_INPUT: '"));
    debugRawInput += line;
    debugRawInput += String(F("' (length: "));
    debugRawInput += String(line.length());
    debugRawInput += String(F(")"));
    sendVirtualMessage(debugRawInput, DEBUG);

    String commandLine = line;
    commandLine.trim();

    String debugCommand = String(F("Command received for processing: '"));
    debugCommand += commandLine;
    debugCommand += String(F("'"));
    sendVirtualMessage(debugCommand, DEBUG);

    String upperCaseCommand = commandLine;
    upperCaseCommand.toUpperCase();

    // DEBUG EXTRA: Print HEX for diagnosis
    if (upperCaseCommand.indexOf("HELP") >= 0 || upperCaseCommand.indexOf("DEBUG") >= 0) {
      String hexDump = "HEX: ";
      for(int i=0; i<upperCaseCommand.length(); i++) {
        char c = upperCaseCommand.charAt(i);
        if(c < 16) hexDump += "0";
        hexDump += String(c, HEX) + " ";
      }
      sendVirtualMessage(hexDump, DEBUG);
    }
    
    // ? Suporte global ao prefixo SYS: / SYS (case-insensitive) para comandos locais
    //    Permite escrever no terminal: "sys: reboot" ou "sys reboot" e executar o comando localmente.
    if (upperCaseCommand.startsWith(String(F("SYS:"))) || upperCaseCommand.startsWith(String(F("SYS ")))) {
        int prefixLen = 4; // "SYS:" ou "SYS "
        String localCmd = commandLine.substring(prefixLen);
        localCmd.trim();
        if (localCmd.length()) {
            sendVirtualMessage(String(F("SYS prefix detected - execute locally: ")) + localCmd, DEBUG);
            processLine(localCmd, currentMillis); //  Chamada recursiva sem o prefixo
        } else {
            sendVirtualMessage(String(F("SYS: no command after prefix.")), WARN);
        }
        return; // Do not continue processing this instance (avoid duplicates)
    }
    
    // 🚨 CRITICAL COMMAND INTERCEPTION: emailday and reboot
    // These commands require safe buffer flush to avoid data loss
    // Must execute BEFORE the command, while data reception continues
    if (upperCaseCommand.startsWith(String(F("EMAILDAY"))) ||
        upperCaseCommand.startsWith(String(F("LOGDAY"))) ||
        upperCaseCommand.startsWith(String(F("SENSORDAY"))) ||
        upperCaseCommand.equalsIgnoreCase(String(F("REBOOT"))) ||
        upperCaseCommand.startsWith(String(F("ESP.RESTART")))) {
        
        String cmdName = upperCaseCommand;
        if (cmdName.indexOf(' ') > 0) {
            cmdName = cmdName.substring(0, cmdName.indexOf(' ')); // Extract command name only
        }
        
        sendVirtualMessage(buildMessage("🚨 Critical command detected: %s", cmdName.c_str()), WARN);
        sendVirtualMessage(F("🔒 Executing safe data flush sequence..."), INFO);
        
        // Execute safe flush: swap buffers, flush to SD, wait for stability
        bool flushOk = safeFlushBeforeCriticalCommand(cmdName);
        
        // Also flush sensor history to SD (or PSRAM if SD absent) and save config to LittleFS
        #if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
        sendVirtualMessage(F("   📊 Flushing sensor history..."), INFO);
        bool _sensorFlushed = sensorConfigManager.getHistory().periodicFlushToSD(sensorConfigManager.getConfig());
        if (!_sensorFlushed) {
            // SD not available: fall back to PSRAM VFS
            sensorConfigManager.getHistory().periodicFlushToPSRAM(sensorConfigManager.getConfig());
        }
        sendVirtualMessage(F("   💾 Saving sensor config to LittleFS..."), INFO);
        sensorConfigManager.save();
        #endif
        
        if (!flushOk) {
            sendVirtualMessage(buildMessage("⚠️ %s: Flush incomplete, but proceeding...", cmdName.c_str()), WARN);
            // Don't block critical commands - proceed anyway
        }
        
        // Continue processing the command normally (will be handled by existing code below)
        sendVirtualMessage(buildMessage("▶️ %s: Proceeding with command execution", cmdName.c_str()), INFO);
    }

    const String configStartDelimiter = "###CONFIG_START###";
    const String configEndDelimiter = "###CONFIG_END###";

    //  Router central para HELP/HELP-XXXX (reduz cadeia de if/else)
    //  Usar wrapper est?tico compat?vel com HelpEmitFn (evita lambda capturadora)
    if (help_dispatch(upperCaseCommand, helpEmitWrapper, MAX_LINES_6_LPI, MAX_LINES_8_LPI)) {
        // Comando de ajuda tratado; nao continuar para os proximos if/else
    }
    // =========================
    // EMAILDAY: Send today's RS232/LPT storage folder via email
    // =========================
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    else if (upperCaseCommand.startsWith(String(F("EMAILDAY")))) {
        String folder = storageGetTodayFolder(); // Returns SD or PSRAM folder for today
        if (folder.length() == 0) {
            sendVirtualMessage(F("❌ EMAILDAY: Today's folder not found"), ERROR);
        } else {
            time_t t = time(nullptr);
            struct tm ti; localtime_r(&t, &ti);
            char dateStr[16];
            snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
                     ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
            String sysName = configManager.getSystemName();
            String subject = String(F("\xF0\x9F\x93\x82 EmailDay ")) + sysName + String(F(" ")) + dateStr;
            String body    = buildEmailInfoTable(
                             L(activeLang->emailDataAttached),
                             "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
            emailDayCmdExtra = configManager.getEmaildayExtraEmails();
            if (!startEmailDayAsync(folder, subject, body)) {
                sendVirtualMessage(F("❌ EMAILDAY: Failed to start email task"), ERROR);
            }
        }
    }
    // =========================
    // LOGDAY: Send today's log file via email
    // =========================
    else if (upperCaseCommand.startsWith(String(F("LOGDAY")))) {
        #if ENABLE_SDCARD==1
        {
            time_t t = time(nullptr);
            struct tm ti; localtime_r(&t, &ti);
            // Log folders use DD-MM-YYYY format: /logs/log_DD-MM-YYYY/
            // This must match getLogFilePath() in log.h exactly.
            char dateStr[16];
            snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
                     ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
            char logFolder[40];
            snprintf(logFolder, sizeof(logFolder), "/logs/log_%s", dateStr);
            // Verify folder exists before sending to avoid a silent empty email
            if (sdcardIsMounted() && !SD_MMC.exists(logFolder)) {
                sendVirtualMessage(buildMessage("❌ LOGDAY: folder not found: %s", logFolder), ERROR);
            } else {
                String sysName = configManager.getSystemName();
                String subject  = String(F("\xF0\x9F\x93\x8B LogDay ")) + sysName + String(F(" ")) + dateStr;
                String body     = buildEmailInfoTable(
                                  L(activeLang->emailDataAttached),
                                  "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                emailDayCmdExtra = configManager.getLogdayExtraEmails();
                if (!startEmailDayAsync(String(logFolder), subject, body)) {
                    sendVirtualMessage(F("❌ LOGDAY: Failed to start email task"), ERROR);
                }
            }
        }
        #else
        // ENABLE_SDCARD=0: no crash log files; email today's PSRAM data folder instead.
        {
            String folder = storageGetTodayFolder();
            if (folder.length() == 0 || !storageIsAvailable()) {
                sendVirtualMessage(F("\u274C LOGDAY: no storage available (PSRAM inactive)."), ERROR);
            } else {
                time_t t = time(nullptr);
                struct tm ti; localtime_r(&t, &ti);
                char dateStr[16];
                snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
                         ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                String sysName = configManager.getSystemName();
                String subject  = String(F("\xF0\x9F\x93\x8B LogDay ")) + sysName + String(F(" ")) + dateStr + String(F(" [PSRAM]"));
                String body     = buildEmailInfoTable(
                                  L(activeLang->emailDataAttached),
                                  "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                emailDayCmdExtra = configManager.getLogdayExtraEmails();
                if (!startEmailDayAsync(folder, subject, body)) {
                    sendVirtualMessage(F("\u274C LOGDAY: Failed to start email task"), ERROR);
                }
            }
        }
        #endif
    }
    // =========================
    // SENSORDAY: Flush sensor history to SD and send today's CSV via email
    // =========================
    else if (upperCaseCommand.startsWith(String(F("SENSORDAY")))) {
        #if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
        #if ENABLE_SDCARD==1
        {
            time_t t = time(nullptr);
            struct tm ti; localtime_r(&t, &ti);
            if (t < 1000000000L) {
                sendVirtualMessage(F("❌ SENSORDAY: NTP not synchronized"), ERROR);
            } else {
                char dateStr[16];
                snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
                         ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
                char sensorFolder[48];
                snprintf(sensorFolder, sizeof(sensorFolder), "/sensor_history/%s", dateStr);
                String sysName = configManager.getSystemName();
                // Data lives on SD (periodic flush) or on the LittleFS mirror when
                // no SD is mounted (same identical path). Accept both before
                // falling back to the in-RAM ring buffer export.
                bool hasSdFolder = SD_MMC.exists(sensorFolder);
                bool hasFlashFolder = false;
#if ENABLE_FILESYSTEM==1
                if (!hasSdFolder && littlefsBegin() && littlefsMounted()) {
                    // Metadata op under lfsMutex: the email task may be walking
                    // the same LittleFS concurrently (dir stat is the observed
                    // crash site). On contention stay with the RAM fallback.
                    extern bool lfsMutexLock(TickType_t timeoutTicks);
                    extern void lfsMutexUnlock();
                    if (lfsMutexLock(pdMS_TO_TICKS(5000))) {
                        hasFlashFolder = getLittleFS().exists(sensorFolder);
                        lfsMutexUnlock();
                    }
                }
#endif
                if (!hasSdFolder && !hasFlashFolder) {
                    // SD folder absent: export today's CSV directly from RAM ring buffer.
                    // No filesystem writes — works even before the first 30-min periodic flush.
                    String csvData = sensorConfigManager.getHistory().exportDayCSV(String(dateStr), sensorConfigManager.getConfig());
                    if (csvData.length() > 0) {
                        String subject = String(F("\xF0\x9F\x8C\xA1\xEF\xB8\x8F SensorDay ")) + sysName + String(F(" ")) + dateStr + String(F(" [RAM]"));
                        String recipients = configManager.getEmailDestination();
                        String sdExtra = configManager.getSensordayExtraEmails();
                        if (sdExtra.length() > 0) recipients = recipients.length() > 0 ? recipients + "," + sdExtra : sdExtra;
                        sendVirtualMessage(buildMessage("❌ SENSORDAY: RAM buffer -> %u lines, sending...", (unsigned)csvData.length()), INFO);
                        // T23: resumo estilo ShiftEnd a partir do CSV em RAM, mantendo o
                        // despejo inline (compat) + footer. Streaming (1 linha
                        // de cada vez — sem triplicar o CSV em RAM).
                        String prefix = "";
                        {
                            SensorDayParser par;
                            sensorDayParserInit(&par);
                            const char* raw = csvData.c_str();
                            size_t total = csvData.length();
                            size_t ls = 0;
                            int guard = 0;
                            for (size_t i = 0; i <= total && guard++ < 3000; i++) {
                                if (i == total || raw[i] == '\n') {
                                    size_t len = i - ls;
                                    if (len > 0 && raw[i - 1] == '\r') len--;
                                    if (len > 0 && len < 512) {
                                        sensorDayParserFeed(&par, std::string(raw + ls, len));
                                    }
                                    ls = i + 1;
                                }
                            }
                            std::string b = sensorDayParserBody(&par, std::string(dateStr));
                            if (!b.empty()) prefix = String(b.c_str());
                        }
                        startInactivityAlertAsync(
                            recipients,
                            subject,
                            prefix + csvData + buildEmailInfoTable(
                                L(activeLang->emailDataInBody),
                                "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", ""));
                    } else {
                        sendVirtualMessage(buildMessage("❌ SENSORDAY: No data in RAM buffer for %s", dateStr), ERROR);
                    }
                } else {
                    String subject = String(F("\xF0\x9F\x8C\xA1\xEF\xB8\x8F SensorDay ")) + sysName + String(F(" ")) + dateStr;
                    // T23: corpo estilo ShiftEnd lido de /sensor_history/<data>/<data>.csv
                    // (SD primeiro, depois LittleFS); fallback legacy sem resumo.
                    String prefix = "";
                    // Streaming direto do ficheiro (1 linha de cada vez): pico
                    // de RAM = 1 linha + aggr. O bulk-read anterior triplicava
                    // o CSV no heap e fazia PANIC por OOM (live .24).
                    {
                        SensorDayParser par;
                        sensorDayParserInit(&par);
                        char csvPath[80];
                        snprintf(csvPath, sizeof(csvPath), "%s/%s.csv", sensorFolder, dateStr);
                        bool got = false;
                        int guard = 0;
                        if (hasSdFolder) {
                            File f = SD_MMC.open(csvPath, FILE_READ);
                            if (f) {
                                got = true;
                                while (f.available() && guard++ < 3000) {
                                    String ln = f.readStringUntil('\n');
                                    if (ln.length() > 0 && ln.length() < 512) {
                                        sensorDayParserFeed(&par, std::string(ln.c_str()));
                                    }
                                }
                                f.close();
                            }
                        }
#if ENABLE_FILESYSTEM==1
                        if ((!got || !sensorDayParserHasData(&par)) && hasFlashFolder) {
                            // Serialized under lfsMutex (same crash guard):
                            // skip the summary (legacy body) on contention.
                            extern bool lfsMutexLock(TickType_t timeoutTicks);
                            extern void lfsMutexUnlock();
                            bool sdLfsHeld = lfsMutexLock(pdMS_TO_TICKS(5000));
                            if (sdLfsHeld) {
                            File f = getLittleFS().open(csvPath, "r");
                            if (f) {
                                got = true;
                                guard = 0;
                                sensorDayParserInit(&par);
                                while (f.available() && guard++ < 3000) {
                                    String ln = f.readStringUntil('\n');
                                    if (ln.length() > 0 && ln.length() < 512) {
                                        sensorDayParserFeed(&par, std::string(ln.c_str()));
                                    }
                                }
                                f.close();
                            }
                            lfsMutexUnlock();
                            }
                        }
#endif
                        if (got) {
                            std::string b = sensorDayParserBody(&par, std::string(dateStr));
                            if (!b.empty()) prefix = String(b.c_str());
                        }
                    }
                    String body    = prefix + buildEmailInfoTable(
                                     L(activeLang->emailDataAttached),
                                     "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                    sendVirtualMessage(buildMessage("❌ SENSORDAY: Sending folder %s...", sensorFolder), INFO);
                    emailDayCmdExtra = configManager.getSensordayExtraEmails();
                    if (!startEmailDayAsync(String(sensorFolder), subject, body)) {
                        sendVirtualMessage(F("❌ SENSORDAY: Failed to start email task"), ERROR);
                    }
                }
            }
        }
        #else
        // SD not compiled: export today's CSV from RAM ring buffer, send inline in email body.
        {
            time_t t = time(nullptr);
            if (t < 1000000000L) {
                sendVirtualMessage(F("❌ SENSORDAY: NTP not synchronized"), ERROR);
            } else {
                struct tm ti; localtime_r(&t, &ti);
                char dateStr[16];
                snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
                         ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
                String sysName = configManager.getSystemName();
                String csvData = sensorConfigManager.getHistory().exportDayCSV(String(dateStr), sensorConfigManager.getConfig());
                if (csvData.length() > 0) {
                    String subject = String(F("\xF0\x9F\x8C\xA1\xEF\xB8\x8F SensorDay ")) + sysName + String(F(" ")) + dateStr + String(F(" [RAM]"));
                    String recipients = configManager.getEmailDestination();
                    String sdExtra = configManager.getSensordayExtraEmails();
                    if (sdExtra.length() > 0) recipients = recipients.length() > 0 ? recipients + "," + sdExtra : sdExtra;
                    sendVirtualMessage(buildMessage("❌ SENSORDAY: RAM buffer -> %u lines, sending...", (unsigned)csvData.length()), INFO);
                    // T23: resumo estilo ShiftEnd antes do despejo inline.
                    String prefix2 = "";
                    if (csvData.length() < 60000) {
                        std::string b2 = sensorDayBuildFromCsv(std::string(csvData.c_str()),
                                                               std::string(dateStr));
                        if (!b2.empty()) prefix2 = String(b2.c_str());
                    }
                    startInactivityAlertAsync(
                        recipients,
                        subject,
                        prefix2 + csvData + buildEmailInfoTable(
                            L(activeLang->emailDataInBody),
                            "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", ""));
                } else {
                    sendVirtualMessage(buildMessage("❌ SENSORDAY: No data in RAM buffer for %s", dateStr), ERROR);
                }
            }
        }
        #endif
        #else
        sendVirtualMessage(F("❌ SENSORDAY: sensor history module not compiled"), ERROR);
        #endif
    }
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL
    // =========================
    // Quick command: RS232 <TEMPLATE> <COMMAND>
    // =========================
#if ENABLE_RS232==1 || ENABLE_SNIFFER==1
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232LIST")))) { // List RS232 templates
        // Get current templates array
        JsonArray arr = configManager.getRs232Templates(); // Template list
        String current = configManager.getRs232CurrentTemplate(); // Active template (volatile/persisted)
        if(arr.size()==0){ // No templates
            help_emitRs232NoTemplates(helpEmitWrapper); // Emit empty notice
        } else {
            help_emitRs232TemplatesHeader(helpEmitWrapper); // Header
            for(size_t i=0;i<arr.size();++i){ // Iterate
                JsonObject o = arr[i]; // Template object
                if(!o.containsKey("name")) continue; // Skip unnamed
                String n = o["name"].as<String>(); // Name
                String line = String(F(" - ")) + n; // Base line
                if(current.length() && n == current) line += String(F(" *")); // Mark active
                if(o.containsKey("baudrate")) line += String(F(" baud=")) + String(o["baudrate"].as<int>()); // Explicit baud
                else if(o.containsKey("baudrate_index")) line += String(F(" idx=")) + String(o["baudrate_index"].as<int>()); // Index
                if(o.containsKey("data_bits")) line += String(F(" ")) + String(o["data_bits"].as<int>()) + String(F("D")); // Data bits
                if(o.containsKey("parity")) line += String(o["parity"].as<const char*>()); // Parity
                if(o.containsKey("stop_bits")) line += String(o["stop_bits"].as<int>()) + String(F("S")); // Stop bits
                sendVirtualMessage(line, COMMAND_RESPONSE); // Send line
            }
            help_emitRs232UsageMixed(helpEmitWrapper); // Extra help
        }
    }
    else if (upperCaseCommand.startsWith(String(F("RS232 ")))) { // Detect RS232 prefix (send with or without template)
        // Extract after initial space
        String rest = commandLine.substring(6); // Part after "RS232 " preserving case
        rest.trim(); // Trim surrounding spaces
        int sp = rest.indexOf(' '); // First space separates possible template from command
        if(sp <= 0){ //  Caso n?o exista espa?o: tratamos rest como COMANDO sem template
            String cmdText = rest; //  Comando inteiro
            if(cmdText.length()==0){ //  Vazio
                help_emitRs232UsageEither(helpEmitWrapper); //  Ajuda
            } else {
                //  Determinar template a usar: current_template se existir sen?o procurar "DEFAULT"
                String tpl = configManager.getRs232CurrentTemplate(); //  Atual
                if(tpl.length()==0){ //  Se n?o h? atual procurar DEFAULT
                    JsonArray arr = configManager.getRs232Templates(); //  Templates
                    for(size_t i=0;i<arr.size();++i){ //  Loop
                        JsonObject o = arr[i]; //  Objeto
                        if(o.containsKey("name") && String(o["name"].as<String>()).equalsIgnoreCase(String(F("DEFAULT")))){ //  Encontrou DEFAULT
                            tpl = o["name"].as<String>(); // ? Usa nome real (preserva caixa)
                            break; //  Sai
                        }
                    }
                }
                bool ok = enqueueRS232Command(cmdText, tpl, String("MANUAL"), String("rs232")); //  Enfileirar com template possivelmente vazio
                if(ok){ // ? Sucesso
                    char line[120];
                    if(tpl.length()) snprintf(line, sizeof(line), "\xF0\x9F\x93\xA4 RS232 enq (auto tpl:%s): %s", tpl.c_str(), cmdText.c_str());
                    else snprintf(line, sizeof(line), "\xF0\x9F\x93\xA4 RS232 enq: %s", cmdText.c_str());
                    sendVirtualMessage(String(line), COMMAND_RESPONSE);
                } else { // ? Falha fila
                    sendVirtualMessage(String(F("RS232 queue full or enqueue error.")), WARN); // Aviso
                }
            }
        } else { //  Existe espa?o: formato <TEMPLATE> <COMANDO>
            String templateName = rest.substring(0, sp); //  Nome template
            String cmdText = rest.substring(sp+1); //  Texto comando
            templateName.trim(); cmdText.trim(); //  Limpa
            if(templateName.length()==0 || cmdText.length()==0){ //  Campos vazios
                help_emitRs232UsageTemplateOnly(helpEmitWrapper); //  Ajuda
            } else {
                bool ok = enqueueRS232Command(cmdText, templateName, String("MANUAL"), String("rs232")); //  Enfileira expl?cito
                if(ok){ // ? Sucesso
                    char line[120];
                    snprintf(line, sizeof(line), "\xF0\x9F\x93\xA4 RS232 enq (tpl:%s): %s", templateName.c_str(), cmdText.c_str());
                    sendVirtualMessage(String(line), COMMAND_RESPONSE);
                } else { // ? Fila cheia
                    sendVirtualMessage(String(F("RS232 queue full or enqueue error.")), WARN); // Aviso
                }
            }
        }
    }
#endif // ENABLE_RS232 || ENABLE_SNIFFER
    // =========================
    //  TELNET ON|OFF / TELNET?
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("TELNET?")))) { //  Estado Telnet
        {
            char s[32];
            snprintf(s, sizeof(s), "\xE2\x84\xB9\xEF\xB8\x8F TELNET=%s", configManager.getTelnetEnabled() ? "ON" : "OFF");
            sendVirtualMessage(String(s), COMMAND_RESPONSE);
        }
    }
    else if (upperCaseCommand.startsWith(String(F("TELNET ")))) { //  Comando TELNET toggle
        String arg = upperCaseCommand.substring(7); //  Extrai argumento
        arg.trim(); //  Limpa
#if ENABLE_TELNET==1
        if(arg == String(F("ON"))) { // ? Ativar
            if(!configManager.getTelnetEnabled()) { //  S? se estava OFF
                configManager.setTelnetEnabled(true); //  Persistir enable
                if(startTelnetServer()) sendVirtualMessage(F("TELNET: ON"), COMMAND_RESPONSE); // Confirmacao
                else sendVirtualMessage(F("TELNET: pending (no active network transport)"), COMMAND_RESPONSE);
            } else sendVirtualMessage(F("TELNET: already ON"), COMMAND_RESPONSE); // Ja ativo
        } else if(arg == String(F("OFF"))) { // ? Desativar
            if(configManager.getTelnetEnabled()) { //  S? se estava ON
                configManager.setTelnetEnabled(false); //  Persistir disable
                stopTelnetServer();
                sendVirtualMessage(F("TELNET: OFF"), COMMAND_RESPONSE);
            } else sendVirtualMessage(F("TELNET: already OFF"), COMMAND_RESPONSE); // Ja inativo
        } else {
            help_emitTelnetUsage(helpEmitWrapper); //  Ajuda
        }
#else
        sendVirtualMessage(F("TELNET: disabled at compile time"), COMMAND_RESPONSE); //  Telnet n?o compilado
#endif
    }
    // =========================
    //  WEBSERVER ON|OFF / WEBSERVER?
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("WEBSERVER?")))) { //  Estado webserver
        {
            char s[32];
            snprintf(s, sizeof(s), "\xE2\x84\xB9\xEF\xB8\x8F WEBSERVER=%s", configManager.getWebServerEnabled() ? "ON" : "OFF");
            sendVirtualMessage(String(s), COMMAND_RESPONSE);
        }
    }
    else if (upperCaseCommand.startsWith(String(F("WEBSERVER ")))) { //  Comando WEBSERVER toggle
        String arg = upperCaseCommand.substring(10); //  Extrai argumento
        arg.trim(); //  Limpa
        if(arg == String(F("ON"))) { // ? Ativar
#if ENABLE_WIFI_WEB_SERVER!=1
            sendVirtualMessage(F("WEBSERVER: unavailable in this build"), COMMAND_RESPONSE);
#else
            if(!configManager.getWebServerEnabled()) { //  S? se estava OFF
                configManager.setWebServerEnabled(true); //  Persistir
                setupConfigManagerEndpoints(); //  Reconfig rotas (seguro chamar mais que uma vez)
                setupWebServer(server); //  Garantir handlers
                server.begin(); //  Iniciar servidor
                sendVirtualMessage(F("WEBSERVER: ON"), COMMAND_RESPONSE); // Confirmacao
            } else sendVirtualMessage(F("WEBSERVER: already ON"), COMMAND_RESPONSE); // Ja ativo
#endif
        } else if(arg == String(F("OFF"))) { // ? Desativar
            if(configManager.getWebServerEnabled()) { //  S? se estava ON
                configManager.setWebServerEnabled(false); //  Persistir
                // ESPAsyncWebServer does not provide stop(); option: indicate OFF state and ignore requests (handlers may check flag)
                sendVirtualMessage(F("WEBSERVER: OFF (ignoring requests)"), COMMAND_RESPONSE); // Aviso curto
            } else sendVirtualMessage(F("WEBSERVER: already OFF"), COMMAND_RESPONSE); // Ja inativo
        } else {
            help_emitWebserverUsage(helpEmitWrapper); //  Ajuda
        }
    }
    // =========================
    //  LOG.COMPACT ON|OFF / LOG.COMPACT?
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("LOG.COMPACT?")))) { //  Estado do modo compacto global
        char s[32];
        snprintf(s, sizeof(s), "LOG.COMPACT=%s", compactLogMode ? "ON" : "OFF");
        sendVirtualMessage(String(s), COMMAND_RESPONSE);
    }
    else if (upperCaseCommand.startsWith(String(F("LOG.COMPACT ")))) { //  Alterna modo de logs compactos
        String arg = upperCaseCommand.substring(String("LOG.COMPACT ").length());
        arg.trim();
        if (arg == String(F("ON"))) {
            compactLogMode = true;            // ? Global: aplica a todas as sa?das
#if ENABLE_TELNET==1
            telnetCompactMode = true;         //  Sincroniza Telnet para comportamento consistente
#endif
            sendVirtualMessage(F("LOG.COMPACT: ON"), COMMAND_RESPONSE);
        } else if (arg == String(F("OFF"))) {
            compactLogMode = false;           // Global: disable compaction
#if ENABLE_TELNET==1
            telnetCompactMode = false;        //  Sincroniza Telnet
#endif
            sendVirtualMessage(F("LOG.COMPACT: OFF"), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(F("Usage: SYS: LOG.COMPACT ON|OFF | LOG.COMPACT?"), COMMAND_RESPONSE);
        }
    }
    // =========================
    //  HEARTBEAT ON|OFF|<interval> / HEARTBEAT?
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("HEARTBEAT?")))) {
        bool enabled = configManager.getHeartbeatEnabled();
        uint32_t interval = configManager.getHeartbeatInterval();
        char buf[64];
        snprintf(buf, sizeof(buf), "HEARTBEAT=%s interval=%lums", enabled ? "ON" : "OFF", (unsigned long)interval);
        sendVirtualMessage(String(buf), COMMAND_RESPONSE);
    }
    else if (upperCaseCommand.startsWith(String(F("HEARTBEAT ")))) {
        String arg = upperCaseCommand.substring(String("HEARTBEAT ").length());
        arg.trim();
        if (arg == String(F("ON"))) {
            configManager.setHeartbeatEnabled(true);
            sendVirtualMessage(F("HEARTBEAT: ON"), COMMAND_RESPONSE);
        } else if (arg == String(F("OFF"))) {
            configManager.setHeartbeatEnabled(false);
            sendVirtualMessage(F("HEARTBEAT: OFF"), COMMAND_RESPONSE);
        } else {
            int seconds = arg.toInt();
            if (seconds == 5 || seconds == 10 || seconds == 15 || seconds == 30 || seconds == 60) {
                uint32_t intervalMs = (uint32_t)seconds * 1000;
                configManager.setHeartbeatInterval(intervalMs);
                char buf[64];
                snprintf(buf, sizeof(buf), "HEARTBEAT: interval=%ds", seconds);
                sendVirtualMessage(String(buf), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(F("Usage: HEARTBEAT ON|OFF|5|10|15|30|60 | HEARTBEAT?"), COMMAND_RESPONSE);
            }
        }
    }
    // ...continua fluxo normal para outros comandos n?o-HELP
#if ENABLE_DB25==1
    else if (
        upperCaseCommand.equalsIgnoreCase(String(F("NANO.REBOOT"))) ||
        upperCaseCommand.equalsIgnoreCase(String(F("NANO_REBOOT"))) ||
        upperCaseCommand.equalsIgnoreCase(String(F("REBOOT_NANO"))) ||
        upperCaseCommand.equalsIgnoreCase(String(F("NANO REBOOT")))
    ) {
        //  Reboot solicitado ao Nano (ponte Centronics?UART)
        sendVirtualMessage(String(F("Sending reboot request to Nano...")), COMMAND_RESPONSE, /*alreadyFormatted=*/true, /*bypassDedup=*/true);
        nanoBridgeReboot();
        delay(50); // breve pausa para garantir transmiss?o do byte a 500k
        sendVirtualMessage(String(F("CMD sent (0xF5). Nano should reboot in ~15ms.")), COMMAND_RESPONSE, /*alreadyFormatted=*/true, /*bypassDedup=*/true);
    }
    // =========================
    //  NANO.ACK <us>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.ACK ")))) {
        String arg = commandLine.substring(String("NANO.ACK ").length()); arg.trim();
        if (arg.length()==0) {
            sendVirtualMessage(String(F("Usage: SYS: NANO.ACK <us> (e.g.: 8)")), COMMAND_RESPONSE);
        } else {
            int val = arg.toInt(); if (val < 1) val = 1; if (val > 255) val = 255;
            nanoBridgeSetAckWidth((uint8_t)val);
            sendVirtualMessage(String(F("NANO.ACK_WIDTH: ")) + String(val) + String(F(" us")), COMMAND_RESPONSE);
        }
    }
    // =========================
    //  NANO.STROBE.FILTER <us>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.STROBE.FILTER ")))) {
        String arg = commandLine.substring(String("NANO.STROBE.FILTER ").length()); arg.trim();
        if (arg.length()==0) {
            sendVirtualMessage(String(F("Usage: SYS: NANO.STROBE.FILTER <us> (e.g.: 2)")), COMMAND_RESPONSE);
        } else {
            int val = arg.toInt(); if (val < 0) val = 0; if (val > 255) val = 255;
            nanoBridgeSetStrobeMinLow((uint8_t)val);
            sendVirtualMessage(String(F("NANO.STROBE_FILTER: ")) + String(val) + String(F(" us")), COMMAND_RESPONSE);
        }
    }
    // =========================
    //  NANO.PAUSE <GRACE|IMMEDIATE|0|1>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.PAUSE ")))) {
        String arg = upperCaseCommand.substring(String("NANO.PAUSE ").length()); arg.trim();
        uint8_t mode = 1; // default graciosa
        if (arg == String(F("GRACE")) || arg == String(F("GRACIOSA")) || arg == String(F("1"))) mode = 1;
        else if (arg == String(F("IMMEDIATE")) || arg == String(F("IMEDIATA")) || arg == String(F("0"))) mode = 0;
        else { sendVirtualMessage(String(F("Usage: SYS: NANO.PAUSE GRACE|IMMEDIATE (or 1|0)")), COMMAND_RESPONSE); return; }
        nanoBridgeSetPauseMode(mode);
        sendVirtualMessage(String(F("NANO.PAUSE_MODE: ")) + (mode?String(F("GRACE")):String(F("IMMEDIATE"))), COMMAND_RESPONSE);
    }
    // =========================
    //  NANO.BUSY FORCE <ON|OFF|0|1>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.BUSY")))) {
        // Aceitar com ou sem a palavra FORCE
        String rest = upperCaseCommand.substring(String("NANO.BUSY").length()); rest.trim();
        if (rest.startsWith(String(F("FORCE")))) { rest = rest.substring(5); rest.trim(); }
        if (rest.length()==0) { sendVirtualMessage(String(F("Usage: SYS: NANO.BUSY FORCE ON|OFF")), COMMAND_RESPONSE); }
        else {
            bool on = (rest == String(F("ON")) || rest == String(F("1")));
            nanoBridgeForceBusy(on);
            sendVirtualMessage(String(F("NANO.BUSY_FORCE: ")) + (on?String(F("ON")):String(F("OFF"))), COMMAND_RESPONSE);
        }
    }
    // =========================
    //  NANO.XONXOFF <ON|OFF|0|1>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.XONXOFF ")))) {
        String arg = upperCaseCommand.substring(String("NANO.XONXOFF ").length()); arg.trim();
        if (arg.length()==0) {
            sendVirtualMessage(String(F("Usage: SYS: NANO.XONXOFF ON|OFF")), COMMAND_RESPONSE);
        } else {
            bool on = (arg == String(F("ON")) || arg == String(F("1")));
            nanoBridgeSetXonXoffEnable(on);
            sendVirtualMessage(String(F("NANO.XONXOFF_ENABLE: ")) + (on?String(F("ON")):String(F("OFF"))), COMMAND_RESPONSE);
        }
    }
    // =========================
    //  NANO.DEFAULTS ? Reaplicar defaults
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("NANO.DEFAULTS")))) {
        nanoBridgeSetAckWidth(8);
        nanoBridgeSetStrobeMinLow(2);
        nanoBridgeSetPauseMode(1);
        nanoBridgeForceBusy(false);
        nanoBridgeSetXonXoffEnable(true);
        sendVirtualMessage(String(F("Defaults applied: ACK=8us, STROBE_FILTER=2us, PAUSE=GRACE, BUSY_FORCE=OFF, XONXOFF=ON")), COMMAND_RESPONSE);
    }
    // =========================
    //  NANO.ACKDELAY <us>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.ACKDELAY ")))) {
        String arg = upperCaseCommand.substring(String("NANO.ACKDELAY ").length()); arg.trim();
        if (!arg.length()) sendVirtualMessage(String(F("Usage: SYS: NANO.ACKDELAY <us>")), COMMAND_RESPONSE);
        else { int v = arg.toInt(); if (v<0) v=0; if (v>255) v=255; nanoBridgeSetAckDelayBefore((uint8_t)v); sendVirtualMessage(String(F("NANO.ACK_DELAY_BEFORE: "))+String(v)+String(F(" us")), COMMAND_RESPONSE);}    
    }
    // =========================
    //  NANO.BUSY.TAIL <us>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.BUSY.TAIL ")))) {
        String arg = upperCaseCommand.substring(String("NANO.BUSY.TAIL ").length()); arg.trim();
        if (!arg.length()) sendVirtualMessage(String(F("Usage: SYS: NANO.BUSY.TAIL <us>")), COMMAND_RESPONSE);
        else { int v = arg.toInt(); if (v<0) v=0; if (v>255) v=255; nanoBridgeSetBusyTail((uint8_t)v); sendVirtualMessage(String(F("NANO.BUSY_TAIL: "))+String(v)+String(F(" us")), COMMAND_RESPONSE);}    
    }
    // (REMOVIDO) NANO.ERROR ? pino /ERROR deixou de ser controlado no modo m?nimo
    // =========================
    //  NANO.RESET ? reset de estado vol?til
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("NANO.RESET")))) {
        nanoBridgeResetState();
        sendVirtualMessage(String(F("NANO volatile state reset (BUSY/ACK/flags)")), COMMAND_RESPONSE);
    }
    // =========================
    //  NANO.PAUSE.GRACEBYTES <k>
    // =========================
    else if (upperCaseCommand.startsWith(String(F("NANO.PAUSE.GRACEBYTES ")))) {
        String arg = upperCaseCommand.substring(String("NANO.PAUSE.GRACEBYTES ").length()); arg.trim();
        if (!arg.length()) sendVirtualMessage(String(F("Usage: SYS: NANO.PAUSE.GRACEBYTES <k> (0..255)")), COMMAND_RESPONSE);
        else { int k = arg.toInt(); if (k<0) k=0; if (k>255) k=255; nanoBridgeSetPauseGraceBytes((uint8_t)k); sendVirtualMessage(String(F("NANO.PAUSE_GRACE_BYTES: "))+String(k), COMMAND_RESPONSE);}    
    }
#endif // ENABLE_DB25
    // =========================
    // CONFIG RESTORE COMMAND (DEPRECATED - dual-storage mode)
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("CONFIG RESTORE")))) {
        sendVirtualMessage(F("⚠️ SYSTEM: CONFIG RESTORE deprecated in dual-storage mode"), INFO);
        sendVirtualMessage(F("💡 Config auto-saved to NVS (WiFi/Email) + LittleFS (rest)"), INFO);
        sendVirtualMessage(F("💡 Use 'CONFIG SAVE' to force immediate save"), INFO);
    }
    // =========================
    // DEBUG_WRITER commands (accepts normalized variants: DEBUG_WRITER, DEBUG WRITER, DEBUGWRITER)
    // =========================
    else if (upperCaseCommand.startsWith(String(F("DEBUG"))) && (upperCaseCommand.indexOf(String(F("WRITER"))) >= 0)) {
        // Normalize for easier matching 
        String normalizedCmd = upperCaseCommand;
        normalizedCmd.replace("_", " ");
        while(normalizedCmd.indexOf("  ") >= 0) normalizedCmd.replace("  ", " ");
        
        // Handle "DEBUGWRITER" concatenated case
        if (normalizedCmd.startsWith(String(F("DEBUGWRITER")))) {
             // Split it to "DEBUG WRITER"
             normalizedCmd = "DEBUG WRITER" + normalizedCmd.substring(11);
        }

        if (normalizedCmd.startsWith(String(F("DEBUG WRITER ON")))) {
             debugWriterSetEnabled(true);
             enableDebugLogs = true; // ✅ AUTO-SYNC: Enable debug logs when debug_writer is ON
             sendVirtualMessage(String(F("🟢 DEBUG_WRITER: ENABLED (persisted, saves to /DEBUG/YYYY-MM-DD.txt)")), COMMAND_RESPONSE);
             sendVirtualMessage(String(F("✅ Debug logs auto-activated (all messages visible including DEBUG level)")), INFO);
        }
        else if (normalizedCmd.startsWith(String(F("DEBUG WRITER OFF")))) {
             debugWriterSetEnabled(false);
             // Note: enableDebugLogs NOT auto-disabled (user may want to keep viewing debug logs)
             sendVirtualMessage(String(F("🔴 DEBUG_WRITER: DISABLED (persisted, survives reboot)")), COMMAND_RESPONSE);
             sendVirtualMessage(String(F("ℹ️ Debug logs remain at current state (use /api/config to change)")), INFO);
        }
        else if (normalizedCmd.equalsIgnoreCase(String(F("DEBUG WRITER STATUS"))) || 
                 normalizedCmd.equalsIgnoreCase(String(F("DEBUG WRITER")))) {
             String status = debugWriterGetStatus();
             sendVirtualMessage(status, COMMAND_RESPONSE);
             // Show debug logs state (related but independent)
             sendVirtualMessage(String(F("🔍 Debug logs (runtime): ")) + 
                                  (enableDebugLogs ? String(F("ON ✅")) : String(F("OFF ❌"))), INFO);
        }
        else if (normalizedCmd.equalsIgnoreCase(String(F("DEBUG WRITER NOW")))) {
            bool ok = debugWriterFlush();
            if (ok) {
                sendVirtualMessage(String(F("✅ DEBUG_WRITER: Flush completed")), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(String(F("⚠️ DEBUG_WRITER: Flush failed (disabled or SD busy)")), WARN);
            }
        }
        else if (upperCaseCommand.startsWith(String(F("DEBUG_WRITER_FLUSH "))) || normalizedCmd.startsWith(String(F("DEBUG WRITER FLUSH ")))) {
            String arg = "";
            if(commandLine.indexOf("FLUSH ") > 0) arg = commandLine.substring(commandLine.indexOf("FLUSH ") + 6);
            else if(commandLine.indexOf("flush ") > 0) arg = commandLine.substring(commandLine.indexOf("flush ") + 6);
            
            arg.trim();
            if (arg.length()) {
                unsigned long seconds = arg.toInt();
                debugWriterSetFlushInterval(seconds);
                sendVirtualMessage(String(F("⏱️  DEBUG_WRITER: Flush interval set to ")) + String(seconds) + String(F("s")), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(String(F("Use: DEBUG_WRITER_FLUSH <seconds> (10-3600)")), COMMAND_RESPONSE);
            }
        }
        else {
             // Fall through if matched "DEBUG WRITER" but no subcommand (unless "DEBUG WRITER" alone handled above)
             // Check if it was just "DEBUG WRITER" which is handled by STATUS check above.
             // If we are here, it's an unknown subcommand like "DEBUG WRITER FOO"
             sendVirtualMessage(String(F("Unknown DEBUG_WRITER command. Use: ON|OFF|STATUS|NOW|FLUSH")), WARN);
        }
    }
    // =========================
    // CONFIG_STATS command - diagnose save loop issues
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("CONFIG_STATS"))) ||
             upperCaseCommand.equalsIgnoreCase(String(F("CONFIG STATS")))) {
        String stats = String(F("📊 ConfigManager Statistics:\n"));
        stats += String(F("  isDirty: ")) + String(configManager.isDirty ? "YES (pending save)" : "NO") + String(F("\n"));
        stats += String(F("  Last save: ")) + String((millis() - configManager.lastSave) / 1000) + String(F("s ago\n"));
        stats += String(F("  Successful saves: ")) + String(configManager.successfulSaveCount) + String(F("\n"));
        stats += String(F("  Throttled attempts: ")) + String(configManager.saveThrottleCount) + String(F("\n"));
        
        if (configManager.saveThrottleCount > 100) {
            stats += String(F("  ⚠️ WARNING: High throttle count indicates save loop bug!\n"));
            stats += String(F("  Check: WiFi reconnect, network code, web handlers\n"));
        }
        
        sendVirtualMessage(stats, COMMAND_RESPONSE);
    }
    // =========================
    // SCAN_WIFI command - scan WiFi networks for USB configurator
    // =========================
    else if (upperCaseCommand.equalsIgnoreCase(String(F("SCAN_WIFI")))) {
        sendVirtualMessage(String(F("🔍 Scanning WiFi networks...")), COMMAND_RESPONSE);
        
        // Use cached async scan (force new scan for configurator - cache must be fresh)
        unsigned long currentTime = millis();
        int networksFound = performCachedWiFiScan((currentTime - lastScanTime) > 10000); // Force if >10s old
        
        // Wait for async scan to complete if needed
        int waitCount = 0;
        while (networksFound == 0 && waitCount < 100) { // Max 10s wait
            delay(100);
            networksFound = performCachedWiFiScan();
            waitCount++;
        }
        
        if (networksFound == 0) {
            sendVirtualMessage(String(F("⚠️ No networks found")), COMMAND_RESPONSE);
        } else if (networksFound > 0) {
            for (int i = 0; i < networksFound; i++) {
                String ssid;
                int32_t rssi;
                wifi_auth_mode_t encryption;
                
                if (!getCachedScanResult(i, ssid, rssi, encryption)) {
                    continue; // Skip if cache read failed
                }
                
                // Format: WIFI_SCAN:SSID,RSSI,ENCRYPTION
                String result = String(F("WIFI_SCAN:"));
                result += ssid;
                result += String(F(","));
                result += String(rssi);
                result += String(F(","));
                result += (encryption == WIFI_AUTH_OPEN) ? String(F("0")) : String(F("1"));
                
                Serial.println(result); // Send directly to Serial (USB CDC)
            }
            
            sendVirtualMessage(String(F("WIFI_SCAN_COMPLETE")), COMMAND_RESPONSE);
            
            String summary = String(F("✅ Found "));
            summary += String(networksFound);
            summary += String(F(" network(s)"));
            sendVirtualMessage(summary, COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(String(F("❌ WiFi scan error")), ERROR);
        }
        
        WiFi.scanDelete(); // Free scan results memory
    }
    // =========================
    //  Tentar processar como comando de sistema (commands.h)
    // =========================
    else if (processSystemCommand(commandLine, upperCaseCommand)) {
        // ? Comando processado por commands.h
    }
    //  Comando DB25? removido (sem MCP)
    else if (upperCaseCommand.startsWith(String(F("SET_ALL_CONFIGS:")))) {
        int startIndex = commandLine.indexOf(configStartDelimiter);
        int endIndex = commandLine.indexOf(configEndDelimiter);

        if (startIndex != -1 && endIndex != -1 && endIndex > startIndex) {
            String jsonStr = commandLine.substring(startIndex + configStartDelimiter.length(), endIndex);

            SpiRamJsonDocument logDoc(4096);
            DeserializationError logError = deserializeJson(logDoc, jsonStr);
            if (!logError) {
                logDoc["pass"] = "********";
                logDoc["smtpPass"] = "********";
                logDoc["telnetPass"] = "********";
                String logJsonStr;
                serializeJson(logDoc, logJsonStr);
                String debugPrefsMsg = String(F("DEBUG_PREFS: JSON saved (masked): '"));
                debugPrefsMsg += logJsonStr;
                debugPrefsMsg += String(F("'"));
                sendVirtualMessage(debugPrefsMsg, DEBUG);
            } else {
                String debugJsonExtracted = String(F("JSON extracted with delimiters: '"));
                debugJsonExtracted += jsonStr;
                debugJsonExtracted += String(F("'"));
                sendVirtualMessage(debugJsonExtracted, DEBUG);
            }

            SpiRamJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, jsonStr);

            if (!error) {
                if (doc.containsKey("system_name")) {
                    String newSystemName = doc["system_name"].as<String>();
                    if (newSystemName.length() > 0) {
                        configManager.setSystemName(newSystemName);
                        // No need to send message here, saveAllConfigs logic might act
                    }
                }
                if (doc.containsKey("ssid")) {
                    String newSsid = doc["ssid"].as<String>();
                    if (newSsid.length() > 0) {
                        ssid = newSsid;
                        configManager.setWifiSSID(ssid); // Sync ConfigManager
                        String ssidDefinedMsg = String(F("SSID set to: '"));
                        ssidDefinedMsg += ssid;
                        ssidDefinedMsg += String(F("'"));
                        sendVirtualMessage(ssidDefinedMsg, DEBUG);
                    } else {
                        // ...
                    }
                }
                if (doc.containsKey("pass")) {
                    String newPass = doc["pass"].as<String>();
                    if (newPass.length() > 0) {
                        password = newPass;
                        configManager.setWifiPassword(password); // Sync ConfigManager
                        sendVirtualMessage(String(F("WiFi password updated (not logged).")), DEBUG);
                    } else {
                        // ...
                    }
                }
                if (doc.containsKey("smtpServer")) {
                    String newSmtpServer = doc["smtpServer"].as<String>();
                    if (newSmtpServer.length() > 0) smtpServer = newSmtpServer;
                }
                if (doc.containsKey("smtpPort")) {
                    String newSmtpPort = doc["smtpPort"].as<String>();
                    if (newSmtpPort.length() > 0) smtpPort = newSmtpPort;
                }
                if (doc.containsKey("smtpUser")) {
                    String newSmtpUser = doc["smtpUser"].as<String>();
                    if (newSmtpUser.length() > 0) smtpUser = newSmtpUser;
                }
                if (doc.containsKey("smtpPass")) {
                    String newSmtpPass = doc["smtpPass"].as<String>();
                    if (newSmtpPass.length() > 0) {
                        smtpPass = newSmtpPass;
                        sendVirtualMessage(String(F("SMTP password updated (not logged).")), DEBUG);
                    } else {
                        sendVirtualMessage(String(F("SMTP password in JSON empty, keeping current value.")), DEBUG);
                    }
                }
                if (doc.containsKey("emailDest")) {
                    String newEmailDest = doc["emailDest"].as<String>();
                    if (newEmailDest.length() > 0) emailDest = newEmailDest;
                }
                bool oldAutoSendEmail = autoSendEmail;
                if (doc.containsKey("autoSendEmail")) {
                    autoSendEmail = doc["autoSendEmail"].as<bool>();
                    // Keep ConfigManager in sync so the web UI and persistent config agree
                    configManager.setEmailAutoSend(autoSendEmail);
                    String autoSendEmailMsg = String(F("Estado de autoSendEmail definido para: "));
                    autoSendEmailMsg += String(autoSendEmail ? "ENABLED" : "DISABLED");
                    sendVirtualMessage(autoSendEmailMsg, COMMAND_RESPONSE);
                    if (oldAutoSendEmail != autoSendEmail) {
                        clearPrinterDataBufferAndNotifyUI(); // Use the new function
                        sendVirtualMessage(String(F("Data buffer cleared due to autoSendEmail change.")), DEBUG);
                        lastReceptionTime = currentMillis;
                    }
                }
                if (doc.containsKey("currentMaxEmailLines")) {
                    int newLimit = doc["currentMaxEmailLines"].as<int>();
                    if (newLimit == MAX_LINES_6_LPI || newLimit == MAX_LINES_8_LPI) {
                        configManager.setEmailMaxLines(newLimit);
                        String limitMsg = String(F("Email line limit set to: "));
                        limitMsg += String(currentMaxEmailLines);
                        limitMsg += String(F(" lines."));
                        sendVirtualMessage(limitMsg, COMMAND_RESPONSE);
                    } else {
                        String limitInvalidMsg = String(F("Invalid line limit in JSON: "));
                        limitInvalidMsg += String(newLimit);
                        limitInvalidMsg += String(F(". Mantendo o valor atual: "));
                        limitInvalidMsg += String(currentMaxEmailLines);
                        // removido sufixo de aviso
                        sendVirtualMessage(limitInvalidMsg, WARN);
                    }
                }
                if (doc.containsKey("telnetUser")) {
#if ENABLE_TELNET==1
                    String newUser = doc["telnetUser"].as<String>(); //  Novo username
                    if (newUser.length() > 0) telnetUser = newUser; // ? Atualiza se n?o vazio
#endif
                }
                if (doc.containsKey("telnetPass")) {
#if ENABLE_TELNET==1
                    String newPass = doc["telnetPass"].as<String>(); //  Nova password
                    if (newPass.length() > 0) telnetPass = newPass; // ? Atualiza se n?o vazio
#endif
                }
                if (doc.containsKey("charsetMode")) {
                    int newCharsetMode = doc["charsetMode"].as<int>();
                    if (newCharsetMode >= 0 && newCharsetMode <= 2) {
                        charsetMode = newCharsetMode;
                        configManager.setPrinterCharsetMode(newCharsetMode);
                        String charsetMsg = String(F("Charset set to: "));
                        if (charsetMode == 0) charsetMsg += String(F("AUTO (UTF-8)"));
                        else if (charsetMode == 1) charsetMsg += String(F("LATIN1"));
                        else if (charsetMode == 2) charsetMsg += String(F("CP850"));
                        charsetMsg += String(F("."));
                        sendVirtualMessage(charsetMsg, COMMAND_RESPONSE);
                    } else {
                        String charsetInvalidMsg = String(F("Invalid charset in JSON: "));
                        charsetInvalidMsg += String(newCharsetMode);
                        charsetInvalidMsg += String(F(". Mantendo o valor atual: "));
                        charsetInvalidMsg += String(charsetMode);
                        // removido sufixo de aviso
                        sendVirtualMessage(charsetInvalidMsg, WARN);
                    }
                }
                // Atualiza printerEmulationMode se presente no JSON
                if (doc.containsKey("printerEmulationMode")) {
                    int newEmuMode = doc["printerEmulationMode"].as<int>();
                    if (newEmuMode >= 0 && newEmuMode <= 2) {
                        printerEmulationMode = newEmuMode;
                        configManager.setPrinterEmulationMode(newEmuMode);
                        String emuMsg = String(F("Emulation mode set to: "));
                        emuMsg += String(printerEmulationMode);
                        emuMsg += String(F("."));
                        sendVirtualMessage(emuMsg, COMMAND_RESPONSE);
                    } else {
                        String emuInvalidMsg = String(F("Invalid emulation mode in JSON: "));
                        emuInvalidMsg += String(newEmuMode);
                        emuInvalidMsg += String(F(". Mantendo o valor atual: "));
                        emuInvalidMsg += String(printerEmulationMode);
                        // removido sufixo de aviso
                        sendVirtualMessage(emuInvalidMsg, WARN);
                    }
                }
                if (doc.containsKey("autoEmailEnabledTimeout")) {
                    autoEmailEnabledInactivityTimeout = doc["autoEmailEnabledTimeout"].as<int>();
                }
                if (doc.containsKey("autoEmailDisabledTimeout")) {
                    autoEmailDisabledInactivityTimeout = doc["autoEmailDisabledTimeout"].as<int>();
                    autoBufferClearInactivityTimeout = autoEmailDisabledInactivityTimeout + 1000;
                }
                
                // Campos adicionais que estavam faltando
                if (doc.containsKey("minEmailContentLength")) {
                    int newMinLength = doc["minEmailContentLength"].as<int>();
                    if (newMinLength >= 0 && newMinLength <= 100) {
                        minEmailContentLength = newMinLength;
                        sendVirtualMessage(String(F("Minimum email length: ")) + String(minEmailContentLength), COMMAND_RESPONSE);
                    }
                }
                
                if (doc.containsKey("ntpServer")) {
                    String newNtpServer = doc["ntpServer"].as<String>();
                    if (newNtpServer.length() > 0) {
                        ntpServer = newNtpServer;
                        atualizarNTPTimezone(); // Aplica a mudan?a imediatamente
                        sendVirtualMessage(String(F("NTP server updated: ")) + ntpServer, COMMAND_RESPONSE);
                    }
                }
                
                if (doc.containsKey("timezone")) {
                    String newTimezone = doc["timezone"].as<String>();
                    if (newTimezone.length() > 0) {
                        timezone = newTimezone;
                        setenv("TZ", timezone.c_str(), 1);
                        tzset();
                        sendVirtualMessage(String(F("Timezone updated: ")) + timezone, COMMAND_RESPONSE);
                    }
                }
                
                if (doc.containsKey("enableDebugLogs")) {
                    enableDebugLogs = doc["enableDebugLogs"].as<bool>();
                    sendVirtualMessage(String(F("Debug logs: ")) + String(enableDebugLogs ? "ENABLED" : "DISABLED"), COMMAND_RESPONSE);
                }
                
#if ENABLE_RS232==1
                // RS232 configs
                if (doc.containsKey("rs232_enabled")) {
                    rs232_enabled = doc["rs232_enabled"].as<bool>();
                    sendVirtualMessage(String(F("RS232: ")) + String(rs232_enabled ? "ENABLED" : "DISABLED"), COMMAND_RESPONSE);
                }
                
                if (doc.containsKey("rs232_invert_pins")) {
                    rs232_invert_pins = doc["rs232_invert_pins"].as<bool>();
                    sendVirtualMessage(String(F("RS232 pin inversion: ")) + String(rs232_invert_pins ? "YES" : "NO"), COMMAND_RESPONSE);
                }
                
                if (doc.containsKey("rs232_baudrate_index")) {
                    int newBaudrateIndex = doc["rs232_baudrate_index"].as<int>();
                    if (newBaudrateIndex >= 0 && newBaudrateIndex < RS232_BAUDRATES_COUNT) {
                        setRS232BaudrateByIndex(newBaudrateIndex);
                        sendVirtualMessage(String(F("RS232 baud: ")) + String(RS232_BAUDRATES[newBaudrateIndex]), COMMAND_RESPONSE);
                    }
                }
#endif // ENABLE_RS232

                if (doc.containsKey("recipients") && doc["recipients"].is<JsonObject>()) {
                    configManager.clearAllRecipients();
                    JsonObject destObj = doc["recipients"].as<JsonObject>();
                    for (JsonPair kv : destObj) {
                        String key = String(kv.key().c_str());
                        String value = kv.value().as<String>();
                        configManager.addRecipient(key, value);
                    }
                    sendVirtualMessage(F("Recipients updated via SET_ALL_CONFIGS (ConfigManager)."), COMMAND_RESPONSE);
                }
                configManager.save(true);
                // Include current IP in success message so the USB configurator can capture it immediately.
                if (hasNetworkConnectivity()) {
                    String okMsg = String(F("All configurations updated. IP:"));
                    okMsg += getCurrentNetworkIPString();
                    sendVirtualMessage(okMsg, COMMAND_RESPONSE);
                } else {
                    sendVirtualMessage(String(F("All configurations updated.")), COMMAND_RESPONSE);
                }
                if (ssid.length() > 0 && password.length() > 0) {
                    // Register new credentials in saved profiles so the hybrid manager uses them.
                    addOrUpdateNetwork(ssid, password);
#if ENABLE_WIFI==1
                    // Only start a new WiFi.begin() if WiFi is not already connected with a valid IP.
                    bool wifiAlreadyUp = (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0));
                    if (!wifiAlreadyUp) {
                        sendVirtualMessage(String(F("WiFi: connecting after SET_ALL_CONFIGS.")), COMMAND_RESPONSE);
                        if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
                            WiFi.mode(WIFI_STA);
                            delay(100);
                        }
                        WiFi.begin(ssid.c_str(), password.c_str());
                        wifiConnectStartTime = currentMillis;
                        currentWifiConnectionState = WIFI_CONNECTING;
                    }
#endif
                }
            } else {
                String errorMsg = String(F("Error processing SET_ALL_CONFIGS JSON: "));
                errorMsg += String(error.c_str());
                sendVirtualMessage(errorMsg, ERROR);
            }
        } else {
            sendVirtualMessage(String(F("SET_ALL_CONFIGS received but JSON not encapsulated or malformed.")), ERROR);
        }
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("GET_FIRMWARE_VERSION")))) {
        enviarTodasConfiguracoesParaUI(); // Agora envia todas as configs, incluindo firmware version
    }
    else if (upperCaseCommand.startsWith(String(F("SEND_EMAIL:")))) {
        int startIndex = commandLine.indexOf(String(F("###DATA_START###")));
        int endIndex = commandLine.indexOf(String(F("###DATA_END###")));

        if (startIndex != -1 && endIndex != -1 && endIndex > startIndex) {
            if (isEmailConfiguredAndWifiConnected()) {
                manualEmailContentBuffer = commandLine.substring(startIndex + String(F("###DATA_START###")).length(), endIndex);
                manualEmailSendRequested = true;
                sendVirtualMessage(String(F("Manual send request received. Will be processed shortly.")), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(String(F("Cannot send manual email: WiFi not connected or email settings incomplete.")), WARN);
            }
        } else {
            sendVirtualMessage(String(F("SEND_EMAIL received but data not encapsulated or malformed.")), ERROR);
        }
    }
    else if (upperCaseCommand.startsWith(String(F("SET_RS232_ENABLE:")))) {
#if ENABLE_RS232==1
        String enableStr = commandLine.substring(String(F("SET_RS232_ENABLE:")).length());
        enableStr.trim();
        int enableValue = enableStr.toInt();
        
        bool newRs232Enabled = (enableValue == 1);
        rs232_enabled = newRs232Enabled;
        
        if (rs232_enabled) {
            // Inverter pinos se configurado
            if (rs232_invert_pins) {
                rs232_rx_pin = RS232_DEFAULT_TX_PIN;
                rs232_tx_pin = RS232_DEFAULT_RX_PIN;
            } else {
                rs232_rx_pin = RS232_DEFAULT_RX_PIN;
                rs232_tx_pin = RS232_DEFAULT_TX_PIN;
            }
            setRS232BaudrateByIndex(rs232_baudrate_index);
            // Apply full configuration (baud/framing/pins)
            applyRS232Config();
            String rs232EnabledMsg = String(F("RS232 ENABLED. Baudrate: "));
            rs232EnabledMsg += String(rs232_baudrate);
            rs232EnabledMsg += String(F(", RX: "));
            rs232EnabledMsg += String(rs232_rx_pin);
            rs232EnabledMsg += String(F(", TX: "));
            rs232EnabledMsg += String(rs232_tx_pin);
            rs232EnabledMsg += String(F("."));
            sendVirtualMessage(rs232EnabledMsg, COMMAND_RESPONSE);
        } else {
            _rs232PortInitialized = false;
            RS232.end();
            sendVirtualMessage(String(F("RS232 DISABLED.")), COMMAND_RESPONSE);
        }
        
    configManager.setRs232Enabled(rs232_enabled);
#else
    sendVirtualMessage(String(F("RS232 disabled in build (ENABLE_RS232=0).")), WARN);
#endif
    }
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    else if (upperCaseCommand.startsWith("ADD_EMAIL_NAME:")) {
        String params = commandLine.substring(String(F("ADD_EMAIL_NAME:")).length());
        int commaIdx = params.indexOf(',');
        if (commaIdx > 0) {
            String nome = params.substring(0, commaIdx);
            String email = params.substring(commaIdx + 1);
            nome.trim(); email.trim();
            if (nome.length() > 0 && email.length() > 0) {
                configManager.addRecipient(nome, email);
                sendVirtualMessage(String(F("Recipient added (ConfigManager): ")) + nome + " -> " + email, COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(F("Invalid parameters for ADD_EMAIL_NAME. Use: ADD_EMAIL_NAME:name,email"), WARN);
            }
        } else {
            sendVirtualMessage(F("Invalid parameters for ADD_EMAIL_NAME. Use: ADD_EMAIL_NAME:name,email"), WARN);
        }
    }
    else if (upperCaseCommand.startsWith("REMOVE_EMAIL_NAME:")) {
        String nome = commandLine.substring(String(F("REMOVE_EMAIL_NAME:")).length());
        nome.trim();
        if (nome.length()) {
            configManager.removeRecipient(nome);
            sendVirtualMessage(String(F("Recipient removed: ")) + nome, COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(F("Invalid name for REMOVE_EMAIL_NAME"), WARN);
        }
    }
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL
    else if (upperCaseCommand.equalsIgnoreCase(String(F("CLEAR_PRINTER_DATA")))) {
        sendVirtualMessage(String(F("CLEAR_PRINTER_DATA received. Clearing printer data buffer.")), COMMAND_RESPONSE);
        clearPrinterDataBufferAndNotifyUI(); // Use the new function
        lastReceptionTime = currentMillis;
        emailProcessInitiatedThisCycle = false;
    safeEnviarMensagemVirtual("ST: init=0 (CLR)", DEBUG);
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("CLEAR_TERMINAL")))) {
        for (int i = 0; i < 50; i++) {
            // Agora, em vez de Serial.println(), adicionamos ao buffer de sa?da
            printerOutputBuffer.push('\n'); 
#if ENABLE_TELNET==1
            if (telnetClient && telnetClient.connected() && telnetAuthenticated) {
                telnetClient.println(); // Telnet ainda pode ter a sua pr?pria limpeza de ecr?
            }
#endif
        }
        lastSerialOutputWasNewline = true; // Ap?s limpar, a ?ltima sa?da ? uma nova linha
        sendVirtualMessage(String(F("Terminal cleared.")), COMMAND_RESPONSE);
    }
    //  Bloco antigo de comandos DB25 bidirecionais removido (modo MCP unidirecional apenas)
    // ====== CONTROLO RS232 MODO RAW/CMD ======
#if ENABLE_RS232==1
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232MODE RAW"))) || upperCaseCommand.equalsIgnoreCase(String(F("RS232RAW ON")))) {
        rs232_passthrough_raw = true;
    sendVirtualMessage(String(F("RS232: RAW ON")), INFO);
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232MODE CMD"))) || upperCaseCommand.equalsIgnoreCase(String(F("RS232RAW OFF")))) {
        rs232_passthrough_raw = false;
    sendVirtualMessage(String(F("RS232: CMD ON")), INFO);
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232MODE?")))) {
    sendVirtualMessage(String(F("RS232: mode=")) + (rs232_passthrough_raw ? String(F("RAW")) : String(F("CMD"))), INFO);
    }
    // Espelhamento RS232 -> USB
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232USB ON"))) || upperCaseCommand.equalsIgnoreCase(String(F("RS232MIRROR ON")))) {
        rs232_mirror_to_usb = true;
    sendVirtualMessage(String(F("RS232: USB mirror ON")), INFO);
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232USB OFF"))) || upperCaseCommand.equalsIgnoreCase(String(F("RS232MIRROR OFF")))) {
        rs232_mirror_to_usb = false;
    sendVirtualMessage(String(F("RS232: USB mirror OFF")), INFO);
    }
    else if (upperCaseCommand.equalsIgnoreCase(String(F("RS232USB?"))) || upperCaseCommand.equalsIgnoreCase(String(F("RS232MIRROR?")))) {
    sendVirtualMessage(String(F("RS232: USB mirror=")) + (rs232_mirror_to_usb ? String(F("ON")) : String(F("OFF"))), INFO);
    }
#endif // ENABLE_RS232
#if ENABLE_DB25==1
    // ====== SYS: NANO.* ? comandos locais de diagn?stico/for?a de sinais ======
    // (REMOVIDO) SYS: NANO.ERROR ? pino /ERROR deixou de ser controlado no modo m?nimo
    else if (upperCaseCommand.startsWith(String(F("SYS: NANO.FORCEBUSY")))) {
        // Formatos aceites: SYS: NANO.FORCEBUSY 1|0|ON|OFF
        String arg = commandLine.substring(commandLine.indexOf(' '));
        int sp = arg.lastIndexOf(' ');
        if (sp >= 0) arg = arg.substring(sp + 1);
        arg.trim(); arg.toUpperCase();
        bool on = (arg == String(F("1")) || arg == String(F("ON")) || arg == String(F("HIGH")));
        bool off= (arg == String(F("0")) || arg == String(F("OFF")) || arg == String(F("LOW")));
        if (on || off) {
            nanoBridgeForceBusy(on);
            sendVirtualMessage(String(F("NANO.FORCEBUSY ")) + (on ? String(F("ON")) : String(F("OFF"))), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(String(F("Usage: SYS: NANO.FORCEBUSY <ON|OFF|1|0>")), WARN);
        }
    }
    // ====== NANO.* ? variantes sem prefixo SYS: (porque o parser remove SYS: e reencaminha) ======
    // (REMOVIDO) NANO.ERROR ? pino /ERROR deixou de ser controlado no modo m?nimo
    else if (upperCaseCommand.startsWith(String(F("NANO.FORCEBUSY")))) {
        // Formatos: NANO.FORCEBUSY ON|OFF|1|0
        String arg;
        int sp = commandLine.indexOf(' ');
        if (sp >= 0) { arg = commandLine.substring(sp + 1); arg.trim(); }
        if (arg.length()==0){ sendVirtualMessage(String(F("Usage: NANO.FORCEBUSY <ON|OFF|1|0>")), WARN); }
        else {
            String a = arg; a.toUpperCase();
            bool on = (a == String(F("1")) || a == String(F("ON")) || a == String(F("HIGH")));
            bool off= (a == String(F("0")) || a == String(F("OFF")) || a == String(F("LOW")));
            if (on || off) {
                nanoBridgeForceBusy(on);
                sendVirtualMessage(String(F("NANO.FORCEBUSY ")) + (on ? String(F("ON")) : String(F("OFF"))), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(String(F("Usage: NANO.FORCEBUSY <ON|OFF|1|0>")), WARN);
            }
        }
    }
#endif // ENABLE_DB25
    else {
    String unknownCommandMsg = String(F("Unknown serial command: "));
        unknownCommandMsg += commandLine;
        sendVirtualMessage(unknownCommandMsg, WARN);
    }
}