#include "../../languages/lang_active.h"
#include "recipients_handler.h"
#include "../../feature_flags.h"
#include "../../coordinator_hooks.h"
#include "../../commands.h"
#include "../../email_message_builder.h"
#include "../../web_shared_logic.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../ConfigManager.h"

extern ConfigManager configManager;

namespace {

String getParam(const String& src, const String& key) {
    String search = key + "=";
    int idx = src.indexOf(search);
    if (idx < 0) return String();
    int start = idx + search.length();
    int end = src.indexOf('&', start);
    return end < 0 ? src.substring(start) : src.substring(start, end);
}

int getParamInt(const String& src, const char* key, int fallback) {
    String v = getParam(src, key);
    return v.length() ? v.toInt() : fallback;
}

#if ENABLE_PAGE_RECIPIENTS==1

String escapeJsonStr(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '\\': out += F("\\\\"); break;
            case '"': out += F("\\\""); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

bool isValidRecipientOp(const String& op) {
    return op == "I" || op == "D" || op == "+" || op == "-" || op == "G" || op == "L" ||
           op == "R" || op == "DF" || op == "DR" || op == "V" || op == "C" || op == "P" ||
           op == "Cw" || op == "Hy";
}

#endif // ENABLE_PAGE_RECIPIENTS

} // namespace

#if ENABLE_PAGE_ALERTS==1

void handleInactivityAlertConfigPost(HttpRequest& req, HttpResponse& res) {
    sendVirtualMessage(String(F("[DIAG alerts] POST handler ENTER")), 1, false, true);
    const String& src = req.body.length() ? req.body : req.query;

    for (int i = 0; i < 6; i++) {
        String suffix = String(i);
        String enabled = getParam(src, "alert_enabled_" + suffix);
        bool isEnabled = (enabled == "1");

        int timeoutInt = 5;
        int maxPerDayInt = 6;
        String emails;

        if (isEnabled) {
            String timeout = getParam(src, "alert_timeout_" + suffix);
            if (timeout.length()) timeoutInt = timeout.toInt();
            String maxPerDay = getParam(src, "alert_max_" + suffix);
            if (maxPerDay.length()) maxPerDayInt = maxPerDay.toInt();
            emails = getParam(src, "alert_emails_" + suffix);
            emails.trim();
            emails = stripControlChars(emails);

            if (timeoutInt < 1 || timeoutInt > 1440) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Timeout must be 1-1440\"}"));
                return;
            }
            if (maxPerDayInt < 1 || maxPerDayInt > 100) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Max per day must be 1-100\"}"));
                return;
            }
            if (!emails.length()) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Emails required when alert is active\"}"));
                return;
            }
            if (emails.indexOf('@') < 0) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Invalid email (missing @)\"}"));
                return;
            }
        }

        configManager.setAlertSlot(i, isEnabled, timeoutInt, maxPerDayInt, emails);
    }

    configManager.saveSection("alerts");
    res.sendJson(F("{\"status\":\"ok\"}"));
}

// Local JSON escaping for stored alert emails (alerts build may have no recipients).
static String alertEscapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '\\': out += F("\\\\"); break;
            case '"':  out += F("\\\""); break;
            default:   out += c;         break;
        }
    }
    return out;
}

// GET mirror of handleInactivityAlertConfigPost: reads the 6 persisted slots and
// returns BOTH the slots array (programmatic consumers) and the flat alert_*_<i>
// field names the page form uses — without it the page's load GET 404s.
void handleInactivityAlertConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String buf = "{\"status\":\"ok\",\"slots\":[";
    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        if (i) buf += ",";
        buf += "{\"enabled\":" + String(configManager.getAlertSlotEnabled(i) ? "true" : "false") +
               ",\"timeout_minutes\":" + String(configManager.getAlertSlotTimeout(i)) +
               ",\"max_per_day\":" + String(configManager.getAlertSlotMaxPerDay(i)) +
               ",\"emails\":\"" + alertEscapeJson(configManager.getAlertSlotEmails(i)) + "\"}";
    }
    buf += "],";
    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        if (i) buf += ",";
        buf += "\"alert_enabled_" + String(i) + "\":\"" +
               String(configManager.getAlertSlotEnabled(i) ? "1" : "0") + "\"" +
               ",\"alert_timeout_" + String(i) + "\":\"" + String(configManager.getAlertSlotTimeout(i)) + "\"" +
               ",\"alert_max_" + String(i) + "\":\"" + String(configManager.getAlertSlotMaxPerDay(i)) + "\"" +
               ",\"alert_emails_" + String(i) + "\":\"" +
               alertEscapeJson(configManager.getAlertSlotEmails(i)) + "\"";
    }
    buf += "}";
    res.sendJson(buf);
}

void handleInactivityAlertTestPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    int slot = getParamInt(src, "i", -1);
    if (slot < 0 || slot >= ConfigManager::ALERT_SLOT_COUNT) {
        res.sendJson(F("{\"status\":\"error\",\"message\":\"Invalid slot index\"}"));
        return;
    }

    String emails = getParam(src, "emails");
    emails.trim();
    if (!emails.length()) {
        res.sendJson(F("{\"status\":\"error\",\"message\":\"Emails required\"}"));
        return;
    }

#if ENABLE_SMTP==1
    // Reuse the async inactivity-alert task with a test subject/body so the user
    // can validate the whole pipeline (SMTP, recipients, template) without waiting
    // for real inactivity.
    String subject = String(F("\xF0\x9F\x9A\xA8 - ")) + configManager.getSystemName() + String(F(" - TEST Inactivity Alert"));
    String body = buildEmailInfoTable(
        String(F("TEST alert — slot ")) + String(slot + 1) +
        String(F(". Device reports no received data for the configured timeout.")),
        "", "", "",
        String(F("This is a test. No action required.")));
    extern bool startInactivityAlertAsync(const String &recipients, const String &subject, const String &body);
    bool started = startInactivityAlertAsync(emails, subject, body);
    if (started) {
        res.sendJson(F("{\"status\":\"ok\",\"message\":\"Test alert started\"}"));
    } else {
        res.sendJson(F("{\"status\":\"error\",\"message\":\"Email already in progress or not configured\"}"));
    }
#else
    (void)slot;
    (void)emails;
    res.sendJson(F("{\"status\":\"error\",\"message\":\"SMTP disabled\"}"));
#endif
}

#if ENABLE_PROXIMITY==1
// OEE alert slots (alertas OEE tab) — proximity/OEE builds only. Same alerts.json
// file (oee_slots array inside inactivity_alerts), same saveSection("alerts").
void handleOeeAlertConfigPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; i++) {
        String suffix = String(i);
        bool isEnabled = (getParam(src, "oee_enabled_" + suffix) == "1");

        int metric = 0;
        int thresholdInt = 60;
        int persistMinInt = 15;
        int maxPerDayInt = 6;
        String emails;

        if (isEnabled) {
            String m = getParam(src, "oee_metric_" + suffix);
            if (m.length()) metric = m.toInt();
            String t = getParam(src, "oee_threshold_" + suffix);
            if (t.length()) thresholdInt = t.toInt();
            String p = getParam(src, "oee_persist_" + suffix);
            if (p.length()) persistMinInt = p.toInt();
            String mp = getParam(src, "oee_max_" + suffix);
            if (mp.length()) maxPerDayInt = mp.toInt();
            emails = getParam(src, "oee_emails_" + suffix);
            emails.trim();
            emails = stripControlChars(emails);

            if (metric < 0 || metric > 2) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Metric must be 0-2\"}"));
                return;
            }
            if (thresholdInt < 1 || thresholdInt > 100) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Threshold must be 1-100\"}"));
                return;
            }
            if (persistMinInt < 1 || persistMinInt > 240) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Persist must be 1-240 minutes\"}"));
                return;
            }
            if (maxPerDayInt < 1 || maxPerDayInt > 100) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Max per day must be 1-100\"}"));
                return;
            }
            if (!emails.length()) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Emails required when alert is active\"}"));
                return;
            }
            if (emails.indexOf('@') < 0) {
                res.send(200, "application/json", F("{\"status\":\"error\",\"message\":\"Invalid email (missing @)\"}"));
                return;
            }
        }

        configManager.setOeeAlertSlot(i, isEnabled, metric, thresholdInt, persistMinInt, maxPerDayInt, emails);
    }

    configManager.saveSection("alerts");
    res.sendJson(F("{\"status\":\"ok\"}"));
}

// GET mirror: persisted OEE slots as an array plus the flat oee_*_<i> fields.
void handleOeeAlertConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String buf = "{\"status\":\"ok\",\"slots\":[";
    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
        if (i) buf += ",";
        buf += "{\"enabled\":" + String(configManager.getOeeAlertEnabled(i) ? "true" : "false") +
               ",\"metric\":" + String(configManager.getOeeAlertMetric(i)) +
               ",\"threshold\":" + String(configManager.getOeeAlertThreshold(i)) +
               ",\"persist_min\":" + String(configManager.getOeeAlertPersistMin(i)) +
               ",\"max_per_day\":" + String(configManager.getOeeAlertMaxPerDay(i)) +
               ",\"emails\":\"" + alertEscapeJson(configManager.getOeeAlertEmails(i)) + "\"}";
    }
    buf += "],";
    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
        if (i) buf += ",";
        buf += "\"oee_enabled_" + String(i) + "\":\"" +
               String(configManager.getOeeAlertEnabled(i) ? "1" : "0") + "\"" +
               ",\"oee_metric_" + String(i) + "\":\"" + String(configManager.getOeeAlertMetric(i)) + "\"" +
               ",\"oee_threshold_" + String(i) + "\":\"" + String(configManager.getOeeAlertThreshold(i)) + "\"" +
               ",\"oee_persist_" + String(i) + "\":\"" + String(configManager.getOeeAlertPersistMin(i)) + "\"" +
               ",\"oee_max_" + String(i) + "\":\"" + String(configManager.getOeeAlertMaxPerDay(i)) + "\"" +
               ",\"oee_emails_" + String(i) + "\":\"" +
               alertEscapeJson(configManager.getOeeAlertEmails(i)) + "\"";
    }
    buf += "}";
    res.sendJson(buf);
}
#endif // ENABLE_PROXIMITY==1

#endif // ENABLE_PAGE_ALERTS

#if ENABLE_PAGE_RECIPIENTS==1

void handleRecipientSlotGet(HttpRequest& req, HttpResponse& res) {
    int idx = getParamInt(req.query, "i", -1);
    if (idx < 1 || idx > 50) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice invalido\"}"));
        return;
    }

    String name, op, emails;
    int freq = 0;
    bool ok = false;
    bool hadException = false;
#if defined(ARDUINO_ARCH_ESP32)
    try { ok = configManager.getRecipientSlotWithFreq(idx, name, op, emails, freq); } catch (...) { hadException = true; }
#else
    ok = configManager.getRecipientSlotWithFreq(idx, name, op, emails, freq);
#endif

    if (!ok || hadException) {
        name = ""; op = "I"; emails = ""; freq = 0;
    }
    if (!isValidRecipientOp(op)) op = "I";
    if (op == "DF") op = "DR";

    bool active = true;
    JsonArray slots = configManager.getRecipientSlots();
    if (ok && idx <= (int)slots.size()) active = slots[idx - 1]["enabled"] | true;

    char buf[400];
    String nameEsc = escapeJsonStr(name);
    String opEsc = escapeJsonStr(op);
    String emailsEsc = escapeJsonStr(emails);
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"index\":%d,\"name\":\"%s\",\"op\":\"%s\",\"emails\":\"%s\",\"freq\":%d,\"active\":%s}",
             idx, nameEsc.c_str(), opEsc.c_str(), emailsEsc.c_str(), freq, active ? "true" : "false");

    res.sendJson(String(buf));
}

void handleRecipientSlotPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    int idx = getParamInt(src, "i", 0);
    if (idx < 1 || idx > 50) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice\"}"));
        return;
    }

    String name = getParam(src, "name");
    String op = getParam(src, "op");
    String emails = getParam(src, "emails");
    int freq = getParamInt(src, "freq", 0);
    String activeValue = getParam(src, "active");
    bool active = (activeValue != "0");

    if (name.length() > 50) name.remove(50);
    if (emails.length() > 800) emails.remove(800);
    if (!op.length()) op = "I";
    if (!isValidRecipientOp(op)) op = "I";
    if (op == "DR") op = "DF";

    bool updated = false;
    bool hadException = false;
#if defined(ARDUINO_ARCH_ESP32)
    try { updated = configManager.updateRecipientSlotWithFreq(idx, name, op, emails, freq, active); } catch (...) { hadException = true; }
#else
    updated = configManager.updateRecipientSlotWithFreq(idx, name, op, emails, freq, active);
#endif

    if (!updated || hadException) {
        res.send(200, "application/json", String(F("{\"status\":\"err\",\"msg\":\"")) + L(activeLang->apiNotWritten) + F("\"}"));
        return;
    }

    resetRecipientThrottleSlot(idx);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("recipients")) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveRecipient) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}

void handleEmaildayExtraEmailsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String emails = configManager.getEmaildayExtraEmails();
    String escaped = sharedWebJsonEscape(emails);
    res.sendJson(String(F("{\"status\":\"ok\",\"emails\":\"")) + escaped + "\"}");
}

void handleEmaildayExtraEmailsPost(HttpRequest& req, HttpResponse& res) {
    String emails = getParam(req.body.length() ? req.body : req.query, "emails");
    emails.trim();
    configManager.setEmaildayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}

void handleSensordayExtraEmailsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String emails = configManager.getSensordayExtraEmails();
    String escaped = sharedWebJsonEscape(emails);
    res.sendJson(String(F("{\"status\":\"ok\",\"emails\":\"")) + escaped + "\"}");
}

void handleSensordayExtraEmailsPost(HttpRequest& req, HttpResponse& res) {
    String emails = getParam(req.body.length() ? req.body : req.query, "emails");
    emails.trim();
    configManager.setSensordayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}

void handleLogdayExtraEmailsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String emails = configManager.getLogdayExtraEmails();
    String escaped = sharedWebJsonEscape(emails);
    res.sendJson(String(F("{\"status\":\"ok\",\"emails\":\"")) + escaped + "\"}");
}

void handleLogdayExtraEmailsPost(HttpRequest& req, HttpResponse& res) {
    String emails = getParam(req.body.length() ? req.body : req.query, "emails");
    emails.trim();
    configManager.setLogdayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}

#else
void handleRecipientSlotGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleRecipientSlotPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleEmaildayExtraEmailsGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleEmaildayExtraEmailsPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleSensordayExtraEmailsGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleSensordayExtraEmailsPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleLogdayExtraEmailsGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
void handleLogdayExtraEmailsPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Recipients not enabled\"}")); }
#endif
