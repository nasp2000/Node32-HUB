// Route registration for TouchMeteo-7 pages and API.

#include "web_routes_touch7.h"
#include "route_registry_api.h"

#include "../http_abstraction.h"
#include "../renderers/falas_page_renderer.h"
#include "../renderers/ecra_page_renderer.h"
#include "../../feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../handlers_unified/touch7_handler.h"

// ---- Body accumulators for JSON POSTs (same shape as ProxBodyAccum) ----
namespace {
struct Touch7BodyAccum {
    static void config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body = "";
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleTouch7ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void bh1750(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
#if ENABLE_BH1750==1
        static String body;
        if (index == 0) body = "";
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleBh1750ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
#else
        (void)request; (void)data; (void)len; (void)index; (void)total;
#endif
    }
};
} // namespace
#endif

void registerTouch7Routes(AsyncWebServer& server, const WebRouteContext& context) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    // HTML pages (streamed, same shape as /sound).
    server.on("/ecra", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamEcraPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ecra", bytes, false);
#endif
    });
    server.on("/falas", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamFalasPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/falas", bytes, false);
#endif
    });

    // JSON APIs without body (shared WiFi+Ethernet via registry).
#if ENABLE_BH1750==1
    registerApiRoute("GET", "/api/bh1750-config", handleBh1750ConfigGet);
    registerApiRoute("POST", "/api/bh1750-measure", handleBh1750MeasurePost);
#endif
    registerApiRoute("GET", "/api/touch7-config", handleTouch7ConfigGet);
    registerApiRoute("GET", "/api/touch7-forecast", handleTouch7ForecastGet);
    registerApiRoute("POST", "/api/touch7-weather-refresh", handleTouch7WeatherRefreshPost);
    registerApiRoute("GET", "/api/touch7-geo", handleTouch7GeoGet);
    registerApiRoute("GET", "/api/touch7-autoloc", handleTouch7AutoLocGet);
    registerApiRoute("POST", "/api/touch7-tap", handleTouch7TapPost);
    registerApiRoute("GET", "/api/touch7-frame", handleTouch7FrameGet);
    registerApiRoute("GET", "/api/touch7-pclk", handleTouch7PclkGet);
    registerApiRoute("POST", "/api/touch7-peer-refresh", handleTouch7PeerRefreshPost);
    registerApiRoute("GET", "/api/falas", handleFalasListGet);
    registerApiRoute("POST", "/api/falas-play", handleFalasPlayPost);
    registerApiRoute("POST", "/api/falas-delete", handleFalasDeletePost);

    // JSON POSTs with body stay manual (accumulate, then dispatch).
    server.on("/api/touch7-config", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                  if (!context.ensureBasicAuth(request)) return;
                  Touch7BodyAccum::config(request, data, len, index, total);
              });
    server.on("/api/bh1750-config", HTTP_POST, [](AsyncWebServerRequest* request) {},
              nullptr,
              [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
#if ENABLE_BH1750==1
                  if (!context.ensureBasicAuth(request)) return;
                  Touch7BodyAccum::bh1750(request, data, len, index, total);
#else
                  if (!context.ensureBasicAuth(request)) return;
                  (void)data; (void)len; (void)index; (void)total;
                  request->send(404, "text/plain", "BH1750 off in this pack");
#endif
              });
#else
    (void)server;
    (void)context;
#endif
}
