// Route registration for proximity sensor pages and API.

#include "web_routes_proximity.h"
#include "route_registry_api.h"

#include "../http_abstraction.h"
#include "../handlers_unified/proximity_handler.h"
#include "../renderers/proximity_renderer.h"
#include "../renderers/proximity_dashboard_renderer.h"
#include "../../feature_flags.h"

#if ENABLE_PROXIMITY==1

// ---- Body accumulators for POST handlers ----
namespace {
struct ProxBodyAccum {
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
        handleProximityConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void shiftConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleProximityShiftConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
    static void shiftdayEmails(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleProximityShiftdayEmailsPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    static void tca9554(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleProximityTca9554Post(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
    static void saveAll(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleProximitySaveAllPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
};
} // namespace

#endif // ENABLE_PROXIMITY==1

static void registerProximityApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_PROXIMITY==1

    // GET routes: registered DIRECTLY on the server with exact-match URIs.
    // Using AsyncURIMatcher::exact() to avoid BackwardCompatible prefix matching
    // where "/api/proximity" would shadow "/api/proximity/config" etc.
    auto getRoute = [&server, &context](const char* path, ApiRouteHandler handler) {
        server.on(AsyncURIMatcher::exact(path), HTTP_GET,
            [&context, handler](AsyncWebServerRequest* request) {
                if (!context.ensureBasicAuth(request)) return;
                HttpRequest hreq;
                HttpResponse hresp;
                handler(hreq, hresp);
                request->send(hresp.code, hresp.contentType, hresp.body);
            });
    };
    getRoute("/api/proximity",                  handleProximityGet);
    getRoute("/api/proximity/config",           handleProximityConfigGet);
    getRoute("/api/proximity/shift-config",     handleProximityShiftConfigGet);
    getRoute("/api/proximity/shiftday-emails",  handleProximityShiftdayEmailsGet);
    getRoute("/api/proximity/events",           handleProximityEventsGet);
    getRoute("/api/proximity/oee",              handleProximityOeeGet);
    getRoute("/api/proximity/shift-history",    handleProximityShiftHistoryGet);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    getRoute("/api/proximity/tca9554",          handleProximityTca9554Get);
#endif

    // Registry entries for Ethernet dispatch via findApiRouteHandler.
    // WiFi uses the exact-match getRoute() handlers above (registered first).
    registerApiRoute("GET", "/api/proximity",               handleProximityGet);
    registerApiRoute("GET", "/api/proximity/config",        handleProximityConfigGet);
    registerApiRoute("GET", "/api/proximity/shift-config",  handleProximityShiftConfigGet);
    registerApiRoute("GET", "/api/proximity/shiftday-emails", handleProximityShiftdayEmailsGet);
    registerApiRoute("GET", "/api/proximity/events",        handleProximityEventsGet);
    registerApiRoute("GET", "/api/proximity/oee",           handleProximityOeeGet);
    registerApiRoute("GET", "/api/proximity/shift-history", handleProximityShiftHistoryGet);
    registerApiRoute("GET", "/api/proximity-chart-files",   handleProximityChartFilesGet);
    registerApiRoute("GET", "/api/proximity-chart-cols",    handleProximityChartColsGet);
    registerApiRoute("GET", "/api/proximity-chart-data",    handleProximityChartDataGet);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    registerApiRoute("GET", "/api/proximity/tca9554",       handleProximityTca9554Get);
#endif
    registerApiRoute("POST","/api/proximity/reset",         handleProximityResetPost);
    registerApiRoute("POST","/api/proximity/reset-all",     handleProximityResetAllPost);
    registerApiRoute("POST","/api/proximity/reset-total",   handleProximityResetTotalPost);
    registerApiRoute("POST","/api/proximity/oee-reset",     handleProximityOeeResetPost);
    registerApiRoute("POST","/api/proximity/test-email",     handleProximityTestEmailPost);

    // Body-accum POST routes — exact match to avoid prefix shadowing.
    server.on(AsyncURIMatcher::exact("/api/proximity/config"), HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            ProxBodyAccum::config(request, data, len, index, total);
        });

    server.on(AsyncURIMatcher::exact("/api/proximity/shift-config"), HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            ProxBodyAccum::shiftConfig(request, data, len, index, total);
        });

    server.on(AsyncURIMatcher::exact("/api/proximity/shiftday-emails"), HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            ProxBodyAccum::shiftdayEmails(request, data, len, index, total);
        });

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    server.on(AsyncURIMatcher::exact("/api/proximity/tca9554"), HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            ProxBodyAccum::tca9554(request, data, len, index, total);
        });
#endif

    server.on(AsyncURIMatcher::exact("/api/proximity/save-all"), HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index + len != total) return;
            ProxBodyAccum::saveAll(request, data, len, index, total);
        });

#else
    (void)server;
    (void)context;
#endif // ENABLE_PROXIMITY==1
}

void registerProximityRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_PROXIMITY==1

    // HTML pages.
    server.on("/proximity-dashboard", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamProximityDashboardPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/proximity-dashboard", bytes, true);
#endif
    });

    server.on("/proximity", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamProximityPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/proximity", bytes, true);
#endif
    });

    // REST API routes.
    registerProximityApiRoutes(server, context);

#else
    (void)server;
    (void)context;
#endif // ENABLE_PROXIMITY==1
}
