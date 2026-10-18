// Handlers for system and RS232 command APIs.

#include "command_api_handler.h"

#include <Arduino.h>
#include <WiFi.h>

#include "../../web_shared_logic.h"

namespace {

void writeJsonEscapedString(Print& out, const String& value) {
    for (size_t index = 0; index < value.length(); ++index) {
        char current = value[index];
        if (current == '"') out.print("\\\"");
        else if (current == '\\') out.print("\\\\");
        else if (current == '\n') out.print("\\n");
        else out.write((uint8_t)current);
    }
}

void sendSystemCommandFormResponse(AsyncWebServerRequest* request, const String& command) {
    if (command.length() == 0) {
        request->send(200, "application/json", "{\"success\":false,\"response\":\"Empty command\"}");
        return;
    }

    String response;
    bool success = false;
    bool streamed = false;
    String commandLower = command;
    commandLower.toLowerCase();
    String captured = sharedRunSystemCommandCapture(command);
    if (captured.length() > 0) {
        response = captured;
        success = true;
        streamed = true; // output already in the Web Serial Monitor buffer
    } else if (commandLower.startsWith("emailday")) {
        response = "📧 EMAILDAY started in background. Check monitor for progress (estimated 30-60s).";
        success = true;
    } else if (commandLower == "help") {
        response = "📋 Command list sent to terminal. Check monitor output.";
        success = true;
    } else if (commandLower == "getip") {
        if (WiFi.status() == WL_CONNECTED) response = "📍 IP: " + WiFi.localIP().toString() + " | SSID: " + WiFi.SSID() + " | RSSI: " + String(WiFi.RSSI()) + "dBm";
        else if (hasNetworkConnectivity()) response = "📍 IP: " + getCurrentNetworkIPString() + " | Ethernet";
        else response = "📶 Network disconnected";
        success = true;
    } else if (commandLower == "hello") {
        response = "👋 ESP32 system active! " + String(millis() / 1000) + "s uptime";
        success = true;
    } else if (commandLower.startsWith("hellp")) {
        response = "❓ Did you mean 'help'?";
        success = true;
    } else if (commandLower == "status") {
        response = "📊 Heap:" + String(ESP.getFreeHeap()) + " Net:" + String(hasNetworkConnectivity() ? "on" : "off") + " RS232:" + String(rs232_enabled ? "on" : "off");
        success = true;
    } else if (commandLower == "get_chip_info") {
        response = "ℹ️ Chip info sent to monitor.";
        success = true;
    } else if (commandLower == "debug_logs_on") {
        response = "Debug logs ON";
        success = true;
    } else if (commandLower == "debug_logs_off") {
        response = "Debug logs OFF";
        success = true;
    } else {
        response = "Command '" + command + "' sent. Check monitor for full output.";
        success = true;
    }

    AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
    responseStream->print(F("{\"success\":"));
    responseStream->print(success ? "true" : "false");
    responseStream->print(F(",\"streamed\":"));
    responseStream->print(streamed ? "true" : "false");
    responseStream->print(F(",\"response\":\""));
    writeJsonEscapedString(*responseStream, response);
    responseStream->print(F("\"}"));
    request->send(responseStream);
}

void sendSystemCommandJsonResponse(AsyncWebServerRequest* request, const String& command) {
    if (command.length() == 0) {
        request->send(200, "application/json", "{\"success\":false,\"response\":\"Empty command\"}");
        return;
    }

    String response;
    bool success = false;
    bool streamed = false;
    String commandLower = command;
    commandLower.toLowerCase();
    String captured = sharedRunSystemCommandCapture(command);
    if (captured.length() > 0) {
        response = captured;
        success = true;
        streamed = true; // output already in the Web Serial Monitor buffer
    } else if (commandLower.startsWith("emailday")) {
        response = "📧 EMAILDAY started in background. Check monitor for progress (estimated 30-60s).";
        success = true;
    } else if (commandLower == "help") {
        response = "📋 Command list sent to monitor.";
        success = true;
    } else if (commandLower == "getip") {
        if (WiFi.status() == WL_CONNECTED) response = "📍 IP: " + WiFi.localIP().toString() + " | SSID: " + WiFi.SSID() + " | RSSI: " + String(WiFi.RSSI()) + "dBm";
        else if (hasNetworkConnectivity()) response = "📍 IP: " + getCurrentNetworkIPString() + " | Ethernet";
        else response = "📶 Network disconnected";
        success = true;
    } else if (commandLower == "hello") {
        response = "👋 ESP32 system active! " + String(millis() / 1000) + "s uptime";
        success = true;
    } else if (commandLower.startsWith("hellp")) {
        response = "❓ Did you mean 'help'?";
        success = true;
    } else if (commandLower == "status") {
        response = "📊 Heap:" + String(ESP.getFreeHeap()) + " Net:" + String(hasNetworkConnectivity() ? "on" : "off") + " RS232:" + String(rs232_enabled ? "on" : "off");
        success = true;
    } else if (commandLower == "get_chip_info") {
        response = "ℹ️ Chip info sent.";
        success = true;
    } else if (commandLower == "debug_logs_on") {
        response = "Debug logs ON";
        success = true;
    } else if (commandLower == "debug_logs_off") {
        response = "Debug logs OFF";
        success = true;
    } else {
        response = "Command '" + command + "' sent. Check monitor.";
        success = true;
    }

    AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
    responseStream->print(F("{\"success\":"));
    responseStream->print(success ? "true" : "false");
    responseStream->print(F(",\"streamed\":"));
    responseStream->print(streamed ? "true" : "false");
    responseStream->print(F(",\"response\":\""));
    writeJsonEscapedString(*responseStream, response);
    responseStream->print(F("\"}"));
    request->send(responseStream);
}

void sendRs232CommandFormResponse(AsyncWebServerRequest* request, const String& command, const String& templateName) {
    // Use only the explicitly provided template.
    // Do NOT auto-inject getRs232CurrentTemplate() for plain web sends — that would override
    // the user's saved port config with a stale template (violates Req 3).
    String effectiveTemplate = templateName;
    String response = "No command provided";
    bool success = false;
    String commandLower = command;
    commandLower.toLowerCase();

    if (commandLower.length() == 0) {
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        responseStream->print(F("{\"success\":false,\"response\":\""));
        responseStream->print(response);
        responseStream->print(F("\"}"));
        request->send(responseStream);
        return;
    }

    if (commandLower == "test" || commandLower == "teste") {
        bool queued = enqueueRS232Command("RS232_TEST", String(""), String("WEB"), String("rs232"));
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        responseStream->print(F("{\"success\":"));
        responseStream->print(queued ? "true" : "false");
        responseStream->print(F(",\"response\":\""));
        responseStream->print(queued ? "✅ RS232 test queued: RS232_TEST" : "❌ Queue full");
        responseStream->print(F("\"}"));
        request->send(responseStream);
        return;
    }

    if (commandLower == "status") {
        String status = String(F("RS232 ")) + String(rs232_enabled ? "enabled" : "disabled") + String(F(" | Baudrate: ")) + String(RS232_BAUDRATES[rs232_baudrate_index]) + String(F(" | Pins ")) + String(rs232_invert_pins ? "inverted" : "normal");
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        responseStream->print(F("{\"success\":true,\"response\":\""));
        writeJsonEscapedString(*responseStream, status);
        responseStream->print(F("\"}"));
        request->send(responseStream);
        return;
    }

    if (commandLower == "help") {
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        responseStream->print(F("{\"success\":true,\"response\":\"📋 Commands: test,status,help or any other (queued)\"}"));
        request->send(responseStream);
        return;
    }

    bool queued = enqueueRS232Command(command, effectiveTemplate, String("WEB"), String("rs232"));
    AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
    SpiRamJsonDocument doc(640);
    doc["success"] = queued;
    doc["response"] = queued ? String("✅ Command queued") : String("❌ RS232 queue full");
    doc["rs232_enabled"] = rs232_enabled;
#include "feature_flags.h"
#if ENABLE_RS232==1
    doc["rs232_paused_by_controller"] = rs232_paused_by_controller;
    doc["rs232_blocked_for_email"] = rs232_blockQueueForEmail;
    doc["rs232_pending"] = (int)rs232SendQueue.size();
#endif
    doc["rs232_baudrate"] = rs232_baudrate;
    serializeJsonPretty(doc, *responseStream);
    request->send(responseStream);
}

void sendRs232CommandJsonResponse(AsyncWebServerRequest* request, const String& command, const String& templateName) {
    // Same rule: honour only an explicit template, never auto-inject the current one.
    String effectiveTemplate = templateName;
    String commandLower = command;
    commandLower.toLowerCase();

    if (commandLower.length() == 0) {
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        SpiRamJsonDocument doc(256);
        doc["success"] = false;
        doc["response"] = "No command provided";
        serializeJsonPretty(doc, *responseStream);
        request->send(responseStream);
        return;
    }

    if (commandLower == "test" || commandLower == "teste") {
        // Keep the monitor echo test neutral: do not reapply the active template.
        // The dedicated /api/test-rs232 endpoint already behaves this way.
        bool queued = enqueueRS232Command("RS232_TEST", String(""), String("WEB"), String("rs232"));
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        SpiRamJsonDocument doc(512);
        doc["success"] = queued;
        doc["response"] = queued ? String("✅ RS232 test queued: RS232_TEST") : String("❌ Queue full");
        serializeJsonPretty(doc, *responseStream);
        request->send(responseStream);
        return;
    }

    if (commandLower == "status") {
        String status = String(F("RS232 ")) + String(rs232_enabled ? "enabled" : "disabled") + String(F(" | Baudrate: ")) + String(RS232_BAUDRATES[rs232_baudrate_index]) + String(F(" | Pins ")) + String(rs232_invert_pins ? "inverted" : "normal");
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        SpiRamJsonDocument doc(512);
        doc["success"] = true;
        doc["response"] = status;
        serializeJsonPretty(doc, *responseStream);
        request->send(responseStream);
        return;
    }

    if (commandLower == "help") {
        AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
        SpiRamJsonDocument doc(512);
        doc["success"] = true;
        doc["response"] = String("📋 Commands: test,status,help or other (queued)");
        serializeJsonPretty(doc, *responseStream);
        request->send(responseStream);
        return;
    }

    bool queued = enqueueRS232Command(command, effectiveTemplate, String("WEB"), String("rs232"));
    AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
    SpiRamJsonDocument doc(512);
    doc["success"] = queued;
    doc["response"] = queued ? String("✅ Command queued") : String("❌ RS232 queue full");
    serializeJsonPretty(doc, *responseStream);
    request->send(responseStream);
}

}  // namespace

void registerCommandApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)context;

    server.on(
        "/api/system-command",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!request->hasParam("command", true)) return;
            String command;
            const AsyncWebParameter* parameter = request->getParam("command", true);
            if (parameter != nullptr) {
                command = parameter->value();
            }
            sendSystemCommandFormResponse(request, command);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body.clear();
            for (size_t dataIndex = 0; dataIndex < len; ++dataIndex) body += (char)data[dataIndex];
            if (index + len != total) return;

            SpiRamJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, body);
            body.clear();
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"response\":\"Invalid JSON\"}");
                return;
            }

            sendSystemCommandJsonResponse(request, doc["command"] | String(""));
        });

    server.on(
        "/api/rs232-command",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!request->hasParam("command", true)) return;
            String command;
            String templateName;
            const AsyncWebParameter* commandParam = request->getParam("command", true);
            if (commandParam != nullptr) command = commandParam->value();
            if (request->hasParam("template", true)) {
                const AsyncWebParameter* templateParam = request->getParam("template", true);
                if (templateParam != nullptr) templateName = templateParam->value();
            }
            sendRs232CommandFormResponse(request, command, templateName);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body.clear();
            for (size_t dataIndex = 0; dataIndex < len; ++dataIndex) body += (char)data[dataIndex];
            if (index + len != total) return;

            SpiRamJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, body);
            body.clear();
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"response\":\"Invalid JSON\"}");
                return;
            }

            sendRs232CommandJsonResponse(request, doc["command"] | String(""), doc["template"] | String(""));
        });

#if ENABLE_MODBUS==1
    server.on(
        "/api/modbus-command",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body.clear();
            for (size_t i = 0; i < len; ++i) body += (char)data[i];
            if (index + len != total) return;

            SpiRamJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, body);
            body.clear();
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"response\":\"Invalid JSON\"}");
                return;
            }

            AsyncResponseStream* responseStream = request->beginResponseStream("application/json");
            sharedWebWriteModbusCommandResponse(*responseStream, doc["command"] | String(""));
            request->send(responseStream);
        });
#endif // ENABLE_MODBUS
}