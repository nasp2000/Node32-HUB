#include "web_routes_storage.h"

#include "route_registry_api.h"
#include "../../feature_flags.h"
#include "../http_abstraction.h"
#include "../handlers_unified/storage_handler.h"
#include "../handlers/heavy_api_request_guard.h"
#if ENABLE_PAGE_FILES==1
#include "../handlers/files_api_handler.h"
#include "../handlers/files_page_handler.h"
#include "../handlers_unified/files_handler.h"
#endif
#if ENABLE_PAGE_SDCARD==1
#include "../renderers/sdcard_page_renderer.h"
#endif

static String reconstructPostBody(AsyncWebServerRequest* request) {
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

static void registerStorageAdminRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/sd-status",         handleSdStatusGet);
    registerApiRoute("GET",  "/api/sdcard-config",      handleSdcardConfigGet);
    registerApiRoute("POST", "/api/storage-retry-sd",   handleStorageRetrySdPost);
    registerApiRoute("POST", "/api/sd/reset-write-counter", handleSdResetWriteCounterPost);
    registerApiRoute("POST", "/api/sdcard-config-save", handleSdcardConfigSavePost);

    server.on("/api/sd-buffers", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HeavyApiRequestGuard guard(req, "/api/sd-buffers");
        if (!guard.allowed()) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleSdBuffersGet(hreq, hresp);
        AsyncResponseStream* response = req->beginResponseStream(hresp.contentType);
        response->addHeader("Cache-Control", "no-cache");
        response->print(hresp.body);
        req->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(req, "/api/sd-buffers", 0, false);
#endif
    });

    server.on("/api/sd-action", HTTP_POST, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        hreq.body = reconstructPostBody(req);
        if (req->hasParam("action", true)) {
            if (hreq.body.length() > 0 && !hreq.body.endsWith("&")) hreq.body += "&";
            hreq.body += "action=" + req->getParam("action", true)->value();
        }
        HttpResponse hresp;
        handleSdActionPost(hreq, hresp);
        req->send(hresp.code, hresp.contentType, hresp.body);
#ifndef QUIET_HTTP_LOGS
        context.logServed(req, "/api/sd-action", 0, false);
#endif
    });
}

void registerStorageRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerStorageAdminRoutes(server, context);
#if ENABLE_PAGE_FILES==1
    registerApiRoute("POST", "/api/sd/mount",       handleSdMountPost);
    registerApiRoute("POST", "/api/sd/unmount",      handleSdUnmountPost);
    registerApiRoute("POST", "/api/sd/create-file",  handleSdCreateFilePost);
    registerApiRoute("POST", "/api/sd/create-dir",   handleSdCreateDirPost);
    registerApiRoute("POST", "/api/sd/rename",       handleSdRenamePost);
    registerApiRoute("POST", "/api/sd/delete",       handleSdDeletePost);
    registerFilesPageRoute(server, context);
    registerFilesApiRoutes(server, context);
#endif
#if ENABLE_PAGE_SDCARD==1
    // SD card & storage configuration page (/sdcard).
    // Always available: also shows PSRAM-only status when SD is disabled.
    server.on("/sdcard", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamSdcardPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/sdcard", bytes, false);
#endif
    });
#endif // ENABLE_PAGE_SDCARD
}