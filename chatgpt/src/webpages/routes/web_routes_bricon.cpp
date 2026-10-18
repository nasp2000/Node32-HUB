// web_routes_bricon.cpp
// Route registration for the Bricon Rambox page (/bricon) and its REST API.

#include "web_routes_bricon.h"
#include "../assets/page_bricon_asset.h"
#include "../handlers_unified/bricon_handler.h"
#include "../renderers/page_render_common.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerBriconRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_BRICON == 1
    registerApiRoute("GET",  "/api/bricon-status",       handleBriconStatusGet);
    registerApiRoute("GET",  "/api/bricon-arrivals",      handleBriconArrivalsGet);
    registerApiRoute("GET",  "/api/bricon-registrations", handleBriconRegistrationsGet);
    registerApiRoute("GET",  "/api/bricon-contests",      handleBriconContestsGet);
    registerApiRoute("GET",  "/api/bricon-pombal",        handleBriconPombalGet);
    registerApiRoute("POST", "/api/bricon-config",        handleBriconConfigPost);
    registerApiRoute("POST", "/api/bricon-poll",          handleBriconPollPost);
    registerApiRoute("POST", "/api/bricon-sync-pombal",   handleBriconSyncPombalPost);
    registerApiRoute("POST", "/api/bricon-sync-calendar", handleBriconSyncCalendarPost);
    registerApiRoute("POST", "/api/bricon-send-training", handleBriconSendTrainingPost);
    registerApiRoute("POST", "/api/bricon-clear-training", handleBriconClearTrainingPost);

    // HTML page.
    server.on("/bricon", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool   _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xF0\x9F\x95\x8A Bricon Rambox", true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(PAGE_BRICON);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_BRICON, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/bricon", pLen + aLen + sLen, false);
#endif
    });

#else
    (void)server;
    (void)context;
#endif // ENABLE_BRICON
}
