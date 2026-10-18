#include "web_routes_ota.h"

#include "../../feature_flags.h"
#include "../handlers/ota_admin_handler.h"
#include "../handlers_unified/ota_handler.h"
#include "../renderers/ota_page_renderer.h"
#include "route_registry_api.h"

void registerOtaRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_OTA==1
    registerApiRoute("POST", "/api/enable-ota", handleEnableOtaPost);
    registerOtaAdminRoutes(server, context);

    server.on("/ota", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamOtaPageToPrint(*response, OtaPageTransportMode::WifiAsync);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ota", bytes, false);
#endif
    });
#else
    (void)server;
    (void)context;
#endif
}
