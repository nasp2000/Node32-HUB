// web_routes_networks.cpp
// Route registration for the /networks page (Varias Redes).
// Uses chunked PROGMEM response — zero heap allocation at serve time.

#include "../../languages/lang_active.h"
#include "web_routes_networks.h"

#include "../assets/page_networks_asset.h"
#include "../handlers_unified/network_handler.h"
#include "../renderers/page_render_common.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerNetworksRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_TELNET==1
    // API routes (internally guarded by feature flags)
    registerApiRoute("POST", "/api/telnet-config", handleTelnetConfigPost);

    // HTML page: /networks
    server.on("/networks", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        if (sendNotModifiedIfCached(req)) return;
        if (!context.allowHtmlClient(req)) return;

        static String _lang;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (_lang != activeLangCode) {
            buildPageFrame(_pre, _suf, L(activeLang->pageTitleNetworksOther).c_str(), true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(PAGE_NETWORKS);
            _lang = activeLangCode;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = req->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_NETWORKS, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); req->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        req->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(req, "/networks", pLen + aLen + sLen, false);
#endif
    });
#else
    (void)server;
    (void)context;
#endif
}
