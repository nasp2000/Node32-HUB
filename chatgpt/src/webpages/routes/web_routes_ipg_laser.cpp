#include "web_routes_ipg_laser.h"

#include "../../feature_flags.h"
#include "../assets/page_ipg_laser_asset.h"
#include "../handlers/ipg_laser_handler.h"
#include "../handlers_unified/ipg_laser_handler.h"
#include "../renderers/page_render_common.h"
#include "route_registry_api.h"

void registerIpgLaserRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/ipg-laser-available", handleIpgLaserAvailableGet);
    registerApiRoute("POST", "/api/ipg-laser-clear",     handleIpgLaserClearPost);
#if ENABLE_IPG_LASER == 1
    server.on("/ipg-laser", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool   _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xF0\x9F\x94\xA5 IPG Laser RS232", true, nullptr);
            _aLen = strlen_P(PAGE_IPG_LASER);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_IPG_LASER, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ipg-laser", pLen + aLen + sLen, false);
#endif
    });

    registerIpgLaserApiRoutes(server, context);
#else
    (void)server;
    (void)context;
#endif
}