// web_routes_unikon.cpp
// Route registration for the Unikon RAMBOX page (/unikon) and its REST API.

#include "web_routes_unikon.h"
#include "../assets/page_unikon_asset.h"
#include "../handlers/unikon_handler.h"
#include "../handlers_unified/unikon_handler.h"
#include "../renderers/page_render_common.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerUnikonRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UNIKON == 1
    registerApiRoute("GET",  "/api/unikon-status",       handleUnikonStatusGet);
    registerApiRoute("GET",  "/api/unikon-arrivals",      handleUnikonArrivalsGet);
    registerApiRoute("GET",  "/api/unikon-registrations", handleUnikonRegistrationsGet);
    registerApiRoute("GET",  "/api/unikon-contests",      handleUnikonContestsGet);
    registerApiRoute("GET",  "/api/unikon-pombal",        handleUnikonPombalGet);
    registerApiRoute("POST", "/api/unikon-sync-pombal",   handleUnikonSyncPombalPost);
    registerApiRoute("POST", "/api/unikon-email-pombal",  handleUnikonEmailPombalPost);
    registerApiRoute("POST", "/api/unikon-poll",          handleUnikonPollPost);
    registerApiRoute("POST", "/api/unikon-sync-zp",       handleUnikonSyncZpPost);
    registerApiRoute("POST", "/api/unikon-poll-w",        handleUnikonPollWPost);
    registerApiRoute("POST", "/api/unikon-sync-calendar", handleUnikonSyncCalendarPost);
    registerApiRoute("POST", "/api/unikon-reset",         handleUnikonResetPost);
    // Training endpoints (fictitious race 990 + pombal ZPs, explicit request only).
    registerApiRoute("POST", "/api/unikon-send-training",  handleUnikonSendTrainingPost);
    registerApiRoute("POST", "/api/unikon-clear-training", handleUnikonClearTrainingPost);

    // HTML page.
    server.on("/unikon", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool   _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xF0\x9F\x90\xA6 Unikon RAMBOX", true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(PAGE_UNIKON);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_UNIKON, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/unikon", pLen + aLen + sLen, false);
#endif
    });

    // POST /api/unikon-config stays manual (body params)
    registerUnikonApiRoutes(server, context);

#else
    (void)server;
    (void)context;
#endif // ENABLE_UNIKON
}
