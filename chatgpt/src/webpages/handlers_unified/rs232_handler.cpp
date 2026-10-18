#include "../../languages/lang_active.h"
#include "rs232_handler.h"
#include "../../feature_flags.h"

#if ENABLE_RS232 == 1
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../ConfigManager.h"
#include "../../commands.h"
#include "../../coordinator_hooks.h"
#include "../../direct_log.h"
#include "../../web_shared_logic.h"

extern ConfigManager configManager;

void handleRs232PersistCurrentTemplatePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String currentTemplate = configManager.getRs232CurrentTemplate();
    if (currentTemplate.length() == 0) {
        res.send(400, "application/json", F("{\"status\":\"error\",\"message\":\"Template atual vazio\"}"));
        return;
    }
    configManager.setRs232CurrentTemplate(currentTemplate);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("rs232")) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveCurrentTemplate) + F("\",\"status\":\"error\"}"));
        return;
    }
    res.sendJson(String(F("{\"status\":\"ok\",\"template\":\"")) + currentTemplate + String(F("\"}")));
}

void handleRs232StatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool enabled = configManager.getRs232Enabled();
    int idx = configManager.getRs232BaudrateIndex();
    int baud = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;
    String parity = configManager.getRs232Parity();
    parity.replace("\"", "\\\"");
    String tmpl = configManager.getRs232CurrentTemplate();
    tmpl.replace("\"", "\\\"");
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"enabled\":%s,\"baudrate\":%d,\"baudrate_index\":%d,\"invert_pins\":%s,\"data_bits\":%d,"
        "\"parity\":\"%s\",\"stop_bits\":%d,\"read_timeout_ms\":%d,\"use_xon_xoff\":%s,\"current_template\":\"%s\"}",
        enabled ? "true" : "false", baud, idx,
        configManager.getRs232InvertPins() ? "true" : "false",
        configManager.getRs232DataBits(), parity.c_str(),
        configManager.getRs232StopBits(),
        configManager.getRs232ReadTimeoutMs(),
        configManager.getRs232UseXonXoff() ? "true" : "false",
        tmpl.c_str());
    res.send(200, "application/json", String(buf));
}

void handleTestRs232Post(HttpRequest& req, HttpResponse& res) {
    (void)req;
    const char* testPayload = "RS232_TEST";
    bool queued = enqueueRS232Command(String(testPayload), String(""), String("TEST"), String("rs232"));
    String msg = queued ? F("Test queued. Check data monitor for response.") : F("Queue full: not queued");
    int baud = 0;
    if (rs232_baudrate_index >= 0 && rs232_baudrate_index < RS232_BAUDRATES_COUNT) {
        baud = RS232_BAUDRATES[rs232_baudrate_index];
    }
    logRs232TestPayloadToSd();
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"success\":%s,\"message\":\"%s\",\"enabled\":%s,\"baudrate_index\":%d,\"baudrate\":%d,\"bytes_enqueued\":%d}",
        queued ? "true" : "false", msg.c_str(),
        rs232_enabled ? "true" : "false",
        rs232_baudrate_index, baud, (int)strlen(testPayload));
    res.send(200, "application/json", String(buf));
}

#else
void handleRs232PersistCurrentTemplatePost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"RS232 not enabled\"}")); }
void handleRs232StatusGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"RS232 not enabled\"}")); }
void handleTestRs232Post(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"RS232 not enabled\"}")); }
#endif
