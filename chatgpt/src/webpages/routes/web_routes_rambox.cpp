// web_routes_rambox.cpp
// Unified RAMBOX page (/rambox): one page, a device selector (Unikon / Bricon / future
// clocks) and the same tab layout for every device. The per-device REST APIs stay as
// they are; this route only serves the unified HTML shell+asset.

#include "web_routes_rambox.h"
#include "../assets/page_rambox_asset.h"
#include "../renderers/page_render_common.h"
#include "../../feature_flags.h"

void registerRamboxRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UNIKON == 1 || ENABLE_BRICON == 1
    server.on("/rambox", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool   _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            // \xF0\x9F\x97\x82 = U+1F5C2 (card index dividers)
            buildPageFrame(_pre, _suf, "RAMBOX \xF0\x9F\x90\xA6", true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(PAGE_RAMBOX);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_RAMBOX, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/rambox", pLen + aLen + sLen, false);
#endif
    });
#else
    (void)server;
    (void)context;
#endif
}
