// command_executor.cpp — System command implementation via CommandRegistry
// Provides processSystemCommand() and executeCommand() via registered handlers.
// All extern dependencies are declared in commands.h.

#include "commands.h"
#include "CommandRegistry.h"
#include "ConfigManager.h"
#include "languages/lang_active.h"
#include "log.h"

#include "feature_flags.h"
#if ENABLE_SCHEDULER==1 && (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
#include "day_report_fallback.h"
#endif
#if ENABLE_GRBL == 1
#include "grbl_sender.h"
#endif

#if ENABLE_WIFI == 1
#include "network_saved_profiles.h"
#endif

extern ConfigManager configManager;
#include "sd_layout.h"   // standard flat day-file paths (all packs)
#include "sensor_day_report.h"  // T23: ShiftEnd-style SensorDay body (pure, native-tested)
#include "flash_fallback_store.h" // no-SD LittleFS remap for day commands (stub when disabled)
extern volatile bool _rs232PortInitialized;
#if ENABLE_WIFI == 1
extern void syncGlobalVariables();
extern void startHybridWiFiConnection();
#endif

// =====================================================================
// Command handler implementations (static, one per command or alias group)
// =====================================================================

#if ENABLE_SCHEDULER==1 && (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
// Track manual day-report commands (slot 0) so the fallback journal can retry
// them ~20min later if the send fails or a reboot happens. No-op for other cmds.
static void dayReportTrackManual(const String& cmdLine) {
    if (dayReportIsAutoDispatch()) return;  // scheduler/retry re-entry: already journaled
    time_t nowT = time(nullptr);
    if (nowT <= 1000) return;
    struct tm ti;
    localtime_r(&nowT, &ti);
    int dayKey = (ti.tm_year + 1900) * 10000 + (ti.tm_mon + 1) * 100 + ti.tm_mday;
    dayReportBeginAttempt(0, cmdLine, dayKey, (long)nowT);
}
#else
static void dayReportTrackManual(const String&) {}
#endif

static bool handleDateTime(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    time_t now = time(nullptr);
    struct tm info; memset(&info, 0, sizeof(info));
    localtime_r(&now, &info);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
    char bufTZ[16]; char bufOff[8];
    strftime(bufTZ, sizeof(bufTZ), "%Z", &info);
    strftime(bufOff, sizeof(bufOff), "%z", &info);
    String line = String(F("Agora: ")) + String(buf) + String(F(" ")) + String(bufTZ) + String(F(" (")) + String(bufOff) + String(F(")"));
    sendVirtualMessage(line, COMMAND_RESPONSE);
    sendVirtualMessage(String(F("TZ=")) + timezone + String(F(" | NTP=")) + ntpServer, COMMAND_RESPONSE);
    return true;
}

static bool handleReboot(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    extern unsigned long lastRebootCommandTime;
    if (lastRebootCommandTime != 0 && (millis() - lastRebootCommandTime) < REBOOT_COOLDOWN_MS) {
        unsigned long remainingSec = (REBOOT_COOLDOWN_MS - (millis() - lastRebootCommandTime)) / 1000UL;
        char msg[128];
        snprintf(msg, sizeof(msg), "REBOOT: aguarde %lu segundos (~%lu minutos) desde ultimo reboot", remainingSec, remainingSec / 60UL);
        sendVirtualMessage(String(msg), WARN, true, true);
        return true;
    }
    lastRebootCommandTime = millis();
#if ENABLE_SDCARD==1
    sendVirtualMessage(String(F("REBOOT: flushing debug writer...")), INFO, true, true);
    debugWriterFlush();
    delay(100);
    sendVirtualMessage(String(F("REBOOT: flushing SD data...")), INFO, true, true);
    bool flushed = flushSdcardCutoffNowBlocking("REBOOT", 3000);
    if (flushed) {
        sendVirtualMessage(String(F("REBOOT: SD data flushed successfully")), INFO, true, true);
    } else {
        sendVirtualMessage(String(F("REBOOT: SD flush timeout (proceeding anyway)")), WARN, true, true);
    }
    delay(500);
#endif
    Serial.println(F("Comando REBOOT recebido - a reiniciar dispositivo..."));
#if ENABLE_TELNET==1
    if (telnetClient && telnetClient.connected() && telnetAuthenticated) {
        telnetClient.println(F("Comando REBOOT recebido - a reiniciar dispositivo..."));
    }
#endif
    sendVirtualMessage(String(F("Comando REBOOT recebido - a reiniciar dispositivo...")), INFO, true, true);
    delay(500);
    safeReboot("Command REBOOT");
    return true;
}

static bool handleSdMutexReset(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    extern SemaphoreHandle_t sdMutex;
    extern TaskHandle_t sdMutexOwnerTask;
    extern uint32_t sdMutexDepth;
    char msg[200];
    snprintf(msg, sizeof(msg), "[EMERGENCY] SD mutex state BEFORE reset: owner=%p depth=%u",
             sdMutexOwnerTask, sdMutexDepth);
    sendVirtualMessage(msg, WARN);
    if (sdMutex != NULL) {
        vSemaphoreDelete(sdMutex);
        sendVirtualMessage("[EMERGENCY] Old mutex destroyed", INFO);
    }
    sdMutex = xSemaphoreCreateRecursiveMutex();
    if (sdMutex == NULL) {
        sendVirtualMessage("[EMERGENCY] FAILED to recreate mutex!", ERROR);
        return true;
    }
    sdMutexOwnerTask = NULL;
    sdMutexDepth = 0;
    sendVirtualMessage("[EMERGENCY] SD mutex recreated. Try accessing SD now.", INFO);
    return true;
}

static bool handleRetrySd(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    extern bool retrySDStorage();
    bool success = retrySDStorage();
    if (success) {
        sendVirtualMessage(F("RETRYSD: SD card is now active"), INFO, true, false);
    } else {
        sendVirtualMessage(F("RETRYSD: SD card still unavailable"), WARN, true, false);
    }
    return true;
}

static bool handleGetAllConfigs(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    enviarTodasConfiguracoesParaUI();
    return true;
}

static bool handleDumpNvs(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    sendVirtualMessage(F("Config files in /littlefs/config/:"), COMMAND_RESPONSE);
    if (!littlefsBegin()) {
        sendVirtualMessage(F("  LittleFS not available"), COMMAND_RESPONSE);
        return true;
    }
    File dir = LittleFS.open("/config");
    if (!dir || !dir.isDirectory()) {
        sendVirtualMessage(F("  /config/ not found"), COMMAND_RESPONSE);
        return true;
    }
    File f = dir.openNextFile();
    if (!f) {
        sendVirtualMessage(F("  (empty)"), COMMAND_RESPONSE);
    }
    while (f) {
        String line = String("  ") + String(f.name()) + " (" + String(f.size()) + " B)";
        sendVirtualMessage(line, COMMAND_RESPONSE);
        f = dir.openNextFile();
    }
    dir.close();
    return true;
}

#if ENABLE_WIFI==1
static bool handleWifiAdd(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String params = cmdLine.substring(8);
    params.trim();
    int spaceIdx = params.indexOf(' ');
    if (spaceIdx < 0) {
        sendVirtualMessage(F("WIFIADD: use WIFIADD <SSID> <PASSWORD>"), WARN);
        return true;
    }
    String ssid = params.substring(0, spaceIdx);
    String password = params.substring(spaceIdx + 1);
    password.trim();
    if (ssid.length() == 0) {
        sendVirtualMessage(F("WIFIADD: SSID cannot be empty"), WARN);
        return true;
    }
    bool exists = false;
    for (const auto& n : savedNetworks) {
        if (n.ssid == ssid) { exists = true; break; }
    }
    if (!exists) {
        WifiNetwork net;
        net.ssid = ssid;
        net.password = password;
        net.rssi = -100;
        net.lastConnected = millis();
        net.connectionCount = 1;
        savedNetworks.push_back(net);
        saveSavedNetworks();
    }
    String msg = String(F("WIFIADD: saved '")) + ssid + String(F("' ("));
    msg += String(password.length() > 0 ? "****" : "open");
    msg += String(F(") - connecting..."));
    sendVirtualMessage(msg, INFO);
    delay(200);
    esp_restart();
    return true;
}

static bool handleWifiList(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    JsonArray nets = configManager.getSavedNetworks();
    if (nets.size() == 0) {
        sendVirtualMessage(F("WIFILIST: no saved networks"), INFO);
    } else {
        for (uint8_t i = 0; i < nets.size(); i++) {
            String s = String(i) + ": " + nets[i]["ssid"].as<String>();
            sendVirtualMessage(s, COMMAND_RESPONSE);
        }
    }
    return true;
}
#endif

static bool handleChipInfo(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    enviarTodasConfiguracoesParaUI();
    return true;
}

static bool handleVersion(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    extern const char* FIRMWARE_BUILD;
    sendVirtualMessage(String(F("Versao do Firmware: ")) + String(FIRMWARE_BUILD), INFO);
    return true;
}

static bool handleHello(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    extern const char* FIRMWARE_VERSION;
    sendVirtualMessage(String(F("ESP32 Node32-Hub - ")) + String(FIRMWARE_VERSION), INFO);
    if (WiFi.status() == WL_CONNECTED) {
        String s = String(F("IP: ")) + WiFi.localIP().toString() + String(F(" | ")) +
                  WiFi.SSID() + String(F(" | RSSI:")) + String(WiFi.RSSI()) + String(F("dBm"));
        sendVirtualMessage(s, INFO);
        sendVirtualMessage(WiFi.localIP().toString(), COMMAND_RESPONSE);
    } else if (hasNetworkConnectivity()) {
        String currentIp = getCurrentNetworkIPString();
        sendVirtualMessage(String(F("IP: ")) + currentIp + String(F(" | Ethernet")), INFO);
        sendVirtualMessage(currentIp, COMMAND_RESPONSE);
    } else {
        sendVirtualMessage(String(F("Rede: DESCONECTADA")), WARN);
    }
    String heapMsg = String(F("Heap livre: ")) + String(ESP.getFreeHeap() / 1024) + String(F(" KB"));
    sendVirtualMessage(heapMsg, INFO);
    return true;
}

static bool handleLedQuery(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    if (!isOnboardLedSupported()) {
        sendVirtualMessage(String(F("LED onboard: nao suportado por esta board/build.")), WARN);
    } else {
        sendVirtualMessage(String(F("LED onboard: ")) + (isOnboardLedOn() ? String(F("ON")) : String(F("OFF"))), COMMAND_RESPONSE);
    }
    return true;
}

static bool handleLedSet(const String& cmdLine, const String& upperCmd) {
    String ledArg = upperCmd.substring(4);
    ledArg.trim();
    if (!isOnboardLedSupported()) {
        sendVirtualMessage(String(F("LED onboard: nao suportado por esta board/build.")), WARN);
        return true;
    }
    if (ledArg == String(F("OFF"))) {
        if (setOnboardLedState(false)) {
            sendVirtualMessage(String(F("LED onboard: OFF")), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(String(F("LED onboard: falha ao aplicar OFF.")), ERROR);
        }
    } else if (ledArg == String(F("ON"))) {
        if (setOnboardLedState(true)) {
            sendVirtualMessage(String(F("LED onboard: ON")), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(String(F("LED onboard: falha ao aplicar ON.")), ERROR);
        }
    } else {
        sendVirtualMessage(String(F("Use: LED ON | LED OFF | LED?")), COMMAND_RESPONSE);
    }
    return true;
}

static bool handleI2cScan(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    runI2CScan();
    sendVirtualMessage(String(F("I2C scan executado (ver Serial).")), COMMAND_RESPONSE);
    return true;
}

static bool handleDebugLogsOn(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    enableDebugLogs = true;
    sendVirtualMessage(String(F("Logs de depuracao ATIVADOS.")), COMMAND_RESPONSE);
    return true;
}

static bool handleDebugLogsOff(const String& cmdLine, const String& upperCmd) {
    (void)cmdLine; (void)upperCmd;
    enableDebugLogs = false;
    sendVirtualMessage(String(F("Logs de depuracao DESATIVADOS.")), COMMAND_RESPONSE);
    return true;
}

static bool handleSdSpeedTest(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
#if ENABLE_SDCARD==1
    size_t mb = 5;
    String param = "";
    int colonIdx = cmdLine.indexOf(':');
    if (colonIdx >= 0) {
        param = cmdLine.substring(colonIdx + 1);
        param.trim();
        long v = param.toInt(); if (v > 0) mb = (size_t)v;
    }
    size_t totalBytes = mb * 1024UL * 1024UL;
    if (!sdcardIsMounted()) {
        sendVirtualMessage(String(F("SD nao montado. Execute SD card mount primeiro.")), WARN);
        return true;
    }
    String upperParam = param;
    upperParam.toUpperCase();
    if (upperParam.startsWith("ALL")) {
        size_t mbLocal = mb;
        if (upperParam.indexOf(':') >= 0) {
            int pos = upperParam.indexOf(':');
            String tail = upperParam.substring(pos+1);
            tail.trim();
            long v = tail.toInt(); if (v>0) mbLocal = (size_t)v;
        }
        size_t totalB = mbLocal * 1024UL * 1024UL;
        sdcardSpeedTestAllAsync(totalB);
        sendVirtualMessage(String(F("SD_SPEED_TEST: tests iniciados em background (ALL speeds).")), COMMAND_RESPONSE);
        return true;
    }
    bool ok = sdcardSpeedTest(totalBytes);
    sendVirtualMessage(String(ok ? F("SD_SPEED_TEST concluido") : F("SD_SPEED_TEST falhou")), COMMAND_RESPONSE);
    return true;
#else
    sendVirtualMessage(String(F("SD nao ativado nesta build (ENABLE_SDCARD=0).")), WARN);
    return true;
#endif
}

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
static bool handleSetLineLimit(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String valueStr = cmdLine.substring(String(F("SET_LINE_LIMIT:")).length());
    valueStr.trim();
    int newLimit = valueStr.toInt();
    if (newLimit == MAX_LINES_6_LPI || newLimit == MAX_LINES_8_LPI) {
        configManager.setEmailMaxLines(newLimit);
        sendVirtualMessage(String(F("Limite de linhas para e-mail definido para: ")) +
                            String(currentMaxEmailLines) + String(F(" linhas.")), COMMAND_RESPONSE);
    } else {
        sendVirtualMessage(String(F("Comando SET_LINE_LIMIT: valor invalido. Use ")) +
                            String(MAX_LINES_6_LPI) + String(F(" ou ")) + String(MAX_LINES_8_LPI), WARN);
    }
    return true;
}
#endif

#if ENABLE_DB25==1
static bool handleSetCharset(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String modeStr = cmdLine.substring(String(F("SET_CHARSET:")).length());
    modeStr.trim();
    modeStr.toUpperCase();
    int newCharsetMode = -1;
    if (modeStr.equalsIgnoreCase(String(F("AUTO")))) newCharsetMode = 0;
    else if (modeStr.equalsIgnoreCase(String(F("LATIN1")))) newCharsetMode = 1;
    else if (modeStr.equalsIgnoreCase(String(F("CP850")))) newCharsetMode = 2;
    if (newCharsetMode != -1) {
        charsetMode = newCharsetMode;
        configManager.setPrinterCharsetMode(newCharsetMode);
        String charsetSetMsg = String(F("Charset definido para: "));
        if (charsetMode == 0) charsetSetMsg += String(F("AUTO (UTF-8)"));
        else if (charsetMode == 1) charsetSetMsg += String(F("LATIN1"));
        else if (charsetMode == 2) charsetSetMsg += String(F("CP850"));
        sendVirtualMessage(charsetSetMsg, COMMAND_RESPONSE);
    } else {
        sendVirtualMessage(String(F("Charset invalido. Use AUTO, LATIN1 ou CP850.")), WARN);
    }
    return true;
}

static bool handleSetPrinterMode(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String modeStr = cmdLine.substring(String(F("SET_PRINTER_MODE:")).length());
    modeStr.trim();
    int newEmulationMode = modeStr.toInt();
    extern const int PRINTER_EMULATION_MODES_COUNT;
    if (newEmulationMode >= 0 && newEmulationMode < PRINTER_EMULATION_MODES_COUNT) {
        printerEmulationMode = newEmulationMode;
        configManager.setPrinterEmulationMode(newEmulationMode);
        sendVirtualMessage(String(F("Modo de emulacao alterado para: ")) +
                            String(newEmulationMode), COMMAND_RESPONSE);
    } else {
        sendVirtualMessage(String(F("Modo de emulacao invalido. Use 0=Generico, 1=Epson ESC/P, 2=IBM Proprinter.")), WARN);
    }
    return true;
}
#endif

#if ENABLE_RS232==1
static bool handleSetRs232Enable(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String enableStr = cmdLine.substring(String(F("SET_RS232_ENABLE:")).length());
    enableStr.trim();
    bool newRs232Enabled = (enableStr.toInt() == 1);
    rs232_enabled = newRs232Enabled;
    if (rs232_enabled) {
        if (rs232_invert_pins) {
            rs232_rx_pin = 17;
            rs232_tx_pin = 16;
        } else {
            rs232_rx_pin = 16;
            rs232_tx_pin = 17;
        }
        setRS232BaudrateByIndex(rs232_baudrate_index);
        RS232.begin(rs232_baudrate, SERIAL_8N1, rs232_rx_pin, rs232_tx_pin);
        _rs232PortInitialized = true;
        sendVirtualMessage(String(F("RS232 ATIVADO. Baudrate: ")) + String(rs232_baudrate) +
                            String(F(", RX: ")) + String(rs232_rx_pin) +
                            String(F(", TX: ")) + String(rs232_tx_pin), COMMAND_RESPONSE);
    } else {
        _rs232PortInitialized = false;
        RS232.end();
        sendVirtualMessage(String(F("RS232 DESATIVADO.")), COMMAND_RESPONSE);
    }
    configManager.setRs232Enabled(newRs232Enabled);
    return true;
}

static bool handleSetRs232Baudrate(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String indexStr = cmdLine.substring(String(F("SET_RS232_BAUDRATE:")).length());
    indexStr.trim();
    int newIndex = indexStr.toInt();
    extern const int RS232_BAUDRATES_COUNT;
    if (newIndex >= 0 && newIndex < RS232_BAUDRATES_COUNT) {
        setRS232BaudrateByIndex(newIndex);
        if (rs232_enabled) {
            RS232.updateBaudRate(rs232_baudrate);
        }
        configManager.setRs232BaudrateIndex(newIndex);
        sendVirtualMessage(String(F("RS232 Baudrate alterado para: ")) + String(rs232_baudrate) +
                            String(F(" (indice ")) + String(newIndex) + String(F(")")), COMMAND_RESPONSE);
    } else {
        sendVirtualMessage(String(F("Indice de baudrate invalido: ")) + String(newIndex) +
                            String(F(". Use 0-7 (0=1200, 1=2400, 2=4800, 3=9600, 4=19200, 5=38400, 6=57600, 7=115200).")), WARN);
    }
    return true;
}

static bool handleSetRs232InvertPins(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String invertStr = cmdLine.substring(String(F("SET_RS232_INVERT_PINS:")).length());
    invertStr.trim();
    bool newInvert = (invertStr.toInt() == 1);
    rs232_invert_pins = newInvert;
    if (rs232_invert_pins) {
        rs232_rx_pin = 17;
        rs232_tx_pin = 16;
    } else {
        rs232_rx_pin = 16;
        rs232_tx_pin = 17;
    }
    if (rs232_enabled) {
        _rs232PortInitialized = false;
        RS232.end();
        RS232.begin(rs232_baudrate, SERIAL_8N1, rs232_rx_pin, rs232_tx_pin);
        _rs232PortInitialized = true;
    }
    configManager.setRs232InvertPins(newInvert);
    sendVirtualMessage(String(F("Pinos RS232 ")) + (rs232_invert_pins ? String(F("INVERTIDOS")) : String(F("NORMAIS"))) +
                        String(F(". RX: ")) + String(rs232_rx_pin) + String(F(", TX: ")) + String(rs232_tx_pin), COMMAND_RESPONSE);
    return true;
}
#endif

#if ENABLE_SMTP==1
static bool handleEmailDay(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String dateParam = "";
    int spaceIdx = cmdLine.indexOf(' ');
    if (spaceIdx > 0 && cmdLine.length() > spaceIdx + 1) {
        dateParam = cmdLine.substring(spaceIdx + 1);
        dateParam.trim();
    }
    if (!storageIsAvailable()) {
        sendVirtualMessage(String(F("No storage available (SD or PSRAM).")), ERROR);
    } else if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(String(F("Email nao configurado ou WiFi desligado.")), ERROR);
    } else {
        bool usingSd = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
        bool usingPsram = (currentStorageMode == STORAGE_MODE_PSRAM);
        if (usingSd) {
            sendVirtualMessage(String(F("EMAILDAY: flushing and swapping SD buffers...")), INFO);
            bool swapped = forceSwapSdcardBuffersForEmailday(3000);
            if (!swapped) {
                sendVirtualMessage(String(F("EMAILDAY: falha ao preparar SD (timeout no flush)")), ERROR);
                return true;
            }
            sendVirtualMessage(String(F("EMAILDAY: aguardando estabilizacao do sistema...")), INFO);
            delay(2000);
            sendVirtualMessage(String(F("EMAILDAY: SD preparado, a processar comando...")), INFO);
        } else if (usingPsram) {
            sendVirtualMessage(String(F("EMAILDAY: using PSRAM (no buffer swap needed)...")), INFO);
        }
        sendVirtualMessage(String(F("Buffer de dados continua a receber normalmente")), INFO);
        String folderPath = "";
        if (dateParam.length() > 0) {
            int day = 0, month = 0, year = 0;
            int dash1 = dateParam.indexOf('-');
            int dash2 = dateParam.lastIndexOf('-');
            bool fmtOk = (dash1 > 0 && dash2 > dash1);
            if (fmtOk) {
                day = dateParam.substring(0, dash1).toInt();
                month = dateParam.substring(dash1 + 1, dash2).toInt();
                year = dateParam.substring(dash2 + 1).toInt();
            }
            bool dateOk = fmtOk && (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2000 && year <= 2099);
            String sdcardNameBase = configManager.getSdcardName();
            if (sdcardNameBase.isEmpty()) sdcardNameBase = "SD_LOG";
            if (!dateOk) {
                if (fmtOk) {
                    sendVirtualMessage(String(F("Data invalida. Use DD-MM-YYYY (ex: 29-12-2025)")), ERROR);
                } else {
                    sendVirtualMessage(String(F("Formato de data invalido. Use DD-MM-YYYY (ex: 29-12-2025)")), ERROR);
                }
            } else if (usingPsram) {
                char dayBuf[16];
                snprintf(dayBuf, sizeof(dayBuf), "%02d-%02d-%04d", day, month, year);
                if (flashFbHasDataForDate(sdcardNameBase, String(dayBuf))) {
                    // Flash mirror holds this date: keep SD-layout path, the
                    // senders merge tiers (VFS miss -> Flash hit).
                    folderPath = String("/") + sdcardNameBase + String("@") + String(dayBuf);
                    sendVirtualMessage(String(F("EMAILDAY: Flash mirror mode for ")) + dayBuf, INFO);
                } else {
                    sendVirtualMessage(String(F("PSRAM mode: specific dates not supported. Using today's folder.")), WARN);
                    folderPath = storageGetTodayFolder();
                    if (folderPath.length() == 0) {
                        sendVirtualMessage(String(F("Erro ao obter pasta do dia.")), ERROR);
                    }
                }
            } else {
                char dayBuf[16];
                snprintf(dayBuf, sizeof(dayBuf), "%02d-%02d-%04d", day, month, year);
                folderPath = String("/") + sdcardNameBase + String("@") + String(dayBuf);
            }
        } else {
            folderPath = storageGetTodayFolder();
            if (folderPath.length() == 0) {
                sendVirtualMessage(String(F("Erro ao obter pasta do dia.")), ERROR);
            }
        }
        if (folderPath.length() > 0) {
            sendVirtualMessage(String(F("EMAILDAY: A preparar envio de ficheiros da pasta: ")) + folderPath, INFO);
            String folderName = folderPath;
            if (folderName.startsWith("/")) {
                folderName = folderName.substring(1);
            }
            extern String getCurrentTriggeredEmailFrequencyLabel();
            String subject = String(F("- ")) + buildEmailTitlePrefix() + String(F(" - EmailDay"));
            String body = buildEmailInfoTable(
                          L(activeLang->emailDataAttached),
                          "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
            extern bool startEmailDayAsync(const String &folderPath, const String &subject, const String &body);
            extern String emailDayCmdExtra;
            emailDayCmdExtra = configManager.getEmaildayExtraEmails();
            dayReportTrackManual(cmdLine);
            bool started = startEmailDayAsync(folderPath, subject, body);
            if (started) {
                sendVirtualMessage(String(F("EMAILDAY: iniciado em background")), COMMAND_RESPONSE);
            } else {
                sendVirtualMessage(String(F("EMAILDAY: nao foi possivel iniciar (ja em curso?)")), ERROR);
            }
        }
    }
    return true;
}

static bool handleLogDay(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String dateParam = "";
    int spaceIdx = cmdLine.indexOf(' ');
    if (spaceIdx > 0 && cmdLine.length() > spaceIdx + 1) {
        dateParam = cmdLine.substring(spaceIdx + 1);
        dateParam.trim();
    }
    if (!storageIsAvailable()) {
        sendVirtualMessage(String(F("LOGDAY: no storage available (SD or PSRAM).")), ERROR);
    } else if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(String(F("LOGDAY: email nao configurado ou WiFi desligado.")), ERROR);
    } else {
        String folderPath = "";
        String dateLabel = "";
        if (dateParam.length() > 0) {
            int dash1 = dateParam.indexOf('-');
            int dash2 = dateParam.lastIndexOf('-');
            if (dash1 > 0 && dash2 > dash1) {
                int day = dateParam.substring(0, dash1).toInt();
                int month = dateParam.substring(dash1 + 1, dash2).toInt();
                int year = dateParam.substring(dash2 + 1).toInt();
                if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2000 && year <= 2099) {
                    char dayStr[16];
                    snprintf(dayStr, sizeof(dayStr), "%02d-%02d-%04d", day, month, year);
                    dateLabel = String(dayStr);
folderPath = String(F("/logs/log_")) + dateLabel;
                } else {
                    sendVirtualMessage(String(F("LOGDAY: data invalida. Use DD-MM-YYYY (ex: 28-02-2026)")), ERROR);
                }
            } else {
                sendVirtualMessage(String(F("LOGDAY: formato invalido. Use DD-MM-YYYY (ex: 28-02-2026)")), ERROR);
            }
        } else {
            time_t now = time(nullptr);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char dayStr[16];
            strftime(dayStr, sizeof(dayStr), "%d-%m-%Y", &timeinfo);
            dateLabel = String(dayStr);
            folderPath = String(F("/logs/log_")) + dateLabel;
        }
        if (folderPath.length() > 0) {
            // Order: SD first, then the merged no-SD tiers (PSRAM hot + Flash).
            bool usingSD = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
            bool folderHasData = false;
#if ENABLE_SDCARD==1
            if (usingSD && SD_MMC.exists(folderPath.c_str())) folderHasData = true;
#endif
            if (!folderHasData && !storageListFiles(folderPath).empty()) folderHasData = true;
            if (!folderHasData) {
                sendVirtualMessage(String(F("LOGDAY: folder not found: ")) + folderPath, ERROR);
            } else {
                extern String getCurrentTriggeredEmailFrequencyLabel();
                String subject = String(F("- ")) + buildEmailTitlePrefix() + String(F(" - LogDay ")) + dateLabel;
                String body = buildEmailInfoTable(
                              L(activeLang->emailDataAttached),
                              "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                extern bool startEmailDayAsync(const String &folderPath, const String &subject, const String &body);
                extern String emailDayCmdExtra;
                emailDayCmdExtra = configManager.getLogdayExtraEmails();
                dayReportTrackManual(cmdLine);
                bool started = startEmailDayAsync(folderPath, subject, body);
                if (started) {
                    sendVirtualMessage(String(F("LOGDAY: iniciado em background (")) + folderPath + String(F(")")), COMMAND_RESPONSE);
                } else {
                    sendVirtualMessage(String(F("LOGDAY: nao foi possivel iniciar (ja em curso?)")), ERROR);
                }
            }
        }
    }
    return true;
}

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
// T23 — SensorDay in ShiftEnd style: min/avg/max summary + 24 ASCII bars of
// temperature (min-max scale of hourly averages), humidity inline when
// available. Reads the daily CSV /sensor_history/<YYYY-MM-DD>/<YYYY-MM-DD>.csv
// (SD first, then the LittleFS mirror on the same path), line by line,
// without loading the whole file. Returns "" when there is no valid temperature;
// the caller falls back to the legacy body (only "data attached") for the email.
static String sensorDaySummaryPrefix(const String& folderPath, const String& dateLabel, bool usingSD) {
    SensorDayParser par;
    sensorDayParserInit(&par);
    auto feedLine = [&](const String& ln) {
        if (ln.length() > 0 && ln.length() < 512) {
            sensorDayParserFeed(&par, std::string(ln.c_str()));
        }
    };
    String csvFile = folderPath + "/" + dateLabel + ".csv";
#if ENABLE_SDCARD==1
    if (usingSD && SD_MMC.exists(csvFile.c_str())) {
        File f = SD_MMC.open(csvFile.c_str(), FILE_READ);
        if (f) {
            int guard = 0;
            while (f.available() && guard++ < 3000) feedLine(f.readStringUntil('\n'));
            f.close();
        }
    }
#endif
    if (!sensorDayParserHasData(&par) && littlefsBegin() && littlefsMounted()) {
#if ENABLE_FILESYSTEM==1
        // Serialized under lfsMutex (same crash guard, exists() included —
        // stat is the observed crash path); legacy body on contention.
        extern bool lfsMutexLock(TickType_t timeoutTicks);
        extern void lfsMutexUnlock();
        if (lfsMutexLock(pdMS_TO_TICKS(5000))) {
        if (LittleFS.exists(csvFile.c_str())) {
#endif
        sensorDayParserInit(&par);
        File f = LittleFS.open(csvFile.c_str(), "r");
        if (f) {
            int guard = 0;
            while (f.available() && guard++ < 3000) feedLine(f.readStringUntil('\n'));
            f.close();
        }
#if ENABLE_FILESYSTEM==1
        }
            lfsMutexUnlock();
        }
#endif
    }
    if (!sensorDayParserHasData(&par)) return String("");
    std::string b = sensorDayParserBody(&par, std::string(dateLabel.c_str()));
    if (b.empty()) return String("");
    return String(b.c_str());
}
static bool handleSensorDay(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String dateParam = "";
    int spaceIdx = cmdLine.indexOf(' ');
    if (spaceIdx > 0 && cmdLine.length() > spaceIdx + 1) {
        dateParam = cmdLine.substring(spaceIdx + 1);
        dateParam.trim();
    }
    if (!storageIsAvailable()) {
        sendVirtualMessage(String(F("SENSORDAY: no storage available (SD or PSRAM).")), ERROR);
    } else if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(String(F("SENSORDAY: email nao configurado ou WiFi desligado.")), ERROR);
    } else {
        String folderPath = "";
        String dateLabel = "";
        if (dateParam.length() > 0) {
            int dash1 = dateParam.indexOf('-');
            int dash2 = dateParam.lastIndexOf('-');
            if (dash1 > 0 && dash2 > dash1) {
                int day = dateParam.substring(0, dash1).toInt();
                int month = dateParam.substring(dash1 + 1, dash2).toInt();
                int year = dateParam.substring(dash2 + 1).toInt();
                if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2000 && year <= 2099) {
                    char dayStr[16];
                    snprintf(dayStr, sizeof(dayStr), "%04d-%02d-%02d", year, month, day);
                    dateLabel = String(dayStr);
                    folderPath = String(F("/sensor_history/")) + dateLabel;
                } else {
                    sendVirtualMessage(String(F("SENSORDAY: data invalida. Use DD-MM-YYYY")), ERROR);
                }
            } else {
                sendVirtualMessage(String(F("SENSORDAY: formato invalido. Use DD-MM-YYYY")), ERROR);
            }
        } else {
            time_t now = time(nullptr);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char dayStr[16];
            strftime(dayStr, sizeof(dayStr), "%Y-%m-%d", &timeinfo);
            dateLabel = String(dayStr);
            folderPath = String(F("/sensor_history/")) + dateLabel;
        }
        if (folderPath.length() > 0) {
            extern SensorConfigManager sensorConfigManager;
            sensorConfigManager.getHistory().periodicFlushToSD(sensorConfigManager.getConfig());
            bool usingPsramSensor = (currentStorageMode == STORAGE_MODE_PSRAM);
            if (usingPsramSensor) {
                // Explicit dates work via the Flash mirror (/sensor_history/<date>
                // or /oee/PROXIMITY_<DD-MM-YYYY>.csv); otherwise today's folder.
                bool flashHasDate = false;
                if (littlefsBegin() && littlefsMounted()) {
                    String shPath = String("/sensor_history/") + dateLabel;
                    if (LittleFS.exists(shPath.c_str())) {
                        flashHasDate = true;
                    } else {
                        int dash1 = dateLabel.indexOf('-');
                        int dash2 = dateLabel.lastIndexOf('-');
                        if (dash1 == 4 && dash2 == 7) {
                            String br = dateLabel.substring(8, 10) + "-" + dateLabel.substring(5, 7) +
                                        "-" + dateLabel.substring(0, 4);
                            String oeePath = String("/oee/PROXIMITY_") + br + ".csv";
                            if (LittleFS.exists(oeePath.c_str())) flashHasDate = true;
                        }
                    }
                }
                if (!flashHasDate) {
                    folderPath = storageGetTodayFolder();
                    if (folderPath.length() == 0) {
                        sendVirtualMessage(String(F("SENSORDAY: pasta PSRAM nao disponivel.")), ERROR);
                        return true;
                    }
                    sendVirtualMessage(String(F("SENSORDAY: modo PSRAM - pasta de hoje (dados combinados).")), WARN);
                } else {
                    sendVirtualMessage(String(F("SENSORDAY: Flash mirror mode for ")) + dateLabel, INFO);
                }
            }
            bool usingSD = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
            // Order: SD first, then the merged no-SD tiers (PSRAM hot + Flash
            // /sensor_history mirror + /oee daily). Explicit dates work without
            // SD via the Flash mirror (PSRAM only ever holds today).
            bool folderHasData = false;
#if ENABLE_SDCARD==1
            if (usingSD && SD_MMC.exists(folderPath.c_str())) folderHasData = true;
#endif
            if (!folderHasData && !storageListFiles(folderPath).empty()) folderHasData = true;
            if (!folderHasData) {
                sendVirtualMessage(String(F("SENSORDAY: folder not found: ")) + folderPath, ERROR);
            } else {
                extern String getCurrentTriggeredEmailFrequencyLabel();
                String subject = String(F("- ")) + buildEmailTitlePrefix() + String(F(" - SensorDay ")) + dateLabel;
                String footer = buildEmailInfoTable(
                              L(activeLang->emailDataAttached),
                              "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                // T23: body in ShiftEnd style from /sensor_history; fallback
                // RAM (exportDayCSV) when the file has no valid temperature.
                // Streaming (1 line at a time) — without tripling the CSV in RAM.
                String prefix = sensorDaySummaryPrefix(folderPath, dateLabel, usingSD);
                if (!prefix.length()) {
                    extern SensorConfigManager sensorConfigManager;
                    String csvData = sensorConfigManager.getHistory().exportDayCSV(dateLabel, sensorConfigManager.getConfig());
                    if (csvData.length() > 0 && csvData.length() < 60000) {
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
                        csvData = String();
                        std::string b = sensorDayParserBody(&par, std::string(dateLabel.c_str()));
                        if (!b.empty()) prefix = String(b.c_str());
                    }
                }
                String body = prefix + footer;
                extern bool startEmailDayAsync(const String &folderPath, const String &subject, const String &body);
                extern String emailDayCmdExtra;
                emailDayCmdExtra = configManager.getSensordayExtraEmails();
                dayReportTrackManual(cmdLine);
                bool started = startEmailDayAsync(folderPath, subject, body);
                if (started) {
                    sendVirtualMessage(String(F("SENSORDAY: iniciado em background (")) + folderPath + String(F(")")), COMMAND_RESPONSE);
                } else {
                    sendVirtualMessage(String(F("SENSORDAY: nao foi possivel iniciar (ja em curso?)")), ERROR);
                }
            }
        }
    }
    return true;
}
#endif // ENABLE_AHT20_BMP280 && ENABLE_SENSOR_CONFIG_PAGE

#if ENABLE_SDCARD==1 && ENABLE_PROXIMITY==1
// Day-boundary alignment shared with the proximity CSV writer (proximity_shift_config.cpp):
// SHIFTDAY must open the SAME shift-aligned daily folder that _proxWriteCSVRow creates.
extern uint8_t proximityShiftDayAlignedDate(time_t now, struct tm* out);
static bool handleShiftDay(const String& cmdLine, const String& upperCmd) {
    (void)upperCmd;
    String dateParam = "";
    int spaceIdx = cmdLine.indexOf(' ');
    if (spaceIdx > 0 && cmdLine.length() > spaceIdx + 1) {
        dateParam = cmdLine.substring(spaceIdx + 1);
        dateParam.trim();
    }
    if (!storageIsAvailable()) {
        sendVirtualMessage(String(F("SHIFTDAY: no storage available (SD or PSRAM).")), ERROR);
    } else if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(String(F("SHIFTDAY: email not configured or WiFi disabled.")), ERROR);
    } else {
        String csvPath = "";
        String dateLabel = "";
        String sdName = configManager.getSdcardName();
        if (sdName.isEmpty()) sdName = F("SD_LOG");
        if (dateParam.length() > 0) {
            int dash1 = dateParam.indexOf('-');
            int dash2 = dateParam.lastIndexOf('-');
            if (dash1 > 0 && dash2 > dash1) {
                int day = dateParam.substring(0, dash1).toInt();
                int month = dateParam.substring(dash1 + 1, dash2).toInt();
                int year = dateParam.substring(dash2 + 1).toInt();
                if (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 2000 && year <= 2099) {
                    char dayStr[16];
                    snprintf(dayStr, sizeof(dayStr), "%02d-%02d-%04d", day, month, year);
                    dateLabel = String(dayStr);
                    csvPath = sd_layout::dayFile(sdName, "PROXIMITY", dateLabel, "csv");
                } else {
                    sendVirtualMessage(String(F("SHIFTDAY: data invalida. Use DD-MM-YYYY")), ERROR);
                }
            } else {
                sendVirtualMessage(String(F("SHIFTDAY: formato invalido. Use DD-MM-YYYY")), ERROR);
            }
        } else {
            time_t now = time(nullptr);
            struct tm timeinfo;
            proximityShiftDayAlignedDate(now, &timeinfo);
            char dayStr[16];
            snprintf(dayStr, sizeof(dayStr), "%02d-%02d-%04d",
                     timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            dateLabel = String(dayStr);
            csvPath = sd_layout::dayFile(sdName, "PROXIMITY", dateLabel, "csv");
        }
        if (csvPath.length() > 0) {
            bool usingSD = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
            if (usingSD && !SD_MMC.exists(csvPath.c_str())) {
                sendVirtualMessage(String(F("SHIFTDAY: CSV not found: ")) + csvPath, ERROR);
            } else {
                extern String getCurrentTriggeredEmailFrequencyLabel();
                String subject = String(F("- ")) + buildEmailTitlePrefix() + String(F(" - ShiftDay ")) + dateLabel;
                String body = buildEmailInfoTable(
                              L(activeLang->emailDataAttached),
                              "", "", "", getCurrentTriggeredEmailFrequencyLabel(), "", "", "");
                extern bool startEmailDayAsync(const String &csvPath, const String &subject, const String &body);
                dayReportTrackManual(cmdLine);
                bool started = startEmailDayAsync(csvPath, subject, body);
                if (started) {
                    sendVirtualMessage(String(F("SHIFTDAY: iniciado em background (")) + csvPath + String(F(")")), COMMAND_RESPONSE);
                } else {
                    sendVirtualMessage(String(F("SHIFTDAY: nao foi possivel iniciar (ja em curso?)")), ERROR);
                }
            }
        }
    }
    return true;
}
#endif // ENABLE_SDCARD && ENABLE_PROXIMITY
#endif // ENABLE_SMTP

#if ENABLE_MODBUS==1
static bool handleModbus(const String& cmdLine, const String& upperCmd) {
    if (upperCmd.equals(F("MODBUS STATUS"))) {
        if (g_modbusDeviceCount == 0) {
            sendVirtualMessage(F("MODBUS: no devices configured"), COMMAND_RESPONSE);
            return true;
        }
        for (uint8_t d = 0; d < g_modbusDeviceCount; ++d) {
            const ModbusDevice& dev = g_modbusDevices[d];
            char line[120];
            snprintf(line, sizeof(line), "  [%u] %s  ID=%u  %s  polls=%lu  errors=%lu  lastPoll=%lums ago",
                     (unsigned)d, dev.name, (unsigned)dev.slaveId,
                     dev.isTcp ? "TCP" : "RTU",
                     (unsigned long)dev.pollCount, (unsigned long)dev.errorCount,
                     dev.lastPollMs ? (unsigned long)(millis() - dev.lastPollMs) : 0UL);
            sendVirtualMessage(String(line), COMMAND_RESPONSE);
            for (uint8_t r = 0; r < dev.registerCount; ++r) {
                const ModbusRegister& reg = dev.registers[r];
                char rline[80];
                snprintf(rline, sizeof(rline), "      reg[%u] %s = %.4g %s  %s",
                         (unsigned)r, reg.label, reg.lastValue, reg.unit,
                         reg.lastOk ? "OK" : "ERR");
                sendVirtualMessage(String(rline), COMMAND_RESPONSE);
            }
        }
        char stat[80];
        snprintf(stat, sizeof(stat), "  RTU: %s  slave: %s",
                 modbusRtuIsReady() ? "ready" : "not ready",
                 modbusSlaveIsRunning() ? "running" : "stopped");
        sendVirtualMessage(String(stat), COMMAND_RESPONSE);
        return true;
    }

    if (upperCmd.equals(F("MODBUS RESET STATS"))) {
        for (uint8_t d = 0; d < g_modbusDeviceCount; ++d) {
            g_modbusDevices[d].pollCount = 0;
            g_modbusDevices[d].errorCount = 0;
        }
        sendVirtualMessage(F("MODBUS: stats reset"), COMMAND_RESPONSE);
        return true;
    }

    if (upperCmd.equals(F("MODBUS SLAVE ON"))) {
        if (!modbusSlaveIsRunning()) {
            JsonVariant mbSec = configManager.getSection("modbus");
            bool mbRtuEn = mbSec["rtu_enabled"].as<bool>();
            bool mbTcpEn = mbSec["tcp_enabled"].as<bool>();
            uint8_t sid = mbSec["slave_id"] | (uint8_t)1;
            int8_t dep = mbSec["rtu_de_pin"] | (int8_t)-1;
            uint32_t baud = mbSec["rtu_baud"] | (uint32_t)9600;
            modbusSlaveBegin(sid, mbRtuEn, mbTcpEn, dep, baud, 502);
            sendVirtualMessage(F("MODBUS SLAVE: started"), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(F("MODBUS SLAVE: already running"), COMMAND_RESPONSE);
        }
        return true;
    }

    if (upperCmd.equals(F("MODBUS SLAVE OFF"))) {
        modbusSlaveEnd();
        sendVirtualMessage(F("MODBUS SLAVE: stopped"), COMMAND_RESPONSE);
        return true;
    }

    if (upperCmd.equals(F("MODBUS POLL")) || upperCmd.startsWith(F("MODBUS POLL "))) {
        String arg = upperCmd.substring(12);
        arg.trim();
        if (arg.length() == 0) {
            for (uint8_t d = 0; d < g_modbusDeviceCount; ++d)
                modbusPollRequestImmediate(d);
            sendVirtualMessage(F("MODBUS POLL: requested for all devices"), COMMAND_RESPONSE);
        } else {
            uint8_t idx = (uint8_t)arg.toInt();
            if (idx >= g_modbusDeviceCount) {
                char err[64];
                snprintf(err, sizeof(err), "MODBUS POLL: invalid device index %u (max %u)",
                         (unsigned)idx, (unsigned)(g_modbusDeviceCount > 0 ? g_modbusDeviceCount - 1 : 0));
                sendVirtualMessage(String(err), ERROR);
            } else {
                modbusPollRequestImmediate(idx);
                char ok[48];
                snprintf(ok, sizeof(ok), "MODBUS POLL: requested for device %u", (unsigned)idx);
                sendVirtualMessage(String(ok), COMMAND_RESPONSE);
            }
        }
        return true;
    }

    if (upperCmd.startsWith(F("MODBUS WRITE "))) {
        String args = cmdLine.substring(13);
        int sp1 = args.indexOf(' ');
        int sp2 = sp1 >= 0 ? args.indexOf(' ', sp1 + 1) : -1;
        if (sp1 < 0 || sp2 < 0) {
            sendVirtualMessage(F("MODBUS WRITE: usage: MODBUS WRITE <device_id> <addr> <value>"), ERROR);
            return true;
        }
        uint8_t devId = (uint8_t)args.substring(0, sp1).toInt();
        uint16_t addr = (uint16_t)args.substring(sp1 + 1, sp2).toInt();
        uint16_t val = (uint16_t)args.substring(sp2 + 1).toInt();
        int8_t found = -1;
        for (uint8_t d = 0; d < g_modbusDeviceCount; ++d)
            if (g_modbusDevices[d].slaveId == devId) { found = (int8_t)d; break; }
        if (found < 0) {
            char err[64];
            snprintf(err, sizeof(err), "MODBUS WRITE: device ID %u not found", (unsigned)devId);
            sendVirtualMessage(String(err), ERROR);
            return true;
        }
        const ModbusDevice& dev = g_modbusDevices[found];
        bool ok = dev.isTcp
            ? modbusTcpWriteSingleReg(dev, addr, val, dev.timeoutMs ? dev.timeoutMs : 500)
            : modbusRtuWriteSingleReg(devId, addr, val, dev.timeoutMs ? dev.timeoutMs : 500);
        if (ok) {
            char msg[64];
            snprintf(msg, sizeof(msg), "MODBUS WRITE: ID=%u addr=0x%04X val=%u", (unsigned)devId, (unsigned)addr, (unsigned)val);
            sendVirtualMessage(String(msg), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(F("MODBUS WRITE: write failed"), ERROR);
        }
        return true;
    }

    if (upperCmd.startsWith(F("MODBUS COIL "))) {
        String args = cmdLine.substring(12);
        int sp1 = args.indexOf(' ');
        int sp2 = sp1 >= 0 ? args.indexOf(' ', sp1 + 1) : -1;
        if (sp1 < 0 || sp2 < 0) {
            sendVirtualMessage(F("MODBUS COIL: usage: MODBUS COIL <device_id> <addr> <0|1>"), ERROR);
            return true;
        }
        uint8_t devId = (uint8_t)args.substring(0, sp1).toInt();
        uint16_t addr = (uint16_t)args.substring(sp1 + 1, sp2).toInt();
        bool coilV = args.substring(sp2 + 1).toInt() != 0;
        int8_t found = -1;
        for (uint8_t d = 0; d < g_modbusDeviceCount; ++d)
            if (g_modbusDevices[d].slaveId == devId) { found = (int8_t)d; break; }
        if (found < 0) {
            char err[64];
            snprintf(err, sizeof(err), "MODBUS COIL: device ID %u not found", (unsigned)devId);
            sendVirtualMessage(String(err), ERROR);
            return true;
        }
        const ModbusDevice& dev = g_modbusDevices[found];
        bool ok = dev.isTcp
            ? modbusTcpWriteCoil(dev, addr, coilV, dev.timeoutMs ? dev.timeoutMs : 500)
            : modbusRtuWriteCoil(devId, addr, coilV, dev.timeoutMs ? dev.timeoutMs : 500);
        if (ok) {
            char msg[72];
            snprintf(msg, sizeof(msg), "MODBUS COIL: ID=%u addr=0x%04X = %s", (unsigned)devId, (unsigned)addr, coilV ? "ON" : "OFF");
            sendVirtualMessage(String(msg), COMMAND_RESPONSE);
        } else {
            sendVirtualMessage(F("MODBUS COIL: write failed"), ERROR);
        }
        return true;
    }

    if (upperCmd.startsWith(F("MODBUS READ "))) {
        String args = cmdLine.substring(12);
        int s1 = args.indexOf(' ');
        int s2 = s1 >= 0 ? args.indexOf(' ', s1+1) : -1;
        int s3 = s2 >= 0 ? args.indexOf(' ', s2+1) : -1;
        if (s1<0 || s2<0 || s3<0) {
            sendVirtualMessage(F("MODBUS READ: usage: MODBUS READ <device_id> <fc> <addr> <count>"), ERROR);
            return true;
        }
        uint8_t devId = (uint8_t)args.substring(0, s1).toInt();
        uint8_t fc = (uint8_t)args.substring(s1+1, s2).toInt();
        uint16_t addr = (uint16_t)args.substring(s2+1, s3).toInt();
        uint8_t cnt = (uint8_t)args.substring(s3+1).toInt();
        if (cnt == 0 || cnt > 16) { sendVirtualMessage(F("MODBUS READ: count 1-16"), ERROR); return true; }
        int8_t readFound = -1;
        for (uint8_t d = 0; d < g_modbusDeviceCount; ++d)
            if (g_modbusDevices[d].slaveId == devId) { readFound = (int8_t)d; break; }
        if (readFound < 0) {
            char err[64];
            snprintf(err, sizeof(err), "MODBUS READ: device ID %u not found", (unsigned)devId);
            sendVirtualMessage(String(err), ERROR); return true;
        }
        const ModbusDevice& rDev = g_modbusDevices[readFound];
        if (!rDev.isTcp && !modbusRtuIsReady()) { sendVirtualMessage(F("MODBUS READ: RTU not ready"), ERROR); return true; }
        uint16_t buf[16] = {};
        bool ok = rDev.isTcp
            ? modbusTcpReadRegs(rDev, fc, addr, cnt, rDev.timeoutMs ? rDev.timeoutMs : 500, buf)
            : modbusRtuReadRegs(devId, fc, addr, cnt, rDev.timeoutMs ? rDev.timeoutMs : 500, buf);
        if (!ok) { sendVirtualMessage(F("MODBUS READ: read failed"), ERROR); return true; }
        String out = String(F("MODBUS READ:"));
        for (uint8_t i = 0; i < cnt; ++i) {
            out += String(F(" [")) + String(i) + String(F("]=")) + String(buf[i]);
        }
        sendVirtualMessage(out, COMMAND_RESPONSE);
        return true;
    }

    sendVirtualMessage(F("MODBUS: unknown command. Try: STATUS, POLL, READ, WRITE, COIL, RESET STATS, SLAVE ON/OFF"), ERROR);
    return true;
}
#endif // ENABLE_MODBUS

static bool handleBootLog(const String& cmdLine, const String& upperCmd) {
    String arg = upperCmd.substring(8);
    arg.trim();
    if (arg.equalsIgnoreCase(String(F("ON")))) {
        configManager.setBootLogEnabled(true);
        sendVirtualMessage(F("BOOT-LOG: ativado"), COMMAND_RESPONSE);
        logSystemEvent(String(F("BOOT-LOG")), String(F("enabled via command")));
    } else if (arg.equalsIgnoreCase(String(F("OFF")))) {
        configManager.setBootLogEnabled(false);
        sendVirtualMessage(F("BOOT-LOG: desativado"), COMMAND_RESPONSE);
        logSystemEvent(String(F("BOOT-LOG")), String(F("disabled via command")));
    } else {
        bool cur = configManager.getBootLogEnabled();
        sendVirtualMessage(
            String(F("BOOT-LOG: ")) + (cur ? String(F("ON")) : String(F("OFF"))) +
            String(F(" | Use: BOOT-LOG ON | BOOT-LOG OFF")),
            COMMAND_RESPONSE);
    }
    return true;
}

#if ENABLE_GRBL == 1
static bool handleGrblLog(const String& cmdLine, const String& upperCmd) {
    String arg = cmdLine.substring(7);
    arg.trim();
    arg.toUpperCase();
    if (arg.equals(String(F("ON")))) {
        grblJobLogSetEnabled(true);
        sendVirtualMessage(String(F("GRBLLOG: ON")), COMMAND_RESPONSE);
    } else if (arg.equals(String(F("OFF")))) {
        grblJobLogSetEnabled(false);
        sendVirtualMessage(String(F("GRBLLOG: OFF")), COMMAND_RESPONSE);
    } else {
        bool cur = grblJobLogIsEnabled();
        sendVirtualMessage(
            String(F("GRBLLOG: ")) + (cur ? String(F("ON")) : String(F("OFF"))) +
            String(F(" | Use: GRBLLOG ON | GRBLLOG OFF")),
            COMMAND_RESPONSE);
    }
    return true;
}
#endif

// =====================================================================
// Registry initialisation — called once at boot
// =====================================================================

void CommandRegistry::begin() {
    _count = 0;

    add("DATE",          handleDateTime, CMD_EXACT);
    add("TIME",          handleDateTime, CMD_EXACT);
    add("GET_DATETIME",  handleDateTime, CMD_EXACT);
    add("REBOOT",        handleReboot,   CMD_EXACT);
    add("SDMUTEXRESET",  handleSdMutexReset, CMD_EXACT);
    add("RETRYSD",       handleRetrySd,  CMD_EXACT);
    add("GET_ALL_CONFIGS", handleGetAllConfigs, CMD_EXACT);
    add("DUMP_NVS",      handleDumpNvs,  CMD_EXACT);

#if ENABLE_WIFI==1
    add("WIFIADD",       handleWifiAdd,  CMD_PREFIX);
    add("WIFILIST",      handleWifiList, CMD_EXACT);
#endif

    add("GET_CHIP_INFO",         handleChipInfo, CMD_EXACT);
    add("GET_FIRMWARE_VERSION", handleChipInfo, CMD_EXACT);
    add("VERSION",       handleVersion, CMD_EXACT);
    add("VER",           handleVersion, CMD_EXACT);
    add("BUILD",         handleVersion, CMD_EXACT);
    add("HELLO",         handleHello,   CMD_EXACT);
    add("GETIP",         handleHello,   CMD_EXACT);

    add("LED?",          handleLedQuery, CMD_EXACT);
    add("LED",           handleLedSet,   CMD_PREFIX);
    add("I2CSCAN",       handleI2cScan,  CMD_EXACT);
    add("DEBUG_LOGS_ON", handleDebugLogsOn, CMD_EXACT);
    add("DEBUG_LOGS_OFF",handleDebugLogsOff, CMD_EXACT);
    add("SD_SPEED_TEST", handleSdSpeedTest, CMD_PREFIX);

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    add("SET_LINE_LIMIT:", handleSetLineLimit, CMD_PREFIX);
#endif

#if ENABLE_DB25==1
    add("SET_CHARSET:",       handleSetCharset,     CMD_PREFIX);
    add("SET_PRINTER_MODE:", handleSetPrinterMode,  CMD_PREFIX);
#endif

#if ENABLE_RS232==1
    add("SET_RS232_ENABLE:",      handleSetRs232Enable,    CMD_PREFIX);
    add("SET_RS232_BAUDRATE:",    handleSetRs232Baudrate,  CMD_PREFIX);
    add("SET_RS232_INVERT_PINS:", handleSetRs232InvertPins, CMD_PREFIX);
#endif

#if ENABLE_SMTP==1
    add("EMAILDAY",  handleEmailDay,  CMD_PREFIX);
    add("LOGDAY",    handleLogDay,    CMD_PREFIX);
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    add("SENSORDAY", handleSensorDay, CMD_PREFIX);
#endif
#if ENABLE_SDCARD==1 && ENABLE_PROXIMITY==1
    add("SHIFTDAY",  handleShiftDay,  CMD_PREFIX);
#endif
#endif

#if ENABLE_MODBUS==1
    add("MODBUS",    handleModbus,   CMD_PREFIX);
#endif

    add("BOOT-LOG",  handleBootLog,  CMD_PREFIX);

#if ENABLE_GRBL==1
    add("GRBLLOG",   handleGrblLog,  CMD_PREFIX);
#endif
}

// =====================================================================
// Registry dispatch
// =====================================================================

bool CommandRegistry::dispatch(const String& cmdLine, const String& upperCmd) {
    for (int i = 0; i < _count; i++) {
        const Entry& e = _entries[i];
        bool match = false;
        if (e.mode == CMD_EXACT) {
            match = upperCmd.equalsIgnoreCase(String(F(e.name)));
        } else {
            match = upperCmd.startsWith(String(F(e.name)));
        }
        if (match) {
            if (e.handler(cmdLine, upperCmd)) {
                return true;
            }
        }
    }
    return false;
}

void CommandRegistry::add(const char* name, CommandHandler handler, CmdMatch mode) {
    if (_count >= MAX_CMD_REGISTRY) return;
    _entries[_count].name = name;
    _entries[_count].handler = handler;
    _entries[_count].mode = mode;
    _count++;
}

// =====================================================================
// Legacy interface kept for backward compatibility
// =====================================================================

CommandRegistry commandRegistry;

bool processSystemCommand(const String &commandLine, const String &upperCaseCommand) {
    return commandRegistry.dispatch(commandLine, upperCaseCommand);
}

void executeCommand(const String &command) {
    String upperCmd = command;
    upperCmd.toUpperCase();
    bool processed = commandRegistry.dispatch(command, upperCmd);
    if (!processed) {
        sendVirtualMessage(String(F("Comando sistema desconhecido: ")) + command, ERROR);
    }
}
