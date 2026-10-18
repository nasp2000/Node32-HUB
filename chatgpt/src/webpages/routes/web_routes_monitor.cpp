#include "web_routes_monitor.h"

#include "../../feature_flags.h"
#include "../../grbl_sender.h"

#if ENABLE_PAGE_MONITOR==1

#include "../../ConfigManager.h"
#include "../../web_monitor_pages.h"
#include "../handlers/monitor_buffer_handler.h"
#include "../handlers_unified/monitor_handler.h"
#include "../handlers/rs232_history_handler.h"
#include "web_routes_config.h"
#include "route_registry_api.h"
#if ENABLE_GRBL == 1
#include "../renderers/grbl_controller_page_renderer.h"
#endif
#include "../renderers/sensor_charts_page_renderer.h"
#include "../renderers/status_page_renderer.h"

#if ENABLE_GRBL == 1
// ---- Inlined from grbl_controller_handler.cpp (WiFi adapter) ----
#include "../http_abstraction.h"
#include "../handlers_unified/grbl_handler.h"
#include "../handlers_unified/grbl_sd_utils.h"
#include "../handlers_unified/ws_handler.h"

#include <Arduino.h>
#include <FS.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "../../grbl_sender.h"
#include "../../psram_vfs.h"
#include "../../grbl_psram_gcode.h"
#include "../../sdcard.h"
#include "../../runtime_storage.h"

extern bool psramVFSInitialized;
extern ConfigManager configManager;

#include "../../sd_security.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC || SELECTED_PACK == PACK_S3_CNC)

static String urlEncode(const String& raw) {
    String out;
    out.reserve(raw.length() * 3);
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < raw.length(); i++) {
        unsigned char c = (unsigned char)raw[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String buildQueryString(AsyncWebServerRequest* request) {
    String out;
    const size_t n = request->params();
    for (size_t i = 0; i < n; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (!p) continue;
        if (out.length() > 0) out += '&';
        out += urlEncode(p->name());
        out += '=';
        out += urlEncode(p->value());
    }
    return out;
}

struct GrblPsramUploadCtx {
    uint8_t* buf = nullptr;
    size_t bufCap = 0;
    size_t bytesReceived = 0;
    String fileName;
    bool done = false;
    bool failed = false;
    String queryString;
    String errorMsg;
};

static void grblPsramUploadHandler(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    GrblPsramUploadCtx* ctx = static_cast<GrblPsramUploadCtx*>(request->_tempObject);

    if (index == 0) {
        if (ctx) { if (ctx->buf) heap_caps_free(ctx->buf); delete ctx; ctx = nullptr; request->_tempObject = nullptr; }

        // Determine initial buffer size: use total if valid (>0), otherwise start small
        size_t initialCap = 16384;
        bool sizeKnown = (total > 0);
        if (sizeKnown) {
            if (total > GRBL_STREAM_PSRAM_MAX_BYTES) {
                ctx = new GrblPsramUploadCtx(); if (ctx) { ctx->done = true; ctx->failed = true; ctx->errorMsg = "file_too_large"; request->_tempObject = ctx; } return;
            }
            if (psramVFSInitialized && total > psramVFSGetFreeBytes()) {
                ctx = new GrblPsramUploadCtx(); if (ctx) { ctx->done = true; ctx->failed = true; ctx->errorMsg = "vfs_budget_exceeded"; request->_tempObject = ctx; } return;
            }
            initialCap = total;
        }
        ctx = new GrblPsramUploadCtx();
        if (!ctx) return;
        request->_tempObject = ctx;
        ctx->buf = (uint8_t*)heap_caps_malloc(initialCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ctx->buf) ctx->buf = (uint8_t*)heap_caps_malloc(initialCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!ctx->buf) { ctx->done = true; ctx->failed = true; ctx->errorMsg = "alloc_failed"; return; }
        ctx->bufCap = initialCap;
        ctx->bytesReceived = 0;
        ctx->queryString = buildQueryString(request);
        ctx->fileName = request->hasParam("name") ? request->getParam("name")->value() : (request->hasHeader("X-Filename") ? request->header("X-Filename") : String("upload.nc"));
    }

    if (!ctx || !ctx->buf || ctx->done) return;

    if (len == 0) {
        ctx->done = true;
        return;
    }

    if (index + len > ctx->bufCap) {
        size_t newCap = ctx->bufCap * 2;
        while (newCap < index + len) newCap *= 2;
        uint8_t* newBuf = (uint8_t*)heap_caps_malloc(newCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!newBuf) newBuf = (uint8_t*)heap_caps_malloc(newCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (newBuf) {
            memcpy(newBuf, ctx->buf, ctx->bufCap);
            heap_caps_free(ctx->buf);
            ctx->buf = newBuf;
            ctx->bufCap = newCap;
        } else {
            ctx->done = true; ctx->failed = true; ctx->errorMsg = "alloc_failed"; return;
        }
    }

    memcpy(ctx->buf + index, data, len);
    ctx->bytesReceived += len;
    // When total is known, use it as completion threshold
    if (total > 0 && ctx->bytesReceived >= (size_t)total) {
        ctx->done = true;
    }
}

static void grblPsramUploadFinal(AsyncWebServerRequest* request) {
    GrblPsramUploadCtx* ctx = static_cast<GrblPsramUploadCtx*>(request->_tempObject);
    if (!ctx) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"upload_incomplete\"}");
        return;
    }
    if (ctx->failed) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"" + ctx->errorMsg + "\"}");
        if (ctx->buf) heap_caps_free(ctx->buf);
        delete ctx; request->_tempObject = nullptr;
        return;
    }
    if (ctx->bytesReceived == 0) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"upload_empty\"}");
        if (ctx->buf) heap_caps_free(ctx->buf);
        delete ctx; request->_tempObject = nullptr;
        return;
    }

    // Direct store from ctx->buf — no String copy (avoids DRAM OOM for >3MB).
    const bool stored = grblPsramGcodeStore(ctx->fileName.c_str(), ctx->buf, ctx->bytesReceived);

    if (stored) {
        String json = "{\"success\":true,\"message\":\"saved\",\"file_path\":\"/psram/";
        json += ctx->fileName;
        json += "\",\"bytes\":";
        json += String(ctx->bytesReceived);
        json += "}";
        request->send(200, "application/json", json);
    } else {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"psram_store_failed\"}");
    }

    if (ctx->buf) heap_caps_free(ctx->buf);
    delete ctx; request->_tempObject = nullptr;
}

struct GrblUploadStreamCtx {
    uint8_t* buf = nullptr;
    size_t bufCap = 0;
    size_t bytesReceived = 0;
    bool done = false;
    bool failed = false;
    String queryString;
    String errorMsg;
    int statusCode = 500;
};

static void grblUploadStreamHandler(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    GrblUploadStreamCtx* ctx = static_cast<GrblUploadStreamCtx*>(request->_tempObject);

    if (index == 0) {
        if (ctx) { if (ctx->buf) heap_caps_free(ctx->buf); delete ctx; ctx = nullptr; request->_tempObject = nullptr; }

        // Determine initial buffer size: use total if valid (>0), otherwise start small
        size_t initialCap = 65536;
        bool sizeKnown = (total > 0);
        if (sizeKnown) {
            if (total > 50UL * 1024UL * 1024UL) {
                ctx = new GrblUploadStreamCtx(); if (ctx) { ctx->done = true; ctx->failed = true; ctx->errorMsg = "file_too_large"; ctx->statusCode = 413; request->_tempObject = ctx; } return;
            }
            initialCap = total;
        }
        ctx = new GrblUploadStreamCtx();
        if (!ctx) return;
        request->_tempObject = ctx;
        ctx->buf = (uint8_t*)heap_caps_malloc(initialCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ctx->buf) ctx->buf = (uint8_t*)heap_caps_malloc(initialCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!ctx->buf) { ctx->done = true; ctx->failed = true; ctx->errorMsg = "alloc_failed"; ctx->statusCode = 500; return; }
        ctx->bufCap = initialCap;
        ctx->bytesReceived = 0;
        ctx->queryString = buildQueryString(request);
    }

    if (!ctx || !ctx->buf || ctx->done) return;

    if (len == 0) {
        ctx->done = true;
        return;
    }

    if (index + len > ctx->bufCap) {
        size_t newCap = ctx->bufCap * 2;
        while (newCap < index + len) newCap *= 2;
        uint8_t* newBuf = (uint8_t*)heap_caps_malloc(newCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!newBuf) newBuf = (uint8_t*)heap_caps_malloc(newCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (newBuf) {
            memcpy(newBuf, ctx->buf, ctx->bufCap);
            heap_caps_free(ctx->buf);
            ctx->buf = newBuf;
            ctx->bufCap = newCap;
        } else {
            ctx->done = true; ctx->failed = true; ctx->errorMsg = "alloc_failed"; ctx->statusCode = 500; return;
        }
    }

    memcpy(ctx->buf + index, data, len);
    ctx->bytesReceived += len;
    // When total is known, use it as completion threshold
    if (total > 0 && ctx->bytesReceived >= (size_t)total) {
        ctx->done = true;
    }
}

static void grblUploadStreamFinal(AsyncWebServerRequest* request) {
    GrblUploadStreamCtx* ctx = static_cast<GrblUploadStreamCtx*>(request->_tempObject);
    if (!ctx) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"upload_incomplete\"}");
        return;
    }
    if (ctx->failed) {
        request->send(ctx->statusCode, "application/json", "{\"success\":false,\"message\":\"" + ctx->errorMsg + "\"}");
        if (ctx->buf) heap_caps_free(ctx->buf);
        delete ctx; request->_tempObject = nullptr;
        return;
    }
    if (ctx->bytesReceived == 0) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"upload_empty\"}");
        if (ctx->buf) heap_caps_free(ctx->buf);
        delete ctx; request->_tempObject = nullptr;
        return;
    }

#if ENABLE_SDCARD==1
    // Write directly from ctx->buf — no String copy (avoids DRAM OOM).
    String path = request->hasParam("path") ? request->getParam("path")->value() : "";
    const bool overwrite = request->hasParam("overwrite") && request->getParam("overwrite")->value() == "1";
    if (path.length() == 0) {
        String fileName = request->hasParam("name") ? request->getParam("name")->value() : "upload.nc";
        path = String("/") + fileName;
    }

    if (!sdcardShouldUseSD()) {
        request->send(503, "application/json", "{\"success\":false,\"message\":\"sd_unavailable\"}");
        heap_caps_free(ctx->buf); delete ctx; request->_tempObject = nullptr; return;
    }
    if (!acquireSDAccess("grbl-upload-final", 8000UL)) {
        request->send(503, "application/json", "{\"success\":false,\"message\":\"sd_busy\"}");
        heap_caps_free(ctx->buf); delete ctx; request->_tempObject = nullptr; return;
    }
    if (SD_MMC.exists(path.c_str()) && !overwrite) {
        releaseSDAccess();
        request->send(409, "application/json", "{\"success\":false,\"message\":\"file_exists\"}");
        heap_caps_free(ctx->buf); delete ctx; request->_tempObject = nullptr; return;
    }
    if (SD_MMC.exists(path.c_str())) {
        SD_MMC.remove(path.c_str());
    }
    File out = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!out) {
        releaseSDAccess();
        request->send(500, "application/json", "{\"success\":false,\"message\":\"open_failed\"}");
        heap_caps_free(ctx->buf); delete ctx; request->_tempObject = nullptr; return;
    }
    const size_t written = out.write(ctx->buf, ctx->bytesReceived);
    out.close();
    storNoteSdWrite(written);
    releaseSDAccess();
    if (written != ctx->bytesReceived) {
        SD_MMC.remove(path.c_str());
        request->send(500, "application/json", "{\"success\":false,\"message\":\"write_failed\"}");
        heap_caps_free(ctx->buf); delete ctx; request->_tempObject = nullptr; return;
    }
    String json = "{\"success\":true,\"message\":\"saved\",\"file_path\":\"";
    json += path;
    json += "\",\"bytes\":";
    json += String(written);
    json += "}";
    request->send(200, "application/json", json);
#else
    request->send(503, "application/json", "{\"success\":false,\"message\":\"sd_disabled\"}");
#endif

    if (ctx->buf) heap_caps_free(ctx->buf);
    delete ctx; request->_tempObject = nullptr;
}

static void registerGrblControllerApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server; (void)context;

    // ---- Registry-migrated GET endpoints ----
    registerApiRoute("GET", "/api/grbl/status", handleGrblStatusGet);
    registerApiRoute("GET", "/api/grbl/debug", handleGrblDebugGet);
    registerApiRoute("GET", "/api/grbl/control-token", handleGrblControlTokenGet);
    registerApiRoute("GET", "/api/grbl/events", handleGrblEventsGet);
    registerApiRoute("GET", "/api/grbl/list-gcode", handleGrblListGcodeGet);
    registerApiRoute("GET", "/api/grbl/frame-bbox", handleGrblFrameBboxGet);
    registerApiRoute("GET", "/api/grbl/psram-file", handleGrblPsramFileGet);
    registerApiRoute("GET", "/api/grbl/psram-read-text", handleGrblPsramReadTextGet);

    // ---- Registry-migrated POST endpoints ----
    registerApiRoute("POST", "/api/grbl/usb-restart", handleGrblUsbRestartPost);
    registerApiRoute("POST", "/api/grbl/transport", handleGrblTransportPost);
    registerApiRoute("POST", "/api/grbl/usb-line-coding", handleGrblUsbLineCodingPost);
    registerApiRoute("POST", "/api/grbl/auto-detect-baud", handleGrblAutoDetectBaudPost);
    registerApiRoute("POST", "/api/grbl/send", handleGrblSendPost);
    registerApiRoute("POST", "/api/grbl/override", handleGrblOverridePost);
    registerApiRoute("POST", "/api/grbl/stream", handleGrblStreamPost);
    registerApiRoute("POST", "/api/grbl/queue-start", handleGrblQueueStartPost);
    registerApiRoute("POST", "/api/grbl/queue-stop", handleGrblQueueStopPost);
    registerApiRoute("POST", "/api/grbl/pause", handleGrblPausePost);
    registerApiRoute("POST", "/api/grbl/resume", handleGrblResumePost);
    registerApiRoute("POST", "/api/grbl/resume-alarm", handleGrblResumeAlarmPost);
    registerApiRoute("POST", "/api/grbl/cancel", handleGrblCancelPost);
    registerApiRoute("POST", "/api/grbl/poll-interval", handleGrblPollIntervalPost);
    registerApiRoute("POST", "/api/grbl/create-dir", handleGrblCreateDirPost);
    registerApiRoute("POST", "/api/grbl/delete-path", handleGrblDeletePathPost);
    registerApiRoute("POST", "/api/grbl/psram-stream", handleGrblPsramStreamPost);
    registerApiRoute("POST", "/api/grbl/psram-clear", handleGrblPsramClearPost);

    // Probe grid / height map endpoints
    registerApiRoute("GET",  "/api/grbl/probe-grid",       handleGrblProbeGridGet);
    registerApiRoute("POST", "/api/grbl/probe-grid",       handleGrblProbeGridPost);
    registerApiRoute("POST", "/api/grbl/probe-grid-cancel", handleGrblProbeGridCancelPost);
    registerApiRoute("GET",  "/api/grbl/height-map",       handleGrblHeightMapGet);
    registerApiRoute("POST", "/api/grbl/height-map-enable", handleGrblHeightMapEnablePost);
    registerApiRoute("POST", "/api/grbl/height-map-clear",  handleGrblHeightMapClearPost);

    // ---- Manual GET (SD streaming) ----
    server.on("/api/grbl/read-gcode-text", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
#if ENABLE_SDCARD == 1
        String path = grblSdNormalizePath(request->arg("path"));
        String rootDir = grblSdRootDir();
        if (!grblSdIsAllowedPath(path, rootDir)) {
            request->send(400, "text/plain; charset=utf-8", "invalid_path");
            return;
        }
        if (!sdcardShouldUseSD()) {
            request->send(503, "text/plain; charset=utf-8", "sd_unavailable");
            return;
        }
        if (!acquireSDAccess("grbl-read-text", 8000UL)) {
            request->send(503, "text/plain; charset=utf-8", "sd_busy");
            return;
        }
        File file = SD_MMC.open(path.c_str(), FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) file.close();
            releaseSDAccess();
            request->send(404, "text/plain; charset=utf-8", "file_not_found");
            return;
        }
        const size_t maxRead = 50UL * 1024UL * 1024UL;
        if ((size_t)file.size() > maxRead) {
            file.close();
            releaseSDAccess();
            request->send(413, "text/plain; charset=utf-8", "file_too_large");
            return;
        }
        file.close();
        AsyncWebServerResponse* response = request->beginResponse(SD_MMC, path, "text/plain; charset=utf-8");
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
#else
        request->send(503, "text/plain; charset=utf-8", "sd_disabled");
#endif
    });

    // ---- Manual POST upload endpoints (body accumulators) ----

    server.on("/api/grbl/psram-upload", HTTP_POST,
        [](AsyncWebServerRequest* request) { grblPsramUploadFinal(request); },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            grblPsramUploadHandler(request, data, len, index, total);
        });

    server.on("/api/grbl/upload-gcode-stream", HTTP_POST,
        [](AsyncWebServerRequest* request) { grblUploadStreamFinal(request); },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            grblUploadStreamHandler(request, data, len, index, total);
        });
    initWsServer(server);
    (void)&wsGrblLoop;
}

#else
static void registerGrblControllerApiRoutes(AsyncWebServer&, const WebRouteContext&) {}
#endif // PACK_P4_CNC
#endif // ENABLE_GRBL

extern ConfigManager configManager;

void registerMonitorRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("POST", "/api/monitor-open",   handleMonitorOpenPost);
    registerApiRoute("POST", "/api/monitor-close",  handleMonitorClosePost);
    registerApiRoute("POST", "/api/clear-buffer",   handleClearBufferPost);
    registerMonitorBufferRoutes(server, context);
    registerSensorApiRoutes(server, context);
    registerRs232HistoryRoutes(server, context);
#if ENABLE_GRBL == 1
    registerGrblControllerApiRoutes(server, context);
#endif

    server.on("/status", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamStatusPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/status", bytes, false);
#endif
    });

    server.on("/monitor", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        streamSharedMonitorPage(*response);
        request->send(response);
    });

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC || SELECTED_PACK == PACK_S3_CNC)
    server.on("/grbl-controller", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamGrblControllerPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/grbl-controller", bytes, false);
#endif
    });
#endif

    server.on("/buffer-viewer", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        streamSharedBufferViewerPage(*response);
        request->send(response);
    });

    server.on("/serial-monitor", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        streamSharedSerialMonitorPage(*response);
        request->send(response);
    });

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
    server.on("/sensor-charts", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->redirect("/");
            return;
        }
        // Charts page always counts as a concurrent session.
        // When charts_no_kick is enabled sessions never expire, but the slot
        // is still claimed so the overall client limit is respected.
        if (!context.allowHtmlClient(request)) return;

        // Use beginResponse_P (via sendSensorChartsPageResponse) to stream 97KB
        // directly from PROGMEM. This avoids allocating a large contiguous heap
        // buffer that AsyncResponseStream would require, which can fail under memory
        // pressure and produce a blank white page.
        const size_t bytes = sendSensorChartsPageResponse(request);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/sensor-charts", bytes, false);
#endif
    });
#endif
}

#else
// ENABLE_PAGE_MONITOR=0: provide an empty stub so the linker is satisfied.
void registerMonitorRoutes(AsyncWebServer&, const WebRouteContext&) {}
#endif // ENABLE_PAGE_MONITOR