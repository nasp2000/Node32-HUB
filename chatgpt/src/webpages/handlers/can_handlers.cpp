// can_handlers.cpp
// REST API handlers for CAN bus operations.

#include "../../languages/lang_active.h"
#include "can_handlers.h"
#include "../../feature_flags.h"

#if ENABLE_CAN==1

#include <ArduinoJson.h>
#include "../../can_runtime.h"

#endif // ENABLE_CAN==1

void registerCanApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {

#if ENABLE_CAN==1

    // POST /api/can/send — transmit one CAN frame
    server.on("/api/can/send", HTTP_POST,
        [](AsyncWebServerRequest*) {},  // empty body handler (body received below)
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            if (!context.ensureBasicAuth(request)) return;

            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"JSON invalido\"}"));
                return;
            }

            CanFrame frame;
            memset(&frame, 0, sizeof(frame));
            frame.id  = doc["id"]  | 0UL;
            frame.dlc = doc["dlc"] | (uint8_t)0;
            frame.ext = doc["ext"] | false;
            frame.rtr = doc["rtr"] | false;
            frame.timestamp_ms = (uint32_t)millis();

            if (frame.dlc > 8) frame.dlc = 8;
            JsonArray arr = doc["data"].as<JsonArray>();
            uint8_t i = 0;
            for (JsonVariant v : arr) {
                if (i >= frame.dlc || i >= 8) break;
                frame.data[i++] = v.as<uint8_t>();
            }

            bool ok = canRuntimeSend(frame);
            request->send(200, F("application/json"),
                ok ? F("{\"success\":true,\"message\":\"Frame enviado\"}")
                   : String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanSendFail) + F("\"}"));
        }
    );

    // POST /api/can/config — save + restart driver
    server.on("/api/can/config", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            if (!context.ensureBasicAuth(request)) return;

            StaticJsonDocument<256> doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"JSON invalido\"}"));
                return;
            }

            CanConfig cfg = canRuntimeGetConfig(); // Start from current config

            if (doc.containsKey("enabled"))      cfg.enabled      = doc["enabled"].as<bool>();
            if (doc.containsKey("bitrate_kbps")) cfg.bitrate_kbps = doc["bitrate_kbps"].as<uint16_t>();
            if (doc.containsKey("mode"))         cfg.mode         = (CanMode)doc["mode"].as<uint8_t>();
            if (doc.containsKey("tx_pin"))       cfg.tx_pin       = doc["tx_pin"].as<int>();
            if (doc.containsKey("rx_pin"))       cfg.rx_pin       = doc["rx_pin"].as<int>();
            if (doc.containsKey("filter_id"))    cfg.filter_id    = doc["filter_id"].as<uint32_t>();
            if (doc.containsKey("filter_mask"))  cfg.filter_mask  = doc["filter_mask"].as<uint32_t>();
            if (doc.containsKey("filter_ext"))   cfg.filter_ext   = doc["filter_ext"].as<bool>();
            if (doc.containsKey("log_to_sd"))    cfg.log_to_sd    = doc["log_to_sd"].as<bool>();

            // Validate bitrate
            const uint16_t valid[] = {25,50,100,125,250,500,800,1000};
            bool validBR = false;
            for (uint8_t i = 0; i < 8; i++) if (cfg.bitrate_kbps == valid[i]) validBR = true;
            if (!validBR) {
                request->send(400, F("application/json"), F("{\"success\":false,\"message\":\"Bitrate invalido\"}"));
                return;
            }

            bool ok = canRuntimeApplyConfig(cfg, true); // save=true
            request->send(200, F("application/json"),
                ok ? String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiCanCfgSaved) + F("\"}")
                   : String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanRestartFail) + F("\"}"));
        }
    );

#else // ENABLE_CAN disabled — register stubs that return 503

    auto canDisabled = [](AsyncWebServerRequest* request) {
        request->send(503, F("application/json"),
            String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
    };
    server.on("/api/can/config",  HTTP_POST, canDisabled);
    server.on("/api/can/send",    HTTP_POST, canDisabled);
    (void)context;

#endif // ENABLE_CAN==1
}
