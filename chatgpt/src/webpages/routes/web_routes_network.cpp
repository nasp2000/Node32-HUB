#include "web_routes_network.h"
#include "route_registry_api.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "../../direct_log.h"
#include "../../feature_flags.h"
#include "../../network_mdns.h"
#include "../../network_status_payload.h"
#include "../handlers/heavy_api_request_guard.h"
#include "../handlers_unified/network_handler.h"
#include "../http_abstraction.h"

#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_wifi.h>
#endif
#include "../renderers/ethernet_page_renderer.h"
#include "../renderers/google_script_page_renderer.h"
#include "../renderers/usb_server_page_renderer.h"
#include "../renderers/wifi_ap_minimal.h"
#include "../../runtime_environment.h"
#include "../renderers/wifi_page_renderer.h"

namespace {

String reconstructPostBody(AsyncWebServerRequest* request) {
    String body = "";
    for (int i = 0; i < request->params(); i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
            if (body.length() > 0) body += "&";
            body += p->name() + "=" + p->value();
        }
    }
    return body;
}

} // anonymous namespace

static void registerNetworkApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/wifi-runtime-status",    handleWifiRuntimeStatusGet);
    registerApiRoute("POST", "/api/wifi-enable",            handleWifiEnablePost);
    registerApiRoute("POST", "/api/wifi-force-ap",          handleWifiForceApPost);
    registerApiRoute("POST", "/api/wifi-disable-ap",        handleWifiDisableApPost);
    registerApiRoute("POST", "/api/clear-ap-lock",          handleClearApLockPost);
    registerApiRoute("GET",  "/api/wifi-intelligent-status", handleWifiIntelligentStatusGet);
    registerApiRoute("GET",  "/api/telnet-status",          handleTelnetStatusGet);
    registerApiRoute("POST", "/api/telnet-toggle",          handleTelnetTogglePost);
    registerApiRoute("GET",  "/api/eth-status",             handleEthStatusGet);
    registerApiRoute("POST", "/api/eth-config",             handleEthConfigPost);
    registerApiRoute("POST", "/api/wifi-reconnect",         handleWifiReconnectPost);
#if ENABLE_USBIP_SERVER==1
    registerApiRoute("GET",  "/api/usb-server-status",   handleUsbServerStatusGet);
    registerApiRoute("GET",  "/api/usb-server-config",   handleUsbServerConfigGet);
    registerApiRoute("GET",  "/api/usb-server-devices",  handleUsbServerDevicesGet);
    registerApiRoute("POST", "/api/usb-server-config",   handleUsbServerConfigPost);
#endif

    // --- GET: WiFi Scan (HeavyApiRequestGuard) ---
    server.on("/api/wifi-scan", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HeavyApiRequestGuard heavyGuard(request, "/api/wifi-scan");
        if (!heavyGuard.allowed()) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleWifiScanGet(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // --- GET: WiFi Status (separate shared function, not unified) ---
    server.on("/api/wifi-status", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        StaticJsonDocument<1024> doc;
        fillNetworkWifiStatusJson(doc);
        String response;
        serializeJsonPretty(doc, response);
        request->send(200, "application/json", response);
    });

    // --- GET/POST/DELETE: Saved Networks ---
    server.on("/api/config/saved_networks", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleSavedNetworksGet(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    server.on("/api/config/saved_networks", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(request);
        hreq.contentType = F("application/x-www-form-urlencoded");
        HttpResponse hresp;
        handleSavedNetworksPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    server.on("/api/config/saved_networks", HTTP_DELETE, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(request);
        hreq.contentType = F("application/x-www-form-urlencoded");
        HttpResponse hresp;
        handleSavedNetworksDelete(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // --- GET/POST: Access Point Config ---
    server.on("/api/config/access-point", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleAccessPointConfigGet(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    server.on("/api/config/access-point", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(request);
        hreq.contentType = F("application/x-www-form-urlencoded");
        HttpResponse hresp;
        handleAccessPointConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // --- POST: WiFi Reconnect ---
    server.on("/api/wifi-reconnect", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleWifiReconnectPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // --- POST: WiFi Switch Network ---
    server.on("/api/wifi-switch-network", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(request);
        hreq.contentType = F("application/x-www-form-urlencoded");
        HttpResponse hresp;
        handleWifiSwitchNetworkPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    });

    // --- POST: WiFi Disable (deferred driver shutdown via timer) ---
    server.on("/api/wifi-disable", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleWifiDisablePost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
        TimerHandle_t t = xTimerCreate("wifi_off", pdMS_TO_TICKS(400), pdFALSE, nullptr,
            [](TimerHandle_t th) {
                stopMdns();
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
                esp_wifi_stop();
#endif
                xTimerDelete(th, 0);
            });
        if (t) xTimerStart(t, 0);
    });
}

void registerNetworkRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerNetworkApiRoutes(server, context);

    server.on("/wifi", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        // AP-only + no Ethernet: serve minimal self-contained page (no external assets)
        if (WiFi.getMode() == WIFI_AP && !ethernetHasIp()) {
            AsyncWebServerResponse* r = request->beginResponse(200, "text/html; charset=utf-8", FPSTR(PAGE_WIFI_AP_MINIMAL));
            r->addHeader("Cache-Control", "no-cache");
            request->send(r);
            return;
        }
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
        const size_t bytes = streamWifiPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/wifi", bytes, false);
#endif
    });

#if ENABLE_ETHERNET==1
    server.on("/ethernet", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamEthernetPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ethernet", bytes, false);
#endif
    });
#else
    (void)context;
#endif

#if ENABLE_PAGE_GOOGLE_SCRIPT==1
    server.on("/google-script", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamGoogleScriptPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/google-script", bytes, false);
#endif
    });
#endif

#if ENABLE_USBIP_SERVER==1
    server.on("/usb-server", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamUsbServerPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/usb-server", bytes, false);
#endif
    });
#endif
}