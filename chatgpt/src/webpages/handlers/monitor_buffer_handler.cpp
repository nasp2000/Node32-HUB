// Handlers for monitor status and receive-buffer APIs.

#include "monitor_buffer_handler.h"

#include <Arduino.h>

#include "../../web_shared_logic.h"
#include "heavy_api_request_guard.h"

namespace {

String readParamValue(AsyncWebServerRequest* request, const char* name) {
    if (request->hasParam(name, true)) return request->getParam(name, true)->value();
    if (request->hasParam(name)) return request->getParam(name)->value();
    return String();
}

int readParamInt(AsyncWebServerRequest* request, const char* name, int fallback) {
    String value = readParamValue(request, name);
    if (!value.length()) return fallback;
    return value.toInt();
}

String sanitizeDownloadFilename(String fileName) {
    fileName.replace("\\", "_");
    fileName.replace("/", "_");
    fileName.replace("\"", "_");
    fileName.replace("'", "_");
    if (!fileName.endsWith(".txt")) fileName += F(".txt");
    return fileName;
}

}  // namespace

void registerMonitorBufferRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/monitor-status", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) {
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-store");
        response->print(sharedWebBuildMonitorStatusJson());
        request->send(response);
    });

    server.on("/api/serial-monitor", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        // Use 500ms guard (JS already uses isLoading flag; guard only catches double-rapid hits)
        HeavyApiRequestGuard heavyGuard(request, "/api/serial-monitor", 500UL);
        if (!heavyGuard.allowed()) return;

        // Large initial buffer: the JSON dump over a 1460 B stream triggers
        // cbuf::resize (O(n) copy) on every overflow -> O(n^2) on the payload,
        // which already caused a Task-WDT crash inside async_tcp (PAGES/BACKLOG.md MON-6).
        // 16 KB keeps the number of resizes (and the copies) low.
        AsyncResponseStream* response = request->beginResponseStream("application/json", 16384);
        if (!response) {
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-store");
        sharedWebStreamSerialMonitorJson(
            *response,
            readParamInt(request, "limit_msgs", readParamInt(request, "limit", 100)),
            request->hasParam("since"),
            (size_t)readParamInt(request, "since", 0),
            request->hasParam("no_cmds") && readParamValue(request, "no_cmds") == String("1"),
            request->hasParam("last_seq"),
            (unsigned long)strtoul(readParamValue(request, "last_seq").c_str(), nullptr, 10),
            readParamInt(request, "max_bytes", 0),
            readParamInt(request, "limit_lines", readParamInt(request, "limit", 0)),
            request->hasParam("debug") && (readParamValue(request, "debug") == String("1") || readParamValue(request, "debug").equalsIgnoreCase("true"))
        );
        request->send(response);
    });

    server.on("/api/received-text", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;

            // Prevent rapid overlap from the monitor page polling loop.
            HeavyApiRequestGuard heavyGuard(request, "/api/received-text", 400UL);
            if (!heavyGuard.allowed()) return;

        int limit = readParamInt(request, "limit", 100);
        int maxBytesWindow = readParamInt(request, "max_bytes", 4096);
        bool includePartial = request->hasParam("include_partial");
        bool download = request->hasParam("download");

        if (download) {
            if (!request->hasParam("max_bytes")) maxBytesWindow = (int)bufferDados.capacity();
            if (!request->hasParam("limit")) limit = 0;
        }

        String output;
        if (!sharedWebBuildReceivedText(limit, maxBytesWindow, includePartial, output)) {
            request->send(503, "text/plain", "Buffer Busy");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("text/plain; charset=utf-8");
        if (!response) {
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-store");
        if (download) {
            String fileName = readParamValue(request, "filename");
            if (!fileName.length()) fileName = F("buffer.txt");
            response->addHeader("Content-Disposition", String(F("attachment; filename=")) + sanitizeDownloadFilename(fileName));
        }
        response->print(output);
        request->send(response);
    });

}