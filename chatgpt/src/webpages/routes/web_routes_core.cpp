#include "web_routes_core.h"

#include <WiFi.h>
#include "../../feature_flags.h"
#include "../assets/config_manager_js_asset.h"
#include "../assets/fragments_system_asset.h"
#include "../assets/page_home_asset.h"
#include "../auto_js.h"
#include "../core_css.h"
#include "../handlers/command_api_handler.h"
#include "../http_abstraction.h"
#include "../handlers_unified/core_admin_handler.h"
#include "../handlers_unified/health_handler.h"
#include "../handlers_unified/i2c_scan_handler.h"
#include "../handlers_unified/selftest_handler.h"
#include "../handlers_unified/test_manifest_handler.h"
#include "route_registry_api.h"
#include "../handlers/heavy_api_request_guard.h"
#include "../renderers/api_reference_renderer.h"
#include "../renderers/home_page_renderer.h"
#include "../renderers/printer_page_renderer.h"
#include "../renderers/lpt_db25_page_renderer.h"
#include "../renderers/wifi_ap_minimal.h"
#include "../../languages/lang_active.h"
#include "../../runtime_environment.h"

extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;

static void registerCoreAdminRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    server.on("/api/status", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleStatusGet(hreq, hresp);
        if (hresp.code == 429) {
            req->send(429, hresp.contentType, hresp.body);
            return;
        }
        AsyncResponseStream* response = req->beginResponseStream(hresp.contentType);
        response->addHeader("Cache-Control", "max-age=5");
        response->addHeader("X-Cache-TTL", "5");
        response->print(hresp.body);
        req->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(req, "/api/status", 0, false);
#endif
    });

    server.on("/api/nvs-stats", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleNvsStatsGet(hreq, hresp);
        req->send(hresp.code, hresp.contentType, hresp.body);
    });

    server.on("/api/restart", HTTP_POST, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HeavyApiRequestGuard guard(req, "/api/restart", 5000UL);
        if (!guard.begin()) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleRestartPost(hreq, hresp);
        req->send(hresp.code, hresp.contentType, hresp.body);
        performRestartActions();
    });

    server.on("/api/test", HTTP_POST, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleTestPost(hreq, hresp);
        req->send(hresp.code, hresp.contentType, hresp.body);
    });

    registerApiRoute("GET", "/api/health", handleHealthGet);
    registerApiRoute("GET", "/api/i2c-scan", handleI2cScanGet);
    registerApiRoute("GET", "/api/self-test", handleSelfTestGet);
    registerApiRoute("GET", "/api/test-manifest", handleTestManifestGet);

#ifdef ENABLE_DEBUG_API
    registerApiRoute("GET", "/api/db25-inject", handleDb25InjectGet);
#endif

    server.on("/api/factory-reset", HTTP_POST, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleFactoryResetPost(hreq, hresp);
        req->send(hresp.code, hresp.contentType, hresp.body);
        req->onDisconnect([]() {
            performFactoryResetActions();
        });
    });

    server.on("/api/heap-snapshot", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleHeapSnapshotGet(hreq, hresp);
        AsyncResponseStream* response = req->beginResponseStream(hresp.contentType);
        response->print(hresp.body);
        req->send(response);
    });
}

void registerCoreRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerCoreAdminRoutes(server, context);
    registerCommandApiRoutes(server, context);

    // Reply silently to favicon requests (avoids 500 errors in browser devtools)
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204, "image/x-icon", "");
    });

    // Serve shared CSS via chunked PROGMEM streaming (avoids copying ~12KB to heap).
    // ETag = FIRMWARE_VERSION: browser caches indefinitely on the same firmware and
    // automatically fetches the new file after a firmware update.
    server.on("/core.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Return 304 when browser already holds the current firmware's copy.
        if (request->hasHeader("If-None-Match") &&
            ifNoneMatchContainsEtag(request->header("If-None-Match"), FIRMWARE_BUILD)) {
            request->send(304);
            return;
        }
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/css",
            [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                static const size_t totalSize = strlen_P(CORE_CSS);
                if (index >= totalSize) return 0;
                size_t remaining  = totalSize - index;
                size_t chunkSize  = (remaining < maxLen) ? remaining : maxLen;
                memcpy_P(buffer, CORE_CSS + index, chunkSize);
                return chunkSize;
            });
        response->addHeader("Cache-Control", "max-age=3600, must-revalidate");
        response->addHeader("ETag", FIRMWARE_BUILD);
        request->send(response);
    });

    // Serve shared JS (auto.js) as fixed-length PROGMEM response.
    // Chunked transfers were occasionally interrupted on busy runtimes,
    // causing net::ERR_INCOMPLETE_CHUNKED_ENCODING on many pages.
    // ETag = FIRMWARE_VERSION: browser caches indefinitely on the same firmware and
    // automatically fetches the new file after a firmware update.
    // Note: pages also append ?v=FIRMWARE_VERSION to the URL for extra safety.
    server.on("/auto.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Return 304 when browser already holds the current firmware's copy.
        if (request->hasHeader("If-None-Match") &&
            ifNoneMatchContainsEtag(request->header("If-None-Match"), FIRMWARE_BUILD)) {
            request->send(304);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(
            200,
            "application/javascript; charset=utf-8",
            reinterpret_cast<const uint8_t*>(AUTO_JS_BODY),
            strlen_P(AUTO_JS_BODY));
        response->addHeader("Cache-Control", "max-age=3600, must-revalidate");
        response->addHeader("ETag", FIRMWARE_BUILD);
        request->send(response);
    });

    // Serve config-manager.js helper (small file, direct PROGMEM send).
    // ETag = FIRMWARE_VERSION: browser caches indefinitely on the same firmware and
    // automatically fetches the new file after a firmware update.
    server.on("/config-manager.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Return 304 when browser already holds the current firmware's copy.
        if (request->hasHeader("If-None-Match") &&
            ifNoneMatchContainsEtag(request->header("If-None-Match"), FIRMWARE_BUILD)) {
            request->send(304);
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(
            200,
            "application/javascript; charset=utf-8",
            reinterpret_cast<const uint8_t*>(CONFIG_MANAGER_JS_BODY),
            strlen_P(CONFIG_MANAGER_JS_BODY));
        response->addHeader("Cache-Control", "max-age=3600, must-revalidate");
        response->addHeader("ETag", FIRMWARE_BUILD);
        request->send(response);
    });

    server.on("/", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        // AP-only mode: serve minimal WiFi config page (no external assets)
        // AP-only + no Ethernet: serve minimal WiFi config page
        if (WiFi.getMode() == WIFI_AP && !ethernetHasIp()) {
            AsyncWebServerResponse* r = request->beginResponse(200, "text/html; charset=utf-8", FPSTR(PAGE_WIFI_AP_MINIMAL));
            r->addHeader("Cache-Control", "no-cache");
            request->send(r);
            return;
        }
        if (!context.ensureBasicAuth(request)) return;
        if (!context.allowHtmlClient(request)) return;

        String homeEtag = buildHomePageEtag();
        if (request->hasHeader("If-None-Match") && ifNoneMatchContainsEtag(request->header("If-None-Match"), homeEtag.c_str())) {
            AsyncWebServerResponse* notModified = request->beginResponse(304);
            notModified->addHeader("ETag", homeEtag);
            notModified->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
            request->send(notModified);
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Content-Language", "pt");
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        response->addHeader("ETag", homeEtag);
        streamHomePageToPrint(*response,
                              PAGE_HOME,
                              FIRMWARE_VERSION,
                              SYSTEM_SECTION,
                              SYSTEM_SECTION_SETTINGS_BUTTON,
                              SYSTEM_SECTION_OTA_BUTTON,
                              SYSTEM_SECTION_CLOSE);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/", strlen_P(PAGE_HOME), true);
#endif
    });

    server.on("/printer", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamPrinterPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/printer", bytes, false);
#endif
    });

#if ENABLE_DB25==1
    server.on("/lpt-db25", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamLptDb25PageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/lpt-db25", bytes, false);
#endif
    });
#endif

#if ENABLE_API_REFERENCE_PAGE==1
    server.on("/api-reference", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        const size_t bytes = streamApiReferencePage(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/api-reference", bytes, false);
#endif
    });
#endif

    // Serve active language JSON for JS i18n (data-i18n attribute replacement)
    server.on("/api/lang", HTTP_GET, [](AsyncWebServerRequest* request) {
        String langEtag = String(FIRMWARE_BUILD) + String("|lang|") + String(activeLangCode);
        if (request->hasHeader("If-None-Match") &&
            ifNoneMatchContainsEtag(request->header("If-None-Match"), langEtag.c_str())) {
            AsyncWebServerResponse* notModified = request->beginResponse(304);
            notModified->addHeader("ETag", langEtag);
            notModified->addHeader("Cache-Control", "private, max-age=300, must-revalidate");
            notModified->addHeader("Content-Language", activeLangCode);
            notModified->addHeader("X-UI-Lang", activeLangCode);
            request->send(notModified);
            return;
        }

        const char* jsonPtr = activeLangUiJson; // read global at request time
        // Compute total size once here so the chunk lambda does not call strlen_P on every chunk.
        const size_t totalSize = strlen_P(jsonPtr);
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "application/json",
            [jsonPtr, totalSize](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                if (index >= totalSize) return 0;
                size_t remaining = totalSize - index;
                size_t chunkSize = (remaining < maxLen) ? remaining : maxLen;
                memcpy_P(buffer, jsonPtr + index, chunkSize);
                return chunkSize;
            });
        response->addHeader("Cache-Control", "private, max-age=300, must-revalidate");
        response->addHeader("ETag", langEtag);
        response->addHeader("Content-Type", "application/json; charset=utf-8");
        response->addHeader("Content-Language", activeLangCode);
        response->addHeader("X-UI-Lang", activeLangCode);
        request->send(response);
    });

    // AP-only mode (no Ethernet): serve minimal WiFi page for any unknown URL.
    // Otherwise return 404 for unknown paths instead of a 302 redirect, so API
    // and page clients can reliably detect a non-existent route.
    server.onNotFound([&context](AsyncWebServerRequest* request) {
        if (WiFi.getMode() == WIFI_AP && !ethernetHasIp()) {
            AsyncWebServerResponse* r = request->beginResponse(200, "text/html; charset=utf-8", FPSTR(PAGE_WIFI_AP_MINIMAL));
            r->addHeader("Cache-Control", "no-cache");
            request->send(r);
            return;
        }
        if (!context.ensureBasicAuth(request)) return;
        String path = request->url();
        if (path.startsWith("/api/")) {
            String body = F("{\"status\":\"error\",\"msg\":\"not_found\"}");
            AsyncWebServerResponse* r = request->beginResponse(404, "application/json", body);
            r->addHeader("Cache-Control", "no-cache, no-store");
            request->send(r);
        } else {
            AsyncWebServerResponse* r = request->beginResponse(404, "text/html; charset=utf-8", F("<!DOCTYPE html><html><head><title>Not Found</title></head><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>"));
            r->addHeader("Cache-Control", "no-cache, no-store");
            request->send(r);
        }
    });
}