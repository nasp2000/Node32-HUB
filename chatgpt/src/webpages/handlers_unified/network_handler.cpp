#include "network_handler.h"
#include <Arduino.h>
#include "../../languages/lang_active.h"
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include "../../ConfigManager.h"
#include "../../commands.h"
#include "../../direct_log.h"
#include "../../network_access_point.h"
#include "../../network_hybrid_manager.h"
#include "../../network_manual_switch.h"
#include "../../network_runtime_state.h"
#include "../../network_saved_profiles.h"
#include "../../network_status_payload.h"
#include "../../sd_diagnostics.h"
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "../../touch7_display.h" // Touch7FlashGuard (quiesce RGB around flash writes)
#endif
#include "../../usbip_server.h"
#include "../../web_shared_logic.h"

extern void syncGlobalVariables();
extern void loadSavedNetworks();
extern void loadAccessPointConfig();

namespace {

String urlDecode(const String& input) {
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < input.length()) {
            char h1 = input.charAt(i + 1);
            char h2 = input.charAt(i + 2);
            auto hex = [](char ch) -> int {
                return (ch >= '0' && ch <= '9') ? (ch - '0') : (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) : (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) : 0;
            };
            out += (char)(hex(h1) * 16 + hex(h2));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

String getFormValue(const String& body, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = body.length();
        String pair = body.substring(start, amp);
        if (pair.startsWith(prefix)) {
            return urlDecode(pair.substring(prefix.length()));
        }
        start = amp + 1;
    }
    return String();
}

bool formTruthy(const String& body, const char* key) {
    String v = getFormValue(body, key);
    v.toLowerCase();
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

// Parse a value from body (JSON or form-urlencoded) based on contentType.
String bodyValue(const String& body, const String& contentType, const char* key) {
    if (contentType.startsWith(F("application/json"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key].isNull()) return String(doc[key].as<String>());
        }
    }
    return getFormValue(body, key);
}

bool bodyTruthy(const String& body, const String& contentType, const char* key) {
    if (contentType.startsWith(F("application/json"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key].isNull()) return doc[key].as<bool>();
        }
    }
    return formTruthy(body, key);
}

void wifiDisabledResponse(HttpResponse& res) {
    res.send(503, "application/json", "{\"error\":\"wifi_disabled\"}");
}

} // anonymous namespace

// === WiFi Scan ===
void handleWifiScanGet(HttpRequest& req, HttpResponse& res) {
#include "feature_flags.h"
#if ENABLE_WIFI==1
    (void)req;
    SpiRamJsonDocument doc(3072);
    JsonArray networks = doc.createNestedArray("networks");

    // Synchronous scan — the WiFi adapter may wrap with async logic.
    int count = WiFi.scanNetworks(false, true, false, 300);
    if (count > 0) {
        int maxNetworks = std::min(count, 10);
        std::vector<int> indices;
        for (int i = 0; i < count && i < maxNetworks; ++i) indices.push_back(i);
        std::sort(indices.begin(), indices.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

        for (int idx : indices) {
            JsonObject net = networks.createNestedObject();
            net["ssid"] = WiFi.SSID(idx);
            net["rssi"] = WiFi.RSSI(idx);
            net["channel"] = WiFi.channel(idx);
            net["encryption"] = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
            String authType;
            switch (WiFi.encryptionType(idx)) {
                case WIFI_AUTH_OPEN: authType = "Open"; break;
                case WIFI_AUTH_WEP: authType = "WEP"; break;
                case WIFI_AUTH_WPA_PSK: authType = "WPA"; break;
                case WIFI_AUTH_WPA2_PSK: authType = "WPA2"; break;
                case WIFI_AUTH_WPA_WPA2_PSK: authType = "WPA/WPA2"; break;
                case WIFI_AUTH_WPA2_ENTERPRISE: authType = "WPA2-Enterprise"; break;
                case WIFI_AUTH_WPA3_PSK: authType = "WPA3"; break;
                default: authType = "Unknown"; break;
            }
            net["authType"] = authType;
        }
        WiFi.scanDelete();
    } else {
        WiFi.scanDelete();
    }

    String json;
    serializeJsonPretty(doc, json);
    res.send(200, "application/json", json);
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === WiFi Intelligent Status ===
void handleWifiIntelligentStatusGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    SpiRamJsonDocument doc(1024);
    doc["network_connected"] = hasNetworkConnectivity();
    doc["network_ip"] = hasNetworkConnectivity() ? getCurrentNetworkIPString() : String("");
    doc["sta_connected"] = (WiFi.status() == WL_CONNECTED);
    doc["sta_ssid"] = WiFi.SSID();
    doc["sta_ip"] = WiFi.localIP().toString();
    doc["sta_rssi"] = WiFi.RSSI();
    doc["ap_active"] = apActive;
    doc["ap_lock"] = apLockedUntilReboot;
    doc["ap_ssid"] = configManager.getAccessPointSSID();
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["force_ap_mode"] = forceAPMode;
    doc["saved_networks_count"] = configManager.getSavedNetworks().size();

    int hybridState = getCurrentHybridStateValue();
    doc["hybrid_state"] = hybridState;
    if (hybridState == 0) doc["hybrid_state_name"] = "BOOT_AP_STA";
    else if (hybridState == 1) doc["hybrid_state_name"] = "STA_ONLY";
    else if (hybridState == 2) doc["hybrid_state_name"] = "RECOVERY_AP_STA";
    else doc["hybrid_state_name"] = "UNKNOWN";

    String json;
    serializeJson(doc, json);
    res.sendJson(json);
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === WiFi Runtime Status ===
void handleWifiRuntimeStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool disabled = configManager.getWifiDisabled();
    bool connected = (WiFi.status() == WL_CONNECTED);
    String json = String("{\"disabled\":") + (disabled ? "true" : "false")
                + ",\"connected\":" + (connected ? "true" : "false")
                + ",\"ip\":\"" + WiFi.localIP().toString() + "\""
                + ",\"ssid\":\"" + WiFi.SSID() + "\"}";
    res.send(200, "application/json", json);
}

// === WiFi Reconnect ===
void handleWifiReconnectPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    String bestNetwork = findBestAvailableNetwork();
    if (bestNetwork.isEmpty()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Nenhuma rede salva disponivel\"}");
        return;
    }
    switchToNetworkManually(bestNetwork);
    String json = String("{\"success\":true,\"message\":\"Trocando para melhor rede: ") + bestNetwork + "\"}";
    res.send(200, "application/json", json);
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === WiFi Switch Network ===
void handleWifiSwitchNetworkPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    String targetSsid = bodyValue(req.body, req.contentType, "ssid");
    targetSsid.trim();
    if (targetSsid.isEmpty()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Parametro obrigatorio: ssid\"}");
        return;
    }

    bool networkExists = false;
    JsonArray savedNetworks = configManager.getSavedNetworks();
    for (JsonObject saved : savedNetworks) {
        if (String(saved["ssid"] | "") == targetSsid) {
            networkExists = true;
            break;
        }
    }

    if (!networkExists) {
        String json = String("{\"success\":false,\"message\":\""+L(activeLang->apiNetNotFound)+"") + targetSsid + "\"}";
        res.send(404, "application/json", json);
        return;
    }

    switchToNetworkManually(targetSsid);
    String json = String("{\"success\":true,\"message\":\"Trocando para rede: ") + targetSsid + "\"}";
    res.send(200, "application/json", json);
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Force AP ===
void handleWifiForceApPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    forceAPMode = true;
    startAccessPoint();
    res.send(200, "application/json", "{\"success\":true,\"message\":\"Access Point ativado manualmente\"}");
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Disable AP ===
void handleWifiDisableApPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    forceAPMode = false;
    if (WiFi.status() == WL_CONNECTED) {
        stopAccessPoint();
        res.send(200, "application/json", "{\"success\":true,\"message\":\"Access Point desativado\"}");
        return;
    }
    res.send(400, "application/json", "{\"success\":false,\"message\":\"Nao e possivel desativar AP sem conexao WiFi\"}");
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Disable WiFi (persist flag — platform adapter handles driver shutdown) ===
void handleWifiDisablePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    configManager.setWifiDisabled(true);
    if (!configManager.saveSection("wifi")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveWifiState)+"\"}");
        return;
    }
    res.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi desligado. Para reativar use 'Ligar WiFi' e reinicie o dispositivo.\"}");
}

// === Enable WiFi ===
void handleWifiEnablePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    configManager.setWifiDisabled(false);
    if (!configManager.saveSection("wifi")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveWifiState)+"\"}");
        return;
    }
    res.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi reativado. Reinicie o dispositivo para que o WiFi volte a funcionar.\"}");
}

// === Clear AP Lock ===
void handleClearApLockPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    apLockedUntilReboot = false;
    res.send(200, "application/json", "{\"success\":true,\"ap_lock\":false}");
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Access Point Config GET ===
void handleAccessPointConfigGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    (void)req;
    SpiRamJsonDocument doc(1024);
    doc["ssid"] = configManager.getAccessPointSSID();
    doc["password"] = "********";
    doc["active"] = apActive;
    doc["force_ap_mode"] = forceAPMode;
    doc["locked_until_reboot"] = apLockedUntilReboot;
    doc["failure_count"] = apFailureCount;
    doc["last_attempt_ms"] = lastAPAttemptTime;
    doc["retry_delay_ms"] = configManager.getAccessPointRetryDelay();
    doc["max_failures"] = configManager.getAccessPointMaxFailures();
    doc["failure_pause_ms"] = configManager.getAccessPointFailurePause();
    doc["activation_delay_ms"] = configManager.getAccessPointActivationDelay();
    doc["has_connectivity"] = hasNetworkConnectivity();
    doc["current_ip"] = getCurrentNetworkIPString();

    if (apActive) {
        doc["ip_address"] = WiFi.softAPIP().toString();
        doc["connected_clients"] = WiFi.softAPgetStationNum();
    } else {
        doc["ip_address"] = "N/A";
        doc["connected_clients"] = 0;
    }

    int hybridState = getCurrentHybridStateValue();
    if (hybridState == 0) doc["hybrid_state"] = "wifi";
    else if (hybridState == 1) doc["hybrid_state"] = "ap";
    else if (hybridState == 2) doc["hybrid_state"] = "hybrid";
    else doc["hybrid_state"] = "unknown";

    String json;
    serializeJsonPretty(doc, json);
    res.send(200, "application/json", json);
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Access Point Config POST ===
void handleAccessPointConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_WIFI==1
    bool configChanged = false;

    String ssid = bodyValue(req.body, req.contentType, "ssid");
    if (ssid.length() > 0) {
        ssid.trim();
        if (ssid.length() < 1 || ssid.length() > 32) {
            res.send(400, "application/json", "{\"success\":false,\"message\":\"SSID deve ter 1-32 caracteres\"}");
            return;
        }
        configManager.setAccessPointSSID(ssid);
        configChanged = true;
    }

    String password = bodyValue(req.body, req.contentType, "password");
    if (password.length() > 0) {
        if (!(password.length() == 0 || (password.length() >= 8 && password.length() <= 63))) {
            res.send(400, "application/json", "{\"success\":false,\"message\":\"Password deve ter 8-63 caracteres ou ser vazia\"}");
            return;
        }
        configManager.setAccessPointPassword(password);
        configChanged = true;
    }

    unsigned long retryDelay = (unsigned long)bodyValue(req.body, req.contentType, "retry_delay_ms").toInt();
    if (retryDelay > 0) { configManager.setAccessPointRetryDelay(retryDelay); configChanged = true; }
    int maxFailures = bodyValue(req.body, req.contentType, "max_failures").toInt();
    if (maxFailures > 0) { configManager.setAccessPointMaxFailures(maxFailures); configChanged = true; }
    unsigned long failurePause = (unsigned long)bodyValue(req.body, req.contentType, "failure_pause_ms").toInt();
    if (failurePause > 0) { configManager.setAccessPointFailurePause(failurePause); configChanged = true; }
    unsigned long activationDelay = (unsigned long)bodyValue(req.body, req.contentType, "activation_delay_ms").toInt();
    if (activationDelay > 0) { configManager.setAccessPointActivationDelay(activationDelay); configChanged = true; }

    if (!configChanged) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Nenhuma configuracao fornecida\"}");
        return;
    }

    configManager.markDirtyAndScheduleSave();
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    Touch7PanelHold apPanelHold_;
#endif
    if (!configManager.saveSection("access_point")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveAp)+"\"}");
        return;
    }
    loadAccessPointConfig();
    res.send(200, "application/json", (String("{\"success\":true,\"message\":\"") + L(activeLang->apiNetApSaved) + "\"}"));
#else
    (void)req;
    wifiDisabledResponse(res);
#endif
}

// === Saved Networks (WiFi-only, no Ethernet equivalent) ===
void handleSavedNetworksGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    JsonArray networks = configManager.getSavedNetworks();
    SpiRamJsonDocument doc(4096);
    JsonArray masked = doc.to<JsonArray>();
    for (JsonObject net : networks) {
        JsonObject entry = masked.createNestedObject();
        entry["ssid"] = net["ssid"] | "";
        entry["password"] = "********";
        entry["rssi"] = net["rssi"] | -100;
        entry["auth_mode"] = net["auth_mode"] | 0;
        entry["lastConnected"] = net["lastConnected"] | 0;
        entry["connectionCount"] = net["connectionCount"] | 0;
    }
    String json;
    serializeJsonPretty(masked, json);
    res.send(200, "application/json", json);
}

void handleSavedNetworksPost(HttpRequest& req, HttpResponse& res) {
    String ssid = bodyValue(req.body, req.contentType, "ssid");
    String password = bodyValue(req.body, req.contentType, "password");
    if (ssid.length() == 0) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Parametro obrigatorio: ssid\"}");
        return;
    }
    configManager.addWifiNetwork(ssid, password);
    configManager.markDirtyAndScheduleSave();
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    Touch7PanelHold wifiPanelHold_;
#endif
    if (!configManager.saveSection("wifi")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveWifiNet)+"\"}");
        return;
    }
    loadSavedNetworks();
    syncGlobalVariables();
    if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != ssid) {
        if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
        currentHybridState = HYBRID_BOOT_AP_STA;
        hybridBootTime = millis();
        hybridBootTimeout = 60000;
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(ssid, password);
    }
    String json = String("{\"success\":true,\"message\":\"");
    json += L(activeLang->apiNetWifiAdded);
    json += ssid;
    json += "\"}";
    res.send(200, "application/json", json);
}

void handleSavedNetworksDelete(HttpRequest& req, HttpResponse& res) {
    String ssid = bodyValue(req.body, req.contentType, "ssid");
    if (ssid.length() == 0) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Parametro obrigatorio: ssid\"}");
        return;
    }

    JsonArray networks = configManager.getSavedNetworks();
    bool found = false;
    for (JsonObject saved : networks) {
        if (String(saved["ssid"] | "") == ssid) { found = true; break; }
    }
    if (!found) {
        String json = String("{\"success\":false,\"message\":\""+L(activeLang->apiNetNotFound)+"") + ssid + "\"}";
        res.send(404, "application/json", json);
        return;
    }

    configManager.removeWifiNetwork(ssid);
    configManager.markDirtyAndScheduleSave();
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    Touch7PanelHold wifiDelHold_;
#endif
    if (!configManager.saveSection("wifi")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrRemoveWifi)+"\"}");
        return;
    }
    loadSavedNetworks();
    syncGlobalVariables();
    String json = String("{\"success\":true,\"message\":\"");
    json += L(activeLang->apiNetWifiRemoved);
    json += ssid;
    json += "\"}";
    res.send(200, "application/json", json);
}

// === Ethernet Status ===
void handleEthStatusGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_ETHERNET==1
    (void)req;
    bool hasEth = false;
    IPAddress ethIp(0, 0, 0, 0);
    ethIp = Ethernet.localIP();
    hasEth = (ethIp[0] || ethIp[1] || ethIp[2] || ethIp[3]);
    String ipStr = hasEth ? ethIp.toString() : String("0.0.0.0");

    String json;
    json.reserve(512);
    json += "{\"status\":\"ok\",\"mode\":\"";
    json += configManager.getEthernetMode();
    json += "\",\"ip\":\"";
    json += ipStr;
    json += "\",\"mac\":\"";
    {
        byte mac[6];
        Ethernet.MACAddress(mac);
        char macBuffer[18];
        snprintf(macBuffer, sizeof(macBuffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        json += macBuffer;
    }
    json += "\",\"gateway\":\"";
    json += configManager.getEthernetGW();
    json += "\",\"dns\":\"";
    json += configManager.getEthernetDNS();
    json += "\",\"mac_override\":\"";
    json += configManager.getEthernetMacOverride();
    json += "\",\"dhcp\":";
    json += (configManager.getEthernetMode() == "dhcp") ? "true" : "false";
    json += F(",\"dhcp_gw\":\"");
    json += Ethernet.gatewayIP().toString();
    json += F("\",\"dhcp_dns\":\"");
    json += Ethernet.dnsServerIP().toString();
    json += '"';

#if ENABLE_USBIP_SERVER==1
    const UsbIpServerStats usbipStats = usbipServerGetStats();
    json += ",\"usbip_enabled\":true,\"usbip_running\":";
    json += usbipStats.running ? "true" : "false";
    json += ",\"usbip_port\":";
    json += String((unsigned long)usbipStats.listen_port);
    json += ",\"usbip_clients\":";
    json += String((unsigned long)usbipStats.active_clients);
    json += ",\"usbip_accepted_clients\":";
    json += String((unsigned long)usbipStats.accepted_clients);
    json += ",\"usbip_rx_bytes\":";
    json += String((unsigned long)usbipStats.rx_bytes);
    json += ",\"usbip_tx_bytes\":";
    json += String((unsigned long)usbipStats.tx_bytes);
#else
    json += ",\"usbip_enabled\":false";
#endif
    json += "}";
    res.send(200, "application/json", json);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

// === Ethernet Config POST ===
void handleEthConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_ETHERNET==1
    String mode = bodyValue(req.body, req.contentType, "mode");
    if (mode.length() == 0) mode = "dhcp";
    mode.toLowerCase();
    if (!(mode == "dhcp" || mode == "static")) {
        res.send(400, "application/json", "{\"status\":\"erro\",\"msg\":\"modo_invalido\"}");
        return;
    }

    String ip = bodyValue(req.body, req.contentType, "ip");
    String gateway = bodyValue(req.body, req.contentType, "gw");
    String dns = bodyValue(req.body, req.contentType, "dns");
    String mac = bodyValue(req.body, req.contentType, "mac");

    if (mode == "static") {
        if (!sharedWebValidIPv4(ip) || !sharedWebValidIPv4(gateway) || !sharedWebValidIPv4(dns)) {
            res.send(400, "application/json", "{\"status\":\"erro\",\"msg\":\"ipv4_invalido\"}");
            return;
        }
    }
    if (mac.length() > 0 && !sharedWebValidMac(mac)) {
        res.send(400, "application/json", "{\"status\":\"erro\",\"msg\":\"mac_invalido\"}");
        return;
    }

    SharedEthernetConfigInput input;
    input.mode = mode;
    input.ip = ip;
    input.gw = gateway;
    input.dns = dns;
    input.mac = mac;
    String responseBody;
    bool ok = sharedWebApplyEthernetConfig(input, responseBody);
    res.send(ok ? 200 : 400, "application/json", responseBody);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

// === Telnet ===
void handleTelnetStatusGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TELNET==1
    (void)req;
    bool enabled = configManager.getTelnetEnabled();
    res.send(200, "application/json", enabled ? "{\"enabled\":true}" : "{\"enabled\":false}");
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleTelnetTogglePost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TELNET==1
    (void)req;
    bool current = configManager.getTelnetEnabled();
    bool newState = !current;
    configManager.setTelnetEnabled(newState);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("telnet")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSave)+"\"}");
        return;
    }
    res.send(200, "application/json", newState ? "{\"success\":true,\"enabled\":true}" : "{\"success\":true,\"enabled\":false}");
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleTelnetConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TELNET==1
    String msg;
    bool upd = false;

    String body = req.body.length() ? req.body : req.query;
    bool hasPresent = body.indexOf(F("telnet_enabled_present=")) >= 0;
    if (hasPresent) {
        bool newVal = body.indexOf(F("telnet_enabled=1")) >= 0 ||
                      body.indexOf(F("telnet_enabled=true")) >= 0 ||
                      body.indexOf(F("telnet_enabled=on")) >= 0;
        bool current = configManager.getTelnetEnabled();
        if (newVal != current) {
            configManager.setTelnetEnabled(newVal);
            msg += F("Telnet State ");
            upd = true;
            if (newVal) {
                if (startTelnetServer()) sendVirtualMessage(F("Telnet ON (networks)"), INFO);
                else sendVirtualMessage(F("Telnet pending: no active network"), WARN);
            } else {
                stopTelnetServer();
                sendVirtualMessage(F("Telnet OFF (networks)"), INFO);
            }
        }
    }

    String v = getFormValue(body, "telnet_password_new");
    if (v.length() && v != F("(não alterado)") && v != F("(nao alterado)")) {
        configManager.setTelnetPassword(v);
        msg += F("TelnetPass ");
        upd = true;
    }

    if (!upd) {
        res.send(200, "application/json", (String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiNetNoChange) + "\"}"));
        return;
    }

    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("telnet");
    if (savedOk) {
        res.send(200, "application/json", (String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiNetTelnetSaved) + "\"}"));
    } else {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSave) + F("\"}"));
    }
#else
    (void)req;
    res.send(501, "application/json", F("{\"status\":\"disabled\"}"));
#endif
}

// === USB Server ===
void handleUsbServerStatusGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_USBIP_SERVER==1
    (void)req;
    res.send(200, "application/json", sharedWebBuildUsbServerStatusJson());
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleUsbServerConfigGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_USBIP_SERVER==1
    (void)req;
    res.send(200, "application/json", sharedWebBuildUsbServerConfigJson());
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleUsbServerConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_USBIP_SERVER==1
    SharedUsbServerConfigInput input;
    input.enabled = bodyTruthy(req.body, req.contentType, "enabled");
    unsigned long portRaw = (unsigned long)bodyValue(req.body, req.contentType, "port").toInt();
    unsigned long recvTimeoutRaw = (unsigned long)bodyValue(req.body, req.contentType, "recv_timeout_ms").toInt();
    unsigned long inWaitTimeoutRaw = (unsigned long)bodyValue(req.body, req.contentType, "in_wait_timeout_ms").toInt();

    if (portRaw < 1 || portRaw > 65535) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"porta_invalida\"}");
        return;
    }
    if (recvTimeoutRaw < 100 || recvTimeoutRaw > 60000) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"recv_timeout_invalido\"}");
        return;
    }
    if (inWaitTimeoutRaw < 20 || inWaitTimeoutRaw > 5000) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"in_wait_timeout_invalido\"}");
        return;
    }

    input.port = (uint16_t)portRaw;
    input.recvTimeoutMs = (uint16_t)recvTimeoutRaw;
    input.inWaitTimeoutMs = (uint16_t)inWaitTimeoutRaw;

    String responseBody;
    bool ok = sharedWebApplyUsbServerConfig(input, responseBody);
    res.send(ok ? 200 : 400, "application/json", responseBody);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleUsbServerDevicesGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_USBIP_SERVER==1
    (void)req;
    res.send(200, "application/json", sharedWebBuildUsbServerDevicesJson());
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}
