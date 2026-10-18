// web_routes_buzzer.cpp
// Route registration for the buzzer page (/buzzer) and its REST API.

#include "web_routes_buzzer.h"

#include "route_registry_api.h"
#include "../renderers/buzzer_page_renderer.h"
#include "../../feature_flags.h"
#include "../http_abstraction.h"
#include "../handlers_unified/buzzer_handler.h"

#if ENABLE_BUZZER==1

// Accumulates JSON body chunks for buzzer POST endpoints.
struct BuzzerBodyAccum {
    static void config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleBuzzerConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void test(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleBuzzerTestPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
};

static void registerBuzzerApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/buzzer-config", handleBuzzerConfigGet);
    registerApiRoute("POST", "/api/buzzer-stop",   handleBuzzerStopPost);

    // POST /api/buzzer-config (body accum)
    server.on("/api/buzzer-config", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            BuzzerBodyAccum::config(request, data, len, index, total);
        });
    // POST /api/buzzer-test (body accum)
    server.on("/api/buzzer-test", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            BuzzerBodyAccum::test(request, data, len, index, total);
        });
}

#endif

void registerBuzzerRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_BUZZER == 1

    server.on("/buzzer", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;
        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        const size_t bytes = streamBuzzerPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/buzzer", bytes, true);
#endif
    });

    registerBuzzerApiRoutes(server, context);

#else
    (void)server;
    (void)context;
#endif // ENABLE_BUZZER
}
