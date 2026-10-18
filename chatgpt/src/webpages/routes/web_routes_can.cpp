// web_routes_can.cpp
// Registers the /can HTML page and all /api/can/* REST endpoints.

#include "web_routes_can.h"
#include "../../feature_flags.h"
#include "../handlers/can_handlers.h"
#include "../handlers_unified/can_handler.h"
#include "../renderers/can_page_renderer.h"
#include "route_registry_api.h"

void registerCanRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/can/status",  handleCanStatusGet);
    registerApiRoute("GET",  "/api/can/frames",  handleCanFramesGet);
    registerApiRoute("GET",  "/api/can/config",  handleCanConfigGet);
    registerApiRoute("POST", "/api/can/clear",   handleCanClearPost);
    registerApiRoute("POST", "/api/can/restart", handleCanRestartPost);
    // POST /api/can/send and /api/can/config stay manual (body accumulators)
    registerCanApiRoutes(server, context);

#if ENABLE_CAN==1
    // HTML monitor + config page: /can
    server.on("/can", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamCanPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/can", bytes, false);
#endif
    });
#else
    (void)server;
    (void)context;
#endif // ENABLE_CAN==1
}
