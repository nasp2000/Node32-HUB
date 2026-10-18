// do_handler.cpp
// REST API for TCA9554 digital output expander.
//
//   GET  /api/do/status          -- returns current output states
//   POST /api/do/set             -- set one channel: {"ch":0,"state":true}
//                                   or set all:     {"mask":255}

#include "do_handler.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../arduinojson_fix.h"
#include "../../tca9554_do.h"
#include "../../ConfigManager.h"
#include "../../config_manager_runtime.h"
#include "../../commands.h"

#define DO_NAME_MAX_LEN 16

void registerDoApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {

    // POST /api/do/set — {"ch":N,"state":true} or {"mask":M}
    server.on("/api/do/set", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            if (!context.ensureBasicAuth(request)) return;

            StaticJsonDocument<64> doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err != DeserializationError::Ok) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"JSON invalido\"}");
                return;
            }

            bool ok = false;
            if (doc.containsKey("mask")) {
                uint8_t mask = (uint8_t)(doc["mask"] | 0);
                ok = tca9554SetAll(mask);
            } else if (doc.containsKey("ch")) {
                uint8_t ch    = (uint8_t)(doc["ch"]    | 0);
                bool    state = (bool)(doc["state"]     | false);
                ok = tca9554SetOutput(ch, state);
            } else {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Falta ch ou mask\"}");
                return;
            }

            if (ok) {
                tca9554SaveState();
                request->send(200, "application/json",
                              "{\"success\":true,\"message\":\"OK\"}");
            } else {
                request->send(500, "application/json",
                              "{\"success\":false,\"message\":\"I2C NACK\"}");
            }
        }
    );

    // POST /api/do/config — {"names":[...8],"invert":N}
    server.on("/api/do/config", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            if (!context.ensureBasicAuth(request)) return;

            SpiRamJsonDocument doc(512);
            DeserializationError err = deserializeJson(doc, data, len);
            if (err != DeserializationError::Ok) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"JSON invalido\"}");
                return;
            }

            JsonVariant doSec = configManager.getSection("do");
            if (!doSec.is<JsonObject>()) doSec.to<JsonObject>();

            uint8_t invert = (uint8_t)(doc["invert"] | (int)0);
            doSec["invert"] = invert;

            JsonArray names = doc["names"].as<JsonArray>();
            if (!names.isNull()) {
                for (int i = 0; i < TCA9554_DO_CHANNELS; i++) {
                    String nm = (i < (int)names.size()) ? (String)(names[i] | "") : String("");
                    if (nm.length() > DO_NAME_MAX_LEN) nm = nm.substring(0, DO_NAME_MAX_LEN);
                    doSec["names"][i] = nm;
                }
            }

            if (!configManager.saveSection("do")) {
                request->send(500, "application/json",
                              "{\"success\":false,\"message\":\"Save failed\"}");
                return;
            }
            tca9554SetInvert(invert); // apply live, no reboot needed
            sendVirtualMessage(F("DO: config saved via web"), INFO, false, true);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"OK\"}");
        }
    );
}

#endif // BOARD_HAS_TCA9554_DO
