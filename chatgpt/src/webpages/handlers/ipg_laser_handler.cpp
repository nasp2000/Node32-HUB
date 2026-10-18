#include "ipg_laser_handler.h"

#include <ArduinoJson.h>

#include "../../ConfigManager.h"
#include "../../feature_flags.h"
#include "../../ipg_laser_module.h"
#include "../../psram_helpers.h"

extern ConfigManager configManager;
extern const int RS232_BAUDRATES[];
extern const int RS232_BAUDRATES_COUNT;

static const size_t IPG_LASER_SEND_MAX_PAYLOAD = 1024;

#if ENABLE_IPG_LASER == 1
static void sendIpgLaserStatus(AsyncWebServerRequest* request) {
    IpgLaserStatus status = ipgLaserGetStatus();
    int idx = configManager.getRs232BaudrateIndex();
    int baud = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;

    SpiRamJsonDocument doc(768);
    doc["captureActive"] = status.captureActive;
    doc["captureEndsInMs"] = status.captureEndsInMs;
    doc["lastRxAgeMs"] = status.lastRxAgeMs;
    doc["logCount"] = status.logCount;
    doc["lastCommand"] = status.lastCommand;
    doc["currentBaudrate"] = baud;
    doc["currentDataBits"] = configManager.getRs232DataBits();
    doc["currentParity"] = configManager.getRs232Parity();
    doc["currentStopBits"] = configManager.getRs232StopBits();
    doc["currentFlowControl"] = configManager.getRs232UseXonXoff();
    doc["currentTemplate"] = configManager.getRs232CurrentTemplate();
    doc["requiredBaudrate"] = 57600;
    doc["requiredDataBits"] = 8;
    doc["requiredParity"] = "none";
    doc["requiredStopBits"] = 1;
    doc["requiredFlowControl"] = "none";
    doc["transport"] = "RS232 null-modem 3-wire";
    doc["remoteModeNote"] = "Laser must be in REMOTE mode with interlocks satisfied before EMON.";

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}
#endif // ENABLE_IPG_LASER == 1

void registerIpgLaserApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_IPG_LASER == 1
    server.on("/api/ipg-laser-status", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        sendIpgLaserStatus(request);
    });

    server.on("/api/ipg-laser-commands", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        size_t count = 0;
        const IpgLaserCommandInfo* catalog = ipgLaserGetCommandCatalog(&count);
        SpiRamJsonDocument doc(24576);
        doc["count"] = (unsigned long)count;
        JsonArray commands = doc.createNestedArray("commands");
        for (size_t i = 0; i < count; ++i) {
            JsonObject item = commands.createNestedObject();
            item["code"] = catalog[i].code;
            item["category"] = catalog[i].category;
            item["usage"] = catalog[i].usage;
            item["description"] = catalog[i].description;
            item["response"] = catalog[i].response;
            item["notes"] = catalog[i].notes;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on("/api/ipg-laser-log", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        IpgLaserLogEntry entries[24];
        uint16_t count = ipgLaserCopyLog(entries, 24);
        SpiRamJsonDocument doc(8192);
        doc["count"] = count;
        JsonArray entriesArray = doc.createNestedArray("entries");
        for (uint16_t i = 0; i < count; ++i) {
            JsonObject item = entriesArray.createNestedObject();
            item["ageMs"] = entries[i].ageMs;
            item["direction"] = entries[i].direction;
            item["text"] = entries[i].text;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on(
        "/api/ipg-laser-send",
        HTTP_POST,
        [&context](AsyncWebServerRequest* request) {
            if (!context.ensureBasicAuth(request)) return;
            // Only reject here when there is truly no body; if contentLength > 0 the
            // onBody callback below will receive the data and send the real response.
            if (request->contentLength() > 0) return;
            request->send(400, "application/json", "{\"success\":false,\"message\":\"JSON body required.\"}");
        },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index == 0) {
                if (total == 0 || total > IPG_LASER_SEND_MAX_PAYLOAD) {
                    request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large.\"}");
                    return;
                }
                String* body = new String();
                if (!body) {
                    request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory.\"}");
                    return;
                }
                body->reserve(total);
                request->_tempObject = body;
            }

            String* bodyBuffer = reinterpret_cast<String*>(request->_tempObject);
            if (!bodyBuffer) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Request buffer missing.\"}");
                return;
            }

            if (bodyBuffer->length() + len > IPG_LASER_SEND_MAX_PAYLOAD) {
                delete bodyBuffer;
                request->_tempObject = nullptr;
                request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large.\"}");
                return;
            }

            bodyBuffer->concat(reinterpret_cast<const char*>(data), len);
            if (index + len < total) return;

            SpiRamJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, *bodyBuffer);
            delete bodyBuffer;
            request->_tempObject = nullptr;
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON body.\"}");
                return;
            }

            String command = doc["command"] | String("");
            String templateName = doc["template"] | String("");
            bool ok = ipgLaserSendCommand(command, templateName);
            if (!ok) {
                request->send(200, "application/json", "{\"success\":false,\"message\":\"Unable to queue IPG command.\"}");
                return;
            }

            request->send(200, "application/json", "{\"success\":true,\"message\":\"Command queued. Waiting for laser response.\"}");
        }
    );

#else
    (void)server;
    (void)context;
#endif
}