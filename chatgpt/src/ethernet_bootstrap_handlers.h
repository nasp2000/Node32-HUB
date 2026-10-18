// ethernet_bootstrap_handlers.h - HTTP handler implementations for the Ethernet bootstrap server
// Implementation-include: include ONLY from ethernet.cpp inside #if ENABLE_ETHERNET==1
// Do NOT add include guards - this file must be included exactly once.
#include "languages/lang_active.h"
#include <ctype.h>

#include "psram_helpers.h"
#include <vector>

#include "psram_helpers.h"

// Unified handler declarations
#include "webpages/http_abstraction.h"

#include "psram_helpers.h"
#include "webpages/handlers_unified/config_handler.h"

#include "psram_helpers.h"
#include "webpages/handlers_unified/network_handler.h"

#include "psram_helpers.h"

// --- GRBL handlers ---
#include "webpages/handlers_unified/grbl_handler.h"

#include "psram_helpers.h"
#include "grbl_web_api.h"

#include "psram_helpers.h"
#include "grbl_psram_gcode.h"

#include "psram_helpers.h"
extern bool psramVFSInitialized;

// ====================================================================
// Helper: construct unified response from EthernetClient
// ====================================================================

static void ethernetSendUnifiedResponse(EthernetClient& client, const HttpResponse& hresp) {
    if (hresp.isBinary && hresp.binaryData) {
        client.print(F("HTTP/1.1 ")); client.println(httpStatusToString(hresp.code));
        client.print(F("Content-Type: ")); client.println(hresp.contentType);
        client.print(F("Content-Length: ")); client.println((unsigned long)hresp.binaryLen);
        client.println();
        client.write(hresp.binaryData, hresp.binaryLen);
        if (hresp.freeFn) hresp.freeFn(hresp.binaryData);
    } else {
        ethernetBootstrapSendResponse(client,
                                      httpStatusToString(hresp.code).c_str(),
                                      hresp.contentType.c_str(),
                                      hresp.body);
    }
}

static void ethernetSendGrblUnified(EthernetClient& client, HttpRequest& hreq, void (*handler)(HttpRequest&, HttpResponse&)) {
    HttpResponse hresp;
    handler(hreq, hresp);
    ethernetSendUnifiedResponse(client, hresp);
}

// ====================================================================
// GET endpoint wrappers
// ====================================================================

static void ethernetHandleGrblStatusGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblStatusGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblDebugGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblDebugGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblControlTokenGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblControlTokenGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblEventsGet(EthernetClient& client, const String& query, const String& headerLastEventId) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    if (headerLastEventId.length() > 0) {
        if (hreq.query.length() > 0) hreq.query += "&";
        hreq.query += "last_id=" + headerLastEventId;
    }
    ethernetSendGrblUnified(client, hreq, handleGrblEventsGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblListGcodeGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblListGcodeGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblReadGcodeTextGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblReadGcodeTextGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblFrameBboxGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblFrameBboxGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPsramFileGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblPsramFileGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPsramReadTextGet(EthernetClient& client, const String& query) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.query = query;
    ethernetSendGrblUnified(client, hreq, handleGrblPsramReadTextGet);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

// ====================================================================
// Simple POST endpoint wrappers (body is form-encoded)
// ====================================================================

static void ethernetHandleGrblUsbRestartPost(EthernetClient& client) {
#if ENABLE_GRBL
    HttpRequest hreq;
    ethernetSendGrblUnified(client, hreq, handleGrblUsbRestartPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblUsbLineCodingPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblUsbLineCodingPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblAutoDetectBaudPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblAutoDetectBaudPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblSendPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblSendPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblStreamPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblStreamPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblQueueStartPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblQueueStartPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblQueueStopPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblQueueStopPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPausePost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblPausePost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblResumePost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblResumePost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblCancelPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblCancelPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPollIntervalPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblPollIntervalPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblCreateDirPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblCreateDirPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblDeletePathPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblDeletePathPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPsramStreamPost(EthernetClient& client, const String& body) {
#if ENABLE_GRBL
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = "application/x-www-form-urlencoded";
    ethernetSendGrblUnified(client, hreq, handleGrblPsramStreamPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblPsramClearPost(EthernetClient& client) {
#if ENABLE_GRBL
    HttpRequest hreq;
    ethernetSendGrblUnified(client, hreq, handleGrblPsramClearPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

// ====================================================================
// Upload POST endpoints (body read from client, then delegated)
// ====================================================================

static void ethernetHandleGrblPsramUploadPost(EthernetClient& client,
                                               const String& query,
                                               int contentLength,
                                               const String& filenameHeader) {
#if ENABLE_GRBL
    if (contentLength <= 0) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"success\":false,\"message\":\"empty_content\"}")));
        return;
    }
    const size_t total = (size_t)contentLength;
    if (total > GRBL_STREAM_PSRAM_MAX_BYTES) {
        ethernetBootstrapSendResponse(client, "413 Payload Too Large", "application/json", String(F("{\"success\":false,\"message\":\"file_too_large\"}")));
        return;
    }

    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"success\":false,\"message\":\"alloc_failed\"}")));
        return;
    }

    size_t readPos = 0;
    unsigned long deadline = millis() + 30000UL;
    while (readPos < total) {
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = min((size_t)avail, total - readPos);
            int n = client.read(buf + readPos, toRead);
            if (n > 0) { readPos += (size_t)n; deadline = millis() + 30000UL; }
        } else if ((long)(millis() - deadline) > 0) {
            free(buf);
            ethernetBootstrapSendResponse(client, "408 Request Timeout", "application/json", String(F("{\"success\":false,\"message\":\"upload_timeout\"}")));
            return;
        }
        esp_task_wdt_reset();
    }

    String fileName = ethernetGetFormValue(query, "name");
    fileName.trim();
    if (fileName.length() == 0) {
        fileName = ethernetUrlDecode(filenameHeader);
        fileName.trim();
    }

    if (psramVFSInitialized && total > psramVFSGetFreeBytes()) {
        free(buf);
        ethernetBootstrapSendResponse(client, "413 Payload Too Large", "application/json", String(F("{\"success\":false,\"message\":\"vfs_budget_exceeded\"}")));
        return;
    }

    HttpRequest hreq;
    hreq.query = query;
    hreq.body = String((const char*)buf, total);
    hreq.contentType = "application/octet-stream";
    if (fileName.length() > 0) {
        if (hreq.query.length() > 0) hreq.query += "&";
        hreq.query += "name=" + fileName;
    }
    free(buf);

    ethernetSendGrblUnified(client, hreq, handleGrblPsramUploadPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

static void ethernetHandleGrblUploadGcodeStreamPost(EthernetClient& client,
                                                     const String& query,
                                                     int contentLength,
                                                     const String& filenameHeader,
                                                     const String& filesizeHeader) {
#if ENABLE_GRBL
    if (contentLength <= 0) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"success\":false,\"message\":\"empty_content\"}")));
        return;
    }
    const size_t total = (size_t)contentLength;
    if (total > 50UL * 1024UL * 1024UL) {
        ethernetBootstrapSendResponse(client, "413 Payload Too Large", "application/json", String(F("{\"success\":false,\"message\":\"file_too_large\"}")));
        return;
    }

    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"success\":false,\"message\":\"alloc_failed\"}")));
        return;
    }

    String fileName = ethernetGetFormValue(query, "name");
    fileName.trim();
    if (fileName.length() == 0) {
        fileName = ethernetUrlDecode(filenameHeader);
        fileName.trim();
    }

    size_t readPos = 0;
    unsigned long deadline = millis() + 30000UL;
    while (readPos < total) {
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = min((size_t)avail, total - readPos);
            int n = client.read(buf + readPos, toRead);
            if (n > 0) { readPos += (size_t)n; deadline = millis() + 30000UL; }
        } else if ((long)(millis() - deadline) > 0) {
            free(buf);
            ethernetBootstrapSendResponse(client, "408 Request Timeout", "application/json", String(F("{\"success\":false,\"message\":\"upload_timeout\"}")));
            return;
        }
        esp_task_wdt_reset();
    }

    HttpRequest hreq;
    hreq.query = query;
    hreq.body = String((const char*)buf, total);
    hreq.contentType = "application/octet-stream";
    if (fileName.length() > 0) {
        if (hreq.query.length() > 0) hreq.query += "&";
        hreq.query += "name=" + fileName;
    }
    free(buf);

    ethernetSendGrblUnified(client, hreq, handleGrblUploadGcodeStreamPost);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"success\":false,\"message\":\"not found\"}")));
#endif
}

// GET /api/chart-caps - compile-time feature flags for chart JS (which data sources are available)
static void ethernetHandleChartCapsGet(EthernetClient& client) {
    if (!configManager.getChartsPageEnabled()) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json",
            F("{\"error\":\"charts disabled\"}"));
        return;
    }
    const bool hasAht20     = (ENABLE_AHT20_BMP280 == 1 && ENABLE_SENSOR_CONFIG_PAGE == 1);
    const bool hasGas       = (ENABLE_MQ135 == 1);
    const bool hasSensors   = hasAht20 || hasGas;
    const bool hasRS232     = (ENABLE_RS232_CHARTS == 1);
    const bool hasModbus    = (ENABLE_MODBUS == 1 && ENABLE_RS232_CHARTS == 1 && ENABLE_SDCARD == 1);
    const bool hasProximity = (ENABLE_PROXIMITY == 1 && ENABLE_PROXIMITY_CHARTS == 1);
    String json = F("{\"sensors\":");
    json += hasSensors  ? F("true") : F("false");
    json += F(",\"aht20\":");
    json += hasAht20    ? F("true") : F("false");
    json += F(",\"gas\":");
    json += hasGas      ? F("true") : F("false");
    json += F(",\"rs232\":");
    json += hasRS232    ? F("true") : F("false");
    json += F(",\"modbus\":");
    json += hasModbus   ? F("true") : F("false");
    json += F(",\"proximity\":");
    json += hasProximity ? F("true") : F("false");
    json += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

// GET /api/wifi-runtime-status — current runtime WiFi state (disabled flag + connection)
static void ethernetHandleWifiRuntimeStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiRuntimeStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/wifi-disable — stop WiFi immediately and persist the choice
static void ethernetHandleWifiDisablePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiDisablePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    // Platform-specific: immediately stop WiFi driver (no timer needed outside async context)
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    #if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
    esp_wifi_stop();
    #endif
}

// POST /api/wifi-enable — clear the disable flag; reboot required to re-init WiFi driver
static void ethernetHandleWifiEnablePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiEnablePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/access-config - lightweight read of access/web settings
static void ethernetHandleAccessConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleAccessConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/session-history
static void ethernetHandleSessionHistoryGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSessionHistoryGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/access-config
static void ethernetHandleAccessConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleAccessConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/access-kick-all
static void ethernetHandleAccessKickAllPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleAccessKickAllPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// --- Modbus handlers ---
#if ENABLE_MODBUS==1

#include "webpages/handlers_unified/modbus_handler.h"

#include "psram_helpers.h"

// GET /api/modbus/config
static void ethernetHandleModbusConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/config
static void ethernetHandleModbusConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleModbusConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/modbus/live
static void ethernetHandleModbusLiveGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusLiveGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/poll-now
static void ethernetHandleModbusPollNowPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
    HttpResponse hresp;
    handleModbusPollNowPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/rtu-write
static void ethernetHandleModbusRtuWritePost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleModbusRtuWritePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/scan-rtu
static void ethernetHandleModbusScanRtuPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
    HttpResponse hresp;
    handleModbusScanRtuPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/modbus/scan-status
static void ethernetHandleModbusScanStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusScanStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/scan-stop
static void ethernetHandleModbusScanStopPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusScanStopPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/modbus/stats
static void ethernetHandleModbusStatsGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusStatsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/reset-stats
static void ethernetHandleModbusResetStatsPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusResetStatsPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/scan-tcp
static void ethernetHandleModbusScanTcpPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    if (body.length()) { hreq.body = body; hreq.contentType = F("application/json"); }
    HttpResponse hresp;
    handleModbusScanTcpPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/modbus/write
static void ethernetHandleModbusWritePost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleModbusWritePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// --- Modbus chart APIs ---
// GET /api/modbus-chart-files
static void ethernetHandleModbusChartFilesGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleModbusChartFilesGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/modbus-chart-cols?date=DD-MM-YYYY
static void ethernetHandleModbusChartColsGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleModbusChartColsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/modbus-chart-data?date=DD-MM-YYYY&from=<epoch>&mode=day|8h|live
static void ethernetHandleModbusChartDataGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleModbusChartDataGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#endif // ENABLE_MODBUS==1

// --- Proximity sensor handlers ---
#if ENABLE_PROXIMITY==1

#include "webpages/handlers_unified/proximity_handler.h"

#include "psram_helpers.h"

// GET /api/proximity
static void ethernetHandleProximityGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/reset?ch=N
static void ethernetHandleProximityResetPost(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleProximityResetPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/reset-all
static void ethernetHandleProximityResetAllPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityResetAllPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity/config
static void ethernetHandleProximityConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/config (JSON body)
static void ethernetHandleProximityConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleProximityConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity/shift-config
static void ethernetHandleProximityShiftConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityShiftConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/shift-config (JSON body)
static void ethernetHandleProximityShiftConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleProximityShiftConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity/shiftday-emails
static void ethernetHandleProximityShiftdayEmailsGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityShiftdayEmailsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/shiftday-emails (JSON body)
static void ethernetHandleProximityShiftdayEmailsPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleProximityShiftdayEmailsPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
// GET /api/proximity/tca9554
static void ethernetHandleProximityTca9554Get(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityTca9554Get(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/proximity/tca9554 (JSON body)
static void ethernetHandleProximityTca9554Post(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleProximityTca9554Post(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

// POST /api/proximity/save-all (JSON body)
static void ethernetHandleProximitySaveAll(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleProximitySaveAllPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity-chart-files
static void ethernetHandleProximityChartFilesGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleProximityChartFilesGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity-chart-cols?date=DD-MM-YYYY
static void ethernetHandleProximityChartColsGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleProximityChartColsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/proximity-chart-data?date=DD-MM-YYYY&from=<epoch>&mode=day|8h|live
static void ethernetHandleProximityChartDataGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleProximityChartDataGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#endif // ENABLE_PROXIMITY==1

// --- Digital output (TCA9554) handlers ---
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1

// GET /api/do/status — returns JSON with current state of all TCA9554 output channels
static void ethernetHandleDoStatusGet(EthernetClient& client) {
    uint8_t mask = tca9554GetMask();
    char buf[128];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "{\"ch_count\":%d,\"mask\":%u,\"outputs\":[",
                    TCA9554_DO_CHANNELS, (unsigned)mask);
    for (int i = 0; i < TCA9554_DO_CHANNELS; i++) {
        if (i > 0) buf[pos++] = ',';
        const char* v = (mask & (1 << i)) ? "true" : "false";
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", v);
    }
    if (pos < (int)sizeof(buf) - 3) { buf[pos++] = ']'; buf[pos++] = '}'; buf[pos] = '\0'; }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(buf));
}

// POST /api/do/set — set one channel {"ch":N,"state":true|false} or all {"mask":M}
static void ethernetHandleDoSetPost(EthernetClient& client, const String& body) {
    SpiRamJsonDocument doc(64);
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json",
            L(activeLang->apiJsonInvalid));
        return;
    }
    bool ok = false;
    if (doc.containsKey("mask")) {
        uint8_t mask = (uint8_t)(doc["mask"] | 0);
        ok = tca9554SetAll(mask);
    } else if (doc.containsKey("ch")) {
        uint8_t ch    = (uint8_t)(doc["ch"]    | 0);
        bool    state = (bool)(doc["state"]     | false);
        ok = tca9554SetOutput(ch, state);
    } else {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json",
            F("{\"success\":false,\"message\":\"Falta ch ou mask\"}"));
        return;
    }
    if (ok) {
        tca9554SaveState();
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            F("{\"success\":true,\"message\":\"OK\"}"));
    } else {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json",
            F("{\"success\":false,\"message\":\"I2C NACK\"}"));
    }
}

// POST /api/do/config — {"names":[...8],"invert":N}
static void ethernetHandleDoConfigPost(EthernetClient& client, const String& body) {
    SpiRamJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json",
            L(activeLang->apiJsonInvalid));
        return;
    }
    JsonVariant doSec = configManager.getSection("do");
    if (!doSec.is<JsonObject>()) doSec.to<JsonObject>();

    uint8_t invert = (uint8_t)(doc["invert"] | (int)0);
    doSec["invert"] = invert;

    JsonArray names = doc["names"].as<JsonArray>();
    if (!names.isNull()) {
        for (int i = 0; i < TCA9554_DO_CHANNELS; i++) {
            String nm = (i < (int)names.size()) ? (String)(names[i] | "") : String("");
            if (nm.length() > 16) nm = nm.substring(0, 16);
            doSec["names"][i] = nm;
        }
    }

    if (!configManager.saveSection("do")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json",
            F("{\"success\":false,\"message\":\"Save failed\"}"));
        return;
    }
    tca9554SetInvert(invert); // apply live, no reboot needed
    sendVirtualMessage(F("DO: config saved via web"), INFO, false, true);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        F("{\"success\":true,\"message\":\"OK\"}"));
}

#endif // BOARD_HAS_TCA9554_DO

// Serve active language pack JSON for JS i18n (data-i18n attribute replacement).
// Streams PROGMEM data in chunks to avoid a large stack buffer.
static String ethernetLangNormalizeEtagToken(String token) {
    token.trim();
    if (token.startsWith("W/")) token = token.substring(2);
    token.trim();
    if (token.length() >= 2 && token.startsWith("\"") && token.endsWith("\"")) {
        token = token.substring(1, token.length() - 1);
    }
    token.trim();
    return token;
}

static bool ethernetLangIfNoneMatchContainsEtag(const String& ifNoneMatch, const String& expected) {
    if (expected.length() == 0) return false;
    String raw = ifNoneMatch;
    raw.trim();
    if (raw.length() == 0) return false;
    if (raw == "*") return true;

    int start = 0;
    while (start < raw.length()) {
        int comma = raw.indexOf(',', start);
        String token = (comma < 0) ? raw.substring(start) : raw.substring(start, comma);
        token = ethernetLangNormalizeEtagToken(token);
        if (token == expected) return true;
        if (comma < 0) break;
        start = comma + 1;
    }
    return false;
}

static void ethernetHandleLangGet(EthernetClient& client, const String& ifNoneMatchHeader) {
    String langEtag = String(FIRMWARE_BUILD) + String("|lang|") + String(activeLangCode);
    if (ethernetLangIfNoneMatchContainsEtag(ifNoneMatchHeader, langEtag)) {
        ethernetBootstrapSendNotModified(client, langEtag.c_str(), "private, max-age=300, must-revalidate");
        return;
    }

    const char* jsonPtr = activeLangUiJson;
    const size_t totalSize = strlen_P(jsonPtr);
    String header = F("HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: private, max-age=300, must-revalidate\r\nContent-Language: ");
    header += activeLangCode;
    header += F("\r\nETag: ");
    header += langEtag;
    header += F("\r\nX-UI-Lang: ");
    header += activeLangCode;
    header += F("\r\nContent-Length: ");
    header += totalSize;
    header += F("\r\nConnection: close\r\n\r\n");
    client.print(header);
    // Stream PROGMEM in 256-byte chunks
    const size_t CHUNK = 256;
    char buf[CHUNK];
    size_t offset = 0;
    while (offset < totalSize) {
        size_t len = (totalSize - offset < CHUNK) ? (totalSize - offset) : CHUNK;
        memcpy_P(buf, jsonPtr + offset, len);
        client.write((uint8_t*)buf, len);
        offset += len;
    }
}

static void ethernetSendSavedConfigs(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleConfigsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetSendUnifiedConfig(EthernetClient& client, const String& query = "") {
    HttpRequest hreq;
    hreq.body = query;
    hreq.contentType = F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleUnifiedConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetSendTelnetStatus(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleTelnetStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleTelnetTogglePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleTelnetTogglePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/telnet-config - save Telnet enable state and password from /networks page


static void ethernetSendNvsStats(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleNvsStatsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#if ENABLE_OTA==1

#include "ota_security.h"

static void ethernetHandleRestartPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRestartPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    performRestartActions();
}

static void ethernetHandleFactoryResetPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleFactoryResetPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    delay(250);
    performFactoryResetActions();
}

static void ethernetHandleFirmwareUpload(EthernetClient& client, int contentLength, const String& filenameHeader, const String& filesizeHeader) {
    if (otaEnabledUntil == 0 || (long)(otaEnabledUntil - millis()) <= 0) {
        String otaMsgEth = String(L(activeLang->apiOtaNotEnabled));
        otaMsgEth.replace("{btn}", L(activeLang->ethBtnRefresh));
        ethernetBootstrapSendResponse(client, "403 Forbidden", "application/json", (String(F("{\"success\":false,\"message\":\"")) + otaMsgEth + "\"}"));
        return;
    }

    String filename = ethernetUrlDecode(filenameHeader);
    if (filename.length() == 0) {
        filename = F("firmware.bin");
    }
    filename.replace("\\", "_");
    filename.replace("/", "_");
    filename.replace("\"", "_");
    filename.replace("'", "_");
    if (!filename.endsWith(F(".bin"))) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiOtaErrFileType)));
        return;
    }

    size_t totalSize = 0;
    if (filesizeHeader.length()) {
        totalSize = (size_t)strtoul(filesizeHeader.c_str(), nullptr, 10);
    }
    if (totalSize == 0 && contentLength > 0) {
        totalSize = (size_t)contentLength;
    }
    if (totalSize > 0 && totalSize > (size_t)ESP.getFlashChipSize()) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiOtaErrTooLarge)));
        return;
    }
    if (totalSize > 0 && totalSize < 260) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiOtaErrTooSmall)));
        return;
    }

    if (Update.isRunning()) {
        Update.abort();
    }

    size_t fwSize = (totalSize > 0) ? totalSize - 260 : 0;
    bool begun = (fwSize > 0) ? Update.begin(fwSize) : Update.begin();
    if (!begun) {
        String json = (String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrOtaBegin)) + String(Update.getError()) + String(F("\"}"));
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", json);
        return;
    }

    if (totalSize > 0) {
        otaVerifyInit(totalSize);
    }

    // 4 KB OTA buffer: lazy PSRAM allocation to avoid 4KB permanently in internal BSS.
    static uint8_t* buffer = nullptr;
    if (!buffer) {
        buffer = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buffer) buffer = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buffer) {
        String json = F("{\"success\":false,\"message\":\"OTA: insufficient memory for write buffer\"}");
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", json);
        return;
    }
    size_t written = 0;
    int remaining = contentLength;
    bool hasError = false;
    String errorMsg;
    unsigned long deadline = millis() + 30000UL;

    while (remaining > 0) {
        int availableBytes = client.available();
        if (availableBytes <= 0) {
            if ((long)(millis() - deadline) > 0) {
                hasError = true;
                errorMsg = F("Timeout a receber firmware");
                break;
            }
            esp_task_wdt_reset();
            continue;
        }

        size_t chunkSize = (size_t)min(remaining, (int)sizeof(buffer));
        int readNow = client.read(buffer, chunkSize);
        if (readNow <= 0) {
            esp_task_wdt_reset();
            continue;
        }
        deadline = millis() + 30000UL;

        // Feed ALL bytes to verification (hashes fw + magic, buffers magic + sig)
        if (totalSize > 0) {
            otaVerifyFeed(buffer, (size_t)readNow);
        }

        // Write only firmware bytes (strip last 260 from flash)
        size_t toWrite = (size_t)readNow;
        if (fwSize > 0) {
            size_t fwRemaining = (written < fwSize) ? fwSize - written : 0;
            toWrite = ((size_t)readNow < fwRemaining) ? (size_t)readNow : fwRemaining;
        }
        size_t justWritten = Update.write(buffer, toWrite);
        written += justWritten;
        remaining -= readNow;
        esp_task_wdt_reset();
        if (justWritten != toWrite) {
            hasError = true;
            errorMsg = L(activeLang->apiOtaWriteFail);
            break;
        }
    }

    if (hasError || remaining != 0) {
        Update.abort();
        if (!errorMsg.length()) {
            errorMsg = F("Upload incompleto");
        }
        String json = String(F("{\"success\":false,\"message\":\"")) + errorMsg + String(F(" | Update.error=")) + String(Update.getError()) + String(F("\"}"));
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", json);
        return;
    }

    // Verify signature (if we have a totalSize)
    if (totalSize > 0) {
        bool sigValid = otaVerifyEnd();
        if (!sigValid) {
            Update.abort();
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json",
                String(L(activeLang->apiOtaErrSignature)));
            return;
        }
    }

    if (!Update.end(true)) {
        String json = String(F("{\"success\":false,\"message\":\"Update.end falhou | Update.error=")) + String(Update.getError()) + String(F("\"}"));
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", json);
        return;
    }

    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(L(activeLang->apiOtaDoneRestarting)));
    delay(150);
    #if ENABLE_SDCARD==1
    debugWriterFlush();
    delay(100);
    flushSdcardNowBlocking("OTA_REBOOT", 5000);
    #endif
    safeReboot("OTA Success");
}

static void ethernetStreamOtaPage(EthernetClient& client) {
    streamSharedOtaPage(client, true);
}
#endif

static bool ethernetHasSavedNetwork(const String& ssid) {
#if ENABLE_WIFI==1
    JsonArray networks = configManager.getSavedNetworks();
    for (JsonObject network : networks) {
        if (ssid == (network["ssid"] | "")) {
            return true;
        }
    }
#else
    (void)ssid;
#endif
    return false;
}

static void ethernetHandleSavedNetworksGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSavedNetworksGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSavedNetworksPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleSavedNetworksPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSavedNetworksDelete(EthernetClient& client, const String& body, const String& query) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    // Also pass query params as a body extension for the unified handler to parse
    if (query.length() > 0) {
        if (hreq.body.length() > 0 && hreq.body[0] == '{') {
            // JSON body with query param — pass query as-is for handler to extract
        } else {
            if (hreq.body.length() > 0 && !hreq.body.endsWith("&")) hreq.body += "&";
            hreq.body += query;
        }
    }
    HttpResponse hresp;
    handleSavedNetworksDelete(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleAccessPointGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleAccessPointConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleAccessPointPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleAccessPointConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleWifiIntelligentStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiIntelligentStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleWifiReconnectPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiReconnectPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleWifiSwitchNetworkPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = (body.length() > 0 && body[0] == '{') ? F("application/json") : F("application/x-www-form-urlencoded");
    HttpResponse hresp;
    handleWifiSwitchNetworkPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleWifiForceApPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiForceApPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleWifiDisableApPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiDisableApPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleClearApLockPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleClearApLockPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#if ENABLE_SDCARD==1 || ENABLE_FILESYSTEM==1
struct EthernetFileEntry {
    String name;
    String fullPath;
    bool isDir;
    uint64_t size;
    time_t lastWrite;
};

static const size_t ETHERNET_FILES_MAX_SCAN_ENTRIES = 1200;

static String ethernetFilesStorageLabel(const String& storageType) {
    if (storageType == F("flash")) return L(activeLang->ethStorageFlash);
    if (storageType == F("psram")) return L(activeLang->ethStoragePsram);
    return L(activeLang->ethStorageSd);
}

static String ethernetFilesResolveStorage(const String& query) {
    String storageType = ethernetGetQueryParam(query, "storage");
    storageType.toLowerCase();
    bool storageSupported = false;
#if ENABLE_SDCARD==1
    if (storageType == F("sd")) storageSupported = true;
#endif
#if ENABLE_FILESYSTEM==1
    if (storageType == F("flash")) storageSupported = true;
#endif
    if (storageType == F("psram") && (psramFound() || psramVFSIsEnabled())) storageSupported = true;
    if (storageSupported) {
        return storageType;
    }
#if ENABLE_SDCARD==1
    if (sdcardIsMounted()) return F("sd");
#endif
#if ENABLE_FILESYSTEM==1
    littlefsBegin();
#endif
    if (psramFound() && psramVFSIsEnabled()) return F("psram");
#if ENABLE_FILESYSTEM==1
    if (littlefsMounted()) return F("flash");
    return F("flash");
#elif ENABLE_SDCARD==1
    return F("sd");
#else
    return F("psram");
#endif
}

static String ethernetFilesNormalizePath(String path) {
    if (path.length() == 0) path = F("/");
    path.replace('\\', '/');
    if (!path.startsWith("/")) path = String('/') + path;
    path = SDSecurity::normalizePath(path);
    if (!SDSecurity::isPathSafe(path)) return F("/");
    if (path.length() == 0) return F("/");
    return path;
}

static bool ethernetFilesResolveStorageState(const String& storageType, bool& usesFlash, bool& usesPsram, fs::FS*& activeFs, bool& mounted, uint64_t& totalBytes, uint64_t& usedBytes) {
    usesFlash = (storageType == F("flash"));
    usesPsram = (storageType == F("psram"));
    activeFs = nullptr;
    mounted = false;
    totalBytes = 0;
    usedBytes = 0;

    if (usesPsram) {
        mounted = psramFound() && psramVFSIsEnabled();
        if (mounted) {
            totalBytes = PSRAM_VFS_BUFFER_SIZE;
            usedBytes = psramVFSGetUsedBytes();
        }
        return mounted;
    }

    if (usesFlash) {
#if ENABLE_FILESYSTEM==1
        mounted = littlefsBegin() && littlefsMounted();
        if (mounted) {
            activeFs = &getLittleFS();
            totalBytes = littlefsTotalBytes();
            usedBytes = littlefsUsedBytes();
        }
#endif
        return mounted;
    }

#if ENABLE_SDCARD==1
    mounted = sdcardIsMounted();
    if (mounted) {
        // Use SD_MMC (native SDMMC object) in SDMMC mode, or the SPI SD object in SPI mode.
        // In SDMMC mode sd_lib.h does NOT alias SD_MMC to SD, so using &SD here would point
        // to the uninitialized SPI SD library and every open() would fail with
        // "File system is not mounted" even though the SDMMC card is correctly mounted.
        activeFs = &SD_MMC;
        totalBytes = sdcardTotalBytes();
        usedBytes = sdcardUsedBytes();
    }
#endif
    return mounted;
}

static void ethernetFilesCollectEntries(const String& currentPath, bool usesPsram, fs::FS* activeFs, std::vector<EthernetFileEntry>& entries, bool& scanTruncated) {
    entries.clear();
    scanTruncated = false;
    if (usesPsram) {
        if (currentPath == F("/")) {
            std::vector<String> folders = psramVFSListFolders();
            for (const String& folder : folders) {
                EthernetFileEntry entry;
                entry.name = folder;
                entry.fullPath = String('/') + folder;
                entry.isDir = true;
                entry.size = 0;
                entry.lastWrite = 0;
                entries.push_back(entry);
                if (entries.size() >= ETHERNET_FILES_MAX_SCAN_ENTRIES) {
                    scanTruncated = true;
                    break;
                }
            }
        } else {
            std::vector<String> files = psramVFSListFiles(currentPath);
            for (const String& filename : files) {
                EthernetFileEntry entry;
                entry.name = filename;
                entry.fullPath = currentPath;
                if (!entry.fullPath.endsWith("/")) entry.fullPath += '/';
                entry.fullPath += filename;
                entry.isDir = false;
                entry.size = psramVFSGetFileSize(currentPath, filename);
                entry.lastWrite = 0;
                entries.push_back(entry);
                if (entries.size() >= ETHERNET_FILES_MAX_SCAN_ENTRIES) {
                    scanTruncated = true;
                    break;
                }
            }
        }
    } else if (activeFs) {
        File dir = activeFs->open(currentPath.c_str());
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            return;
        }
        File file = dir.openNextFile();
        while (file) {
            EthernetFileEntry entry;
            entry.name = String(file.name());
            int slash = entry.name.lastIndexOf('/');
            if (slash >= 0) entry.name = entry.name.substring(slash + 1);
            entry.fullPath = currentPath;
            if (!entry.fullPath.endsWith("/")) entry.fullPath += '/';
            entry.fullPath += entry.name;
            entry.isDir = file.isDirectory();
            entry.size = file.size();
            entry.lastWrite = file.getLastWrite();
            entries.push_back(entry);
            file.close();
            if (entries.size() >= ETHERNET_FILES_MAX_SCAN_ENTRIES) {
                scanTruncated = true;
                break;
            }
            file = dir.openNextFile();
            yield();
        }
        dir.close();
    }

    std::sort(entries.begin(), entries.end(), [](const EthernetFileEntry& a, const EthernetFileEntry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name.compareTo(b.name) < 0;
    });
}

static void ethernetFilesSendRedirect(EthernetClient& client, const String& storageType, const String& path) {
    String location = String(F("/files?storage=")) + ethernetUrlEncode(storageType) + String(F("&path=")) + ethernetUrlEncode(path);
    client.println(F("HTTP/1.1 302 Found"));
    client.print(F("Location: "));
    client.println(location);
    client.println(F("Connection: close"));
    client.println(F("Content-Length: 0"));
    client.println();
}

static void ethernetStreamFilesPage(Print& out, const String& query) {
    Print& client = out;
    String storageType = ethernetFilesResolveStorage(query);
    String currentPath = ethernetFilesNormalizePath(ethernetGetQueryParam(query, "path"));
    String parentPath = F("/");
    if (currentPath != F("/")) {
        int slash = currentPath.lastIndexOf('/');
        parentPath = (slash > 0) ? currentPath.substring(0, slash) : String(F("/"));
    }

    bool usesFlash = false;
    bool usesPsram = false;
    bool mounted = false;
    fs::FS* activeFs = nullptr;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    ethernetFilesResolveStorageState(storageType, usesFlash, usesPsram, activeFs, mounted, totalBytes, usedBytes);

    std::vector<EthernetFileEntry> entries;
    bool scanTruncated = false;
    if (mounted) {
        ethernetFilesCollectEntries(currentPath, usesPsram, activeFs, entries, scanTruncated);
    }

    int page = ethernetGetQueryParam(query, "p").toInt();
    if (page < 0) page = 0;
    const int PAGE_SIZE = 10;
    int totalCount = (int)entries.size();
    int startIndex = page * PAGE_SIZE;
    int endIndex   = startIndex + PAGE_SIZE;
    // Clamp to first page if the requested page is out of range
    if (startIndex >= totalCount && totalCount > 0) { page = 0; startIndex = 0; endIndex = PAGE_SIZE; }

    #define EFILES(s) client.print(s)

    // ----- HTML head -----
    EFILES("<!DOCTYPE html><html lang='pt'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='Cache-Control' content='no-cache,must-revalidate'><title>&#128193; Explorador</title><link rel='stylesheet' href='/core.css'></head><body><main class='page'><div class='wrap'>");

    // Home button (mandatory first child of .wrap)
    EFILES("<div class='actions nav-inicio'><a href='/' class='btn'>&#127968; In&iacute;cio</a></div>");
    EFILES("<h1>&#128193; Explorador de Ficheiros</h1>");

    // Shortcut hint bar
    EFILES("<div style='background:#064e3b;border-left:4px solid #10b981;border-radius:4px;padding:8px 12px;font-size:12px;color:#d1fae5;margin:12px 0'>&#128161; <strong>Ctrl+A:</strong> Selecionar tudo | <strong>Delete:</strong> Apagar sele&ccedil;&atilde;o | <strong>Esc:</strong> Limpar</div>");

    // ----- Storage stats section -----
    EFILES("<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px'>");
    if (usesPsram) {
        EFILES("<h2 style='margin-top:0;font-size:18px;color:#60a5fa'>&#129504; "); EFILES(L(activeLang->ethPsramSpace)); EFILES("</h2>");
    } else if (usesFlash) {
        EFILES("<h2 style='margin-top:0;font-size:18px;color:#60a5fa'>&#9889; "); EFILES(L(activeLang->ethFlashSpace)); EFILES("</h2>");
    } else {
        EFILES("<h2 style='margin-top:0;font-size:18px;color:#60a5fa'>&#128190; Espa&ccedil;o no Cart&atilde;o SD</h2>");
    }
    if (mounted) {
        uint64_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        float pct = (totalBytes > 0) ? ((float)usedBytes * 100.0f / (float)totalBytes) : 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        unsigned int pctInt = (unsigned int)(pct + 0.5f);
        client.print(F("<div style='display:grid;grid-template-columns:auto auto auto;gap:16px;font-size:13px;margin-bottom:12px'>"));
        client.print(F("<div><strong style='color:#60a5fa'>&#128190; Total:</strong> ")); client.print(ethernetFormatBytes(totalBytes)); client.print(F("</div>"));
        client.print(F("<div><strong style='color:#60a5fa'>&#128202; Usado:</strong> ")); client.print(ethernetFormatBytes(usedBytes)); client.print(F("</div>"));
        client.print(F("<div><strong style='color:#60a5fa'>&#128154; Livre:</strong> ")); client.print(ethernetFormatBytes(freeBytes)); client.print(F("</div>"));
        client.print(F("</div>"));
        client.print(F("<div style='width:100%;height:14px;border:1px solid #3b82f6;border-radius:6px;overflow:hidden;background:#0a1929'><div style='height:100%;width:"));
        client.print(pctInt);
        client.print(F("%;background:linear-gradient(90deg,#2563eb,#3b82f6)'></div></div>"));
        client.print(F("<div style='font-size:11px;color:#94a3b8;margin-top:4px'>"));
        if (pct < 0.01f && usedBytes > 0) { client.print(F("&lt; 0.01")); } else { client.print(pct, 2); }
        client.print(F("% utilizado</div>"));
    } else {
        EFILES("<div style='padding:12px;background:#7f1d1d;border-left:4px solid #dc2626;border-radius:4px;color:#fecaca;margin-top:8px'>&#9888;&#65039; Armazenamento n&atilde;o montado ou indispon&iacute;vel</div>");
    }
    EFILES("</div>"); // close storage section

    // ----- Files / Folders section -----
    EFILES("<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px'>");
    EFILES("<h2 style='margin-top:0;font-size:18px;color:#60a5fa'>&#128193; Ficheiros e Pastas</h2>");

    // Filter input
    EFILES("<div style='margin-bottom:10px'><input type='text' id='filterInput' class='filter-input' placeholder='&#128269; Filtrar ficheiros/pastas' onkeyup='filterFiles(this.value)'></div>");

    // Breadcrumb navigation
    EFILES("<div style='font-size:12px;background:#0a1929;border:1px solid #3b82f6;border-radius:6px;padding:6px 10px;margin-bottom:10px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap'>");
    client.print(F("<a href='/files?storage=")); client.print(ethernetUrlEncode(storageType)); client.print(F("&path=%2F' style='color:#60a5fa;text-decoration:none'>/</a>"));
    if (currentPath != F("/")) {
        String acc = F("/");
        String remaining = currentPath.substring(1);
        while (remaining.length() > 0) {
            int sl = remaining.indexOf('/');
            String part = (sl == -1) ? remaining : remaining.substring(0, sl);
            remaining = (sl == -1) ? String() : remaining.substring(sl + 1);
            if (part.length() == 0) break;
            acc += part;
            client.print(F(" / <a style='color:#60a5fa;text-decoration:none' href='/files?storage="));
            client.print(ethernetUrlEncode(storageType));
            client.print(F("&path=")); client.print(ethernetUrlEncode(acc));
            client.print(F("'>")); client.print(ethernetHtmlEscape(part)); client.print(F("</a>"));
            acc += '/';
        }
    }
    EFILES("</div>");

    // Storage selector (inside Files section, matching WiFi layout)
    EFILES("<div style='margin-bottom:10px;padding:8px;background:#0a1929;border:1px solid #3b82f6;border-radius:6px'>");
    EFILES("<label style='font-size:13px;color:#94a3b8;margin-right:8px'>&#128190; "); EFILES(L(activeLang->ethStorageLabel)); EFILES("</label>");
    EFILES("<select id='storage-select' onchange='switchStorage()' style='padding:4px 8px;border:1px solid #3b82f6;border-radius:4px;background:#0f172a;color:#e0e7ff;font-size:12px;cursor:pointer'>");
#if ENABLE_SDCARD==1
    client.print(F("<option value='sd'")); if (storageType == F("sd")) client.print(F(" selected")); client.print(F(">&#128190; SD Card</option>"));
#endif
    if (psramFound() || psramVFSIsEnabled()) {
        client.print(F("<option value='psram'")); if (storageType == F("psram")) client.print(F(" selected")); client.print(F(">&#129504; ")); client.print(L(activeLang->ethStoragePsram)); client.print(F("</option>"));
    }
#if ENABLE_FILESYSTEM==1
    bool flashReadyForSelector = littlefsBegin() && littlefsMounted();
    client.print(F("<option value='flash'")); if (storageType == F("flash")) client.print(F(" selected")); client.print(F(">&#9889; Flash (LittleFS)"));
    if (!flashReadyForSelector) client.print(F(" (n&atilde;o montada)"));
    client.print(F("</option>"));
#endif
    EFILES("</select>");
    EFILES("</div>");

    // Action button grid - 4 columns matching WiFi layout
    EFILES("<div style='display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:6px;margin-bottom:10px'>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#2563eb;border:1px solid #2563eb' onclick='selectAll()'>&#9989; Selecionar</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#1e40af;border:1px solid #1e40af' onclick='clearAll()'>&#9746; Limpar</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#7c3aed;border:1px solid #7c3aed' onclick='sortByType()'>&#128260; Ordenar</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#3b82f6;border:1px solid #3b82f6' onclick='(function(){var url=new URL(window.location);url.searchParams.set(\"_t\",Date.now());window.location.href=url.toString();})()'>&#128260; Atualizar</button>");
    // Back button (form GET)
    client.print(F("<form method='GET' action='/files' style='margin:0'><input type='hidden' name='storage' value='"));
    client.print(ethernetHtmlEscape(storageType));
    client.print(F("'><input type='hidden' name='path' value='"));
    client.print(ethernetHtmlEscape(parentPath));
    client.print(F("'><button class='btn' style='width:100%;padding:6px 12px;font-size:12px;background:#1e40af;border:1px solid #1e40af'>&#11013;&#65039; Voltar</button></form>"));
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#dc2626;border:1px solid #dc2626' onclick='deleteSelectedFiles()'>&#128465;&#65039; Apagar</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#065f46;border:1px solid #065f46' onclick='createNewFolder()'>&#128193; Nova Pasta</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#7c3aed;border:1px solid #7c3aed' onclick='renameSelected()'>&#9999;&#65039; Renomear</button>");
    EFILES("</div>");
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
    EFILES("<div style='display:grid;grid-template-columns:1fr 1fr 1fr 1fr 1fr;gap:6px;margin:0 0 10px 0;padding:10px;border:1px solid #0ea5e9;border-radius:8px;background:#0b2540'>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#0891b2;border:1px solid #0891b2' onclick='radioPlaySelected()'>&#9654; Play</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#0e7490;border:1px solid #0e7490' onclick='radioStopPreview()'>&#9632; Stop</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#1d4ed8;border:1px solid #1d4ed8' onclick='radioRandomSelected()'>&#128256; Random</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#4338ca;border:1px solid #4338ca' onclick='radioNextSelected()'>&#9197; Next</button>");
    EFILES("<button class='btn' style='padding:6px 12px;font-size:12px;background:#0284c7;border:1px solid #0284c7' onclick='openRadioUi()'>&#128251; UI Radio</button>");
    EFILES("</div>");
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
    EFILES("<div style='font-size:12px;color:#93c5fd;margin:4px 0 12px 0'>Internal player active on ESP32 (UDA1334A output)</div>");
#endif

    // Compact table CSS (matches WiFi)
    EFILES("<style>body{overflow-x:hidden}#files-table{width:100%;table-layout:fixed}#files-table td{padding:0 1px;font-size:12px;height:16px;line-height:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#files-table td a{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}#files-table tr:nth-child(even){background:#042743}#files-table a{color:#fff!important;text-decoration:none}#files-table th{padding:0 1px;font-size:14px;font-weight:700;height:18px;line-height:18px;background:#0a2540}.file-row{cursor:pointer}.file-row.selected{background:#1d5c92!important}.filter-input{width:100%;padding:3px 6px;margin:3px 0;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;font-size:12px}input[type='checkbox']{width:14px;height:14px;cursor:pointer}.dl-link{color:#7dd3fc!important;background:rgba(59,130,246,0.08);border-radius:3px;padding:0 2px}</style>");

    // Table - 4 cols: checkbox / Tipo / Nome / Tamanho (no Acoes column)
    client.print(F("<div style='overflow-x:auto'><table id='files-table' class='table' style='width:100%;border-collapse:collapse' data-cwd='"));
    client.print(ethernetHtmlEscape(currentPath));
    client.print(F("' data-storage='"));
    client.print(ethernetHtmlEscape(storageType));
    client.print(F("'><thead><tr>"));
    client.print(F("<th style='width:5%;padding:0 1px'><input type='checkbox' id='selectAll' onchange='toggleSelectAll(this)'></th>"));
    client.print(F("<th style='width:8%;font-size:10px;white-space:nowrap'>Tipo</th>"));
    client.print(F("<th>Nome</th>"));
    client.print(F("<th style='width:20%;font-size:10px;white-space:nowrap'>Tamanho</th>"));
    client.print(F("</tr></thead><tbody>"));

    if (!mounted) {
        EFILES("<tr><td colspan='4' style='text-align:center;color:#94a3b8'>"); EFILES(L(activeLang->ethStorageUnavailable)); EFILES("</td></tr>");
    } else if (entries.empty()) {
        EFILES("<tr><td colspan='4' style='text-align:center;color:#94a3b8'>Sem ficheiros ou pastas</td></tr>");
    } else {
        for (int _ei = startIndex; _ei < endIndex && _ei < totalCount; _ei++) {
            const EthernetFileEntry& entry = entries[_ei];
            String fnameLower = entry.name;
            fnameLower.toLowerCase();
            const char* ext = "&#128196;";
            if (entry.isDir) ext = "&#128193;";
            else if (fnameLower.endsWith(".txt"))  ext = "&#128221;";
            else if (fnameLower.endsWith(".pdf"))  ext = "&#128213;";
            else if (fnameLower.endsWith(".zip") || fnameLower.endsWith(".rar")) ext = "&#128284;";
            else if (fnameLower.endsWith(".jpg") || fnameLower.endsWith(".png") || fnameLower.endsWith(".gif")) ext = "&#128444;";
            else if (fnameLower.endsWith(".csv") || fnameLower.endsWith(".xlsx")) ext = "&#128202;";

            // Row with data attributes for JS sort/filter
            client.print(F("<tr class='file-row' data-name='"));
            client.print(ethernetHtmlEscape(entry.name));
            client.print(F("' data-isdir='"));
            client.print(entry.isDir ? F("1") : F("0"));
            client.print(F("' data-last='"));
            client.print((uint32_t)entry.lastWrite);
            client.print(F("'>"));

            // Checkbox
            client.print(F("<td style='text-align:center'><input type='checkbox' class='file-checkbox' value='"));
            client.print(ethernetHtmlEscape(entry.fullPath));
            client.print(F("' onchange='onCheckboxChange(this)'></td>"));

            // Tipo icon
            client.print(F("<td class='file-type-cell'>"));
            client.print(ext);
            client.print(F("</td>"));

            // Nome (directory ? browse link; file ? download dl-link)
            client.print(F("<td data-filter='"));
            client.print(ethernetHtmlEscape(entry.name));
            client.print(F("'>"));
            if (entry.isDir) {
                client.print(F("<a href='/files?storage="));
                client.print(ethernetUrlEncode(storageType));
                client.print(F("&path=")); client.print(ethernetUrlEncode(entry.fullPath));
                client.print(F("'>")); client.print(ethernetHtmlEscape(entry.name)); client.print(F("</a>"));
            } else {
                client.print(F("<a class='dl-link' href='/api/sd/download?storage="));
                client.print(ethernetUrlEncode(storageType));
                client.print(F("&path=")); client.print(ethernetUrlEncode(entry.fullPath));
                client.print(F("' download='"));
                client.print(ethernetHtmlEscape(entry.name));
                client.print(F("'>")); client.print(ethernetHtmlEscape(entry.name)); client.print(F("</a>"));
            }
            client.print(F("</td>"));

            // Tamanho
            client.print(F("<td>"));
            client.print(entry.isDir ? String(F("&#8212;")) : ethernetFormatBytes(entry.size));
            client.print(F("</td>"));
            client.print(F("</tr>"));
        }
    }
    EFILES("</tbody></table></div>");

    // Pagination controls (mirrors WiFi files_page_handler.cpp)
    {
        int totalPages = (totalCount + PAGE_SIZE - 1) / PAGE_SIZE;
        if (totalPages < 1) totalPages = 1;
        client.print(F("<div style='text-align:center;margin:30px 0 20px'>"));
        client.print(F("<div style='display:inline-block;background:#1e293b;border:2px solid #3b82f6;border-radius:6px;padding:10px 20px;margin-bottom:15px'>"));
        client.print(F("<span style='color:#94a3b8;font-size:13px;font-weight:600'>P&#193;GINA </span>"));
        client.print(F("<span style='color:#60a5fa;font-size:18px;font-weight:700'>"));
        client.print(page + 1);
        client.print(F("</span><span style='color:#475569;font-size:13px;margin:0 6px'>/</span>"));
        client.print(F("<span style='color:#94a3b8;font-size:16px;font-weight:600'>"));
        client.print(totalPages);
        client.print(F("</span></div>"));
        if (totalPages > 1) {
            String encSt  = ethernetUrlEncode(storageType);
            String encPth = ethernetUrlEncode(currentPath);
            client.print(F("<div style='display:flex;justify-content:center;gap:8px'>"));
            if (page > 0) {
                client.print(F("<a href='/files?storage=")); client.print(encSt); client.print(F("&path=")); client.print(encPth);
                client.print(F("&p=0' style='display:inline-block;background:#1e40af;color:#fff;padding:8px 16px;border-radius:6px;text-decoration:none;font-size:14px;font-weight:600'>&#9198; Primeira</a>"));
                client.print(F("<a href='/files?storage=")); client.print(encSt); client.print(F("&path=")); client.print(encPth);
                client.print(F("&p=")); client.print(page - 1);
                client.print(F("' style='display:inline-block;background:#1e40af;color:#fff;padding:8px 16px;border-radius:6px;text-decoration:none;font-size:14px;font-weight:600'>&#9664; Anterior</a>"));
            } else {
                client.print(F("<span style='display:inline-block;background:#1e293b;color:#475569;padding:8px 16px;border-radius:6px;font-size:14px;font-weight:600;cursor:not-allowed'>&#9198; Primeira</span>"));
                client.print(F("<span style='display:inline-block;background:#1e293b;color:#475569;padding:8px 16px;border-radius:6px;font-size:14px;font-weight:600;cursor:not-allowed'>&#9664; Anterior</span>"));
            }
            if (page + 1 < totalPages) {
                client.print(F("<a href='/files?storage=")); client.print(encSt); client.print(F("&path=")); client.print(encPth);
                client.print(F("&p=")); client.print(page + 1);
                client.print(F("' style='display:inline-block;background:#1e40af;color:#fff;padding:8px 16px;border-radius:6px;text-decoration:none;font-size:14px;font-weight:600'>Pr&#243;xima &#9654;</a>"));
                client.print(F("<a href='/files?storage=")); client.print(encSt); client.print(F("&path=")); client.print(encPth);
                client.print(F("&p=")); client.print(totalPages - 1);
                client.print(F("' style='display:inline-block;background:#1e40af;color:#fff;padding:8px 16px;border-radius:6px;text-decoration:none;font-size:14px;font-weight:600'>&#218;ltima &#9197;</a>"));
            } else {
                client.print(F("<span style='display:inline-block;background:#1e293b;color:#475569;padding:8px 16px;border-radius:6px;font-size:14px;font-weight:600;cursor:not-allowed'>Pr&#243;xima &#9654;</span>"));
                client.print(F("<span style='display:inline-block;background:#1e293b;color:#475569;padding:8px 16px;border-radius:6px;font-size:14px;font-weight:600;cursor:not-allowed'>&#218;ltima &#9197;</span>"));
            }
            client.print(F("</div>"));
        }
        client.print(F("</div>"));
    }

    // ----- JavaScript -----
    EFILES("<script>");
    EFILES("function filterFiles(t){var rows=document.querySelectorAll('.file-row');var f=t.toLowerCase();rows.forEach(function(r){var n=(r.querySelector('[data-filter]')||{getAttribute:function(){return '';}}).getAttribute('data-filter').toLowerCase();r.style.display=(f===''||n.indexOf(f)!==-1)?'':'none';});}");
    EFILES("function switchStorage(){var s=document.getElementById('storage-select').value;window.location.href='/files?storage='+encodeURIComponent(s)+'&path=%2F';}");
    EFILES("function sortByType(){var tb=document.querySelector('#files-table tbody');if(!tb)return;var rows=Array.from(tb.querySelectorAll('.file-row'));rows.sort(function(a,b){var da=a.getAttribute('data-isdir')==='1';var db=b.getAttribute('data-isdir')==='1';if(da!==db)return da?-1:1;var la=parseInt(a.getAttribute('data-last')||'0',10);var lb=parseInt(b.getAttribute('data-last')||'0',10);if(la!==lb)return lb-la;return (a.getAttribute('data-name')||'').localeCompare(b.getAttribute('data-name')||'');});rows.forEach(function(r){tb.appendChild(r);});}");
    EFILES("var selectedFiles=[];");
    EFILES("function onCheckboxChange(cb){var p=cb.value;if(cb.checked){if(selectedFiles.indexOf(p)<0)selectedFiles.push(p);}else{selectedFiles=selectedFiles.filter(function(x){return x!==p;});}var row=cb.closest('.file-row');if(row){row.classList[cb.checked?'add':'remove']('selected');}updateSelectAllState();}");
    EFILES("function toggleSelectAll(cb){document.querySelectorAll('.file-checkbox').forEach(function(c){c.checked=cb.checked;onCheckboxChange(c);});}");
    EFILES("function selectAll(){var cb=document.getElementById('selectAll');if(cb){cb.checked=true;toggleSelectAll(cb);}}");
    EFILES("function clearAll(){var cb=document.getElementById('selectAll');if(cb){cb.checked=false;toggleSelectAll(cb);}}");
    EFILES("function updateSelectAllState(){var total=document.querySelectorAll('.file-checkbox').length;var checked=document.querySelectorAll('.file-checkbox:checked').length;var cb=document.getElementById('selectAll');if(cb){cb.checked=(total>0&&checked===total);cb.indeterminate=(checked>0&&checked<total);}}");
    EFILES("function postForm(url,data){var f=document.createElement('form');f.method='POST';f.action=url;Object.keys(data).forEach(function(k){var i=document.createElement('input');i.type='hidden';i.name=k;i.value=data[k];f.appendChild(i);});document.body.appendChild(f);f.submit();}");
    EFILES("function deleteSelectedFiles(){if(!selectedFiles||!selectedFiles.length){customAlert('Select files to delete');return;}var storage=document.querySelector('#files-table').getAttribute('data-storage');customConfirm('Delete '+selectedFiles.length+' file(s)?',function(ok){if(!ok)return;fetch('/api/sd/delete-multiple?storage='+encodeURIComponent(storage),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({files:selectedFiles})}).then(function(r){return r.json();}).then(function(d){if(d.success){location.reload();}else{customAlert(d.message||'Error deleting');}}).catch(function(){customAlert('Connection failed');});}); }");
    EFILES("function renameSelected(){if(!selectedFiles||!selectedFiles.length){customAlert('Select 1 file to rename');return;}if(selectedFiles.length>1){customAlert('Select only 1 file to rename');return;}var path=selectedFiles[0];var name=path.split('/').pop();var tbl=document.querySelector('#files-table');var cwd=tbl.getAttribute('data-cwd');var storage=tbl.getAttribute('data-storage');customPrompt('New name for: '+name,name,function(v){if(!v||!v.trim())return;postForm('/api/sd/rename',{cwd:cwd,src:name,dst:v.trim(),storage:storage});});}");
    EFILES("function createNewFolder(){var tbl=document.querySelector('#files-table');var cwd=tbl.getAttribute('data-cwd');var storage=tbl.getAttribute('data-storage');customPrompt('New folder name:','nova_pasta',function(n){if(!n||!n.trim())return;var body='cwd='+encodeURIComponent(cwd)+'&name='+encodeURIComponent(n.trim())+'&storage='+encodeURIComponent(storage)+'&json=1';fetch('/api/sd/create-dir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).then(function(r){return r.json();}).then(function(d){if(d&&d.status==='ok'){location.reload();}else{customAlert((d&&d.msg)||'Error creating folder');}}).catch(function(e){customAlert('Connection error: '+e);});});}");
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
    EFILES("window._radioQueue=[];window._radioIdx=-1;window._radioPlayable=function(){var out=[];(selectedFiles||[]).forEach(function(p){var l=String(p||'').toLowerCase();if(l.endsWith('.mp3'))out.push(p);});return out;};window._radioSetQueue=function(arr){window._radioQueue=(arr||[]).slice(0);window._radioIdx=window._radioQueue.length?0:-1;try{localStorage.setItem('radio_playlist',JSON.stringify(window._radioQueue));}catch(e){}};window._radioApiPost=function(path,params){var q=[];Object.keys(params||{}).forEach(function(k){q.push(encodeURIComponent(k)+'='+encodeURIComponent(params[k]));});return fetch(path+(q.length?('?'+q.join('&')):''),{method:'POST'}).then(function(r){return r.json();});};window._radioApiPostJson=function(path,obj){return fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj||{})}).then(function(r){return r.json();});};window._radioSyncQueue=function(i){var q=window._radioQueue||[];return window._radioApiPostJson('/api/radio/queue',{items:q,index:i});};window._radioPlayAt=function(i){var q=window._radioQueue||[];if(!q.length)return;if(i<0||i>=q.length)i=0;window._radioIdx=i;window._radioSyncQueue(i).then(function(d){if(!d.success)throw new Error(d.message||'failed');return window._radioApiPost('/api/radio/play-sd',{path:q[i]});}).then(function(d){if(!d.success)customAlert('Error: '+(d.message||'failed'));}).catch(function(e){customAlert('Connection error with internal player: '+(e&&e.message?e.message:''));});};window.radioPlaySelected=function(){var q=window._radioPlayable();if(!q.length){customAlert('Select .mp3 files');return;}window._radioSetQueue(q);window._radioPlayAt(0);};window.radioRandomSelected=function(){var q=window._radioPlayable();if(!q.length){customAlert('Select .mp3 files');return;}window._radioSetQueue(q);window._radioPlayAt(Math.floor(Math.random()*q.length));};window.radioNextSelected=function(){var q=window._radioQueue||[];if(!q.length){customAlert('No active queue. Use Play or Random first.');return;}window._radioApiPost('/api/radio/next',{}).then(function(d){if(!d.success){customAlert('Error: '+(d.message||'failed'));return;}var n=window._radioIdx+1;if(n>=q.length)n=0;window._radioIdx=n;}).catch(function(){customAlert('Connection error with internal player');});};window.radioStopPreview=function(){window._radioApiPost('/api/radio/stop',{}).then(function(d){if(!d.success)customAlert('Error: '+(d.message||'failed'));}).catch(function(){customAlert('Connection error with internal player');});};window.openRadioUi=function(){try{if((window._radioQueue||[]).length){localStorage.setItem('radio_playlist',JSON.stringify(window._radioQueue));}}catch(e){}window.location.href='/radio-ui';};");
#endif
    EFILES("document.addEventListener('keydown',function(e){if(e.key==='Delete'){deleteSelectedFiles();}else if(e.ctrlKey&&e.key==='a'){e.preventDefault();selectAll();}else if(e.key==='Escape'){clearAll();var fi=document.getElementById('filterInput');if(fi){fi.value='';filterFiles('');}}});");
    EFILES("</script>");

    client.print(F("<script src='/auto.js?v="));
    client.print(FIRMWARE_VERSION);
    client.print(F("'></script>"));

    EFILES("</div></main></body></html>");
    #undef EFILES
}



static bool ethernetFilesAcquireMutexIfNeeded(const String& storageType) {
#if ENABLE_SDCARD==1
    if (storageType == F("sd")) {
        return acquireSDAccess("eth-files", 8000UL);
    }
#endif
    (void)storageType;
    return true;
}

static void ethernetFilesReleaseMutexIfNeeded(const String& storageType) {
#if ENABLE_SDCARD==1
    if (storageType == F("sd")) {
        releaseSDAccess();
    }
#else
    (void)storageType;
#endif
}





static void ethernetHandleSdDeleteMultiplePost(EthernetClient& client, const String& body, const String& query) {
    String storageType = ethernetGetQueryParam(query, "storage");
    if (storageType.length() == 0) storageType = ethernetGetRequestValue(body, "storage");
    if (storageType.length() == 0) storageType = F("sd");
    if (storageType == F("psram")) {
        ethernetBootstrapSendResponse(client, "501 Not Implemented", "application/json", String(L(activeLang->apiErrPsramUnsupported)));
        return;
    }
    bool usesFlash = false, usesPsram = false, mounted = false;
    fs::FS* activeFs = nullptr;
    uint64_t totalBytes = 0, usedBytes = 0;
    ethernetFilesResolveStorageState(storageType, usesFlash, usesPsram, activeFs, mounted, totalBytes, usedBytes);
    if (!mounted || !activeFs) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json", String(L(activeLang->apiErrNotMounted)));
        return;
    }
    SpiRamJsonDocument doc(4096);
    if (deserializeJson(doc, body) != DeserializationError::Ok || !doc["files"].is<JsonArray>()) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiJsonInvalid)));
        return;
    }
    if (!ethernetFilesAcquireMutexIfNeeded(storageType)) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json", String(F("{\"success\":false,\"message\":\"SD ocupado\"}")));
        return;
    }
    int deleted = 0;
    int errors = 0;
    for (JsonVariant item : doc["files"].as<JsonArray>()) {
        String fullPath = ethernetFilesNormalizePath(String(item.as<const char*>()));
        if (!SDSecurity::isPathSafe(fullPath)) {
            errors++;
            continue;
        }
        File file = activeFs->open(fullPath.c_str());
        bool isDir = file && file.isDirectory();
        if (file) file.close();
        bool ok = isDir ? activeFs->rmdir(fullPath.c_str()) : activeFs->remove(fullPath.c_str());
        if (ok) deleted++; else errors++;
    }
    ethernetFilesReleaseMutexIfNeeded(storageType);
    String json = String(F("{\"success\":")) + (errors == 0 ? F("true") : F("false")) + String(F(",\"deleted\":")) + String(deleted) + String(F(",\"errors\":")) + String(errors) + String(String(F(",\"message\":")) + L(activeLang->apiOpDone) + F("\"}"));
    ethernetBootstrapSendResponse(client, errors == 0 ? "200 OK" : "207 Multi-Status", "application/json", json);
}

static void ethernetHandleSdDownloadGet(EthernetClient& client, const String& query) {
    String storageType = ethernetGetQueryParam(query, "storage");
    if (storageType.length() == 0) storageType = F("sd");
    String fullPath = ethernetFilesNormalizePath(ethernetGetQueryParam(query, "path"));
    if (fullPath.length() == 0 || fullPath == F("/")) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "text/plain", String(F("faltapath")));
        return;
    }

    if (storageType == F("psram")) {
        if (!psramVFSIsEnabled()) {
            ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("storage_nao_montado")));
            return;
        }
        int lastSlash = fullPath.lastIndexOf('/');
        if (lastSlash <= 0 || lastSlash >= (int)fullPath.length() - 1) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "text/plain", String(F("caminho_invalido")));
            return;
        }
        String folder = fullPath.substring(0, lastSlash);
        String filename = fullPath.substring(lastSlash + 1);
        size_t fileSize = psramVFSGetFileSize(folder, filename);
        if (fileSize == 0) {
            ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("nao_existe")));
            return;
        }
        client.println(F("HTTP/1.1 200 OK"));
        client.print(F("Content-Type: "));
        client.println(SDSecurity::getContentType(filename));
        client.print(F("Content-Disposition: attachment; filename=\""));
        client.print(filename);
        client.println(F("\""));
        client.print(F("Content-Length: "));
        client.println((unsigned long)fileSize);
        client.println(F("Connection: close"));
        client.println();
        uint8_t buffer[1024];
        size_t offset = 0;
        while (offset < fileSize) {
            size_t toRead = min((size_t)sizeof(buffer), fileSize - offset);
            size_t bytesRead = psramVFSReadChunk(folder, filename, offset, buffer, toRead);
            if (bytesRead == 0) break;
            client.write(buffer, bytesRead);
            offset += bytesRead;
            yield();
        }
        return;
    }

    bool usesFlash = false, usesPsram = false, mounted = false;
    fs::FS* activeFs = nullptr;
    uint64_t totalBytes = 0, usedBytes = 0;
    ethernetFilesResolveStorageState(storageType, usesFlash, usesPsram, activeFs, mounted, totalBytes, usedBytes);
    if (!mounted || !activeFs) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("storage_nao_montado")));
        return;
    }
    if (!ethernetFilesAcquireMutexIfNeeded(storageType)) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json", String(F("{\"success\":false,\"message\":\"SD ocupado\"}")));
        return;
    }
    File file = activeFs->open(fullPath.c_str());
    if (!file || file.isDirectory()) {
        if (file) file.close();
        ethernetFilesReleaseMutexIfNeeded(storageType);
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("nao_existe")));
        return;
    }
    String filename = fullPath.substring(fullPath.lastIndexOf('/') + 1);
    client.println(F("HTTP/1.1 200 OK"));
    client.print(F("Content-Type: "));
    client.println(SDSecurity::getContentType(fullPath));
    client.print(F("Content-Disposition: attachment; filename=\""));
    client.print(filename);
    client.println(F("\""));
    client.print(F("Content-Length: "));
    client.println((unsigned long)file.size());
    client.println(F("Connection: close"));
    client.println();
    uint8_t buffer[1024];
    while (file.available()) {
        size_t bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead == 0) break;
        client.write(buffer, bytesRead);
        yield();
    }
    file.close();
    ethernetFilesReleaseMutexIfNeeded(storageType);
}
#endif

// --- Sensor handlers ---
#include "webpages/handlers_unified/sensor_handler.h"

#include "psram_helpers.h"

static void ethernetSendSensorFlags(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorFlagsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
static void ethernetHandleSensorConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleSensorConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorMeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorMeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorResetStatsPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorResetStatsPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorSlotsGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorSlotsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorFlushPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorFlushPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
static void ethernetHandleSensorChartsSessionPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorChartsSessionPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorChartsHeartbeatPost(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleSensorChartsHeartbeatPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorChartsSessionDelete(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleSensorChartsSessionDelete(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorChartsSessionEndPost(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleSensorChartsSessionEndPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
static void ethernetHandleSensorHistoryGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleSensorHistoryGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorHistoryFilesGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSensorHistoryFilesGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSensorHistoryFileGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleSensorHistoryFileGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if ENABLE_MQ135==1
static void ethernetHandleMq135ConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleMq135ConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleMq135ConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleMq135ConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleMq135MeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleMq135MeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleMq135CalibrateR0Post(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleMq135CalibrateR0Post(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if ENABLE_HX711==1
static void ethernetHandleHx711ConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleHx711ConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleHx711ConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleHx711ConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleHx711MeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleHx711MeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleHx711TarePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleHx711TarePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if ENABLE_INA226==1
static void ethernetHandleIna226ConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleIna226ConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleIna226ConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleIna226ConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleIna226MeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleIna226MeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if ENABLE_SOUND_SENSOR==1
static void ethernetHandleSoundConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSoundConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSoundConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleSoundConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSoundMeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSoundMeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

#if ENABLE_MAX31865==1
static void ethernetHandleMax31865ConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleMax31865ConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleMax31865ConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleMax31865ConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleMax31865MeasurePost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleMax31865MeasurePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif

// --- Audio (UDA1334A) handlers ---
#if ENABLE_UDA1334A==1
#include "webpages/handlers_unified/audio_handler.h"

#include "psram_helpers.h"

// GET /api/uda1334a-config
static void ethernetHandleUda1334aConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleUda1334aConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/uda1334a-config
static void ethernetHandleUda1334aConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleUda1334aConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/uda1334a-test
static void ethernetHandleUda1334aTestPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleUda1334aTestPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/radio/status
static void ethernetHandleRadioStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/radio/queue
static void ethernetHandleRadioQueueGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioQueueGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/radio/favorites
static void ethernetHandleRadioFavoritesGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioFavoritesGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/stop
static void ethernetHandleRadioStopPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioStopPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/next
static void ethernetHandleRadioNextPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioNextPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/random
static void ethernetHandleRadioRandomPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioRandomPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/volume
static void ethernetHandleRadioVolumePost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioVolumePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/play-url
static void ethernetHandleRadioPlayUrlPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioPlayUrlPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/play-sd
static void ethernetHandleRadioPlaySdPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioPlaySdPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/queue
static void ethernetHandleRadioQueuePost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleRadioQueuePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/favorites/add
static void ethernetHandleRadioFavoriteAddPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioFavoritesAddPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/favorites/remove
static void ethernetHandleRadioFavoriteRemovePost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioFavoritesRemovePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/favorites/play
static void ethernetHandleRadioFavoritePlayPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioFavoritesPlayPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// ---- New feature handlers ----

// GET /api/radio/artwork — returns binary image
static void ethernetHandleRadioArtworkGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioArtworkGet(hreq, hresp);
    if (hresp.isBinary && hresp.binaryData) {
        ethernetSendUnifiedResponse(client, hresp);
    } else {
        ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    }
}

// GET /api/radio/search?q=...
static void ethernetHandleRadioSearchGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleRadioSearchGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/radio/playlists
static void ethernetHandleRadioPlaylistsGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioPlaylistsGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/playlist/save
static void ethernetHandleRadioPlaylistSavePost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioPlaylistSavePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/playlist/load
static void ethernetHandleRadioPlaylistLoadPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioPlaylistLoadPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/playlist/delete
static void ethernetHandleRadioPlaylistDeletePost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioPlaylistDeletePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/shuffle
static void ethernetHandleRadioShufflePost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioShufflePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/repeat
static void ethernetHandleRadioRepeatPost(EthernetClient& client, const String& query, const String& body) {
    HttpRequest hreq;
    hreq.query = query;
    hreq.body = body;
    HttpResponse hresp;
    handleRadioRepeatPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/radio/eq
static void ethernetHandleRadioEqGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleRadioEqGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/eq (JSON body)
static void ethernetHandleRadioEqPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleRadioEqPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// POST /api/radio/schedule (JSON body)
static void ethernetHandleRadioSchedulePost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleRadioSchedulePost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#endif // ENABLE_UDA1334A==1

// --- Buzzer handlers ---
#if ENABLE_BUZZER == 1

#include "webpages/handlers_unified/buzzer_handler.h"

#include "psram_helpers.h"

static void ethernetHandleBuzzerConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleBuzzerConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleBuzzerConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleBuzzerConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleBuzzerTestPost(EthernetClient& client, const String& body) {
    HttpRequest hreq;
    hreq.body = body;
    hreq.contentType = F("application/json");
    HttpResponse hresp;
    handleBuzzerTestPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleBuzzerStopPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleBuzzerStopPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#endif // ENABLE_BUZZER

// --- Unikon / IPG Laser / CAN handlers ---
#if ENABLE_UNIKON==1
#include "unikon_protocol.h"

#include "psram_helpers.h"
static UnikonArrival* ethernetUnikonAllocTempArray() {
    size_t bytes = (size_t)UNIKON_MAX_ARRIVALS * sizeof(UnikonArrival);
    return (UnikonArrival*)(psramFound() ? ps_malloc(bytes) : malloc(bytes));
}
static void ethernetHandleUnikonStatusGet(EthernetClient& client) {
    UnikonStatus s = unikonGetStatus();
    UnikonConfig c = unikonGetConfig();
    uint32_t agoSec = s.lastPollMs > 0 ? (millis() - s.lastPollMs) / 1000UL : 0;
    uint16_t raceKnownCount = 0;
    uint16_t trainingKnownCount = 0;
    UnikonArrival* arrivals = ethernetUnikonAllocTempArray();
    if (arrivals) {
        uint16_t arrivalCount = unikonGetArrivals(arrivals, UNIKON_MAX_ARRIVALS);
        for (uint16_t i = 0; i < arrivalCount; i++) {
            if (arrivals[i].arrivalTime[0] == '\0' || memcmp(arrivals[i].arrivalTime, "000000", 6) == 0) continue;
            if (arrivals[i].isTraining) trainingKnownCount++;
            else raceKnownCount++;
        }
        free(arrivals);
    } else {
        raceKnownCount = s.totalKnown;
    }
    String emailDest = configManager.getEmailDestination();
    char buf[896];
    snprintf(buf, sizeof(buf),
        "{\"connected\":%s,\"lastPollAgoSec\":%lu,\"totalKnown\":%u,\"raceKnownCount\":%u,\"trainingKnownCount\":%u,\"lastNewCount\":%u"
        ",\"registeredCount\":%u,\"contestCount\":%u"
        ",\"moduleId\":\"%s\",\"clubName\":\"%s\",\"soltaName\":\"%s\",\"clubCode\":\"%s\",\"fancierName\":\"%s\""
        ",\"enabled\":%s"
        ",\"intervalSec\":%u,\"polling\":%s"
        ",\"passiveListen\":%s"
        ",\"officialFormat\":%s"
        ",\"emailDest\":\"%s\"}",
        s.connected ? "true" : "false",
        (unsigned long)agoSec,
        (unsigned)s.totalKnown, (unsigned)raceKnownCount, (unsigned)trainingKnownCount, (unsigned)s.lastNewCount,
        (unsigned)s.registeredCount, (unsigned)s.contestCount,
        s.moduleId, s.clubName, s.soltaName, s.clubCode, s.fancierName,
        c.enabled ? "true" : "false",
        (unsigned)c.pollIntervalSec,
        (c.enabled && c.pollIntervalSec > 0) ? "true" : "false",
        c.passiveListen ? "true" : "false",
        c.officialFormat ? "true" : "false",
        emailDest.c_str());
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(buf));
}
static void ethernetHandleUnikonArrivalsGet(EthernetClient& client) {
    UnikonArrival* arrivals = ethernetUnikonAllocTempArray();
    if (!arrivals) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json",
            String(F("{\"count\":0,\"arrivals\":[]}")));
        return;
    }
    bool officialFormat = unikonGetConfig().officialFormat;
    uint16_t count = unikonGetArrivals(arrivals, UNIKON_MAX_ARRIVALS);
    String json;
    json.reserve(count * 64 + 20);
    json = "{\"count\":";
    json += count;
    json += ",\"arrivals\":[";
    for (uint16_t i = 0; i < count; i++) {
        char ringDisplay[16];
        char chipDisplay[16];
        unikonFormatDisplayRing(arrivals[i].ring, ringDisplay, sizeof(ringDisplay));
        unikonFormatDisplayChip(arrivals[i].chip, arrivals[i].hasChip, chipDisplay, sizeof(chipDisplay));
        if (i > 0) json += ',';
        json += "{\"ring\":\""; json += officialFormat ? ringDisplay : arrivals[i].ring;
        json += "\",\"chip\":\""; json += officialFormat ? chipDisplay : arrivals[i].chip;
        json += "\",\"time\":\""; json += arrivals[i].arrivalTime;
        json += "\",\"date\":\""; json += arrivals[i].arrivalDate;
        json += "\",\"kind\":\""; json += arrivals[i].isTraining ? "training" : "race";
        json += "\"}";
    }
    json += "]}";
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
    free(arrivals);
}
static void ethernetHandleUnikonRegistrationsGet(EthernetClient& client) {
    UnikonArrival* regs = ethernetUnikonAllocTempArray();
    if (!regs) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json",
            String(F("{\"count\":0,\"registrations\":[]}")));
        return;
    }
    bool officialFormat = unikonGetConfig().officialFormat;
    uint16_t count = unikonGetRegistrations(regs, UNIKON_MAX_ARRIVALS);
    String json;
    json.reserve(count * 36 + 24);
    json = "{\"count\":";
    json += count;
    json += ",\"registrations\":[";
    for (uint16_t i = 0; i < count; i++) {
        char ringDisplay[16];
        char chipDisplay[16];
        unikonFormatDisplayRing(regs[i].ring, ringDisplay, sizeof(ringDisplay));
        unikonFormatDisplayChip(regs[i].chip, regs[i].hasChip, chipDisplay, sizeof(chipDisplay));
        if (i > 0) json += ',';
        json += "{\"ring\":\""; json += officialFormat ? ringDisplay : regs[i].ring;
        json += "\",\"chip\":\""; json += officialFormat ? chipDisplay : regs[i].chip;
        json += "\"}";
    }
    json += "]}";
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
    free(regs);
}
static void ethernetHandleUnikonContestsGet(EthernetClient& client) {
    UnikonContest contests[UNIKON_MAX_CONTESTS];
    uint16_t count = unikonGetContests(contests, UNIKON_MAX_CONTESTS);
    String json;
    json.reserve(count * 48 + 20);
    json = "{\"count\":";
    json += count;
    json += ",\"contests\":[";
    for (uint16_t i = 0; i < count; i++) {
        if (i > 0) json += ',';
        json += "{\"code\":\""; json += contests[i].code;
        json += "\",\"date\":\""; json += contests[i].date;
        json += "\",\"name\":\""; json += contests[i].name;
        json += "\"}";
    }
    json += "]}";
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}
static void ethernetHandleUnikonConfigPost(EthernetClient& client, const String& body) {
    UnikonConfig cfg = unikonGetConfig();
    auto getParam = [&](const char* key) -> String {
        String search = String(key) + "=";
        int idx = body.indexOf(search);
        if (idx < 0) return String();
        int start = idx + search.length();
        int end   = body.indexOf('&', start);
        return end < 0 ? body.substring(start) : body.substring(start, end);
    };
    String v;
    v = getParam("enabled");  if (v.length()) cfg.enabled         = v.toInt() != 0;
    v = getParam("interval"); if (v.length()) cfg.pollIntervalSec = (uint16_t)v.toInt();
    v = getParam("passive");  if (v.length()) cfg.passiveListen   = v.toInt() != 0;
    v = getParam("official_format"); if (v.length()) cfg.officialFormat = v.toInt() != 0;
    unikonSetConfig(cfg);
    if (!littlefsBegin()) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":true,\"message\":\"LittleFS temporarily unavailable. Config saved in memory and will be persisted after Flash mount.\"}")));
        return;
    }
    if (!getLittleFS().exists("/config/unikon.json")) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":false,\"message\":\"\\u274c Save verification failed - file not written\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"Saved\"}")));
}
static void ethernetHandleUnikonPollPost(EthernetClient& client) {
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":false,\"message\":\"Unikon disabled\"}")));
        return;
    }
    unikonRequestWOnlyPoll();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"Arrivals W-poll scheduled\"}")));
}

static void ethernetHandleUnikonSyncZpPost(EthernetClient& client) {
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":false,\"message\":\"Unikon disabled\"}")));
        return;
    }
    unikonRequestPoll();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"Manual ZP sync scheduled\"}")));
}
static void ethernetHandleUnikonSyncCalendarPost(EthernetClient& client) {
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":false,\"message\":\"Unikon disabled\"}")));
        return;
    }
    unikonRequestIcOnlyPoll();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"\U0001F4C5 Calendar sync scheduled\"}")));
}

static void ethernetHandleUnikonSyncPombalPost(EthernetClient& client) {
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String(F("{\"success\":false,\"message\":\"Unikon disabled\"}")));
        return;
    }
    unikonRequestXOnlyPoll();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"\U0001F426 Pigeon loft sync scheduled\"}")));
}

static void ethernetHandleUnikonEmailPombalPost(EthernetClient& client) {
    String msg;
    bool ok = unikonQueuePombalSeasonEmail(msg);
    String json = String(F("{\"success\":")) + (ok ? F("true") : F("false")) +
                  String(F(",\"message\":\"")) + ethernetJsonEscapeActive(msg) + String(F("\"}"));
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}
static void ethernetHandleUnikonResetPost(EthernetClient& client) {
    unikonResetArrivalsOnly();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"Reset ok\"}")));
}
#endif // ENABLE_UNIKON

// Always available: reports whether IPG Laser module is compiled in
static void ethernetHandleIpgLaserAvailableGet(EthernetClient& client) {
#if ENABLE_IPG_LASER==1
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"available\":true,\"message\":\"IPG Laser module is enabled.\"}")));
#else
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"available\":false,\"message\":\"IPG Laser module is disabled. Enable ENABLE_IPG_LASER in user_config.h.\"}")));
#endif
}

#if ENABLE_IPG_LASER==1
#include "ipg_laser_module.h"

#include "psram_helpers.h"
static void ethernetHandleIpgLaserStatusGet(EthernetClient& client) {
    IpgLaserStatus status = ipgLaserGetStatus();
    int idx = configManager.getRs232BaudrateIndex();
    int baud = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;
    String json = F("{");
    json += F("\"captureActive\":"); json += status.captureActive ? F("true") : F("false");
    json += F(",\"captureEndsInMs\":"); json += String(status.captureEndsInMs);
    json += F(",\"lastRxAgeMs\":"); json += String(status.lastRxAgeMs);
    json += F(",\"logCount\":"); json += String(status.logCount);
    json += F(",\"lastCommand\":\""); json += ethernetJsonEscapeActive(String(status.lastCommand)); json += F("\"");
    json += F(",\"currentBaudrate\":"); json += String(baud);
    json += F(",\"currentDataBits\":"); json += String(configManager.getRs232DataBits());
    json += F(",\"currentParity\":\""); json += ethernetJsonEscapeActive(configManager.getRs232Parity()); json += F("\"");
    json += F(",\"currentStopBits\":"); json += String(configManager.getRs232StopBits());
    json += F(",\"currentFlowControl\":"); json += configManager.getRs232UseXonXoff() ? F("true") : F("false");
    json += F(",\"currentTemplate\":\""); json += ethernetJsonEscapeActive(configManager.getRs232CurrentTemplate()); json += F("\"");
    json += F(",\"requiredBaudrate\":57600");
    json += F(",\"requiredDataBits\":8");
    json += F(",\"requiredParity\":\"none\"");
    json += F(",\"requiredStopBits\":1");
    json += F(",\"requiredFlowControl\":\"none\"");
    json += F(",\"transport\":\"RS232 null-modem 3-wire\"");
    json += F(",\"remoteModeNote\":\"Laser must be in REMOTE mode with interlocks satisfied before EMON.\"");
    json += F("}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleIpgLaserCommandsGet(EthernetClient& client) {
    size_t count = 0;
    const IpgLaserCommandInfo* catalog = ipgLaserGetCommandCatalog(&count);
    String json = F("{\"count\":");
    json += String((unsigned long)count);
    json += F(",\"commands\":[");
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) json += ',';
        json += F("{\"code\":\""); json += ethernetJsonEscapeActive(String(catalog[i].code));
        json += F("\",\"category\":\""); json += ethernetJsonEscapeActive(String(catalog[i].category));
        json += F("\",\"usage\":\""); json += ethernetJsonEscapeActive(String(catalog[i].usage));
        json += F("\",\"description\":\""); json += ethernetJsonEscapeActive(String(catalog[i].description));
        json += F("\",\"response\":\""); json += ethernetJsonEscapeActive(String(catalog[i].response));
        json += F("\",\"notes\":\""); json += ethernetJsonEscapeActive(String(catalog[i].notes));
        json += F("\"}");
    }
    json += F("]}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleIpgLaserLogGet(EthernetClient& client) {
    IpgLaserLogEntry entries[24];
    uint16_t count = ipgLaserCopyLog(entries, 24);
    String json = F("{\"count\":");
    json += String(count);
    json += F(",\"entries\":[");
    for (uint16_t i = 0; i < count; ++i) {
        if (i > 0) json += ',';
        json += F("{\"ageMs\":"); json += String(entries[i].ageMs);
        json += F(",\"direction\":\""); json += ethernetJsonEscapeActive(String(entries[i].direction));
        json += F("\",\"text\":\""); json += ethernetJsonEscapeActive(String(entries[i].text));
        json += F("\"}");
    }
    json += F("]}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleIpgLaserSendPost(EthernetClient& client, const String& body) {
    SpiRamJsonDocument doc(384);
    deserializeJson(doc, body);
    String command = doc["command"] | String("");
    String templateName = doc["template"] | String("");
    bool ok = ipgLaserSendCommand(command, templateName);
    ethernetBootstrapSendResponse(
        client,
        "200 OK",
        "application/json",
        ok
            ? String(F("{\"success\":true,\"message\":\"Command queued. Waiting for laser response.\"}"))
            : String(F("{\"success\":false,\"message\":\"Unable to queue IPG command.\"}"))
    );
}

static void ethernetHandleIpgLaserClearPost(EthernetClient& client) {
    ipgLaserClearLog();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        String(F("{\"success\":true,\"message\":\"IPG response log cleared.\"}")));
}
#endif // ENABLE_IPG_LASER

#if ENABLE_CAN==1
static const char* ethernetCanStateStr(CanBusState s) {
    switch (s) {
        case CAN_BUS_RUNNING:    return "RUNNING";
        case CAN_BUS_BUS_OFF:    return "BUS_OFF";
        case CAN_BUS_RECOVERING: return "RECOVERING";
        case CAN_BUS_ERROR:      return "ERROR";
        default:                 return "STOPPED";
    }
}
static void ethernetHandleCanStatusGet(EthernetClient& client) {
    CanBusStats bs = canRuntimeGetBusStats();
    CanRuntimeStats rs = canRuntimeGetStats();
    const CanConfig& cfg = canRuntimeGetConfig();
    String json;
    json.reserve(220);
    json  = F("{\"state\":\"");     json += ethernetCanStateStr(bs.state);
    json += F("\",\"mode\":");      json += (int)cfg.mode;
    json += F(",\"bitrate_kbps\":"); json += cfg.bitrate_kbps;
    json += F(",\"tec\":");         json += bs.tec;
    json += F(",\"rec\":");         json += bs.rec;
    json += F(",\"total_rx\":");    json += rs.total_rx;
    json += F(",\"total_tx\":");    json += rs.total_tx;
    json += F(",\"rx_errors\":");   json += bs.rx_errors;
    json += F(",\"tx_errors\":");   json += bs.tx_errors;
    json += F(",\"bus_off_count\":"); json += rs.bus_off_events;
    json += F(",\"buffer_drops\":"); json += rs.buffer_drops;
    json += F(",\"buffer_frames\":"); json += canRuntimeFrameCount();
    json += F(",\"msgs_to_rx\":");  json += bs.msgs_to_rx;
    json += F(",\"success\":true}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}
static void ethernetHandleCanFramesGet(EthernetClient& client, const String& query) {
    size_t limit = 128;
    int li = query.indexOf(F("limit="));
    if (li >= 0) {
        long v = atol(query.c_str() + li + 6);
        if (v > 0 && v <= (long)CAN_FRAME_BUFFER_SIZE) limit = (size_t)v;
    }
    CanFrame* frames = (CanFrame*)malloc(limit * sizeof(CanFrame));
    if (!frames) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"success\":false,\"message\":\"OOM\"}")));
        return;
    }
    size_t n = canRuntimeGetFrames(frames, limit);
    String json;
    json.reserve(64 + n * 80);
    json = F("{\"success\":true,\"count\":"); json += n;
    json += F(",\"frames\":[");
    for (size_t i = 0; i < n; i++) {
        const CanFrame& f = frames[i];
        if (i > 0) json += ',';
        json += F("{\"ts\":"); json += f.timestamp_ms;
        json += F(",\"id\":"); json += f.id;
        json += F(",\"dlc\":"); json += f.dlc;
        json += f.ext ? F(",\"ext\":true") : F(",\"ext\":false");
        json += f.rtr ? F(",\"rtr\":true") : F(",\"rtr\":false");
        json += F(",\"data\":[");
        for (uint8_t b = 0; b < f.dlc && b < 8; b++) {
            if (b > 0) json += ',';
            json += f.data[b];
        }
        json += F("]}");
    }
    json += F("]}");
    free(frames);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}
static void ethernetHandleCanConfigGet(EthernetClient& client) {
    const CanConfig& c = canRuntimeGetConfig();
    String json;
    json.reserve(180);
    json  = F("{\"success\":true,\"config\":{\"enabled\":");
    json += c.enabled ? F("true") : F("false");
    json += F(",\"bitrate_kbps\":"); json += c.bitrate_kbps;
    json += F(",\"mode\":");        json += (int)c.mode;
    json += F(",\"tx_pin\":");      json += c.tx_pin;
    json += F(",\"rx_pin\":");      json += c.rx_pin;
    json += F(",\"filter_id\":");   json += c.filter_id;
    json += F(",\"filter_mask\":"); json += c.filter_mask;
    json += c.filter_ext ? F(",\"filter_ext\":true") : F(",\"filter_ext\":false");
    json += c.log_to_sd  ? F(",\"log_to_sd\":true}}") : F(",\"log_to_sd\":false}}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}
static void ethernetHandleCanSendPost(EthernetClient& client, const String& body) {
    SpiRamJsonDocument doc(256);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiJsonInvalid)));
        return;
    }
    CanFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.id  = doc["id"]  | 0UL;
    frame.dlc = doc["dlc"] | (uint8_t)0;
    frame.ext = doc["ext"] | false;
    frame.rtr = doc["rtr"] | false;
    frame.timestamp_ms = (uint32_t)millis();
    if (frame.dlc > 8) frame.dlc = 8;
    JsonArray arr = doc["data"].as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= frame.dlc || i >= 8) break;
        frame.data[i++] = v.as<uint8_t>();
    }
    bool ok = canRuntimeSend(frame);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        ok ? String(F("{\"success\":true,\"message\":\"Frame enviado\"}"))
           : String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanSendFail) + F("\"}")));
}
static void ethernetHandleCanClearPost(EthernetClient& client) {
    canRuntimeClearFrames();
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"success\":true,\"message\":\"Buffer limpo\"}")));
}
static void ethernetHandleCanConfigPost(EthernetClient& client, const String& body) {
    SpiRamJsonDocument doc(256);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiJsonInvalid)));
        return;
    }
    CanConfig cfg = canRuntimeGetConfig();
    if (doc.containsKey("enabled"))      cfg.enabled      = doc["enabled"].as<bool>();
    if (doc.containsKey("bitrate_kbps")) cfg.bitrate_kbps = doc["bitrate_kbps"].as<uint16_t>();
    if (doc.containsKey("mode"))         cfg.mode         = (CanMode)doc["mode"].as<uint8_t>();
    if (doc.containsKey("tx_pin"))       cfg.tx_pin       = doc["tx_pin"].as<int>();
    if (doc.containsKey("rx_pin"))       cfg.rx_pin       = doc["rx_pin"].as<int>();
    if (doc.containsKey("filter_id"))    cfg.filter_id    = doc["filter_id"].as<uint32_t>();
    if (doc.containsKey("filter_mask"))  cfg.filter_mask  = doc["filter_mask"].as<uint32_t>();
    if (doc.containsKey("filter_ext"))   cfg.filter_ext   = doc["filter_ext"].as<bool>();
    if (doc.containsKey("log_to_sd"))    cfg.log_to_sd    = doc["log_to_sd"].as<bool>();
    const uint16_t valid[] = {25,50,100,125,250,500,800,1000};
    bool validBR = false;
    for (uint8_t vi = 0; vi < 8; vi++) if (cfg.bitrate_kbps == valid[vi]) validBR = true;
    if (!validBR) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(L(activeLang->apiErrBitrate)));
        return;
    }
    bool ok = canRuntimeApplyConfig(cfg, true);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        ok ? String(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiCanCfgSaved) + F("\"}"))
           : String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanRestartFail) + F("\"}")));
}
static void ethernetHandleCanRestartPost(EthernetClient& client) {
    bool ok = canRuntimeApplyConfig(canRuntimeGetConfig(), false);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json",
        ok ? String(F("{\"success\":true,\"message\":\"Driver CAN reiniciado\"}"))
           : String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanRestartFail) + F("\"}")));
}
#endif // ENABLE_CAN==1

static void ethernetHandleWifiScanGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleWifiScanGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetStreamPrinterPage(EthernetClient& client) {
    streamSharedPrinterPage(client);
}

static void ethernetStreamWifiPage(EthernetClient& client) {
    streamSharedWifiPage(client);
}

static void ethernetStreamEmailPage(EthernetClient& client) {
    streamSharedEmailPage(client);
}

static void ethernetStreamStructuresPage(EthernetClient& client) {
    streamSharedStructuresPage(client);
}

static String ethernetJsonEscapeActive(const String& input) {
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '"': output += F("\\\""); break;
            case '\\': output += F("\\\\"); break;
            case '\n': output += F("\\n"); break;
            case '\r': output += F("\\r"); break;
            case '\t': output += F("\\t"); break;
            default: output += c; break;
        }
    }
    return output;
}

static void ethernetSendRecipientsList(EthernetClient& client, const String& query) {
    unsigned long limit = 50;
    unsigned long offset = 0;
    const unsigned long recipientsMaxLimit = 200;

    String value = ethernetGetFormValue(query, "limit");
    if (value.length()) {
        long parsed = value.toInt();
        if (parsed < 1) parsed = 1;
        if ((unsigned long)parsed > recipientsMaxLimit) parsed = (long)recipientsMaxLimit;
        limit = (unsigned long)parsed;
    }
    value = ethernetGetFormValue(query, "offset");
    if (value.length()) {
        long parsed = value.toInt();
        if (parsed < 0) parsed = 0;
        offset = (unsigned long)parsed;
    } else {
        value = ethernetGetFormValue(query, "page");
        if (value.length()) {
            long page = value.toInt();
            if (page < 1) page = 1;
            offset = (unsigned long)((page - 1) * (long)limit);
        }
    }

    JsonArray slots = configManager.getRecipientSlots();
    size_t total = slots.size();
    SpiRamJsonDocument doc(4096);
    JsonObject root = doc.to<JsonObject>();
    root["total"] = (int)total;
    root["offset"] = (int)offset;
    root["limit"] = (int)limit;
    JsonArray items = root.createNestedArray("items");

    if (offset < total) {
        size_t end = offset + limit;
        if (end > total) end = total;
        for (size_t i = offset; i < end; ++i) {
            JsonObject slot = slots[i].as<JsonObject>();
            JsonObject o = items.createNestedObject();
            o["name"] = slot["name"] | String("");
            o["op"] = slot["op"] | String("I");
            o["emails"] = slot["emails"] | String("");
            o["freq"] = slot["freq"] | 0;
        }
    }

    String json;
    serializeJson(doc, json);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetSendRecipientSlots(EthernetClient& client) {
    SpiRamJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("slots");
    JsonArray rawSlots = configManager.getRecipientSlots();
    for (int i = 1; i <= 50; ++i) {
        String name, op, emails;
        int freq = 0;
        if (!configManager.getRecipientSlotWithFreq(i, name, op, emails, freq)) {
            break;
        }
        if (op == F("DF")) op = F("DR");
        bool active = true;
        if (i <= (int)rawSlots.size()) {
            active = rawSlots[i - 1]["enabled"] | true;
        }
        JsonObject slot = arr.createNestedObject();
        slot["name"] = name;
        slot["op"] = op;
        slot["emails"] = emails;
        slot["freq"] = freq;
        slot["active"] = active;
    }
    String json;
    serializeJson(doc, json);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleRecipientSlotGet(EthernetClient& client, const String& query) {
    int idx = ethernetGetFormValue(query, "i").toInt();
    if (idx < 1 || idx > 50) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String((String(F("{\"status\":\"err\",\"msg\":\"")) + L(activeLang->apiErrBadIndex) + "\"}")));
        return;
    }
    String name, op, emails;
    int freq = 0;
    bool ok = configManager.getRecipientSlotWithFreq(idx, name, op, emails, freq);
    if (!ok) {
        name = String();
        op = F("I");
        emails = String();
        freq = 0;
    }
    if (op != F("I") && op != F("D") && op != F("+") && op != F("-") && op != F("G") && op != F("L") && op != F("R") && op != F("DF") && op != F("DR") && op != F("V") && op != F("C") && op != F("P") && op != F("Cw") && op != F("Hy")) {
        op = F("I");
    }
    if (op == F("DF")) op = F("DR");
    bool active = true;
    JsonArray slots = configManager.getRecipientSlots();
    if (ok && idx <= (int)slots.size()) {
        active = slots[idx - 1]["enabled"] | true;
    }
    String out = F("{\"status\":\"ok\",\"index\":");
    out += String(idx);
    out += F(",\"name\":\"");
    out += ethernetJsonEscapeActive(name);
    out += F("\",\"op\":\"");
    out += ethernetJsonEscapeActive(op);
    out += F("\",\"emails\":\"");
    out += ethernetJsonEscapeActive(emails);
    out += F("\",\"freq\":");
    out += String(freq);
    out += F(",\"active\":");
    out += active ? F("true") : F("false");
    out += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", out);
}

static void ethernetHandleRecipientSlotPost(EthernetClient& client, const String& body) {
    int idx = ethernetGetFormValue(body, "i").toInt();
    if (idx < 1 || idx > 50) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"indice\"}")));
        return;
    }
    String name = ethernetGetFormValue(body, "name");
    String op = ethernetGetFormValue(body, "op");
    String emails = ethernetGetFormValue(body, "emails");
    int freq = ethernetGetFormValue(body, "freq").toInt();
    bool active = ethernetGetFormValue(body, "active") != F("0");
    if (op.length() == 0) op = F("I");
    if (op == F("DR")) op = F("DF");
    bool updated = configManager.updateRecipientSlotWithFreq(idx, name, op, emails, freq, active);
    if (!updated) {
        ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(String(F("{\"status\":\"err\",\"msg\":\"")) + L(activeLang->apiNotWritten) + F("\"}")));
        return;
    }
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("recipients")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveRecipient) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\"}")));
}

static void ethernetHandleInactivityAlertConfigPost(EthernetClient& client, const String& body) {
    bool sensorsResetTimer = ethernetFormHasTruthyValue(body, "sensors_reset_timer");
    configManager.setInactivityAlertSensorsResetTimer(sensorsResetTimer);

    for (int i = 0; i < 6; i++) {
        String suffix = String(i);
        String keyEnabled = "alert_enabled_" + suffix;
        String keyTimeout = "alert_timeout_" + suffix;
        String keyMax = "alert_max_" + suffix;
        String keyEmails = "alert_emails_" + suffix;
        bool isEnabled = ethernetFormHasTruthyValue(body, keyEnabled.c_str());

        int timeoutInt = 5;
        int maxPerDayInt = 6;
        String emails;

        if (isEnabled) {
            String timeout = ethernetGetFormValue(body, keyTimeout.c_str());
            if (timeout.length()) timeoutInt = timeout.toInt();
            String maxPerDay = ethernetGetFormValue(body, keyMax.c_str());
            if (maxPerDay.length()) maxPerDayInt = maxPerDay.toInt();
            emails = ethernetGetFormValue(body, keyEmails.c_str());
            emails.trim();

            if (timeoutInt < 1 || timeoutInt > 1440) {
                ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"error\",\"message\":\"Timeout must be 1-1440\"}")));
                return;
            }
            if (maxPerDayInt < 1 || maxPerDayInt > 100) {
                ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"error\",\"message\":\"Max per day must be 1-100\"}")));
                return;
            }
            if (!emails.length()) {
                ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"error\",\"message\":\"Emails required when alert is active\"}")));
                return;
            }
            if (emails.indexOf('@') < 0) {
                ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"error\",\"message\":\"Invalid email (missing @)\"}")));
                return;
            }
        }

        configManager.setAlertSlot(i, isEnabled, timeoutInt, maxPerDayInt, emails);
    }

    if (!configManager.saveSection("alerts")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"status\":\"error\",\"message\":\"Save failed\"}")));
        return;
    }
    configManager.markDirtyAndScheduleSave();

    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\"}")));
}

static void ethernetStreamRecipientsPage(EthernetClient& client) {
    streamSharedRecipientsPage(client);
}

static void ethernetSendRs232Templates(EthernetClient& client) {
    SpiRamJsonDocument doc(2048);
    JsonArray outArr = doc.createNestedArray("templates");
    JsonArray src = configManager.getRs232Templates();
    for (JsonObject s : src) {
        bool isBuiltIn = s.containsKey("built_in") && s["built_in"].as<bool>();
        if (isBuiltIn) {
            continue;
        }
        JsonObject o = outArr.createNestedObject();
        if (s.containsKey("name")) {
            String name = s["name"].as<String>();
            if (name.length()) {
                o["name"] = name;
            }
        }
        if (s.containsKey("baudrate_index")) o["baudrate_index"] = s["baudrate_index"].as<int>();
        if (s.containsKey("data_bits")) o["data_bits"] = s["data_bits"].as<int>();
        if (s.containsKey("parity")) o["parity"] = String(s["parity"].as<String>());
        if (s.containsKey("stop_bits")) o["stop_bits"] = s["stop_bits"].as<int>();
        if (s.containsKey("invert_pins")) o["invert_pins"] = s["invert_pins"].as<bool>();
        if (s.containsKey("read_timeout_ms")) o["read_timeout_ms"] = s["read_timeout_ms"].as<int>();
        if (s.containsKey("enabled")) o["enabled"] = s["enabled"].as<bool>();
        if (s.containsKey("use_xon_xoff")) o["use_xon_xoff"] = s["use_xon_xoff"].as<bool>();
        int baudrate = -1;
        if (s.containsKey("baudrate")) {
            baudrate = s["baudrate"].as<int>();
        } else if (s.containsKey("baudrate_index")) {
            int idx = s["baudrate_index"].as<int>();
            if (idx >= 0 && idx < RS232_BAUDRATES_COUNT) {
                baudrate = RS232_BAUDRATES[idx];
            }
        }
        if (baudrate > 0) {
            o["baudrate"] = baudrate;
        }
    }

    String json;
    serializeJson(doc, json);
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleRs232TemplatesPost(EthernetClient& client, const String& body) {
    String name = ethernetGetFormValue(body, "name");
    if (name.length() == 0) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"success\":false,\"error\":\"missing name\"}")));
        return;
    }

    int baudIdx = configManager.getRs232BaudrateIndex();
    int dataBits = configManager.getRs232DataBits();
    String parity = configManager.getRs232Parity();
    int stopBits = configManager.getRs232StopBits();
    bool invertPins = configManager.getRs232InvertPins();
    int readTimeoutMs = configManager.getRs232ReadTimeoutMs();
    bool enabled = configManager.getRs232Enabled();
    bool useXonXoff = configManager.getRs232UseXonXoff();

    String value = ethernetGetFormValue(body, "baudrate_index");
    if (value.length()) {
        baudIdx = value.toInt();
    }
    value = ethernetGetFormValue(body, "data_bits");
    if (value.length()) {
        dataBits = value.toInt();
    }
    value = ethernetGetFormValue(body, "parity");
    if (value.length()) {
        parity = value;
    }
    value = ethernetGetFormValue(body, "stop_bits");
    if (value.length()) {
        stopBits = value.toInt();
    }
    if (body.indexOf("invert_pins=") >= 0) {
        invertPins = ethernetFormHasTruthyValue(body, "invert_pins");
    }
    value = ethernetGetFormValue(body, "read_timeout_ms");
    if (value.length()) {
        readTimeoutMs = value.toInt();
    }
    if (body.indexOf("enabled=") >= 0) {
        enabled = ethernetFormHasTruthyValue(body, "enabled");
    }
    if (body.indexOf("use_xon_xoff=") >= 0) {
        useXonXoff = ethernetFormHasTruthyValue(body, "use_xon_xoff");
    }

    int baudNumeric = -1;
    if (baudIdx >= 0 && baudIdx < RS232_BAUDRATES_COUNT) {
        baudNumeric = RS232_BAUDRATES[baudIdx];
    }

    configManager.addOrUpdateRs232Template(name, baudIdx, dataBits, parity, stopBits, invertPins, readTimeoutMs, enabled, baudNumeric, useXonXoff);
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("rs232");
    triggerScreenConfigSaved();

    SpiRamJsonDocument outDoc(2048);
    outDoc["success"] = savedOk;
    outDoc["templates"] = configManager.getRs232Templates();
    String json;
    serializeJson(outDoc, json);
    ethernetBootstrapSendResponse(client, savedOk ? "200 OK" : "500 Internal Server Error", "application/json", json);
}

static void ethernetHandleRs232TemplatesDelete(EthernetClient& client, const String& query) {
    String name = ethernetGetFormValue(query, "name");
    if (name.length() == 0) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"success\":false,\"error\":\"missing name\"}")));
        return;
    }

    configManager.removeRs232Template(name);
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("rs232");
    triggerScreenConfigSaved();

    SpiRamJsonDocument outDoc(2048);
    outDoc["success"] = savedOk;
    outDoc["templates"] = configManager.getRs232Templates();
    String json;
    serializeJson(outDoc, json);
    ethernetBootstrapSendResponse(client, savedOk ? "200 OK" : "500 Internal Server Error", "application/json", json);
}


static bool ethernetApplyRs232TemplateByName(const String& name) {
    JsonArray arr = configManager.getRs232Templates();
    for (size_t i = 0; i < arr.size(); ++i) {
        JsonObject o = arr[i];
        if (!(o.containsKey("name") && String(o["name"].as<String>()) == name)) {
            continue;
        }

#if ENABLE_RS232==1
        if (o.containsKey("baudrate_index")) rs232_baudrate_index = o["baudrate_index"].as<int>();
        if (o.containsKey("baudrate")) {
            int baudrate = o["baudrate"].as<int>();
            for (int index = 0; index < RS232_BAUDRATES_COUNT; ++index) {
                if (RS232_BAUDRATES[index] == baudrate) {
                    rs232_baudrate_index = index;
                    break;
                }
            }
        }
        if (o.containsKey("data_bits")) rs232_data_bits = o["data_bits"].as<int>();
        if (o.containsKey("parity")) rs232_parity = String((const char*)o["parity"].as<const char*>());
        if (o.containsKey("stop_bits")) rs232_stop_bits = o["stop_bits"].as<int>();
        if (o.containsKey("invert_pins")) rs232_invert_pins = o["invert_pins"].as<bool>();
        if (o.containsKey("read_timeout_ms")) rs232_read_timeout_ms = o["read_timeout_ms"].as<int>();
        if (o.containsKey("use_xon_xoff")) rs232_template_use_xon_xoff = o["use_xon_xoff"].as<bool>();
#elif ENABLE_SNIFFER==1
        // Sniffer mode: no RS232 globals; write template values directly to ConfigManager.
        // applyRS232Config() (called below) reads ConfigManager to reinit the sniffer.
        if (o.containsKey("baudrate_index")) configManager.setRs232BaudrateIndex(o["baudrate_index"].as<int>());
        else if (o.containsKey("baudrate")) {
            int baud = o["baudrate"].as<int>();
            const int snBauds[] = {1200,2400,4800,9600,19200,38400,57600,115200};
            for (int idx = 0; idx < 8; ++idx) { if (snBauds[idx] == baud) { configManager.setRs232BaudrateIndex(idx); break; } }
        }
        if (o.containsKey("data_bits")) configManager.setRs232DataBits(o["data_bits"].as<int>());
        if (o.containsKey("parity")) configManager.setRs232Parity(String(o["parity"].as<const char*>()));
        if (o.containsKey("stop_bits")) configManager.setRs232StopBits(o["stop_bits"].as<int>());
        if (o.containsKey("invert_pins")) configManager.setRs232InvertPins(o["invert_pins"].as<bool>());
        if (o.containsKey("read_timeout_ms")) configManager.setRs232ReadTimeoutMs(o["read_timeout_ms"].as<int>());
#endif // ENABLE_RS232 / ENABLE_SNIFFER
        configManager.setRs232CurrentTemplateVolatile(name);
        syncGlobalVariables(false);
        applyRS232Config();
        return true;
    }
    return false;
}

static void ethernetHandleRs232ApplyPost(EthernetClient& client, const String& body) {
    String name = ethernetGetRequestValue(body, "template");
    if (name.length() == 0) {
        name = ethernetGetRequestValue(body, "name");
    }
    if (name.length() == 0) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"success\":false,\"error\":\"missing template name\"}")));
        return;
    }
    SpiRamJsonDocument out(256);
    out["success"] = ethernetApplyRs232TemplateByName(name);
    out["template"] = name;
    String json;
    serializeJson(out, json);
    ethernetBootstrapSendResponse(client, out["success"] ? "200 OK" : "404 Not Found", "application/json", json);
}


static void ethernetHandleStorageRetrySdPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleStorageRetrySdPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSdResetWriteCounterPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSdResetWriteCounterPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleSdBuffersGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSdBuffersGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#ifdef ENABLE_DEBUG_API
static void ethernetHandleDb25InjectGet(EthernetClient& client, const String& query) {
    HttpRequest hreq;
    hreq.query = query;
    HttpResponse hresp;
    handleDb25InjectGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}
#endif


static void ethernetHandleTestPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleTestPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleEmaildayExtraEmailsGet(EthernetClient& client) {
    String json = String(F("{\"status\":\"ok\",\"emails\":\""))
        + ethernetJsonEscapeActive(configManager.getEmaildayExtraEmails())
        + String(F("\"}"));
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleEmaildayExtraEmailsPost(EthernetClient& client, const String& body) {
    String emails = ethernetGetRequestValue(body, "emails");
    emails.trim();
    configManager.setEmaildayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\",\"msg\":\"Emails guardados\"}")));
}

static void ethernetHandleLogdayExtraEmailsGet(EthernetClient& client) {
    String json = String(F("{\"status\":\"ok\",\"emails\":\""))
        + ethernetJsonEscapeActive(configManager.getLogdayExtraEmails())
        + String(F("\"}"));
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleLogdayExtraEmailsPost(EthernetClient& client, const String& body) {
    String emails = ethernetGetRequestValue(body, "emails");
    emails.trim();
    configManager.setLogdayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\",\"msg\":\"Emails guardados\"}")));
}

static void ethernetHandleSensordayExtraEmailsGet(EthernetClient& client) {
    String json = String(F("{\"status\":\"ok\",\"emails\":\""))
        + ethernetJsonEscapeActive(configManager.getSensordayExtraEmails())
        + String(F("\"}"));
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleSensordayExtraEmailsPost(EthernetClient& client, const String& body) {
    String emails = ethernetGetRequestValue(body, "emails");
    emails.trim();
    configManager.setSensordayExtraEmails(emails);
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("sdcard")) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveExtraEmails) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\",\"msg\":\"Emails guardados\"}")));
}

static void ethernetHandleRs232ColsGet(EthernetClient& client, const String& query) {
    if (!configManager.getChartsPageEnabled()) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"charts disabled\"}")));
        return;
    }

    String requestedDate = ethernetGetQueryParam(query, "date");
    time_t now = time(nullptr);
    struct tm ti = {};
    localtime_r(&now, &ti);
    char ds[11];
    snprintf(ds, sizeof(ds), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String resolvedDate = requestedDate.length() == 10 ? requestedDate : String(ds);
#if ENABLE_SDCARD==1
    String chartPath = chartHistoryRs232PathForDate(configManager.getSystemName(), resolvedDate);
    uint8_t chartCols = 0;
    ChartColumnDef chartDefs[CHART_HISTORY_RS232_MAX_COLUMNS] = {};
#endif
    int cols = 0;
    bool useLiveColumns = requestedDate.length() == 0 && resolvedDate == String(ds);
#if ENABLE_RS232==1 && ENABLE_SDCARD==1
    if (useLiveColumns && rs232LiveMutex && xSemaphoreTakeRecursive(rs232LiveMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (!rs232LiveBuffer.isEmpty()) cols = rs232LiveBuffer.last().valueCount;
        xSemaphoreGiveRecursive(rs232LiveMutex);
    }
    chartHistoryReadRs232Meta(chartPath, chartCols, chartDefs);
    if (cols == 0) cols = chartCols;
#endif

    String json = String(F("{\"cols\":")) + String(cols) + String(F(",\"date\":\"")) + ethernetJsonEscapeActive(resolvedDate) + String(F("\""));
    if (cols > 0) {
        json += F(",\"col_names\":[");
        for (int ci = 0; ci < cols; ++ci) {
            if (ci > 0) json += ',';
            String chosenName;
#if ENABLE_SDCARD==1
            if (chartCols > ci) {
                chosenName = chartDefs[ci].label[0] ? String(chartDefs[ci].label) : String(chartDefs[ci].key);
                chosenName.trim();
            }
#endif
            if (chosenName.length() == 0) chosenName = String(F("RS232 Col ")) + String(ci + 1);
            json += '"';
            json += ethernetJsonEscapeActive(chosenName);
            json += '"';
        }
        json += ']';
    }
    json += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleRs232HistoryFilesGet(EthernetClient& client) {
    if (!configManager.getChartsPageEnabled()) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"charts disabled\"}")));
        return;
    }

    String json = F("[");
    bool first = true;
    if (sdMutexLock(pdMS_TO_TICKS(3000))) {
        File root = SD_MMC.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                if (entry.isDirectory()) {
                    String name = String(entry.name());
                    if (name.length() >= 11) {
                        String datePart = name.substring(name.length() - 10);
                        if (datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                            if (!first) json += ',';
                            json += '"';
                            json += ethernetJsonEscapeActive(datePart);
                            json += '"';
                            first = false;
                        }
                    }
                }
                entry.close();
                entry = root.openNextFile();
            }
            root.close();
        }
        sdMutexUnlock();
    }
    json += ']';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleRs232HistoryGet(EthernetClient& client, const String& query) {
    if (!configManager.getChartsPageEnabled()) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"charts disabled\"}")));
        return;
    }

    uint32_t fromTs = ethernetGetQueryParam(query, "from").toInt();
    String period = ethernetGetQueryParam(query, "period");
    String date = ethernetGetQueryParam(query, "date");
    if (date.length() == 0) {
        time_t refTs = fromTs > 0 ? (time_t)fromTs : time(nullptr);
        struct tm refInfo = {};
        localtime_r(&refTs, &refInfo);
        char autoDate[11];
        snprintf(autoDate, sizeof(autoDate), "%02d-%02d-%04d", refInfo.tm_mday, refInfo.tm_mon + 1, refInfo.tm_year + 1900);
        date = String(autoDate);
    }
    if (date.length() != 10 || date.charAt(2) != '-' || date.charAt(5) != '-') {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"error\":\"Invalid date\"}")));
        return;
    }

    // Do NOT override mode to "from" when fromTs provided - that blocked the live buffer.
    String mode = ethernetGetQueryParam(query, "mode");
    if (mode.length() == 0) mode = F("day");
    if (mode != F("live") && mode != F("8h") && mode != F("day")) mode = F("day");
    if (fromTs == 0 && period.length() > 0 && ethernetGetQueryParam(query, "mode").length() == 0) {
        if (period == F("1h")) mode = F("live");
        else if (period == F("8h") || period == F("12h")) mode = F("8h");
        else mode = F("day");
    }

    time_t minTs = fromTs > 0 ? (time_t)fromTs : 0;
    time_t nowTs = time(nullptr);
    struct tm nowInfo = {};
    localtime_r(&nowTs, &nowInfo);
    char todayBuf[11];
    snprintf(todayBuf, sizeof(todayBuf), "%02d-%02d-%04d", nowInfo.tm_mday, nowInfo.tm_mon + 1, nowInfo.tm_year + 1900);
    if (fromTs == 0 && date == String(todayBuf)) {
        if (mode == F("live")) minTs = nowTs - 3600;
        else if (mode == F("8h")) minTs = nowTs - 28800;
    }

    // Derive resolution from actual time range - symmetric with sensor handler
    uint32_t rangeSeconds = (minTs > 0 && nowTs > minTs) ? (uint32_t)(nowTs - minTs) : 86400UL;
    const int maxOut = (rangeSeconds <= 3600) ? 72 : ((rangeSeconds <= 28800) ? 288 : 1440);
    const int targetOut = maxOut;
    // Merged serving (file history + live ring tail, always minute-fresh —
    // same envelope as WiFi, one stride budget over the union).
#if ENABLE_RS232==1 && ENABLE_SDCARD==1
    ChartServePoint* raw = (ChartServePoint*)ps_malloc((size_t)1500 * sizeof(ChartServePoint));
    ChartServePoint* tail = (ChartServePoint*)ps_malloc((size_t)512 * sizeof(ChartServePoint));
    if (!raw || !tail) {
        if (raw) free(raw);
        if (tail) free(tail);
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json",
                                      String(F("{\"error\":\"no memory\"}")));
        return;
    }
    String chartPath = chartHistoryRs232PathForDate(configManager.getSystemName(), date);
    uint16_t nRaw = chartHistoryReadRawPoints(chartPath, CHART_HISTORY_SOURCE_RS232,
                                              CHART_HISTORY_RS232_MAX_COLUMNS,
                                              (uint32_t)minTs, raw, 1500);
    uint16_t nTail = 0;
    uint8_t nCols = 0;
    if (rs232LiveMutex && xSemaphoreTakeRecursive(rs232LiveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t liveSize = rs232LiveBuffer.size();
        for (size_t i = 0; i < liveSize && nTail < 512; i++) {
            const Rs232LivePoint& pt = rs232LiveBuffer[i];
            if (minTs != 0 && (time_t)pt.timestamp < minTs) continue;
            ChartServePoint* q = &tail[nTail++];
            q->timestamp = pt.timestamp;
            q->valueCount = (pt.valueCount > CHART_SERVE_MAX_COLS) ? CHART_SERVE_MAX_COLS : pt.valueCount;
            for (uint8_t c = 0; c < CHART_SERVE_MAX_COLS; c++) {
                q->values[c] = (c < RS232_LIVE_MAX_COLS) ? pt.values[c] : NAN;
            }
            if (pt.valueCount > nCols) nCols = pt.valueCount;
        }
        xSemaphoreGiveRecursive(rs232LiveMutex);
    }
    if (nCols > CHART_HISTORY_RS232_MAX_COLUMNS) nCols = CHART_HISTORY_RS232_MAX_COLUMNS;
    if (nCols == 0) {
        for (uint16_t i = 0; i < nRaw; i++) {
            if (raw[i].valueCount > nCols) nCols = raw[i].valueCount;
        }
        if (nCols > CHART_HISTORY_RS232_MAX_COLUMNS) nCols = CHART_HISTORY_RS232_MAX_COLUMNS;
    }
    char keys[CHART_HISTORY_RS232_MAX_COLUMNS][16];
    int8_t decs[CHART_HISTORY_RS232_MAX_COLUMNS];
    for (uint8_t c = 0; c < nCols; c++) {
        snprintf(keys[c], sizeof(keys[c]), "rs232_col%u", (unsigned)(c + 1));
        decs[c] = 4;
    }
    uint16_t mCap = nRaw + nTail;
    ChartServePoint* merged = (ChartServePoint*)ps_malloc(((size_t)mCap + 1) * sizeof(ChartServePoint));
    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    if (merged) {
        uint32_t n = chartMergePoints(raw, nRaw, tail, nTail, merged, mCap + 1);
        chartHistoryServePointsJson(client, merged, n, keys, decs, nCols, (uint16_t)targetOut);
        free(merged);
    } else {
        client.print(F("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}"));
    }
    free(raw);
    free(tail);
#else
    ethernetBootstrapBeginStreamResponse(client, "200 OK", "application/json");
    client.print(F("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}"));
#endif
}

static void ethernetStreamMq135Page(EthernetClient& client) {
#if ENABLE_MQ135==1
    streamSharedMq135Page(client);
#else
    ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
}

static void ethernetStreamRs232Page(EthernetClient& client) {
    streamSharedRs232Page(client);
}

static void ethernetSendScheduledCommandSlots(EthernetClient& client) {
    String json = F("{\"status\":\"ok\",\"slots\":[");
    for (int i = 1; i <= 10; ++i) {
        String cmd, unit, days, tpl, type;
        int interval = 0;
        bool enabled = false;
        bool ok = configManager.getSchedCmdSlot(i, cmd, interval, unit, days, tpl, enabled, type);
        if (i > 1) {
            json += ',';
        }
        json += F("{\"i\":");
        json += String(i);
        json += F(",\"command\":\"");
        json += ethernetJsonEscapeActive(ok ? cmd : String(""));
        json += F("\",\"interval\":");
        json += String(ok ? interval : 0);
        json += F(",\"unit\":\"");
        json += ethernetJsonEscapeActive(ok ? unit : String("m"));
        json += F("\",\"days\":\"");
        json += ethernetJsonEscapeActive(ok ? days : String(""));
        json += F("\",\"template\":\"");
        json += ethernetJsonEscapeActive(ok ? tpl : String(""));
        json += F("\",\"enabled\":");
        json += (ok && enabled) ? F("true") : F("false");
        json += '}';
    }
    json += F("]}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetSendScheduledDatetimeSlots(EthernetClient& client) {
    String json = F("{\"status\":\"ok\",\"slots\":[");
    for (int i = 1; i <= 8; ++i) {
        String cmd, date, timev, tpl, type;
        bool enabled = false;
        bool daily = false;
        bool ok = configManager.getSchedDtSlot(i, cmd, date, timev, tpl, enabled, type, daily);
        if (i > 1) {
            json += ',';
        }
        json += F("{\"i\":");
        json += String(i);
        json += F(",\"command\":\"");
        json += ethernetJsonEscapeActive(ok ? cmd : String(""));
        json += F("\",\"date\":\"");
        json += ethernetJsonEscapeActive(ok ? date : String(""));
        json += F("\",\"time\":\"");
        json += ethernetJsonEscapeActive(ok ? timev : String(""));
        json += F("\",\"template\":\"");
        json += ethernetJsonEscapeActive(ok ? tpl : String(""));
        json += F("\",\"enabled\":");
        json += (ok && enabled) ? F("true") : F("false");
        json += '}';
    }
    json += F("]}");
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", json);
}

static void ethernetHandleScheduledCommandSlotGet(EthernetClient& client, const String& query) {
    int i = ethernetGetFormValue(query, "i").toInt();
    if (i < 1 || i > 30) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"idx\"}")));
        return;
    }
    String cmd, unit, days, tpl, type;
    int interval = 0;
    bool enabled = false;
    if (!configManager.getSchedCmdSlot(i, cmd, interval, unit, days, tpl, enabled, type)) {
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"status\":\"err\",\"msg\":\"slot\"}")));
        return;
    }
    if (type.length() == 0) {
        type = F("rs232");
    }
    String out = F("{\"status\":\"ok\",\"command\":\"");
    out += ethernetJsonEscapeActive(cmd);
    out += F("\",\"interval\":");
    out += String(interval);
    out += F(",\"unit\":\"");
    out += ethernetJsonEscapeActive(unit);
    out += F("\",\"days\":\"");
    out += ethernetJsonEscapeActive(days);
    out += F("\",\"template\":\"");
    out += ethernetJsonEscapeActive(tpl);
    out += F("\",\"type\":\"");
    out += ethernetJsonEscapeActive(type);
    out += F("\",\"enabled\":");
    out += enabled ? F("true") : F("false");
    out += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", out);
}

static void ethernetHandleScheduledCommandSlotPost(EthernetClient& client, const String& body) {
    int i = ethernetGetFormValue(body, "i").toInt();
    if (i < 1 || i > 30) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"idx\"}")));
        return;
    }
    String cmd = ethernetGetFormValue(body, "command");
    int interval = ethernetGetFormValue(body, "interval").toInt();
    String unit = ethernetGetFormValue(body, "unit");
    if (unit == F("s") || unit == F("seconds")) unit = F("seconds");
    else if (unit == F("h") || unit == F("hours")) unit = F("hours");
    else unit = F("minutes");
    String days = ethernetGetFormValue(body, "days");
    String tpl = ethernetGetFormValue(body, "template");
    String type = ethernetGetFormValue(body, "type");
    if (type.length() == 0) {
        type = F("rs232");
    }
    bool enabled = ethernetFormHasTruthyValue(body, "enabled");
    if (enabled) {
        if (cmd.length() == 0) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"cmd\"}")));
            return;
        }
        if (interval <= 0) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"interval\"}")));
            return;
        }
    }
    if (!configManager.setSchedCmdSlot(i, cmd, interval, unit, days, tpl, enabled, type)) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"status\":\"err\",\"msg\":\"gravar\"}")));
        return;
    }
#if ENABLE_SCHEDULER==1
    resetSchedCmdSlotTimer(i);
#endif
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("schedule");
    char cfgMsg[176];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d type=%s interval=%d cmd=%.96s", i, type.c_str(), interval, cmd.c_str());
    observabilityLogConfigChange("/api/scheduled-command-slot", cfgMsg, savedOk);
    if (!savedOk) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveSched) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\"}")));
}

static void ethernetHandleScheduledDatetimeSlotGet(EthernetClient& client, const String& query) {
    int i = ethernetGetFormValue(query, "i").toInt();
    if (i < 1 || i > 16) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String((String(F("{\"status\":\"err\",\"msg\":\"")) + L(activeLang->apiErrBadIndex) + "\"}")));
        return;
    }
    String cmd, date, timev, tpl, type;
    bool enabled = false;
    bool daily = false;
    bool ok = configManager.getSchedDtSlot(i, cmd, date, timev, tpl, enabled, type, daily);
    if (!ok) {
        cmd = String();
        date = String();
        timev = String();
        tpl = String();
        type = F("rs232");
        enabled = false;
        daily = false;
    }
    if (type.length() == 0) {
        type = F("rs232");
    }
    String out = F("{\"status\":\"ok\",\"command\":\"");
    out += ethernetJsonEscapeActive(cmd);
    out += F("\",\"date\":\"");
    out += ethernetJsonEscapeActive(date);
    out += F("\",\"time\":\"");
    out += ethernetJsonEscapeActive(timev);
    out += F("\",\"template\":\"");
    out += ethernetJsonEscapeActive(tpl);
    out += F("\",\"type\":\"");
    out += ethernetJsonEscapeActive(type);
    out += F("\",\"daily\":");
    out += daily ? F("true") : F("false");
    out += F(",\"enabled\":");
    out += enabled ? F("true") : F("false");
    out += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", out);
}

static void ethernetHandleScheduledDatetimeSlotPost(EthernetClient& client, const String& body) {
    int i = ethernetGetFormValue(body, "i").toInt();
    if (i < 1) {
        i = ethernetGetFormValue(body, "sd_i").toInt();
    }
    if (i < 1 || i > 16) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"indice\"}")));
        return;
    }
    String command = ethernetGetFormValue(body, "command");
    if (command.length() == 0) command = ethernetGetFormValue(body, "sd_cmd");
    String date = ethernetGetFormValue(body, "date");
    if (date.length() == 0) date = ethernetGetFormValue(body, "sd_date");
    String timev = ethernetGetFormValue(body, "time");
    if (timev.length() == 0) timev = ethernetGetFormValue(body, "sd_time");
    String tpl = ethernetGetFormValue(body, "template");
    if (tpl.length() == 0) tpl = ethernetGetFormValue(body, "sd_tpl");
    String type = ethernetGetFormValue(body, "type");
    if (type.length() == 0) type = ethernetGetFormValue(body, "sd_type");
    String dailyS = ethernetGetFormValue(body, "daily");
    if (dailyS.length() == 0) dailyS = ethernetGetFormValue(body, "sd_daily");
    String enabledS = ethernetGetFormValue(body, "enabled");
    if (enabledS.length() == 0) enabledS = ethernetGetFormValue(body, "sd_en");
    bool enabled = (enabledS == F("1") || enabledS == F("true") || enabledS == F("on"));
    bool daily = (dailyS == F("1") || dailyS == F("true") || dailyS == F("on"));
    if (type.length() == 0) {
        type = F("rs232");
    }
    if (enabled) {
        if (command.length() == 0) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"command vazio\"}")));
            return;
        }
        if (!(date.length() == 10 && date.charAt(4) == '-' && date.charAt(7) == '-')) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"data invalida\"}")));
            return;
        }
        if (!(timev.length() == 5 && timev.charAt(2) == ':')) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"hora invalida\"}")));
            return;
        }
    }
    if (!configManager.setSchedDtSlot(i, command, date, timev, tpl, enabled, type, daily)) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"status\":\"err\",\"msg\":\"gravar\"}")));
        return;
    }
#if ENABLE_SCHEDULER==1
    resetSchedDtSlotTimer(i);
#endif
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("schedule");
    char cfgMsg[192];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d type=%s daily=%s cmd=%.96s", i, type.c_str(), daily ? "yes" : "no", command.c_str());
    observabilityLogConfigChange("/api/scheduled-datetime-slot", cfgMsg, savedOk);
    if (!savedOk) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveSched) + F(" por data\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", String(F("{\"status\":\"ok\"}")));
}

static void ethernetHandleDailyRebootGet(EthernetClient& client, const String& query) {
    int i = ethernetGetFormValue(query, "i").toInt();
    if (i < 1 || i > 2) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"indice\"}")));
        return;
    }
    String timev;
    bool enabled = false;
    bool ok = configManager.getDailyRebootSlot(i, timev, enabled);
    if (!ok) {
        timev = (i == 1) ? String(F("00:00")) : String();
        enabled = (i == 1);
    }
    // Slot 1 follows the OEE shift layout when shifts are configured (same as WiFi API).
    bool followsShifts = (i == 1) && dailyRebootFollowsShifts();
    char effStr[8] = {0};
    if (followsShifts) {
        int effH = 0, effM = 0;
        dailyRebootEffectiveHM(&effH, &effM);
        snprintf(effStr, sizeof(effStr), "%02d:%02d", effH, effM);
        timev = String(effStr);
    }
    String out = F("{\"status\":\"ok\",\"i\":");
    out += String(i);
    out += F(",\"time\":\"");
    out += ethernetJsonEscapeActive(timev);
    out += F("\",\"enabled\":");
    out += enabled ? F("true") : F("false");
    if (followsShifts) {
        out += F(",\"follows_shifts\":true,\"effective_time\":\"");
        out += effStr;
        out += F("\"");
    }
    out += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", out);
}

static void ethernetHandleDailyRebootPost(EthernetClient& client, const String& body) {
    int i = ethernetGetFormValue(body, "i").toInt();
    if (i < 1 || i > 2) {
        ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"indice\"}")));
        return;
    }
    String timev = ethernetGetFormValue(body, "time");
    timev.trim();
    String enabledS = ethernetGetFormValue(body, "enabled");
    bool enabled = (enabledS == F("1") || enabledS == F("true") || enabledS == F("on") || enabledS == F("Sim") || enabledS == F("sim") || enabledS == F("YES") || enabledS == F("Yes") || enabledS == F("y") || enabledS == F("Y"));
    if (enabled) {
        bool okFmt = (timev.length() == 5 && timev.charAt(2) == ':' && isDigit((char)timev[0]) && isDigit((char)timev[1]) && isDigit((char)timev[3]) && isDigit((char)timev[4]));
        if (!okFmt) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"hora\"}")));
            return;
        }
        int hh = (timev[0] - '0') * 10 + (timev[1] - '0');
        int mm = (timev[3] - '0') * 10 + (timev[4] - '0');
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
            ethernetBootstrapSendResponse(client, "400 Bad Request", "application/json", String(F("{\"status\":\"err\",\"msg\":\"hora\"}")));
            return;
        }
    }
    if (!configManager.setDailyRebootSlot(i, timev, enabled)) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(F("{\"status\":\"err\",\"msg\":\"gravar\"}")));
        return;
    }
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("schedule");
#if ENABLE_SCHEDULER==1
    resetDailyRebootSlotTimer(i);
#endif
    char cfgMsg[96];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d time=%s enabled=%s", i, timev.c_str(), enabled ? "yes" : "no");
    observabilityLogConfigChange("/api/daily-reboot", cfgMsg, savedOk);
    if (!savedOk) {
        ethernetBootstrapSendResponse(client, "500 Internal Server Error", "application/json", String(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveReboot) + F("\",\"status\":\"err\",\"msg\":\"save\"}")));
        return;
    }
    String out = F("{\"status\":\"ok\",\"i\":");
    out += String(i);
    out += F(",\"time\":\"");
    out += ethernetJsonEscapeActive(timev);
    out += F("\",\"enabled\":");
    out += enabled ? F("true") : F("false");
    out += '}';
    ethernetBootstrapSendResponse(client, "200 OK", "application/json", out);
}

// GET /api/heap-snapshot - FreeRTOS task stacks + heap pressure (Ethernet version)
static void ethernetHandleHeapSnapshotGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleHeapSnapshotGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/self-test - Comprehensive device audit (Ethernet version)
#include "webpages/handlers_unified/selftest_handler.h"
static void ethernetHandleSelfTestGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleSelfTestGet(hreq, hresp);
    if (hresp.isBinary && hresp.binaryData) {
        ethernetBootstrapSendBinaryResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(),
                                            hresp.binaryData, hresp.binaryLen, hresp.freeFn);
        return;
    }
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// GET /api/test-manifest - Test contract for the AI agent (Ethernet version)
#include "webpages/handlers_unified/test_manifest_handler.h"
static void ethernetHandleTestManifestGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleTestManifestGet(hreq, hresp);
    if (hresp.isBinary && hresp.binaryData) {
        ethernetBootstrapSendBinaryResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(),
                                            hresp.binaryData, hresp.binaryLen, hresp.freeFn);
        return;
    }
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

// --- Camera handlers ---
#if ENABLE_CAMERA==1

#include "webpages/handlers_unified/camera_handler.h"

#include "psram_helpers.h"
#include "camera_manager.h"

#include "psram_helpers.h"

// ── MJPEG stream (multipart/x-mixed-replace) ─────────────────────────
// Blocking loop — while actively streaming, other Ethernet clients queue.
// Acceptable for camera boards where the stream is the primary UI.
static void ethernetHandleCameraStreamGet(EthernetClient& client) {
    if (!cameraIsReady()) {
        ethernetBootstrapSendResponse(client, "503 Service Unavailable", "text/plain", String(F("Camera not ready")));
        return;
    }
    if (!cameraStreamIsActive()) cameraStreamStart();

    client.println(F("HTTP/1.1 200 OK"));
    client.print(F("Content-Type: multipart/x-mixed-replace; boundary=--frame\r\n"));
    client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
    client.println(F("Pragma: no-cache"));
    client.println(F("Expires: 0"));
    client.println(F("X-Accel-Buffering: no"));
    client.println();

    // Original 100ms header-read timeout would stall frame writes.
    // We want short writes so the W5500 socket buffer doesn't overflow.
    client.setTimeout(10);

    while (client.connected()) {
        esp_task_wdt_reset();
        size_t availLen = 0;
        const uint8_t* avail = cameraStreamFrame(availLen);
        if (!avail || availLen == 0) {
            delay(5);
            continue;
        }
        // Drain any incoming data (e.g. browser TCP window probes)
        while (client.available()) client.read();

        // Multipart boundary + JPEG headers
        client.print(F("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "));
        client.print(availLen);
        client.print(F("\r\n\r\n"));

        // Write JPEG in W5500-safe chunks (EthernetChunkedPrint uses 512 B)
        {
            size_t written = 0;
            while (written < availLen) {
                size_t chunk = (availLen - written > 512) ? 512 : (availLen - written);
                client.write(avail + written, chunk);
                written += chunk;
            }
        }
        client.print(F("\r\n"));
        cameraStreamFrameSent();
        delay(10);
    }
    // Client disconnected or error — clean up
    client.stop();
}

static void ethernetHandleCameraStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraStartPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraStartPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraStopPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraStopPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraSnapshotGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraSnapshotGet(hreq, hresp);
    if (hresp.isBinary && hresp.binaryData) {
        client.println(F("HTTP/1.1 200 OK"));
        client.print(F("Content-Type: ")); client.println(hresp.contentType);
        client.println(F("Cache-Control: no-cache, no-store"));
        client.println(F("Connection: close"));
        client.print(F("Content-Length: ")); client.println((unsigned long)hresp.binaryLen);
        client.println();
        client.write(hresp.binaryData, hresp.binaryLen);
        if (hresp.freeFn) hresp.freeFn(hresp.binaryData);
    } else {
        ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    }
}

static void ethernetHandleCameraMotionDiffGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraMotionDiffGet(hreq, hresp);
    if (hresp.isBinary && hresp.binaryData) {
        client.println(F("HTTP/1.1 200 OK"));
        client.print(F("Content-Type: ")); client.println(hresp.contentType);
        client.println(F("Cache-Control: no-cache, no-store"));
        client.println(F("Connection: close"));
        client.print(F("Content-Length: ")); client.println((unsigned long)hresp.binaryLen);
        client.println();
        client.write(hresp.binaryData, hresp.binaryLen);
        if (hresp.freeFn) hresp.freeFn(hresp.binaryData);
    } else {
        ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
    }
}

static void ethernetHandleCameraConfigGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraConfigGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraConfigPost(EthernetClient& client, const String& body) {
    HttpRequest hreq; hreq.body = body;
    HttpResponse hresp;
    handleCameraConfigPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraPhotoPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraPhotoPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraRecordingStatusGet(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraRecordingStatusGet(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraRecordingStartPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraRecordingStartPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

static void ethernetHandleCameraRecordingStopPost(EthernetClient& client) {
    HttpRequest hreq;
    HttpResponse hresp;
    handleCameraRecordingStopPost(hreq, hresp);
    ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
}

#endif // ENABLE_CAMERA==1

