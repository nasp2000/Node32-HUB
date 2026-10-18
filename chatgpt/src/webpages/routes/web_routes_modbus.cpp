#include "web_routes_modbus.h"
#include "route_registry_api.h"

#include "../../feature_flags.h"
#include "../../ConfigManager.h"
#include "../http_abstraction.h"
#include "../handlers_unified/modbus_handler.h"
#include "../renderers/modbus_renderer.h"
#include "../renderers/page_render_common.h"
#include "../assets/page_modbus_asset.h"

extern ConfigManager configManager;

#if ENABLE_MODBUS==1

// ---- Body accumulators for POST handlers ----
namespace {
struct MbBodyAccum {
    static void config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleModbusConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void pollNow(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
        HttpResponse hresp;
        handleModbusPollNowPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void rtuWrite(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleModbusRtuWritePost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void scanRtu(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
        HttpResponse hresp;
        handleModbusScanRtuPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void scanTcp(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
        HttpResponse hresp;
        handleModbusScanTcpPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void write(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleModbusWritePost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
};
} // namespace

#endif // ENABLE_MODBUS==1

void registerModbusApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_MODBUS==1

    registerApiRoute("GET",  "/api/modbus/config",      handleModbusConfigGet);
    registerApiRoute("GET",  "/api/modbus/live",        handleModbusLiveGet);
    registerApiRoute("GET",  "/api/modbus/scan-status", handleModbusScanStatusGet);
    registerApiRoute("POST", "/api/modbus/scan-stop",   handleModbusScanStopPost);
    registerApiRoute("GET",  "/api/modbus/stats",       handleModbusStatsGet);
    registerApiRoute("POST", "/api/modbus/reset-stats", handleModbusResetStatsPost);

    // Body-accum POST routes
    server.on("/api/modbus/config", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::config(request, data, len, index, total);
        });

    server.on("/api/modbus/poll-now", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::pollNow(request, data, len, index, total);
        });

    server.on("/api/modbus/rtu-write", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::rtuWrite(request, data, len, index, total);
        });

    server.on("/api/modbus/scan-rtu", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::scanRtu(request, data, len, index, total);
        });

    server.on("/api/modbus/scan-tcp", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::scanTcp(request, data, len, index, total);
        });

    server.on("/api/modbus/write", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            MbBodyAccum::write(request, data, len, index, total);
        });

#else
    (void)server;
    (void)context;
#endif // ENABLE_MODBUS==1
}

void registerModbusRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    // Register REST API routes (graceful stubs when ENABLE_MODBUS==0)
    registerModbusApiRoutes(server, context);

#if ENABLE_MODBUS_RTU==1
    // RTU config page: /modbus-rtu — chunked PROGMEM streaming (~55KB, no heap buffer)
    server.on("/modbus-rtu", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool     _init = false;
        static String   _pre, _suf;
        static size_t   _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "Modbus RTU", true,
                "<script>if(document.readyState==='loading')"
                "{document.addEventListener('DOMContentLoaded',initModbusRtuPage);}"
                "else{initModbusRtuPage();}</script>");
            _aLen = strlen_P(PAGE_MODBUS_RTU);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_MODBUS_RTU, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/modbus-rtu", pLen + aLen + sLen, false);
#endif
        request->send(response);
    });
#endif

#if ENABLE_MODBUS_TCP==1
    // TCP config page: /modbus-tcp — chunked PROGMEM streaming (~18KB, no heap buffer)
    // PAGE_MODBUS_TCP already embeds auto.js and the init call, so includeAutoJs=false.
    server.on("/modbus-tcp", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool     _init = false;
        static String   _pre, _suf;
        static size_t   _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "Modbus TCP", false, nullptr);
            _aLen = strlen_P(PAGE_MODBUS_TCP);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_MODBUS_TCP, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/modbus-tcp", pLen + aLen + sLen, false);
#endif
        request->send(response);
    });
#endif

#if ENABLE_MODBUS==1
    // Live data page: /modbus-live — chunked PROGMEM streaming (~25KB, no heap buffer)
    server.on("/modbus-live", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        // Hide page when Modbus is not active in config (e.g. enabled but no RTU/TCP configured).
        {
            JsonVariant _mbSec = configManager.getSection("modbus");
            bool _mbActive = _mbSec["enabled"].as<bool>() &&
                             (_mbSec["rtu_enabled"].as<bool>() || _mbSec["tcp_enabled"].as<bool>());
            if (!_mbActive) { request->redirect("/"); return; }
        }
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool     _init = false;
        static String   _pre, _suf;
        static size_t   _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xf0\x9f\x93\x88 Modbus Live", true,
                "<script>if(document.readyState==='loading')"
                "{document.addEventListener('DOMContentLoaded',initModbusLivePage);}"
                "else{initModbusLivePage();}</script>");
            _aLen = strlen_P(PAGE_MODBUS_LIVE);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_MODBUS_LIVE, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/modbus-live", pLen + aLen + sLen, false);
#endif
        request->send(response);
    });
#else
    (void)server;
    (void)context;
#endif // ENABLE_MODBUS==1
}
