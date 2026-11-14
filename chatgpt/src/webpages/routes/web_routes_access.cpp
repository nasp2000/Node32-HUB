#include "web_routes_access.h"
#include "route_registry_api.h"

#include "../../feature_flags.h" // ENABLE_PAGE_ACCESS (page gate, ACC-4)

#include "../http_abstraction.h"
#include "../handlers_unified/config_handler.h"
#include "../handlers/heavy_api_request_guard.h"
#include "../renderers/access_page_renderer.h"

static String reconstructPostBody(AsyncWebServerRequest* request) {
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

static void registerAccessApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server;
    (void)context;

    // POST /api/access-config — kept manual due to HeavyApiRequestGuard
    server.on("/api/access-config", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HeavyApiRequestGuard guard(request, "/api/access-config", 3000UL);
        if (!guard.begin()) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(request);
        hreq.contentType = F("application/x-www-form-urlencoded");
        HttpResponse hresp;
        handleAccessConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // Simple API routes — auto-registered on both WiFi and Ethernet
    registerApiRoute("GET",  "/api/access-config",    handleAccessConfigGet);
    registerApiRoute("GET",  "/api/session-history",  handleSessionHistoryGet);
    registerApiRoute("POST", "/api/access-kick-all",  handleAccessKickAllPost);
}

void registerAccessRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    // APIs stay available in every build (scripts, diagnostics, self-test);
    // only the page is gated, exactly like /sdcard (ENABLE_PAGE_SDCARD).
    registerAccessApiRoutes(server, context);

#if ENABLE_PAGE_ACCESS==1
    // Access page (/access). With the flag off the route is not registered at
    // all (404 on both transports) and the home button is not emitted (ACC-4).
    registerPageGet("/access", [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamAccessPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/access", bytes, false);
#endif
    });
#endif // ENABLE_PAGE_ACCESS
}
