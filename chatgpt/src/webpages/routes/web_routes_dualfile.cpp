#include "web_routes_dualfile.h"

#include "../../feature_flags.h"
#include "../assets/page_dualfile_asset.h"
#include "../http_abstraction.h"
#include "../handlers_unified/dualfile_handler.h"
#include "../renderers/dualfile_page_renderer.h"
#include "route_registry_api.h"

void registerDualFileRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_DUALFILE==1

    // HTML page - manual route (streaming from PROGMEM)
    server.on("/dualfile", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!context.allowHtmlClient(request)) return;
        sendDualFilePageResponse(request);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/dualfile", strlen_P(PAGE_DUALFILE), true);
#endif
    });

    // API routes (RouteRegistry)
    registerApiRoute("GET",  "/api/dualfile/list",  handleDualFileListGet);
    registerApiRoute("POST", "/api/dualfile/copy",  handleDualFileCopyPost);
    registerApiRoute("GET",  "/api/dualfile/view",  handleDualFileViewGet);
    registerApiRoute("POST", "/api/dualfile/write", handleDualFileWritePost);

    // Upload - manual route (body accumulation, cannot use RouteRegistry)
    server.on("/api/dualfile/upload", HTTP_POST,
        [&context](AsyncWebServerRequest* request) {
            if (!context.ensureBasicAuth(request)) return;
            handleDualFileUploadPost(request);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data,
           size_t len, size_t index, size_t total) {
            handleDualFileUploadBody(request, data, len, index, total);
        });

#else
    (void)server;
    (void)context;
#endif // ENABLE_DUALFILE==1
}
