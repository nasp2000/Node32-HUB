#pragma once

#include <ESPAsyncWebServer.h>

#include "../../direct_log.h"
#include "../handlers_unified/config_handler.h"
#include "config_snapshot_handler.h"   // invalidateConfigsCache

// Reconstruct POST body from form-encoded request params (same pattern as network_api_handler).
inline String configReconstructPostBody(AsyncWebServerRequest* request) {
    String body = "";
    for (int i = 0; i < request->params(); i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
            if (body.length() > 0) body += "&";
            body += p->name() + "=" + p->value();
        }
    }
    return body;
}

// Handler that receives accumulated JSON body chunks.
struct ConfigJsonAccumulator {
    static void handle(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;

        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);

        invalidateConfigsCache();
    }
};

inline void setupConfigManagerEndpoints() {
    // Handle both form-urlencoded and JSON body for /api/config POST.
    // Registry also registers this route, but this server.on() overrides it
    // (called later from process_line_impl.h). Must handle both content types.
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Form-urlencoded: body available as POST params.
            HttpRequest hreq;
            hreq.body = configReconstructPostBody(request);
            hreq.contentType = F("application/x-www-form-urlencoded");
            HttpResponse hresp;
            handleConfigPost(hreq, hresp);
            request->send(hresp.code, hresp.contentType, hresp.body);
            invalidateConfigsCache();
        },
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            ConfigJsonAccumulator::handle(request, data, len, index, total);
        }
    );

    directLogPrintln(F("ConfigManager API endpoints configured"));
}
