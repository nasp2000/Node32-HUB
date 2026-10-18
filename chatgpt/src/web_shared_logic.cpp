// web_shared_logic.cpp
// Implementation of shared web utility functions declared in web_shared_logic.h.
// These functions were previously inline in the header, causing compile-time bloat
// across 9 translation units. Moved here to compile once.

#include "web_shared_logic.h"
#include "usbip_server.h"
#include "usb_host_manager.h"
#include "littlefs_manager.h"
#include <esp_task_wdt.h>   // feed the task WDT inside the long monitor JSON loops (async_tcp)

bool sharedWebBufferDadosLock(TickType_t timeoutTicks, bool& didLockOut) {
    didLockOut = false;
    if (timeoutTicks == 0) {
        taskENTER_CRITICAL(&bufferDadosSpinlock);
        if (!bufferDadosBusy) {
            bufferDadosBusy = true;
            taskEXIT_CRITICAL(&bufferDadosSpinlock);
            didLockOut = true;
            return true;
        }
        taskEXIT_CRITICAL(&bufferDadosSpinlock);
        return false;
    }
    unsigned long timeoutMs = timeoutTicks * portTICK_PERIOD_MS;
    unsigned long start = millis();
    while (true) {
        taskENTER_CRITICAL(&bufferDadosSpinlock);
        if (!bufferDadosBusy) {
            bufferDadosBusy = true;
            taskEXIT_CRITICAL(&bufferDadosSpinlock);
            didLockOut = true;
            return true;
        }
        taskEXIT_CRITICAL(&bufferDadosSpinlock);
        if ((millis() - start) > timeoutMs) {
            return false;
        }
        taskYIELD();
    }
}

void sharedWebBufferDadosUnlock(bool didLock) {
    if (didLock) {
        bufferDadosBusy = false;
    }
}

String stripControlChars(const String& input) {
    String output;
    output.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if ((uint8_t)c >= 0x20 || c == '\n' || c == '\r' || c == '\t') {
            output += c;
        }
    }
    return output;
}

String sharedWebJsonEscape(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '"': output += F("\\\""); break;
            case '\\': output += F("\\\\"); break;
            case '\n': output += F("\\n"); break;
            case '\r': output += F("\\r"); break;
            case '\t': output += F("\\t"); break;
            default:
                if ((uint8_t)c < 32) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04X", (unsigned char)c);
                    output += escaped;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

String normalizeEmailToken(const String& tokenRaw) {
    String token = tokenRaw;
    token.trim();
    if (token.length() == 0) return String();

    int lt = token.lastIndexOf('<');
    int gt = token.lastIndexOf('>');
    if (lt >= 0 && gt > lt) {
        String emailOnly = token.substring(lt + 1, gt);
        emailOnly.trim();
        return emailOnly;
    }

    if (token.startsWith("<") && token.endsWith(">") && token.length() > 2) {
        token = token.substring(1, token.length() - 1);
        token.trim();
    }
    return token;
}

String normalizeEmailDestinationList(const String& rawList, bool& anyInvalid, bool& allInvalid) {
    anyInvalid = false;
    allInvalid = false;

    String normalized;
    normalized.reserve(rawList.length());
    String seenLower = "\n";
    seenLower.reserve(rawList.length() + 2);
    bool hadAnyToken = false;
    bool hasValid = false;

    int position = 0;
    while (position < (int)rawList.length()) {
        int comma = rawList.indexOf(',', position);
        int semicolon = rawList.indexOf(';', position);
        int next = -1;
        if (comma < 0) next = semicolon;
        else if (semicolon < 0) next = comma;
        else next = min(comma, semicolon);
        if (next < 0) next = rawList.length();

        String piece = rawList.substring(position, next);
        position = next + 1;
        piece.trim();
        if (piece.length() == 0) continue;
        hadAnyToken = true;

        String emailOnly = normalizeEmailToken(piece);
        int atPos = emailOnly.indexOf('@');
        if (emailOnly.length() == 0 || atPos <= 0 || atPos >= (int)emailOnly.length() - 1) {
            anyInvalid = true;
            continue;
        }

        String lower = emailOnly;
        lower.toLowerCase();
        String marker = String('\n') + lower + String('\n');
        if (seenLower.indexOf(marker) >= 0) continue;

        if (normalized.length()) normalized += F(", ");
        normalized += emailOnly;
        seenLower += lower;
        seenLower += '\n';
        hasValid = true;
    }

    allInvalid = hadAnyToken && !hasValid;
    return normalized;
}

bool sharedWebValidIPv4(const String& value) {
    int parts = 0;
    int start = 0;
    while (start < (int)value.length()) {
        int dot = value.indexOf('.', start);
        String segment = (dot == -1) ? value.substring(start) : value.substring(start, dot);
        segment.trim();
        if (segment.length() == 0) return false;
        for (size_t i = 0; i < segment.length(); ++i) {
            if (!isDigit((unsigned char)segment[i])) return false;
        }
        int part = segment.toInt();
        if (part < 0 || part > 255) return false;
        parts++;
        if (dot == -1) break;
        start = dot + 1;
    }
    return parts == 4;
}

bool sharedWebValidMac(String mac) {
    mac.trim();
    if (!mac.length()) return true;
    mac.toUpperCase();
    mac.replace('-', ':');
    int colons = 0;
    int hexCount = 0;
    for (size_t i = 0; i < mac.length(); ++i) {
        char c = mac[i];
        if (c == ':') {
            if (hexCount != 2) return false;
            hexCount = 0;
            colons++;
            continue;
        }
        if (!isxdigit((unsigned char)c)) return false;
        hexCount++;
        if (hexCount > 2) return false;
    }
    return (hexCount == 2) && (colons == 5);
}

void sharedWebAppendUInt64(String& out, uint64_t value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
    out += buffer;
}

String sharedWebBuildEthernetStatusJson() {
    const UsbIpServerStats usbipStats = usbipServerGetStats();
    String json;
    json.reserve(420);
    json += F("{\"status\":\"ok\",\"mode\":\"");
    json += sharedWebJsonEscape(configManager.getEthernetMode());
    json += F("\",\"ip\":\"");
#include "feature_flags.h"
#if ENABLE_ETHERNET==1
    IPAddress ip = Ethernet.localIP();
    if (ip[0] || ip[1] || ip[2] || ip[3]) json += ip.toString();
    else json += F("0.0.0.0");
    json += F("\",\"mac\":\"");
    byte mac[6];
    Ethernet.MACAddress(mac);
    char macBuffer[18];
    snprintf(macBuffer, sizeof(macBuffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    json += macBuffer;
#else
    json += F("disabled\",\"mac\":\"disabled");
#endif
    json += F("\",\"gateway\":\"");
    json += sharedWebJsonEscape(configManager.getEthernetGW());
    json += F("\",\"dns\":\"");
    json += sharedWebJsonEscape(configManager.getEthernetDNS());
    json += F("\",\"mac_override\":\"");
    json += sharedWebJsonEscape(configManager.getEthernetMacOverride());
    // DHCP-assigned values from the Ethernet library (always current, regardless of mode)
#if ENABLE_ETHERNET==1
    json += F("\",\"dhcp_gw\":\"");
    json += Ethernet.gatewayIP().toString();
    json += F("\",\"dhcp_dns\":\"");
    json += Ethernet.dnsServerIP().toString();
#else
    json += F("\",\"dhcp_gw\":\"0.0.0.0\",\"dhcp_dns\":\"0.0.0.0");
#endif
    json += F("\",\"dhcp\":");
    json += (configManager.getEthernetMode() == "dhcp") ? F("true") : F("false");
    json += F(",\"usbip_enabled\":");
#if ENABLE_USBIP_SERVER==1
    json += F("true");
#else
    json += F("false");
#endif
    json += F(",\"usbip_running\":");
    json += usbipStats.running ? F("true") : F("false");
    json += F(",\"usbip_port\":");
    json += String((unsigned long)usbipStats.listen_port);
    json += F(",\"usbip_clients\":");
    json += String((unsigned long)usbipStats.active_clients);
    json += F(",\"usbip_accepted_clients\":");
    json += String((unsigned long)usbipStats.accepted_clients);
    json += F(",\"usbip_rx_bytes\":");
    json += String((unsigned long)usbipStats.rx_bytes);
    json += F(",\"usbip_tx_bytes\":");
    json += String((unsigned long)usbipStats.tx_bytes);
    json += '}';
    return json;
}

String sharedWebBuildUsbServerStatusJson() {
    const UsbIpServerStats stats = usbipServerGetStats();
    const UsbIpRuntimeConfig runtime = usbipServerGetRuntimeConfig();

    String json;
    json.reserve(320);
    json += F("{\"success\":true,\"compiled\":");
#if ENABLE_USBIP_SERVER==1
    json += F("true");
#else
    json += F("false");
#endif
    json += F(",\"running\":");
    json += stats.running ? F("true") : F("false");
    json += F(",\"enabled\":");
    json += runtime.enabled ? F("true") : F("false");
    json += F(",\"port\":");
    json += String((unsigned long)runtime.listen_port);
    json += F(",\"recv_timeout_ms\":");
    json += String((unsigned long)runtime.recv_timeout_ms);
    json += F(",\"in_wait_timeout_ms\":");
    json += String((unsigned long)runtime.in_wait_timeout_ms);
    json += F(",\"active_clients\":");
    json += String((unsigned long)stats.active_clients);
    json += F(",\"accepted_clients\":");
    json += String((unsigned long)stats.accepted_clients);
    json += F(",\"rx_bytes\":");
    json += String((unsigned long)stats.rx_bytes);
    json += F(",\"tx_bytes\":");
    json += String((unsigned long)stats.tx_bytes);
    json += F(",\"rx_bytes_per_sec\":");
    json += String((unsigned long)stats.rx_bytes_per_sec);
    json += F(",\"tx_bytes_per_sec\":");
    json += String((unsigned long)stats.tx_bytes_per_sec);
    json += F(",\"devices_dirty\":");
    json += stats.devices_dirty ? F("true") : F("false");
    json += F("}");
    return json;
}

String sharedWebBuildUsbServerConfigJson() {
    JsonVariant section = configManager.getSection("usb_server");

    String json;
    json.reserve(240);
    json += F("{\"success\":true,\"enabled\":");
    json += (section["enabled"] | true) ? F("true") : F("false");
    json += F(",\"port\":");
    json += String((unsigned long)(section["port"] | 3240));
    json += F(",\"recv_timeout_ms\":");
    json += String((unsigned long)(section["recv_timeout_ms"] | 1200));
    json += F(",\"in_wait_timeout_ms\":");
    json += String((unsigned long)(section["in_wait_timeout_ms"] | 220));
    json += F("}");
    return json;
}

String sharedWebBuildUsbServerDevicesJson() {
    String json;
    json.reserve(1024);
    json += F("{\"success\":true,\"devices\":[");

    bool first = true;
    for (uint8_t slot = 0; slot < USB_HOST_MAX_CDC_DEVICES; ++slot) {
        const UsbCdcDevice dev = usbHostGetDevice(slot);
        if (!dev.connected) continue;

        if (!first) json += ',';
        first = false;

        json += F("{\"slot\":");
        json += String((unsigned long)slot);
        json += F(",\"dev_addr\":");
        json += String((unsigned long)dev.dev_addr);
        json += F(",\"description\":\"");
        json += sharedWebJsonEscape(String(dev.description));
        json += F("\",\"speed\":");
        json += String((unsigned long)dev.speed);
        json += F(",\"vid\":");
        json += String((unsigned long)dev.vid);
        json += F(",\"pid\":");
        json += String((unsigned long)dev.pid);
        json += F(",\"bcd_device\":");
        json += String((unsigned long)dev.bcd_device);
        json += F(",\"iface_class\":");
        json += String((unsigned long)dev.iface_class);
        json += F(",\"iface_subclass\":");
        json += String((unsigned long)dev.iface_subclass);
        json += F(",\"iface_protocol\":");
        json += String((unsigned long)dev.iface_protocol);
        json += F(",\"in_ep_addr\":");
        json += String((unsigned long)dev.in_ep_addr);
        json += F(",\"out_ep_addr\":");
        json += String((unsigned long)dev.out_ep_addr);
        json += F(",\"rx_bytes\":");
        json += String((unsigned long)dev.rx_bytes);
        json += F(",\"tx_bytes\":");
        json += String((unsigned long)dev.tx_bytes);
        json += F("}");
    }

    json += F("]}");
    return json;
}

bool sharedWebApplyUsbServerConfig(const SharedUsbServerConfigInput& input, String& responseBody) {
    if (input.port == 0) {
        responseBody = F("{\"success\":false,\"message\":\"porta_invalida\"}");
        return false;
    }
    if (input.recvTimeoutMs < 100 || input.recvTimeoutMs > 60000) {
        responseBody = F("{\"success\":false,\"message\":\"recv_timeout_invalido\"}");
        return false;
    }
    if (input.inWaitTimeoutMs < 20 || input.inWaitTimeoutMs > 5000) {
        responseBody = F("{\"success\":false,\"message\":\"in_wait_timeout_invalido\"}");
        return false;
    }

    JsonVariant section = configManager.getSection("usb_server");
    section["enabled"] = input.enabled;
    section["port"] = input.port;
    section["recv_timeout_ms"] = input.recvTimeoutMs;
    section["in_wait_timeout_ms"] = input.inWaitTimeoutMs;

    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("usb_server")) {
        responseBody = F("{\"success\":false,\"message\":\"erro_guardar_littlefs\"}");
        return false;
    }

    UsbIpRuntimeConfig runtimeCfg;
    runtimeCfg.enabled = input.enabled;
    runtimeCfg.listen_port = input.port;
    runtimeCfg.recv_timeout_ms = input.recvTimeoutMs;
    runtimeCfg.in_wait_timeout_ms = input.inWaitTimeoutMs;
    usbipServerSetRuntimeConfig(runtimeCfg);

    responseBody = F("{\"success\":true,\"message\":\"saved\"}");
    return true;
}

bool sharedWebApplyEthernetConfig(const SharedEthernetConfigInput& input, String& responseBody) {
    String mode = input.mode.length() ? input.mode : String(F("dhcp"));
    mode.toLowerCase();
    if (!(mode == "dhcp" || mode == "static")) {
        responseBody = F("{\"status\":\"erro\",\"msg\":\"modo_invalido\"}");
        return false;
    }
    if (mode == "static" && !(sharedWebValidIPv4(input.ip) && sharedWebValidIPv4(input.gw) && sharedWebValidIPv4(input.dns))) {
        responseBody = F("{\"status\":\"erro\",\"msg\":\"ipv4_invalido\"}");
        return false;
    }
    if (input.mac.length() && !sharedWebValidMac(input.mac)) {
        responseBody = F("{\"status\":\"erro\",\"msg\":\"mac_invalido\"}");
        return false;
    }

    configManager.setEthernetMode(mode);
    if (mode == "static") {
        configManager.setEthernetIP(input.ip);
        configManager.setEthernetGW(input.gw);
        configManager.setEthernetDNS(input.dns);
    } else {
        configManager.setEthernetIP("");
        configManager.setEthernetGW("");
        configManager.setEthernetDNS("");
    }
    // Never wipe the stored MAC override on a partial form POST. Only update it
    // when a valid MAC was actually provided; otherwise keep the current value so
    // a round-trip `mode=dhcp` cannot zero out the interface MAC.
    if (input.mac.length()) {
        configManager.setEthernetMacOverride(input.mac);
    }
    configManager.saveSection("ethernet");
    responseBody = F("{\"status\":\"ok\"}");
    return true;
}

String sharedWebBuildSdStatusJson() {
    bool mounted = sdcardIsMounted();
    String json;
    json.reserve(256);
    json += F("{\"status\":\"ok\",\"mounted\":");
    json += mounted ? F("true") : F("false");
    json += F(",\"sd_status\":");
    json += String(sdcardStatus());
    json += F(",\"storage_mode\":\"");
    json += sharedWebJsonEscape(String(getStorageModeString()));
    json += F("\",\"is_psram\":");
    json += (currentStorageMode == STORAGE_MODE_PSRAM) ? F("true") : F("false");
    if (mounted) {
        json += F(",\"total_bytes\":");
        sharedWebAppendUInt64(json, sdcardTotalBytes());
        json += F(",\"used_bytes\":");
        sharedWebAppendUInt64(json, sdcardUsedBytes());
    }
#if ENABLE_FILESYSTEM==1
    // LittleFS (Flash) fallback storage — always reported so the
    // "Configurar Armazenamento" page shows it with or without SD.
    if (littlefsMounted()) {
        json += F(",\"flash_total_bytes\":");
        sharedWebAppendUInt64(json, littlefsTotalBytes());
        json += F(",\"flash_used_bytes\":");
        sharedWebAppendUInt64(json, littlefsUsedBytes());
    }
#endif
    json += '}';
    return json;
}

String sharedWebBuildSdcardConfigJson() {
    String json;
    json.reserve(320);
    json += F("{\"status\":\"ok\",\"enabled\":");
    json += configManager.getSdcardEnabled() ? F("true") : F("false");
    json += F(",\"flush_interval\":\"");
    json += sharedWebJsonEscape(configManager.getSdcardFlushInterval());
    json += F("\",\"format\":\"");
    json += sharedWebJsonEscape(configManager.getSdcardFormat());
    json += F("\",\"structured\":");
    json += configManager.getSdcardStructured() ? F("true") : F("false");
    json += F(",\"email_send\":");
    json += configManager.getSdcardEmailSend() ? F("true") : F("false");
    json += F(",\"email_from_sd\":");
    json += configManager.getSdcardEmailFromSd() ? F("true") : F("false");
    json += F(",\"max_size_mb\":");
    json += String(configManager.getSdcardMaxSize());
    json += F(",\"emailday_extra_emails\":\"");
    json += sharedWebJsonEscape(configManager.getEmaildayExtraEmails());
    json += F("\",\"auto_cleanup\":");
    json += configManager.getSdAutoCleanup() ? F("true") : F("false");
    json += F(",\"cleanup_threshold\":");
    json += String(configManager.getSdCleanupThreshold());
    json += F(",\"keep_days\":");
    json += String(configManager.getSdKeepDays());
    json += F("}");
    return json;
}

bool sharedWebApplySdcardConfig(const SharedSdcardConfigInput& input, String& responseBody) {
    // Minimum buffer-write interval is 10m (flash/SD wear protection).
    // Legacy sub-10m values are coerced to 10m instead of rejected.
    String flushInterval = input.flushInterval;
    if (!(flushInterval == "10m" || flushInterval == "30m" || flushInterval == "60m")) {
        if (flushInterval == "2s" || flushInterval == "10s" || flushInterval == "15s" ||
            flushInterval == "30s" || flushInterval == "1m" || flushInterval == "5m") {
            flushInterval = "10m";
        } else {
            responseBody = F("{\"status\":\"error\",\"msg\":\"invalid_flush_interval\"}");
            return false;
        }
    }
    if (!(input.format == "txt" || input.format == "csv" || input.format == "bin")) {
        responseBody = F("{\"status\":\"error\",\"msg\":\"invalid_format\"}");
        return false;
    }
    if (input.maxSizeMb <= 0 || input.maxSizeMb > 32) {
        responseBody = F("{\"status\":\"error\",\"msg\":\"invalid_max_size\"}");
        return false;
    }
    if (input.cleanupThreshold < 50 || input.cleanupThreshold > 95) {
        responseBody = F("{\"status\":\"error\",\"msg\":\"invalid_cleanup_threshold\"}");
        return false;
    }

    configManager.setSdcardEnabled(input.enabled);
    configManager.setSdcardStructured(input.structured);
    configManager.setSdcardEmailSend(input.emailSend);
    configManager.setSdcardEmailFromSd(input.emailFromSd);
    configManager.setSdcardFlushInterval(flushInterval);
    configManager.setSdcardFormat(input.format);
    configManager.setSdcardMaxSize(input.maxSizeMb);
    configManager.setEmaildayExtraEmails(input.extraEmails);
    configManager.setSdAutoCleanup(input.autoCleanup);
    configManager.setSdCleanupThreshold(input.cleanupThreshold);
    configManager.setSdKeepDays(input.keepDays);
    configManager.markDirtyAndScheduleSave();
    responseBody = F("{\"status\":\"ok\",\"msg\":\"Config saved\"}");
    return true;
}

String sharedWebApplySdAction(const String& action, bool& ok) {
    String actionLower = action;
    actionLower.toLowerCase();
    const char* msg = "Invalid action";
    ok = false;
    if (actionLower == "mount") {
        sdcardBeginAsync();
        ok = true;
        msg = "Mount started in background";
    } else if (actionLower == "unmount") {
        ok = sdcardUnmount();
        msg = ok ? "Unmounted" : "Unmount failed";
    } else if (actionLower == "refresh") {
        if (sdcardIsMounted()) {
            ok = true;
            msg = "Updated";
        } else {
            sdcardBeginAsync();
            ok = true;
            msg = "Mount started in background";
        }
    }

    String json;
    json.reserve(128);
    json += F("{\"status\":\"");
    json += ok ? F("ok") : F("error");
    json += F("\",\"message\":\"");
    json += sharedWebJsonEscape(String(msg));
    json += F("\"}");
    return json;
}

String sharedWebBuildMonitorStatusJson() {
    unsigned long bufSz = 0;
    // Use spinlock for an atomic snapshot of bufferDados.size() without full lock
    taskENTER_CRITICAL(&bufferDadosSpinlock);
    bool busy = bufferDadosBusy;
    taskEXIT_CRITICAL(&bufferDadosSpinlock);
    if (!busy) {
        bool didLock = false;
        if (sharedWebBufferDadosLock(pdMS_TO_TICKS(5), didLock)) {
            bufSz = (unsigned long)bufferDados.size();
            sharedWebBufferDadosUnlock(didLock);
        }
    }
    unsigned long bufCap = (unsigned long)bufferDados.capacity();

    String json;
    json.reserve(192);
    json += F("{\"status\":\"ok\",\"isReceiving\":");
    json += isReceivingData ? F("true") : F("false");
    json += F(",\"bufferUsage\":");
    json += String(bufSz);
    json += F(",\"bufferSize\":");
    json += String(bufCap);
    json += F(",\"buffer_seq\":");
    json += String((unsigned long)bufferDadosSeq);
    json += F(",\"last_rx_ms\":");
    json += String((unsigned long)lastReceptionTime);
    json += F(",\"now_ms\":");
    json += String((unsigned long)millis());
    json += '}';
    return json;
}

bool sharedWebBuildReceivedText(int limit, int maxBytesWindow, bool includePartial, String& output) {
    if (limit < 0) limit = 0;
    if (maxBytesWindow <= 0) maxBytesWindow = 4096;

    bool didLock = false;
    if (!sharedWebBufferDadosLock(pdMS_TO_TICKS(20), didLock)) {
        return false;
    }

    output = String();
    output.reserve((size_t)maxBytesWindow + 128U);

    int bufSize = (int)bufferDados.size();
    int window = bufSize;
    if (window > maxBytesWindow) window = maxBytesWindow;
    int start = bufSize - window;
    if (start < 0) start = 0;
    bool lastEndsWithNewline = (bufSize > 0 && bufferDados[bufSize - 1] == '\n');

    String line;
    line.reserve(128);
    int linesEmitted = 0;
    for (int i = start; i < bufSize; ++i) {
        uint8_t b = bufferDados[i];
        if (b == '\r') continue;
        if (b == '\n') {
            if (line.length() > 0) {
                int tabPos = line.indexOf('\t');
                if (tabPos > 0 && tabPos <= 20) {
                    String possibleTs = line.substring(0, tabPos);
                    if (possibleTs.length() == 19 && possibleTs.charAt(4) == '-' && possibleTs.charAt(7) == '-' && possibleTs.charAt(10) == ' ' && possibleTs.charAt(13) == ':' && possibleTs.charAt(16) == ':') {
                        line = line.substring(tabPos + 1);
                    }
                }
                output += line;
                output += F("\r\n");
                linesEmitted++;
            }
            line = String();
            if (limit > 0 && linesEmitted >= limit) break;
            continue;
        }
        if (b == '\t') line += '\t';
        else if (b >= 32 && b != 127) line += (char)b;
        else {
            char hex[6];
            snprintf(hex, sizeof(hex), "<%02X>", b);
            line += hex;
        }
    }

    if ((limit == 0 || linesEmitted < limit) && (lastEndsWithNewline || includePartial) && line.length() > 0) {
        int tabPos = line.indexOf('\t');
        if (tabPos > 0 && tabPos <= 20) {
            String possibleTs = line.substring(0, tabPos);
            if (possibleTs.length() == 19 && possibleTs.charAt(4) == '-' && possibleTs.charAt(7) == '-' && possibleTs.charAt(10) == ' ' && possibleTs.charAt(13) == ':' && possibleTs.charAt(16) == ':') {
                line = line.substring(tabPos + 1);
            }
        }
        output += line;
        output += F("\r\n");
    }

    sharedWebBufferDadosUnlock(didLock);
    return true;
}

bool sharedWebClearReceivedBuffer(String& responseBody) {
    bool didLock = false;
    if (!sharedWebBufferDadosLock(pdMS_TO_TICKS(30), didLock)) {
        return false;
    }

    unsigned long removed = 0;
    while (!bufferDados.isEmpty()) {
        bufferDados.pop();
        removed++;
    }
    bufferDadosSeq = 0;
    lastReceptionTime = millis();
    sharedWebBufferDadosUnlock(didLock);

    responseBody = F("{\"status\":\"ok\",\"removed\":");
    responseBody += String(removed);
    responseBody += '}';
    return true;
}

void sharedWebWriteJsonEscaped(Print& out, const char* input) {
    if (!input) return;
    while (*input) {
        char c = *input++;
        switch (c) {
            case '"': out.print(F("\\\"")); break;
            case '\\': out.print(F("\\\\")); break;
            case '\n': out.print(F("\\n")); break;
            case '\r': out.print(F("\\r")); break;
            case '\t': out.print(F("\\t")); break;
            default:
                if ((uint8_t)c < 32) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04X", (unsigned char)c);
                    out.print(escaped);
                } else {
                    out.write((uint8_t)c);
                }
                break;
        }
    }
}

void sharedWebWriteJsonEscaped(Print& out, const String& input) {
    sharedWebWriteJsonEscaped(out, input.c_str());
}

String sharedRunSystemCommandCapture(const String& command) {
    String cmd = command;
    cmd.trim();
    if (cmd.length() == 0) return String();
    beginSystemCommandCapture();
    processLine(cmd, millis());
    return endSystemCommandCapture();
}

void sharedWebWriteSystemCommandResponse(Print& out, const String& command) {
    String response;
    bool success = false;
    bool streamed = false;
    String trimmedCommand = command;
    trimmedCommand.trim();

    if (trimmedCommand.length() == 0) {
        response = F("Empty command");
    } else {
        String commandLower = trimmedCommand;
        commandLower.toLowerCase();
        String captured = sharedRunSystemCommandCapture(trimmedCommand);
        if (captured.length() > 0) {
            response = captured;
            success = true;
            streamed = true; // output already in the Web Serial Monitor buffer
        } else if (commandLower.startsWith("emailday")) {
            response = F("📧 EMAILDAY started in background. Check monitor for progress (estimated 30-60s).");
            success = true;
        } else if (commandLower == "help") {
            response = F("📋 Command list sent to monitor. Check monitor output.");
            success = true;
        } else if (commandLower == "getip") {
            if (WiFi.status() == WL_CONNECTED) {
                response = String(F("📍 IP: ")) + WiFi.localIP().toString() + String(F(" | SSID: ")) + WiFi.SSID() + String(F(" | RSSI: ")) + String(WiFi.RSSI()) + String(F("dBm"));
            } else if (hasNetworkConnectivity()) {
                response = String(F("📍 IP: ")) + getCurrentNetworkIPString() + String(F(" | Ethernet"));
            } else {
                response = F("📶 Network disconnected");
            }
            success = true;
        } else if (commandLower == "hello") {
            response = String(F("👋 ESP32 system active! ")) + String(millis() / 1000UL) + String(F("s uptime"));
            success = true;
        } else if (commandLower.startsWith("hellp")) {
            response = F("❓ Did you mean 'help'?");
            success = true;
        } else if (commandLower == "status") {
            response = String(F("📊 Heap:")) + String(ESP.getFreeHeap()) + String(F(" Net:")) + String(hasNetworkConnectivity() ? F("on") : F("off")) + String(F(" RS232:")) + String(rs232_enabled ? F("on") : F("off"));
            success = true;
        } else if (commandLower == "get_chip_info") {
            response = F("ℹ️ Chip info sent to monitor.");
            success = true;
        } else if (commandLower == "debug_logs_on") {
            response = F("Debug logs ON");
            success = true;
        } else if (commandLower == "debug_logs_off") {
            response = F("Debug logs OFF");
            success = true;
        } else {
            response = String(F("Command '")) + trimmedCommand + String(F("' sent. Check monitor for full output."));
            success = true;
        }
    }

    out.print(F("{\"success\":"));
    out.print(success ? F("true") : F("false"));
    out.print(F(",\"streamed\":"));
    out.print(streamed ? F("true") : F("false"));
    out.print(F(",\"response\":\""));
    sharedWebWriteJsonEscaped(out, response);
    out.print(F("\"}"));
}

#if ENABLE_RS232==1
void sharedWebWriteRs232CommandResponse(Print& out, const String& command, const String& templateName) {
    String effectiveTemplate = templateName;
    if (effectiveTemplate.length() == 0) {
        effectiveTemplate = configManager.getRs232CurrentTemplate();
    }

    String trimmedCommand = command;
    trimmedCommand.trim();
    String commandLower = trimmedCommand;
    commandLower.toLowerCase();

    if (trimmedCommand.length() == 0) {
        out.print(F("{\"success\":false,\"response\":\"No command provided\"}"));
        return;
    }

    if (commandLower == "test" || commandLower == "teste") {
        bool queued = enqueueRS232Command(String(F("RS232_TEST")), String(), String(F("WEB")), String(F("rs232")));
        out.print(F("{\"success\":"));
        out.print(queued ? F("true") : F("false"));
        out.print(F(",\"response\":\""));
        out.print(queued ? F("✅ RS232 test queued: RS232_TEST") : F("❌ Queue full"));
        out.print(F("\"}"));
        return;
    }

    if (commandLower == "status") {
        int baudValue = rs232_baudrate;
        if (rs232_baudrate_index >= 0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT) {
            baudValue = RS232_BAUDRATES[rs232_baudrate_index];
        }
        String response = String(F("RS232 ")) + String(rs232_enabled ? F("enabled") : F("disabled")) + String(F(" | Baudrate: ")) + String(baudValue) + String(F(" | Pins ")) + String(rs232_invert_pins ? F("inverted") : F("normal"));
        out.print(F("{\"success\":true,\"response\":\""));
        sharedWebWriteJsonEscaped(out, response);
        out.print(F("\"}"));
        return;
    }

    if (commandLower == "help") {
        out.print(F("{\"success\":true,\"response\":\"📋 Commands: test,status,help or any other (queued)\"}"));
        return;
    }

    bool queued = enqueueRS232Command(trimmedCommand, effectiveTemplate, String(F("WEB")), String(F("rs232")));
    out.print(F("{\"success\":"));
    out.print(queued ? F("true") : F("false"));
    out.print(F(",\"response\":\""));
    out.print(queued ? F("✅ Command queued") : F("❌ RS232 queue full"));
    out.print(F("\",\"rs232_enabled\":"));
    out.print(rs232_enabled ? F("true") : F("false"));
    out.print(F(",\"rs232_paused_by_controller\":"));
    out.print(rs232_paused_by_controller ? F("true") : F("false"));
    out.print(F(",\"rs232_blocked_for_email\":"));
    out.print(rs232_blockQueueForEmail ? F("true") : F("false"));
    out.print(F(",\"rs232_pending\":"));
    out.print((int)rs232SendQueue.size());
    out.print(F(",\"rs232_baudrate\":"));
    out.print(rs232_baudrate);
    out.print(F("}"));
}
#endif // ENABLE_RS232

#if ENABLE_MODBUS==1
void sharedWebWriteModbusCommandResponse(Print& out, const String& command) {
    String trimmed = command;
    trimmed.trim();
    if (trimmed.length() == 0) {
        out.print(F("{\"success\":false,\"response\":\"No command provided\"}"));
        return;
    }
    bool queued = enqueueRS232Command(trimmed, String(""), String(F("WEB")), String(F("modbus")));
    out.print(F("{\"success\":"));
    out.print(queued ? F("true") : F("false"));
    out.print(F(",\"response\":\""));
    out.print(queued ? F("\u2705 Comando Modbus em fila") : F("\u274c Fila cheia"));
    out.print(F("\"}")); 
}
#endif // ENABLE_MODBUS

void sharedWebStreamSerialMonitorJson(
    Print& out,
    int limitMsgs,
    bool hasSince,
    size_t sinceValue,
    bool skipCmds,
    bool hasLastSeq,
    unsigned long lastSeqValue,
    int maxBytesWindow,
    int limitLines,
    bool debug
) {
    const int hardMaxMessages = 500;
    if (limitMsgs <= 0 || limitMsgs > hardMaxMessages) {
        limitMsgs = hardMaxMessages;
    }

    out.print(F("{\"heap_free\":"));
    out.print(ESP.getFreeHeap());
    out.print(F(",\"psram_found\":"));
    out.print(psramFound() ? F("true") : F("false"));

    out.print(F(",\"messages\":["));
    if (psramLogBuffer && psramLogCount > 0) {
        // When NTP becomes valid after boot, old log entries may still contain
        // boot-relative seconds (millis/1000). Remap those legacy entries to
        // wall-clock so the Serial Monitor timeline stays coherent.
        bool canRemapLegacyTs = false;
        unsigned long displayEpochBase = 0;
        {
            time_t nowTs = time(nullptr);
            if (nowTs > 1000000000UL) {
                struct tm li;
                struct tm gm;
                localtime_r(&nowTs, &li);
                gmtime_r(&nowTs, &gm);

                long tzOffset = (long)(li.tm_hour - gm.tm_hour) * 3600L
                              + (long)(li.tm_min  - gm.tm_min)  * 60L
                              + (long)(li.tm_sec  - gm.tm_sec);
                if (tzOffset < -43200L) tzOffset += 86400L;
                if (tzOffset >  43200L) tzOffset -= 86400L;

                long displayNow = (long)nowTs + tzOffset;
                if (displayNow > 0) {
                    unsigned long uptimeSec = millis() / 1000UL;
                    if ((unsigned long)displayNow > uptimeSec) {
                        displayEpochBase = (unsigned long)displayNow - uptimeSec;
                        canRemapLegacyTs = true;
                    }
                }
            }
        }

        size_t bufCount = (psramLogCount < SHARED_WEB_LOG_BUFFER_SIZE) ? psramLogCount : SHARED_WEB_LOG_BUFFER_SIZE;
        size_t start = (psramLogCount >= SHARED_WEB_LOG_BUFFER_SIZE) ? psramLogHead : 0;
        size_t iBegin = 0;
        size_t iCount = bufCount;

        if (hasSince) {
            if (sinceValue >= psramLogCount) {
                iCount = 0;
            } else {
                size_t oldestAbs = psramLogCount - bufCount;
                iBegin = (sinceValue > oldestAbs) ? (sinceValue - oldestAbs) : 0;
                iCount = bufCount - iBegin;
            }
        }

        if ((int)iCount > limitMsgs) {
            iBegin += iCount - (size_t)limitMsgs;
            iCount = (size_t)limitMsgs;
        }

        bool first = true;
        unsigned int wdtFeedMsgs = 0;
        for (size_t i = iBegin; i < iBegin + iCount; ++i) {
            // Long dumps must feed the task WDT (async_tcp is subscribed); the
            // escaped JSON build + stream resize used to take >5 s at the first
            // serial-monitor poll after boot and rebooted the board (TWDT).
            if ((++wdtFeedMsgs & 31) == 0) esp_task_wdt_reset();
            size_t idx = (start + i) % SHARED_WEB_LOG_BUFFER_SIZE;
            unsigned long msgTs = psramLogBuffer[idx].timestamp;
            if (canRemapLegacyTs && msgTs < 1000000000UL) {
                msgTs = displayEpochBase + msgTs;
            }
            if (!first) out.print(',');
            out.print(F("{\"ts\":"));
            out.print(msgTs);
            out.print(F(",\"timestamp\":"));
            out.print(msgTs);
            out.print(F(",\"text\":\""));
            sharedWebWriteJsonEscaped(out, psramLogBuffer[idx].text);
            out.print(F("\",\"type\":\""));
            sharedWebWriteJsonEscaped(out, psramLogBuffer[idx].type);
            out.print(F("\"}"));
            first = false;
        }
    }
    out.print(F("]"));

    out.print(F(",\"rs232_commands\":["));
#if ENABLE_RS232==1
    if (!skipCmds) {
        bool first = true;
        for (size_t i = 0; i < rs232SendQueue.size(); ++i) {
            if (!first) out.print(',');
            out.print(F("{\"command\":\""));
            sharedWebWriteJsonEscaped(out, rs232SendQueue[i]);
            out.print(F("\"}"));
            first = false;
        }
    }
#endif
    out.print(F("]"));

    out.print(F(",\"received_data\":["));
    bool resetNeeded = false;
    {
        bool bufferDadosLocked = false;
        bool didLock = false;
        if (sharedWebBufferDadosLock(pdMS_TO_TICKS(10), didLock)) {
            bufferDadosLocked = true;
        }

        unsigned long currentSeq = bufferDadosSeq;
        unsigned long delta = currentSeq - lastSeqValue;
        int bufSize = (int)bufferDados.size();
        int window = 0;
        if (maxBytesWindow <= 0) maxBytesWindow = 0;
        if (limitLines < 0) limitLines = 0;

        if (hasLastSeq && delta > (unsigned long)bufferDados.capacity()) {
            resetNeeded = true;
        } else if (hasLastSeq && delta == 0) {
            window = 0;
        } else if (hasLastSeq) {
            window = (int)min((unsigned long)bufSize, delta);
            if (maxBytesWindow > 0 && window > maxBytesWindow) window = maxBytesWindow;
        } else {
            window = min(bufSize, 1024);
            if (limitLines == 0) limitLines = 8;
        }

        if (window > 0) {
            int start = bufSize - window;
            if (start < 0) start = 0;
            String line;
            line.reserve(128);
            int linesEmitted = 0;
            bool first = true;
            bool useRecipientFormat = false;
            bool filterHeaders = configManager.getAmadaFilterHeaders();

            unsigned int wdtFeedBytes = 0;
            for (int i = start; i < bufSize; ++i) {
                // Whole-buffer scans must keep feeding the task WDT (see the
                // messages loop above — same TWDT reboot risk).
                if ((++wdtFeedBytes & 511) == 0) esp_task_wdt_reset();
                uint8_t b = bufferDados[i];
                if (b == '\r') continue;
                if (b == '\n') {
                    String trimmed = line;
                    trimmed.trim();
                    if (trimmed.length() > 0) {
                        if (filterHeaders && isHeaderLine(trimmed)) {
                            line = String();
                            continue;
                        }

                        String displayLine = trimmed;
                        if (looksLikeISQ20_MTF1(trimmed)) {
                            String formatted;
                            if (useRecipientFormat) buildISQ20_MTF1_CSVRow(trimmed, formatted);
                            else formatted = trimmed;
                            if (formatted.length() > 0) displayLine = formatted;
                        } else if (looksLikeISQ20_AWSE(trimmed)) {
                            String formatted;
                            if (useRecipientFormat) buildISQ20_AWSE_CSVRow(trimmed, formatted);
                            else formatted = trimmed;
                            if (formatted.length() > 0) displayLine = formatted;
                        } else {
                            if (trimmed.indexOf(',') > 0) {
                                int commas = 0;
                                for (unsigned int j = 0; j < trimmed.length(); ++j) {
                                    if (trimmed[j] == ',') commas++;
                                }
                                if (commas > 0 && (commas % 2 != 0) && useRecipientFormat) {
                                    String spaced = trimmed;
                                    spaced.replace(",", ", ");
                                    while (spaced.indexOf("  ") >= 0) spaced.replace("  ", " ");
                                    displayLine = spaced;
                                }
                            }
                            if (trimmed.indexOf('\t') >= 0) {
                                String tabbed = trimmed;
                                tabbed.replace("\t", "   |   ");
                                displayLine = tabbed;
                            }
                        }

                        if (!first) out.print(',');
                        out.print(F("{\"source\":\"parallel\",\"timestamp\":"));
                        out.print(lastReceptionTime / 1000UL);
                        out.print(F(",\"data\":\""));
                        sharedWebWriteJsonEscaped(out, displayLine);
                        out.print(F("\"}"));
                        first = false;
                        linesEmitted++;
                    }
                    line = String();
                    if (limitLines > 0 && linesEmitted >= limitLines) break;
                    continue;
                }

                if (b >= 32 && b != 127) line += (char)b;
                else {
                    char hex[6];
                    snprintf(hex, sizeof(hex), "<%02X>", b);
                    line += hex;
                }
            }
        }

        if (bufferDadosLocked) {
            sharedWebBufferDadosUnlock(didLock);
        }
    }
    out.print(F("]"));
    out.print(F(",\"current_seq\":"));
    out.print((unsigned long)bufferDadosSeq);
    out.print(F(",\"total\":"));
    out.print((unsigned long)psramLogCount);
    if (resetNeeded) {
        out.print(F(",\"reset\":true,\"reset_reason\":\"overflow\""));
    }

    if (debug) {
        int dbgSize = 0;
        int dbgCap = 0;
        bool didLock2 = false;
        if (sharedWebBufferDadosLock(pdMS_TO_TICKS(20), didLock2)) {
            dbgSize = (int)bufferDados.size();
            dbgCap = (int)bufferDados.capacity();
            sharedWebBufferDadosUnlock(didLock2);
        } else {
            dbgSize = (int)bufferDados.size();
            dbgCap = (int)bufferDados.capacity();
        }
        out.print(F(",\"debug_buffer_size\":"));
        out.print(dbgSize);
        out.print(F(",\"debug_buffer_capacity\":"));
        out.print(dbgCap);
    }

    out.print('}');
}
