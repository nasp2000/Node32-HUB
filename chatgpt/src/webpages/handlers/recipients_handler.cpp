// Handlers for recipient slots and inactivity-alert configuration APIs.

#include "../../languages/lang_active.h"
#include "recipients_handler.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../../ConfigManager.h"
#include "../../psram_helpers.h"

extern ConfigManager configManager;

void registerRecipientRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/recipients", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        unsigned long limit = 50;
        unsigned long offset = 0;
        const unsigned long maxLimit = 200;

        if (request->hasArg("limit")) {
            limit = (unsigned long)constrain(request->arg("limit").toInt(), 1, (int)maxLimit);
        }
        if (request->hasArg("offset")) {
            long offv = request->arg("offset").toInt();
            if (offv < 0) offv = 0;
            offset = (unsigned long)offv;
        } else if (request->hasArg("page")) {
            int page = request->arg("page").toInt();
            if (page < 1) page = 1;
            offset = (unsigned long)((page - 1) * (int)limit);
        }

        JsonArray slots = configManager.getRecipientSlots();
        size_t total = slots.size();
        if (offset >= total) {
            SpiRamJsonDocument doc(512);
            JsonObject root = doc.to<JsonObject>();
            root["total"] = (int)total;
            root["offset"] = (int)offset;
            root["limit"] = (int)limit;
            root.createNestedArray("items");
            AsyncResponseStream* response = request->beginResponseStream("application/json");
            response->addHeader("Cache-Control", "max-age=10");
            serializeJson(root, *response);
            request->send(response);
            return;
        }

        size_t end = offset + limit;
        if (end > total) end = total;
        size_t itemsCount = end - offset;
        SpiRamJsonDocument doc(512 + itemsCount * 128);
        JsonObject root = doc.to<JsonObject>();
        root["total"] = (int)total;
        root["offset"] = (int)offset;
        root["limit"] = (int)limit;
        JsonArray items = root.createNestedArray("items");
        for (size_t i = offset; i < end; ++i) {
            JsonObject slot = slots[i].as<JsonObject>();
            JsonObject item = items.createNestedObject();
            item["name"] = slot["name"] | String("");
            item["op"] = slot["op"] | String("I");
            item["emails"] = slot["emails"] | String("");
            item["freq"] = slot["freq"] | 0;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->addHeader("Cache-Control", "max-age=10");
        serializeJson(root, *response);
        request->send(response);
    });

    server.on("/api/recipient-slots", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        SpiRamJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("slots");
        JsonArray rawSlots = configManager.getRecipientSlots();
        for (int i = 1; i <= 50; ++i) {
            String name;
            String op;
            String emails;
            int freq = 0;
            if (!configManager.getRecipientSlotWithFreq(i, name, op, emails, freq)) break;
            if (op == "DF") op = "DR";
            bool active = true;
            if (i <= (int)rawSlots.size()) active = rawSlots[i - 1]["enabled"] | true;
            JsonObject slot = arr.createNestedObject();
            slot["name"] = name;
            slot["op"] = op;
            slot["emails"] = emails;
            slot["freq"] = freq;
            slot["active"] = active;
        }
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

}