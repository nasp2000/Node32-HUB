// Handlers for lightweight RS232 template APIs.

#include "../../languages/lang_active.h"
#include "rs232_template_handler.h"
#include "../../feature_flags.h"

#if ENABLE_RS232==1

#include <Arduino.h>

#include "../../ConfigManager.h"
#include "../../coordinator_hooks.h"
#include "../../commands.h"
#include "../../direct_log.h"
#include "../../psram_helpers.h"
#include "../../web_shared_logic.h"

extern void syncGlobalVariables(bool includeRS232);
#if ENABLE_RS232==1
extern void applyRS232Config();
// Deferred apply flag: set instead of calling applyRS232Config() directly from async context.
extern volatile bool _pendingRS232Apply;
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern int rs232_read_timeout_ms;
extern bool rs232_template_use_xon_xoff;
#else
static inline void applyRS232Config() {}
static int rs232_data_bits = 8;
static String rs232_parity("N");
static int rs232_stop_bits = 1;
static int rs232_read_timeout_ms = 100;
static bool rs232_template_use_xon_xoff = false;
#endif // ENABLE_RS232

namespace {

void sendRs232TemplateApplyResult(AsyncWebServerRequest* request, bool found, const String& name) {
    SpiRamJsonDocument responseDoc(256);
    responseDoc["success"] = found;
    responseDoc["template"] = name;
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJsonPretty(responseDoc, *response);
    request->send(response);
}

void sendRs232TemplatesSnapshot(AsyncWebServerRequest* request, bool success) {
    SpiRamJsonDocument responseDoc(2048);
    responseDoc["success"] = success;
    responseDoc["templates"] = configManager.getRs232Templates();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJsonPretty(responseDoc, *response);
    request->send(response);
}

void sendRs232TemplatesList(AsyncWebServerRequest* request) {
    size_t freeHeap = ESP.getFreeHeap();
    bool compact = request->hasParam("compact");
    int limitParam = -1;
    if (request->hasParam("limit")) {
        const AsyncWebParameter* parameter = request->getParam("limit");
        if (parameter != nullptr) {
            limitParam = (int)strtoul(parameter->value().c_str(), nullptr, 10);
        }
    }

    size_t capacity = 4096;
    if (compact) capacity = 1536;
    if (freeHeap < 120000 && capacity > 3072) capacity = 3072;
    if (freeHeap < 90000 && capacity > 2048) capacity = 2048;
    if (freeHeap < 70000 && capacity > 1536) capacity = 1536;
    if (freeHeap < 60000 && capacity > 1024) capacity = 1024;
    if (freeHeap < 48000) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), "{\"low_heap\":1,\"heap\":%u,\"templates\":[]}", (unsigned)freeHeap);
        request->send(200, "application/json", buffer);
        return;
    }

    SpiRamJsonDocument responseDoc(capacity);
    responseDoc["heap_free"] = (int)freeHeap;
    responseDoc["doc_cap"] = (int)capacity;
    JsonArray sourceTemplates = configManager.getRs232Templates();
    JsonArray outputTemplates = responseDoc.createNestedArray("templates");

    int totalTemplates = (int)sourceTemplates.size();
    int maxTemplates = totalTemplates;
    if (freeHeap < 120000) maxTemplates = min(maxTemplates, 40);
    if (freeHeap < 90000) maxTemplates = min(maxTemplates, 28);
    if (freeHeap < 70000) maxTemplates = min(maxTemplates, 18);
    if (freeHeap < 60000) maxTemplates = min(maxTemplates, 10);
    if (limitParam >= 0) maxTemplates = min(maxTemplates, limitParam);

    int startIndex = totalTemplates > maxTemplates ? (totalTemplates - maxTemplates) : 0;
    for (int index = startIndex; index < totalTemplates; ++index) {
        JsonObject sourceTemplate = sourceTemplates[index];
        bool isBuiltIn = sourceTemplate.containsKey("built_in") && sourceTemplate["built_in"].as<bool>();
        if (isBuiltIn) {
            continue;
        }

        JsonObject outputTemplate = outputTemplates.createNestedObject();
        if (sourceTemplate.containsKey("name")) {
            String name = sourceTemplate["name"].as<String>();
            if (name.length()) {
                outputTemplate["name"] = name;
            }
        }
        if (sourceTemplate.containsKey("baudrate_index")) {
            outputTemplate["baudrate_index"] = sourceTemplate["baudrate_index"].as<int>();
        }
        if (!compact) {
            if (sourceTemplate.containsKey("data_bits")) outputTemplate["data_bits"] = sourceTemplate["data_bits"].as<int>();
            if (sourceTemplate.containsKey("parity")) outputTemplate["parity"] = String(sourceTemplate["parity"].as<String>());
            if (sourceTemplate.containsKey("stop_bits")) outputTemplate["stop_bits"] = sourceTemplate["stop_bits"].as<int>();
            if (sourceTemplate.containsKey("invert_pins")) outputTemplate["invert_pins"] = sourceTemplate["invert_pins"].as<bool>();
            if (sourceTemplate.containsKey("read_timeout_ms")) outputTemplate["read_timeout_ms"] = sourceTemplate["read_timeout_ms"].as<int>();
            if (sourceTemplate.containsKey("enabled")) outputTemplate["enabled"] = sourceTemplate["enabled"].as<bool>();
            if (sourceTemplate.containsKey("use_xon_xoff")) outputTemplate["use_xon_xoff"] = sourceTemplate["use_xon_xoff"].as<bool>();
        }

        int baudrate = -1;
        if (sourceTemplate.containsKey("baudrate")) {
            baudrate = sourceTemplate["baudrate"].as<int>();
        } else if (sourceTemplate.containsKey("baudrate_index")) {
            int baudrateIndex = sourceTemplate["baudrate_index"].as<int>();
            if (baudrateIndex >= 0 && baudrateIndex < RS232_BAUDRATES_COUNT) {
                baudrate = RS232_BAUDRATES[baudrateIndex];
            }
        }
        if (baudrate > 0) {
            outputTemplate["baudrate"] = baudrate;
        }
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    serializeJsonPretty(responseDoc, *response);
    request->send(response);
}

bool applyRs232TemplateByName(const String& name) {
    JsonArray templates = configManager.getRs232Templates();
    for (size_t index = 0; index < templates.size(); ++index) {
        JsonObject templateObject = templates[index];
        if (!templateObject.containsKey("name") || String(templateObject["name"].as<String>()) != name) {
            continue;
        }

#if ENABLE_RS232==1
        if (templateObject.containsKey("baudrate_index")) {
            rs232_baudrate_index = templateObject["baudrate_index"].as<int>();
        }
        if (templateObject.containsKey("baudrate")) {
            int baudrate = templateObject["baudrate"].as<int>();
            for (int baudrateIndex = 0; baudrateIndex < RS232_BAUDRATES_COUNT; ++baudrateIndex) {
                if (RS232_BAUDRATES[baudrateIndex] == baudrate) {
                    rs232_baudrate_index = baudrateIndex;
                    break;
                }
            }
        }
        if (templateObject.containsKey("data_bits")) {
            rs232_data_bits = templateObject["data_bits"].as<int>();
        }
        if (templateObject.containsKey("parity")) {
            rs232_parity = String((const char*)templateObject["parity"].as<const char*>());
        }
        if (templateObject.containsKey("stop_bits")) {
            rs232_stop_bits = templateObject["stop_bits"].as<int>();
        }
        if (templateObject.containsKey("invert_pins")) {
            rs232_invert_pins = templateObject["invert_pins"].as<bool>();
        }
        if (templateObject.containsKey("read_timeout_ms")) {
            rs232_read_timeout_ms = templateObject["read_timeout_ms"].as<int>();
        }
        if (templateObject.containsKey("use_xon_xoff")) {
            rs232_template_use_xon_xoff = templateObject["use_xon_xoff"].as<bool>();
        }
        configManager.setRs232CurrentTemplateVolatile(name);
        syncGlobalVariables(false);
        // Defer RS232 reconfiguration to the main loop to avoid UART driver race.
        _pendingRS232Apply = true;
#endif // ENABLE_RS232
        return true;
    }

    return false;
}

void handleRs232ApplyRequest(AsyncWebServerRequest* request, const String& name) {
    if (name.length() == 0) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"missing template name\"}");
        return;
    }

    sendRs232TemplateApplyResult(request, applyRs232TemplateByName(name), name);
}

bool addOrUpdateRs232TemplateFromJson(JsonObject templateObject) {
    if (!templateObject.containsKey("name")) {
        return false;
    }

    String name = templateObject["name"] | String("");
    if (name.length() == 0) {
        return false;
    }

    int baudrateIndex = templateObject.containsKey("baudrate_index")
                            ? templateObject["baudrate_index"].as<int>()
                            : configManager.getRs232BaudrateIndex();
    int baudrateNumeric = -1;
    if (templateObject.containsKey("baudrate")) {
        baudrateNumeric = templateObject["baudrate"].as<int>();
    } else if (baudrateIndex >= 0 && baudrateIndex < RS232_BAUDRATES_COUNT) {
        baudrateNumeric = RS232_BAUDRATES[baudrateIndex];
    }

    int dataBits = templateObject.containsKey("data_bits")
                       ? templateObject["data_bits"].as<int>()
                       : configManager.getRs232DataBits();
    String parity = templateObject.containsKey("parity")
                        ? String((const char*)templateObject["parity"].as<const char*>())
                        : configManager.getRs232Parity();
    int stopBits = templateObject.containsKey("stop_bits")
                       ? templateObject["stop_bits"].as<int>()
                       : configManager.getRs232StopBits();
    bool invertPins = false;
    if (templateObject.containsKey("invert_pins")) {
        invertPins = templateObject["invert_pins"].as<bool>();
    } else if (templateObject.containsKey("invert")) {
        invertPins = templateObject["invert"].as<bool>();
    }

    int readTimeoutMs = templateObject.containsKey("read_timeout_ms")
                            ? templateObject["read_timeout_ms"].as<int>()
                            : (templateObject.containsKey("read_timeout")
                                   ? templateObject["read_timeout"].as<int>()
                                   : configManager.getRs232ReadTimeoutMs());
    bool enabled = templateObject.containsKey("enabled")
                       ? templateObject["enabled"].as<bool>()
                       : true;
    bool useXonXoff = false;
    if (templateObject.containsKey("use_xon_xoff")) {
        useXonXoff = templateObject["use_xon_xoff"].as<bool>();
    } else if (templateObject.containsKey("xon")) {
        useXonXoff = templateObject["xon"].as<bool>();
    }

    configManager.addOrUpdateRs232Template(
        name,
        baudrateIndex,
        dataBits,
        parity,
        stopBits,
        invertPins,
        readTimeoutMs,
        enabled,
        baudrateNumeric,
        useXonXoff);
    return true;
}

}  // namespace

void registerRs232TemplateRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/rs232-templates", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendRs232TemplatesList(request);
    });

    server.on(
        "/api/rs232-templates",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {
            String name;
            int baudrateIndex = configManager.getRs232BaudrateIndex();
            int dataBits = configManager.getRs232DataBits();
            String parity = configManager.getRs232Parity();
            int stopBits = configManager.getRs232StopBits();
            bool invertPins = configManager.getRs232InvertPins();
            bool useXonXoff = configManager.getRs232UseXonXoff();
            int readTimeoutMs = configManager.getRs232ReadTimeoutMs();
            bool enabled = configManager.getRs232Enabled();

            if (request->hasParam("name", true)) {
                const AsyncWebParameter* parameter = request->getParam("name", true);
                if (parameter != nullptr) {
                    name = parameter->value();
                }
            }
            if (request->hasParam("baudrate_index", true)) {
                const AsyncWebParameter* parameter = request->getParam("baudrate_index", true);
                if (parameter != nullptr) {
                    baudrateIndex = parameter->value().toInt();
                }
            }
            if (request->hasParam("data_bits", true)) {
                const AsyncWebParameter* parameter = request->getParam("data_bits", true);
                if (parameter != nullptr) {
                    dataBits = parameter->value().toInt();
                }
            }
            if (request->hasParam("parity", true)) {
                const AsyncWebParameter* parameter = request->getParam("parity", true);
                if (parameter != nullptr) {
                    parity = parameter->value();
                }
            }
            if (request->hasParam("stop_bits", true)) {
                const AsyncWebParameter* parameter = request->getParam("stop_bits", true);
                if (parameter != nullptr) {
                    stopBits = parameter->value().toInt();
                }
            }
            if (request->hasParam("invert_pins", true)) {
                const AsyncWebParameter* parameter = request->getParam("invert_pins", true);
                if (parameter != nullptr) {
                    invertPins = (parameter->value() == String("1") || parameter->value() == String("true"));
                }
            }
            if (request->hasParam("read_timeout_ms", true)) {
                const AsyncWebParameter* parameter = request->getParam("read_timeout_ms", true);
                if (parameter != nullptr) {
                    readTimeoutMs = parameter->value().toInt();
                }
            }
            if (request->hasParam("enabled", true)) {
                const AsyncWebParameter* parameter = request->getParam("enabled", true);
                if (parameter != nullptr) {
                    enabled = (parameter->value() == String("1") || parameter->value() == String("true"));
                }
            }
            if (request->hasParam("use_xon_xoff", true)) {
                const AsyncWebParameter* parameter = request->getParam("use_xon_xoff", true);
                if (parameter != nullptr) {
                    useXonXoff = (parameter->value() == String("1") || parameter->value() == String("true"));
                }
            }

            if (name.length() == 0) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"missing name\"}");
                return;
            }

            int baudrateNumeric = (baudrateIndex >= 0 && baudrateIndex < RS232_BAUDRATES_COUNT)
                                      ? RS232_BAUDRATES[baudrateIndex]
                                      : -1;
            configManager.addOrUpdateRs232Template(
                name,
                baudrateIndex,
                dataBits,
                parity,
                stopBits,
                invertPins,
                readTimeoutMs,
                enabled,
                baudrateNumeric,
                useXonXoff);
            configManager.markDirtyAndScheduleSave();
            if (!configManager.saveSection("rs232")) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveRs232Template)+"\",\"error\":\"save failed\"}");
                return;
            }
            triggerScreenConfigSaved();
#if ENABLE_BUZZER == 1
            buzzerOnSave();
#endif
            sendRs232TemplatesSnapshot(request, true);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) {
                body = "";
            }
            for (size_t dataIndex = 0; dataIndex < len; ++dataIndex) {
                body += (char)data[dataIndex];
            }
            if (index + len != total) {
                return;
            }

            SpiRamJsonDocument bodyDoc(4096);
            DeserializationError error = deserializeJson(bodyDoc, body);
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"invalid json\"}");
                return;
            }

            bool any = false;
            if (bodyDoc.containsKey("templates") && bodyDoc["templates"].is<JsonArray>()) {
                JsonArray templates = bodyDoc["templates"].as<JsonArray>();
                for (JsonVariant variant : templates) {
                    if (variant.is<JsonObject>()) {
                        any = addOrUpdateRs232TemplateFromJson(variant.as<JsonObject>()) || any;
                    }
                }
            } else if (bodyDoc.is<JsonObject>()) {
                any = addOrUpdateRs232TemplateFromJson(bodyDoc.as<JsonObject>()) || any;
            }

            if (!any) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"no valid templates found\"}");
                return;
            }

            configManager.markDirtyAndScheduleSave();
            if (!configManager.saveSection("rs232")) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSave)+" template RS232\",\"error\":\"save failed\"}");
                return;
            }
            triggerScreenConfigSaved();
#if ENABLE_BUZZER == 1
            buzzerOnSave();
#endif
            sendRs232TemplatesSnapshot(request, true);
        });

    server.on("/api/rs232-templates", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"missing name\"}");
            return;
        }

        String name;
        const AsyncWebParameter* parameter = request->getParam("name");
        if (parameter != nullptr) {
            name = parameter->value();
        }

        configManager.removeRs232Template(name);
        configManager.markDirtyAndScheduleSave();
        if (!configManager.saveSection("rs232")) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveRs232Template)+",\"error\":\"save failed\"}");
            return;
        }
        triggerScreenConfigSaved();
#if ENABLE_BUZZER == 1
        buzzerOnSave();
#endif
        sendRs232TemplatesSnapshot(request, true);
    });

    server.on(
        "/api/rs232-apply",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {
            String name;
            if (request->hasParam("template", true)) {
                const AsyncWebParameter* parameter = request->getParam("template", true);
                if (parameter != nullptr) {
                    name = parameter->value();
                }
            }
            if (name.length() == 0 && request->hasParam("name", true)) {
                const AsyncWebParameter* parameter = request->getParam("name", true);
                if (parameter != nullptr) {
                    name = parameter->value();
                }
            }

            handleRs232ApplyRequest(request, name);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) {
                body.clear();
            }
            for (size_t dataIndex = 0; dataIndex < len; ++dataIndex) {
                body += (char)data[dataIndex];
            }
            if (index + len != total) {
                return;
            }

            SpiRamJsonDocument bodyDoc(512);
            DeserializationError error = deserializeJson(bodyDoc, body);
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"invalid json\"}");
                return;
            }

            handleRs232ApplyRequest(request, bodyDoc["template"] | String(""));
        });

}
#endif // ENABLE_RS232