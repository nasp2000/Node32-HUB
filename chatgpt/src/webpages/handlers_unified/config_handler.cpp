#include "config_handler.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../ConfigManager.h"
#include "../../arduinojson_fix.h"
#include "../../commands.h"
#include "../../coordinator_hooks.h"
#include "../../direct_log.h"
#include "../../observability.h"
#include "../../psram_helpers.h"
#include "../../telnet_server.h"
#include <WiFi.h>
#include "network_handler.h"
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "../../touch7_display.h" // Touch7FlashGuard (quiesce RGB around flash writes)
#endif
#include "../../network_hybrid_manager.h"
#include "../../network_saved_profiles.h"
#include "../../web_shared_logic.h"
#include "../../feature_flags.h"
// Both modules can be enabled in the same build (pack RAMBOX), so both headers
// are included independently — an #elif here left unikonGetConfig() undeclared.
#if ENABLE_BRICON == 1
#include "../../bricon_protocol.h"     // briconActiveModuleName(), briconGetConfig()
#endif
#if ENABLE_UNIKON == 1
#include "../../unikon_protocol.h"     // unikonGetConfig(), unikonIsEnabled()
#endif

#if ENABLE_RS232 == 1
// Runtime RS232 state (diagnostics_impl.h). commands.h brings the other globals.
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
#endif

extern ConfigManager configManager;
extern void syncGlobalVariables();
extern void syncGlobalVariables(bool includeRS232);
extern void setupLanguage();
extern void webclientForceReset();
extern String buildSessionHistoryJson();
extern void applyRS232Config();
extern int minEmailContentLength;

namespace {

// ── URL/form parsing helpers ──────────────────────────────────────────

String urlDecode(const String& input) {
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '+') { out += ' '; continue; }
        if (c == '%' && i + 2 < input.length()) {
            auto hex = [](char ch) -> int {
                return (ch >= '0' && ch <= '9') ? (ch - '0') :
                       (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) :
                       (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) : 0;
            };
            out += (char)(hex(input.charAt(i + 1)) * 16 + hex(input.charAt(i + 2)));
            i += 2;
        } else { out += c; }
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

bool bodyHasKey(const String& body, const String& contentType, const char* key) {
    if (contentType.startsWith(F("application/json"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            return doc.containsKey(key);
        }
        return false;
    }
    return body.indexOf(String(key) + '=') >= 0;
}

String bodyValue(const String& body, const String& contentType, const char* key) {
    if (contentType.startsWith(F("application/json"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key].isNull()) return String(doc[key].as<String>());
        }
        return String();
    }
    return getFormValue(body, key);
}

bool bodyTruthy(const String& body, const String& contentType, const char* key) {
    if (contentType.startsWith(F("application/json"))) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key].isNull()) return doc[key].as<bool>();
        }
        return false;
    }
    return formTruthy(body, key);
}

// ── Helpers ───────────────────────────────────────────────────────────

void appendToken(String& msg, bool& upd, const char* token) {
    msg += token;
    upd = true;
}

void applyStructuresFromBody(const String& body, const String& contentType, String& msg, bool& upd) {
    bool hasAutoDetect = bodyHasKey(body, contentType, "structures_auto_detect");
    if (!hasAutoDetect) {
        hasAutoDetect = bodyHasKey(body, contentType, "structures_present");
    }
    if (!hasAutoDetect && !bodyHasKey(body, contentType, "structures")) return;

    bool autoDetect = bodyTruthy(body, contentType, "structures_auto_detect");
    bool mtf1Enabled = bodyTruthy(body, contentType, "amada_isq20_mtf1_enabled");
    bool awseEnabled = bodyTruthy(body, contentType, "amada_isq20_awse_enabled");
    bool filterHeaders = bodyTruthy(body, contentType, "amada_filter_headers");

    configManager.setStructuresAutoDetectEnabled(autoDetect);
    if (!autoDetect && mtf1Enabled && awseEnabled) {
        awseEnabled = false;
    }
    configManager.setAmadaISQ20MTF1Enabled(mtf1Enabled);
    configManager.setAmadaISQ20AWSEEnabled(awseEnabled);
    configManager.setAmadaFilterHeaders(filterHeaders);

    String headerEveryStr = bodyValue(body, contentType, "amada_header_every");
    if (headerEveryStr.length()) {
        int headerEvery = headerEveryStr.toInt();
        if (headerEvery < 0 || headerEvery > 100) headerEvery = 20;
        configManager.setAmadaHeaderEvery(headerEvery);
    }
    appendToken(msg, upd, "Structures ");
}

} // anonymous namespace

// ── GET /api/config?section=... ───────────────────────────────────────

void handleConfigGet(HttpRequest& req, HttpResponse& res) {
    String debugValue = bodyValue(req.body, req.contentType, "debug");
    if (debugValue.length() == 0) debugValue = getFormValue(req.query, "debug");
    debugValue.toLowerCase();
    bool debugEnabled = (debugValue == "1" || debugValue == "true" || debugValue == "on" || debugValue == "yes");

    String section = bodyValue(req.body, req.contentType, "section");
    if (section.length() == 0) section = getFormValue(req.query, "section");
    const bool sectionValid = (section == "wifi" || section == "email" || section == "rs232" ||
                               section == "system" || section == "recipients" || section == "scheduler" ||
                               section == "ethernet" || section == "ftp");

    if (section.length() == 0) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing section. Use /api/config?section=wifi|email|rs232|system|recipients|scheduler|ftp\"}");
        return;
    }
    if (!sectionValid) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid section. Use wifi|email|rs232|system|recipients|scheduler|ftp\"}");
        return;
    }

    SpiRamJsonDocument full = configManager.getFullConfig(true);
    size_t outCapacity = 512;
    if (section == "wifi") {
        outCapacity = measureJson(full["wifi"]) + 256;
    } else if (section == "email") {
        outCapacity = measureJson(full["smtp"]) + measureJson(full["email"]) + 256;
    } else if (section == "rs232") {
        outCapacity = measureJson(full["rs232"]) + measureJson(full["printer"]) + 256;
    } else if (section == "system") {
        outCapacity = 1024;
    } else if (section == "recipients") {
        outCapacity = measureJson(full["recipients"]) + measureJson(full["numeric_rules"]) + 256;
    } else if (section == "scheduler") {
        outCapacity = measureJson(full["sched_cmd"]) + measureJson(full["sched_dt"]) + 256;
    } else if (section == "ftp") {
        outCapacity = measureJson(full["ftp"]) + 256;
    }

    SpiRamJsonDocument doc(outCapacity);
    if (section == "wifi")         doc["wifi"] = full["wifi"];
    if (section == "email")        { doc["smtp"] = full["smtp"]; doc["email"] = full["email"]; }
    if (section == "rs232") {
        doc["rs232"] = full["rs232"];
        doc["printer"] = full["printer"];

        // Which clock module owns the port right now ("rambox" | "bricon" | "none")
        // and whether it is in manual/bench mode (uses the RS232 page settings).
        const char* activeModule = "none";
#if ENABLE_BRICON == 1
        activeModule = briconActiveModuleName();
#elif ENABLE_UNIKON == 1
        activeModule = unikonIsEnabled() ? "rambox" : "none";
#endif
        bool manualSerial = false;
#if ENABLE_BRICON == 1
        if (strcmp(activeModule, "bricon") == 0) manualSerial = briconGetConfig().manualSerial;
#endif
#if ENABLE_UNIKON == 1
        if (strcmp(activeModule, "rambox") == 0) manualSerial = unikonGetConfig().manualSerial;
#endif
        doc["active_module"]        = activeModule;
        doc["active_module_manual"] = manualSerial;

#if ENABLE_RS232 == 1
        // Live values: while a module owns the port the runtime globals hold the
        // profile actually in force, which is not what ConfigManager has stored.
        // The page must always show what is active at that moment.
        JsonObject live = doc["rs232"];
        live["enabled"]        = rs232_enabled;
        live["baudrate_index"] = rs232_baudrate_index;
        live["baudrate"]       = rs232_baudrate;
        live["data_bits"]      = rs232_data_bits;
        live["parity"]         = rs232_parity;
        live["stop_bits"]      = rs232_stop_bits;
        live["invert_pins"]    = rs232_invert_pins;
#endif
    }
    if (section == "system")       { doc["system_name"] = configManager.getSystemName();
                                     doc["system_language"] = configManager.getSystemLanguage();
                                     doc["mdns_enabled"] = configManager.getMdnsEnabled();
                                     JsonObject ntpOut = doc.createNestedObject("ntp");
                                     ntpOut["timezone"] = configManager.getNtpTimezone();
                                     ntpOut["server"] = configManager.getNtpServer(); }
    if (section == "recipients")   { doc["recipients"] = full["recipients"]; doc["numeric_rules"] = full["numeric_rules"]; }
    if (section == "scheduler")    { doc["sched_cmd"] = full["sched_cmd"]; doc["sched_dt"] = full["sched_dt"]; }
    if (section == "ethernet")        { doc["ethernet"] = full["ethernet"]; }
    if (section == "ftp")             { doc["ftp"] = full["ftp"]; }

    if (debugEnabled) {
        JsonObject debug = doc.createNestedObject("_debug");
        size_t approxLen = measureJsonPretty(doc);
        debug["section"] = section;
        debug["payload_bytes"] = (unsigned long)approxLen;
    }

    String json;
    serializeJsonPretty(doc, json);
    res.send(200, "application/json", json);
}

// ── POST /api/config ──────────────────────────────────────────────────

void handleConfigPost(HttpRequest& req, HttpResponse& res) {
    // Sub-path dispatch: the v3 AsyncWebServer BackwardCompatible matcher
    // routes POST /api/config/* here instead of the dedicated handlers
    // (proven live 17-09-2026: saved_networks/access-point/unknown sub-paths
    // all landed here). Forward explicitly so saves work.
    if (req.path.endsWith("/api/config/saved_networks")) {
        handleSavedNetworksPost(req, res);
        return;
    }
    if (req.path.endsWith("/api/config/access-point")) {
        handleAccessPointConfigPost(req, res);
        return;
    }
    String section = bodyValue(req.body, req.contentType, "section");
    const bool sectionValid = (section == "wifi" || section == "email" || section == "rs232" ||
                               section == "system" || section == "recipients" || section == "scheduler" ||
                               section == "ethernet" || section == "ftp");
    if (!sectionValid) {
        res.send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing/invalid section in POST. Use wifi|email|rs232|system|recipients|scheduler|ftp\"}");
        return;
    }

    bool upd = false;
    bool ntpUpdated = false;
    bool wifiUpdated = false;
    String newSsid;
    String newPass;
    String msg;

    // ── WiFi section ──
    String v = bodyValue(req.body, req.contentType, "ssid");
    if (v.length() && v != F("********")) {
        configManager.setWifiSSID(v);
        appendToken(msg, upd, "WiFi SSID ");
        wifiUpdated = true; newSsid = v;
    }
    v = bodyValue(req.body, req.contentType, "password");
    if (v.length() && v != F("********")) {
        configManager.setWifiPassword(v);
        appendToken(msg, upd, "WiFi Pass ");
        wifiUpdated = true; newPass = v;
    }

    // ── System section ──
    v = bodyValue(req.body, req.contentType, "system_name");
    if (v.length()) { v = stripControlChars(v); configManager.setSystemName(v); appendToken(msg, upd, "System Name "); }

    // mDNS toggle: accept the key itself (robust for direct API/harness calls)
    // OR the legacy hidden-input marker. Only apply when a value is actually
    // present, so a marker-only legacy body never silently disables mDNS.
    if (bodyHasKey(req.body, req.contentType, "mdns_enabled") || req.body.indexOf("mdns_enabled_present") >= 0) {
        String mv = bodyValue(req.body, req.contentType, "mdns_enabled");
        if (bodyHasKey(req.body, req.contentType, "mdns_enabled") || mv.length()) {
            configManager.setMdnsEnabled(mv == "1" || mv == "true" || mv == "on");
            appendToken(msg, upd, "mDNS ");
        }
    }

    v = bodyValue(req.body, req.contentType, "system_language");
    if (v.length()) { configManager.setSystemLanguage(v); appendToken(msg, upd, "Language "); setupLanguage(); }

    v = bodyValue(req.body, req.contentType, "timezone");
    if (v.length()) { configManager.setNtpTimezone(v); appendToken(msg, upd, "NTP Timezone "); ntpUpdated = true; }

    v = bodyValue(req.body, req.contentType, "ntp_server");
    if (v.length()) { configManager.setNtpServer(v); appendToken(msg, upd, "NTP Server "); ntpUpdated = true; }

    // ── Printer section ──
    v = bodyValue(req.body, req.contentType, "emulation_mode");
    if (v.length()) { configManager.setPrinterEmulationMode(v.toInt()); appendToken(msg, upd, "Printer Emul "); }

    v = bodyValue(req.body, req.contentType, "charset_mode");
    if (v.length()) { configManager.setPrinterCharsetMode(v.toInt()); appendToken(msg, upd, "Printer Charset "); }

    // ── Unified password (web + telnet) ──
    v = bodyValue(req.body, req.contentType, "unified_password");
    if (v.length() && v != F("********")) {
        configManager.setWebPassword(v);
        configManager.setTelnetPassword(v);
        appendToken(msg, upd, "UnifiedPass ");
    }

    // ── Telnet ──
    if (bodyHasKey(req.body, req.contentType, "telnet_enabled")) {
        bool newVal = bodyTruthy(req.body, req.contentType, "telnet_enabled");
        bool current = configManager.getTelnetEnabled();
        if (newVal != current) {
            configManager.setTelnetEnabled(newVal);
            appendToken(msg, upd, "Telnet ");
            if (newVal) {
                if (!startTelnetServer()) sendVirtualMessage(F("Telnet pending: network transport unavailable"), WARN);
            } else {
                stopTelnetServer();
            }
        }
    }

    // ── SMTP / Email section ──
    bool emailSectionPresent = bodyHasKey(req.body, req.contentType, "smtp_server")
        || bodyHasKey(req.body, req.contentType, "smtp_port")
        || bodyHasKey(req.body, req.contentType, "smtp_user")
        || bodyHasKey(req.body, req.contentType, "smtp_pass")
        || bodyHasKey(req.body, req.contentType, "email_method")
        || bodyHasKey(req.body, req.contentType, "email_destination")
        || bodyHasKey(req.body, req.contentType, "email_dest")
        || bodyHasKey(req.body, req.contentType, "google_script_url");

    v = bodyValue(req.body, req.contentType, "smtp_server");
    if (v.length()) { configManager.setSmtpServer(v); appendToken(msg, upd, "SMTP Server "); }

    v = bodyValue(req.body, req.contentType, "smtp_port");
    if (v.length()) { configManager.setSmtpPort(v.toInt()); appendToken(msg, upd, "SMTP Port "); }

    v = bodyValue(req.body, req.contentType, "smtp_user");
    if (v.length()) { configManager.setSmtpUsername(v); appendToken(msg, upd, "SMTP User "); }

    v = bodyValue(req.body, req.contentType, "smtp_pass");
    if (v.length() && v != F("********")) { configManager.setSmtpPassword(v); appendToken(msg, upd, "SMTP Pass "); }

    v = bodyValue(req.body, req.contentType, "google_script_url");
    if (v.length()) { configManager.setGoogleScriptUrl(v); appendToken(msg, upd, "Google Script URL "); }

    v = bodyValue(req.body, req.contentType, "email_method");
    if (v.length()) { configManager.setEmailMethod(v); appendToken(msg, upd, "Email Method "); }

    if (emailSectionPresent || bodyHasKey(req.body, req.contentType, "smtp_tls_insecure")) {
        bool newVal = bodyTruthy(req.body, req.contentType, "smtp_tls_insecure");
        if (newVal != configManager.getSmtpTlsInsecure()) {
            configManager.setSmtpTlsInsecure(newVal);
            appendToken(msg, upd, "TLS Insecure ");
        }
    }

    v = bodyValue(req.body, req.contentType, "email_destination");
    if (!v.length()) v = bodyValue(req.body, req.contentType, "email_dest");
    if (v.length()) { configManager.setEmailDestination(v); appendToken(msg, upd, "Email Dest "); }

    if (bodyHasKey(req.body, req.contentType, "email_auto_send_present")
        || bodyHasKey(req.body, req.contentType, "email_auto_send")) {
        bool newVal = bodyTruthy(req.body, req.contentType, "email_auto_send");
        if (newVal != configManager.getEmailAutoSend()) {
            configManager.setEmailAutoSend(newVal);
            appendToken(msg, upd, "AutoSend ");
        }
    }

    if (bodyHasKey(req.body, req.contentType, "email_attach_txt_present")
        || bodyHasKey(req.body, req.contentType, "email_attach_txt")) {
        configManager.setEmailAttachTxt(bodyTruthy(req.body, req.contentType, "email_attach_txt"));
        appendToken(msg, upd, "AttachTxt ");
    }

    if (bodyHasKey(req.body, req.contentType, "email_include_timestamp_present")
        || bodyHasKey(req.body, req.contentType, "email_include_timestamp")) {
        configManager.setEmailIncludeTimestamp(bodyTruthy(req.body, req.contentType, "email_include_timestamp"));
        appendToken(msg, upd, "IncTS ");
    }

    v = bodyValue(req.body, req.contentType, "timeout_active");
    if (v.length()) { configManager.setTimeoutAutoEmailEnabled(v.toInt()); appendToken(msg, upd, "ToutActive "); }

    v = bodyValue(req.body, req.contentType, "timeout_inactive");
    if (v.length()) { configManager.setTimeoutAutoEmailDisabled(v.toInt()); appendToken(msg, upd, "ToutInactive "); }

    v = bodyValue(req.body, req.contentType, "email_max_lines");
    if (!v.length()) v = bodyValue(req.body, req.contentType, "max_lines");
    if (v.length()) { configManager.setEmailMaxLines(v.toInt()); appendToken(msg, upd, "MaxLines "); }

    v = bodyValue(req.body, req.contentType, "email_min_content_length");
    if (!v.length()) v = bodyValue(req.body, req.contentType, "min_email_chars");
    if (v.length()) { configManager.setEmailMinContentLength(v.toInt()); minEmailContentLength = configManager.getEmailMinContentLength(); appendToken(msg, upd, "MinLen "); }

    v = bodyValue(req.body, req.contentType, "email_subject");
    if (v.length()) { configManager.setEmailSubject(v); appendToken(msg, upd, "Subject "); }

    if (bodyHasKey(req.body, req.contentType, "email_dont_copy_sender")) {
        configManager.setEmailDontCopySender(bodyTruthy(req.body, req.contentType, "email_dont_copy_sender"));
        appendToken(msg, upd, "DontCopySender ");
    }

    // ── Recipients (JSON array via recipients_json or recipients fields) ──
    {
        String recipientsJson = bodyValue(req.body, req.contentType, "recipients_json");
        if (recipientsJson.length()) {
            SpiRamJsonDocument recipientsDoc(4096);
            DeserializationError err = deserializeJson(recipientsDoc, recipientsJson);
            if (!err && recipientsDoc.is<JsonArray>()) {
                configManager.clearAllRecipients();
                for (JsonObject r : recipientsDoc.as<JsonArray>()) {
                    String name = r["name"] | "";
                    String email = r["email"] | "";
                    if (email.length()) configManager.addRecipient(name, email);
                }
                appendToken(msg, upd, "Recipients ");
            }
        }
    }

    // ── RS232 section ──
    if (bodyHasKey(req.body, req.contentType, "baudrate_index")) {
        configManager.setRs232BaudrateIndex(bodyValue(req.body, req.contentType, "baudrate_index").toInt());
        appendToken(msg, upd, "RS232 BaudIdx ");
    }

    v = bodyValue(req.body, req.contentType, "invert_pins");
    if (!v.length()) v = bodyValue(req.body, req.contentType, "rs232_invert_pins");
    if (v.length()) { configManager.setRs232InvertPins(v == "true" || v == "1" || v == "on"); appendToken(msg, upd, "RS232 Invert "); }

    if (bodyHasKey(req.body, req.contentType, "rs232_enabled")) {
        configManager.setRs232Enabled(bodyTruthy(req.body, req.contentType, "rs232_enabled"));
        appendToken(msg, upd, "RS232 Enabled ");
    }

    v = bodyValue(req.body, req.contentType, "data_bits");
    if (v.length()) { configManager.setRs232DataBits(v.toInt()); appendToken(msg, upd, "RS232 DataBits "); }

    v = bodyValue(req.body, req.contentType, "parity");
    if (v.length()) { configManager.setRs232Parity(v); appendToken(msg, upd, "RS232 Parity "); }

    v = bodyValue(req.body, req.contentType, "stop_bits");
    if (v.length()) { configManager.setRs232StopBits(v.toInt()); appendToken(msg, upd, "RS232 StopBits "); }

    v = bodyValue(req.body, req.contentType, "read_timeout");
    if (!v.length()) v = bodyValue(req.body, req.contentType, "read_timeout_ms");
    if (v.length()) { configManager.setRs232ReadTimeoutMs(v.toInt()); appendToken(msg, upd, "RS232 ReadTout "); }

    if (bodyHasKey(req.body, req.contentType, "use_xon_xoff")) {
        configManager.setRs232UseXonXoff(bodyTruthy(req.body, req.contentType, "use_xon_xoff"));
        appendToken(msg, upd, "RS232 XON/XOFF ");
    }

    // ── Structures ──
    applyStructuresFromBody(req.body, req.contentType, msg, upd);

    // ── FTP section ──
    v = bodyValue(req.body, req.contentType, "ftp_host");
    if (v.length()) { configManager.setFtpHost(v); appendToken(msg, upd, "FTP Host "); }
    v = bodyValue(req.body, req.contentType, "ftp_port");
    if (v.length()) { configManager.setFtpPort(v.toInt()); appendToken(msg, upd, "FTP Port "); }
    v = bodyValue(req.body, req.contentType, "ftp_user");
    if (v.length() || bodyHasKey(req.body, req.contentType, "ftp_user")) { configManager.setFtpUser(v); appendToken(msg, upd, "FTP User "); }
    v = bodyValue(req.body, req.contentType, "ftp_pass");
    if (v.length() && v != F("********")) { configManager.setFtpPassword(v); appendToken(msg, upd, "FTP Pass "); }
    if (bodyHasKey(req.body, req.contentType, "ftp_anonymous")) {
        configManager.setFtpAnonymous(bodyTruthy(req.body, req.contentType, "ftp_anonymous"));
        appendToken(msg, upd, "FTP Anonymous ");
    }
    v = bodyValue(req.body, req.contentType, "ftp_remote_path");
    if (v.length() || bodyHasKey(req.body, req.contentType, "ftp_remote_path")) { configManager.setFtpRemotePath(v); appendToken(msg, upd, "FTP Path "); }
    v = bodyValue(req.body, req.contentType, "ftp_local_dir");
    if (v.length() || bodyHasKey(req.body, req.contentType, "ftp_local_dir")) { configManager.setFtpLocalDir(v); appendToken(msg, upd, "FTP LocalDir "); }
    if (bodyHasKey(req.body, req.contentType, "ftp_enabled")) {
        configManager.setFtpEnabled(bodyTruthy(req.body, req.contentType, "ftp_enabled"));
        appendToken(msg, upd, "FTP Enabled ");
    }
    if (bodyHasKey(req.body, req.contentType, "ftp_auto_upload")) {
        configManager.setFtpAutoUpload(bodyTruthy(req.body, req.contentType, "ftp_auto_upload"));
        appendToken(msg, upd, "FTP Auto ");
    }
    v = bodyValue(req.body, req.contentType, "ftp_interval_min");
    if (v.length()) { configManager.setFtpIntervalMin(v.toInt()); appendToken(msg, upd, "FTP Interval "); }
    if (bodyHasKey(req.body, req.contentType, "ftp_only_new")) {
        configManager.setFtpOnlyNew(bodyTruthy(req.body, req.contentType, "ftp_only_new"));
        appendToken(msg, upd, "FTP OnlyNew ");
    }
    if (bodyHasKey(req.body, req.contentType, "ftp_delete_after")) {
        configManager.setFtpDeleteAfter(bodyTruthy(req.body, req.contentType, "ftp_delete_after"));
        appendToken(msg, upd, "FTP DeleteAfter ");
    }
    v = bodyValue(req.body, req.contentType, "ftp_max_files");
    if (v.length()) { configManager.setFtpMaxFiles(v.toInt()); appendToken(msg, upd, "FTP MaxFiles "); }
    v = bodyValue(req.body, req.contentType, "ftp_filter");
    if (v.length() || bodyHasKey(req.body, req.contentType, "ftp_filter")) { configManager.setFtpFilter(v); appendToken(msg, upd, "FTP Filter "); }

    if (!upd) {
        res.send(200, "application/json", "{\"success\":true,\"message\":\"No changes detected\"}");
        return;
    }

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: del/rebuild the RGB panel around ALL flash writes below
    // (LittleFS + NVS) — the VSYNC restart ISR is flash-hosted, so any write
    // with the panel streaming risks a reset. Display blacks out ~2-3 s.
    Touch7PanelHold cfgPanelHold_;
#endif
    if (ntpUpdated) {
        configTzTime(configManager.getNtpTimezone().c_str(), configManager.getNtpServer().c_str());
        // Persist the NTP settings: setNtpTimezone/setNtpServer only mark the
        // manager dirty (auto-save disabled), so without this explicit save the
        // timezone/server revert to defaults after a reboot.
        ntpManager.save();
    }

    if (wifiUpdated && newSsid.length() > 0) {
        configManager.addWifiNetwork(newSsid, newPass);
    }

    syncGlobalVariables();
#if ENABLE_RS232==1
    // RS232 packs: re-apply UART config on POST. RS232=0 packs (CAM, CAM_P4,
    // ENVIRONMENT, RADIO, S3_TOUCH7, WAVESHARE — all also DB25/UNIKON=0, so no
    // UART machinery exists): skip — the light path only refreshes dead
    // RS232 globals (syncGlobalVariables() above already ran).
    applyRS232Config();
#endif
    const char* stem = "core";
    if (section == "wifi")       stem = "wifi";
    else if (section == "email")      stem = "smtp_email";
    else if (section == "system")     stem = "system";
    else if (section == "rs232")      stem = "rs232";
    else if (section == "recipients") stem = "recipients";
    else if (section == "scheduler")  stem = "schedule";
    else if (section == "ethernet")   stem = "ethernet";
    else if (section == "ftp")        stem = "ftp";
    bool savedOk = configManager.saveSection(stem);
    // The System section can also touch Telnet and the unified (web) password.
    // Those live in their own files/NVS — persist them too so nothing is lost.
    if (section == "system") {
        bool telnetTouched = bodyHasKey(req.body, req.contentType, "telnet_enabled")
            || bodyHasKey(req.body, req.contentType, "unified_password");
        if (telnetTouched) configManager.saveSection("telnet");
        if (bodyHasKey(req.body, req.contentType, "unified_password")) configManager.saveSection("wifi");
    }

    if (wifiUpdated && newSsid.length() > 0) {
        loadSavedNetworks();
        syncGlobalVariables();
        if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != newSsid) {
            if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
            currentHybridState = HYBRID_BOOT_AP_STA;
            hybridBootTime = millis();
            hybridBootTimeout = 60000;
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(newSsid, newPass);
        }
    }

    if (savedOk) {
        res.send(200, "application/json", "{\"success\":true,\"message\":\"Config saved\"}");
    } else {
        res.send(500, "application/json", "{\"success\":false,\"message\":\"Save failed\"}");
    }
}

// ── GET /api/configs ──────────────────────────────────────────────────

void handleConfigsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    SpiRamJsonDocument doc(2048);
    JsonObject saved = doc.createNestedObject("saved_configs");
    saved["wifi_ssid"] = configManager.getWifiSSID();
    saved["printer_emulation_mode"] = configManager.getPrinterEmulationMode();
    saved["charset_mode"] = configManager.getPrinterCharsetMode();
    saved["ntp_timezone"] = configManager.getNtpTimezone();
    saved["timezone"] = configManager.getNtpTimezone();
    JsonObject ntp = saved.createNestedObject("ntp");
    ntp["timezone"] = configManager.getNtpTimezone();
    ntp["server"] = configManager.getNtpServer();
    saved["ntp_server"] = configManager.getNtpServer();
    saved["system_name"] = configManager.getSystemName();
    saved["unified_password"] = "";
    saved["smtp_server"] = configManager.getSmtpServer();
    saved["smtp_port"] = configManager.getSmtpPort();
    saved["smtp_user"] = configManager.getSmtpUsername();
    saved["smtp_pass"] = "********";
    saved["smtp_tls_insecure"] = configManager.getSmtpTlsInsecure();
    saved["google_script_url"] = configManager.getGoogleScriptUrl();
    saved["email_destination"] = configManager.getEmailDestination();
    saved["autoEmailEnabledTimeout"] = configManager.getTimeoutAutoEmailEnabled();
    saved["autoEmailDisabledTimeout"] = configManager.getTimeoutAutoEmailDisabled();
    JsonObject structures = saved.createNestedObject("structures");
    structures["auto_detect"] = configManager.getStructuresAutoDetectEnabled();
    JsonObject amadaMtf1 = structures.createNestedObject("amada_isq20_mtf1");
    amadaMtf1["enabled"] = configManager.getAmadaISQ20MTF1Enabled();
    JsonObject amadaAwse = structures.createNestedObject("amada_isq20_awse");
    amadaAwse["enabled"] = configManager.getAmadaISQ20AWSEEnabled();
    structures["amada_filter_headers"] = configManager.getAmadaFilterHeaders();
    structures["amada_header_every"] = configManager.getAmadaHeaderEvery();
    JsonObject email = saved.createNestedObject("email");
    email["method"] = configManager.getEmailMethod();
    email["destination"] = configManager.getEmailDestination();
    email["auto_send"] = configManager.getEmailAutoSend();
    email["max_lines"] = configManager.getEmailMaxLines();
    email["min_content_length"] = configManager.getEmailMinContentLength();
    email["attach_txt"] = configManager.getEmailAttachTxt();
    email["include_timestamp"] = configManager.getEmailIncludeTimestamp();

    String json;
    serializeJsonPretty(doc, json);
    res.send(200, "application/json", json);
}

// ── GET /api/access-config ────────────────────────────────────────────

void handleAccessConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    JsonVariant webCfg = configManager.getSection("web");
    SpiRamJsonDocument doc(512);
    JsonObject web = doc.createNestedObject("web");
    web["max_sessions"] = webCfg["max_sessions"] | 3;
    web["idle_timeout_min"] = webCfg["idle_timeout_min"] | 10;
    web["abs_timeout_min"] = webCfg["abs_timeout_min"] | 20;
    web["username"] = webCfg["username"] | "";
    web["auth_enabled"] = webCfg["auth_enabled"] | false;
    web["viewer_enabled"] = webCfg["viewer_enabled"] | false;
    web["viewer_username"] = webCfg["viewer_username"] | "viewer";
    doc["charts_exempt"] = configManager.getChartsExemptSessions();
    doc["serial_monitor_enabled"] = configManager.getSerialMonitorEnabled();

    String json;
    serializeJson(doc, json);
    res.send(200, "application/json", json);
}

// ── POST /api/access-config ───────────────────────────────────────────

void handleAccessConfigPost(HttpRequest& req, HttpResponse& res) {
    bool upd = false;
    String msg;

    auto getInt = [&](const char* key, int minVal, int maxVal) -> int {
        String v = bodyValue(req.body, req.contentType, key);
        if (!v.length()) return -1;
        int val = v.toInt();
        return (val >= minVal && val <= maxVal) ? val : -1;
    };

    int val = getInt("web_max_sessions", 1, 5);
    if (val >= 0) { configManager.setWebMaxSessions(val); appendToken(msg, upd, "MaxSessions "); }

    val = getInt("web_idle_timeout_min", 1, 120);
    if (val >= 0) { configManager.setWebIdleTimeoutMin(val); appendToken(msg, upd, "IdleTimeout "); }

    val = getInt("web_abs_timeout_min", 5, 240);
    if (val >= 0) { configManager.setWebAbsTimeoutMin(val); appendToken(msg, upd, "AbsTimeout "); }

    String v = bodyValue(req.body, req.contentType, "web_username");
    if (v.length() && v != configManager.getWebUsername()) { configManager.setWebUsername(v); appendToken(msg, upd, "WebUser "); }

    if (bodyHasKey(req.body, req.contentType, "auth_enabled_present")) {
        bool posted = bodyTruthy(req.body, req.contentType, "auth_enabled");
        if (posted != configManager.getWebAuthEnabled()) { configManager.setWebAuthEnabled(posted); appendToken(msg, upd, posted ? "AuthEnabled " : "AuthDisabled "); }
    }

    v = bodyValue(req.body, req.contentType, "web_password_new");
    if (v.length() && v != F("(não alterado)") && v != F("(nao alterado)")) { configManager.setWebPassword(v); appendToken(msg, upd, "WebPass "); }

    if (bodyHasKey(req.body, req.contentType, "viewer_enabled_present")) {
        bool posted = bodyTruthy(req.body, req.contentType, "viewer_enabled");
        if (posted != configManager.getWebViewerEnabled()) { configManager.setWebViewerEnabled(posted); appendToken(msg, upd, posted ? "ViewerEnabled " : "ViewerDisabled "); }
    }

    v = bodyValue(req.body, req.contentType, "viewer_username");
    if (v.length() && v != configManager.getWebViewerUsername()) { configManager.setWebViewerUsername(v); appendToken(msg, upd, "ViewerUser "); }

    v = bodyValue(req.body, req.contentType, "viewer_password");
    if (v.length() && v != F("(não alterado)") && v != F("(nao alterado)")) { configManager.setWebViewerPassword(v); appendToken(msg, upd, "ViewerPass "); }

    if (bodyHasKey(req.body, req.contentType, "charts_exempt_present")) {
        bool posted = bodyTruthy(req.body, req.contentType, "charts_exempt");
        if (posted != configManager.getChartsExemptSessions()) { configManager.setChartsExemptSessions(posted); appendToken(msg, upd, posted ? "ChartsExempt:on " : "ChartsExempt:off "); }
    }

    if (bodyHasKey(req.body, req.contentType, "serial_monitor_enabled_present")) {
        bool posted = bodyTruthy(req.body, req.contentType, "serial_monitor_enabled");
        if (posted != configManager.getSerialMonitorEnabled()) { configManager.setSerialMonitorEnabled(posted); appendToken(msg, upd, posted ? "SerialMon:on " : "SerialMon:off "); }
    }

    if (!upd) {
        res.send(200, "application/json", "{\"success\":true,\"message\":\"No changes detected\"}");
        return;
    }

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: del/rebuild the RGB panel around the flash write (see handleConfigPost).
    Touch7PanelHold accessPanelHold_;
#endif
    bool savedOk = configManager.saveSection("access");
    if (savedOk) {
        res.send(200, "application/json", "{\"success\":true,\"message\":\"Access settings saved\"}");
    } else {
        res.send(500, "application/json", "{\"success\":false,\"message\":\"Save error\"}");
    }
}

// ── GET /api/session-history ──────────────────────────────────────────

void handleSessionHistoryGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String json = buildSessionHistoryJson();
    res.send(200, "application/json", json);
}

// ── POST /api/access-kick-all ─────────────────────────────────────────

void handleAccessKickAllPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    webclientForceReset();
    sendVirtualMessage(F("Web sessions force-reset via /api/access-kick-all"), INFO);
    res.send(200, "application/json", "{\"success\":true,\"message\":\"All sessions terminated\"}");
}
