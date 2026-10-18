// web_routes_do.cpp
// Route registration for the /do control page and /api/do/* endpoints.

#include "web_routes_do.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include "../handlers/do_handler.h"
#include "../handlers_unified/do_handler.h"
#include "../renderers/do_renderer.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerDoRoutes(AsyncWebServer& server, const WebRouteContext& context) {

    // HTML control page.
    server.on("/do", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamDoPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/do", bytes, true);
#endif
    });

    // REST API routes.
    registerApiRoute("GET", "/api/do/status", handleDoStatusGet);
    registerApiRoute("GET", "/api/do/config", handleDoConfigGet);
    registerDoApiRoutes(server, context); // POST /api/do/set + /api/do/config (body accum)
}

#else
void registerDoRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server; (void)context;
}
#endif // BOARD_HAS_TCA9554_DO
