// Handler for the unified config snapshot API.

#include "config_snapshot_handler.h"

#include <Arduino.h>

#include "../../ConfigManager.h"
#include "../../psram_helpers.h"
#include "../../web_shared_logic.h"
#include "heavy_api_request_guard.h"

extern SpiRamJsonDocument get_all_configs();

// PSRAM cache for the serialized /api/configs JSON response.
// Avoids rebuilding the full config document on every request.
static char*         s_configsCache    = nullptr;
static size_t        s_configsCacheLen = 0;
static unsigned long s_configsCacheMs  = 0;
static const unsigned long CONFIGS_CACHE_TTL_MS = 5000UL;

// Mark the cache stale. Called after a successful POST /api/config save.
void invalidateConfigsCache() {
    s_configsCacheMs = 0;
}

void registerConfigSnapshotRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/configs", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        // Fast path: serve from PSRAM cache if still fresh.
        unsigned long now = millis();
        if (s_configsCache && s_configsCacheLen > 0 && (now - s_configsCacheMs) < CONFIGS_CACHE_TTL_MS) {
            AsyncResponseStream* resp = request->beginResponseStream("application/json", s_configsCacheLen);
            if (resp) {
                resp->addHeader("Cache-Control", "max-age=5");
                resp->write((const uint8_t*)s_configsCache, s_configsCacheLen);
                request->send(resp);
                return;
            }
        }

        HeavyApiRequestGuard heavyGuard(request, "/api/configs", 2000UL);
        if (!heavyGuard.allowed()) return;

        SpiRamJsonDocument doc = get_all_configs();

        JsonObject saved;
        if (doc.containsKey("saved_configs") && doc["saved_configs"].is<JsonObject>()) {
            saved = doc["saved_configs"].as<JsonObject>();
        } else {
            saved = doc.createNestedObject("saved_configs");
        }

        if (saved.containsKey("printerEmulationMode")) saved["printer_emulation_mode"] = saved["printerEmulationMode"];
        if (saved.containsKey("charsetMode")) saved["charset_mode"] = saved["charsetMode"];

        if (!saved.containsKey("smtp_server") && saved.containsKey("smtp.server")) saved["smtp_server"] = saved["smtp.server"];
        if (!saved.containsKey("smtp_port") && saved.containsKey("smtp.port")) saved["smtp_port"] = saved["smtp.port"];
        if (!saved.containsKey("smtp_user") && saved.containsKey("smtp.username")) saved["smtp_user"] = saved["smtp.username"];
        if (!saved.containsKey("smtp_pass") && saved.containsKey("smtp.password")) saved["smtp_pass"] = saved["smtp.password"];
        if (!saved.containsKey("smtp_server")) saved["smtp_server"] = configManager.getSmtpServer();
        if (!saved.containsKey("smtp_port")) saved["smtp_port"] = configManager.getSmtpPort();
        if (!saved.containsKey("smtp_user")) saved["smtp_user"] = configManager.getSmtpUsername();
        if (!saved.containsKey("smtp_tls_insecure")) saved["smtp_tls_insecure"] = configManager.getSmtpTlsInsecure();
        saved["smtp_pass"] = "********";
        if (saved.containsKey("web") && saved["web"].is<JsonObject>()) {
            if (saved["web"].containsKey("password")) saved["web"]["password"] = "********";
            if (saved["web"].containsKey("viewer_password")) saved["web"]["viewer_password"] = "********";
        }
        if (!saved.containsKey("google_script_url")) saved["google_script_url"] = configManager.getGoogleScriptUrl();
        if (!saved.containsKey("email_destination")) saved["email_destination"] = configManager.getEmailDestination();

        saved["recipients_count"] = (int)configManager.getRecipientSlots().size();
        saved["recipients_endpoint"] = "/api/recipients";

        if (!saved.containsKey("rs232_baudrate_list")) {
            JsonArray list = saved.createNestedArray("rs232_baudrate_list");
            for (int i = 0; i < RS232_BAUDRATES_COUNT; ++i) list.add(RS232_BAUDRATES[i]);
        }
        if (!saved.containsKey("rs232_baudrate_index")) saved["rs232_baudrate_index"] = configManager.getRs232BaudrateIndex();
        if (!saved.containsKey("rs232_baudrate")) {
            int idx = configManager.getRs232BaudrateIndex();
            int baud = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;
            saved["rs232_baudrate"] = baud;
        }
        if (!saved.containsKey("rs232_current_template")) saved["rs232_current_template"] = configManager.getRs232CurrentTemplate();

        if (configManager.getRs232Templates().size() > 0) {
            JsonArray src = configManager.getRs232Templates();
            JsonArray dst = saved.createNestedArray("rs232_templates");
            for (JsonObject templateObject : src) {
                JsonObject next = dst.createNestedObject();
                for (JsonPair keyValue : templateObject) next[keyValue.key().c_str()] = keyValue.value();
            }
        }

        JsonObject emailObj;
        if (!saved.containsKey("email")) {
            emailObj = saved.createNestedObject("email");
        } else {
            emailObj = saved["email"].as<JsonObject>();
        }
        emailObj["method"] = configManager.getEmailMethod();
        if (!emailObj.containsKey("destination")) emailObj["destination"] = configManager.getEmailDestination();
        if (!emailObj.containsKey("subject")) emailObj["subject"] = configManager.getEmailSubject();
        if (!emailObj.containsKey("auto_send")) emailObj["auto_send"] = configManager.getEmailAutoSend();
        if (!emailObj.containsKey("max_lines")) emailObj["max_lines"] = configManager.getEmailMaxLines();
        if (!emailObj.containsKey("min_content_length")) emailObj["min_content_length"] = configManager.getEmailMinContentLength();
        if (!emailObj.containsKey("dont_copy_sender")) emailObj["dont_copy_sender"] = configManager.getEmailDontCopySender();
        if (!emailObj.containsKey("attach_txt")) emailObj["attach_txt"] = configManager.getEmailAttachTxt();
        if (!emailObj.containsKey("include_timestamp")) emailObj["include_timestamp"] = configManager.getEmailIncludeTimestamp();

        if (doc.overflowed()) {
            // Document overflowed: capacity was too small, response would be incomplete.
            // This should not happen with the updated PSRAM-aware sizing in get_all_configs().
            request->send(507, "application/json", F("{\"error\":\"config document overflowed\"}"));
            return;
        }

        // Pre-size the response buffer exactly to avoid incremental realloc during serialisation.
        // Each realloc attempt under heap pressure may fail and silently truncate the response,
        // resulting in ERR_CONTENT_LENGTH_MISMATCH on the browser side.
        size_t approxLen = measureJson(doc);

        // Serialize to PSRAM cache first so the buffer can be reused for the response
        // and for future cache-hit requests without rebuilding the JSON tree.
        if (psramFound()) {
            char* newBuf = (char*)ps_malloc(approxLen + 2);
            if (newBuf) {
                free(s_configsCache);
                s_configsCache    = newBuf;
                s_configsCacheLen = serializeJson(doc, s_configsCache, approxLen + 2);
                s_configsCacheMs  = millis();
            }
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json", approxLen + 64);
        if (!response) {
            request->send(503, "application/json", F("{\"error\":\"response buffer allocation failed\"}"));
            return;
        }
        response->addHeader("Cache-Control", "max-age=30");
        response->addHeader("X-Cache-TTL", "30");

        // Prefer writing from the PSRAM cache to avoid re-traversing the JSON tree.
        if (s_configsCache && s_configsCacheLen > 0) {
            response->write((const uint8_t*)s_configsCache, s_configsCacheLen);
        } else {
            serializeJson(doc, *response);
        }
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/api/configs", approxLen, false);
#endif
    });

}