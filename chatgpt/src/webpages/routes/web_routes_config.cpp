#include "../../languages/lang_active.h"
#include "web_routes_config.h"
#include "route_registry_api.h"

#include "../../feature_flags.h"

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
#include "../handlers/email_handlers.h"
#include "../handlers_unified/email_handler.h"
#endif
#if ENABLE_FTP_CLIENT==1
#include "../handlers_unified/ftp_handler.h"
#endif
#if ENABLE_PAGE_RECIPIENTS==1
#include "../handlers/recipients_handler.h"
#endif
#include "../handlers_unified/recipients_handler.h"
#include "../handlers/config_snapshot_handler.h"
#include "../handlers_unified/config_handler.h"
#include "../http_abstraction.h"
#include "../handlers_unified/sensor_handler.h"
#include "../handlers/rs232_template_handler.h"
#include "../handlers_unified/rs232_handler.h"
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
#include "../renderers/email_page_renderer.h"
#endif
#if ENABLE_PAGE_FTP==1
#include "../renderers/ftp_page_renderer.h"
#endif
#include "../renderers/mq135_page_renderer.h"
#include "../renderers/hx711_page_renderer.h"
#include "../renderers/ina226_page_renderer.h"
#include "../renderers/sound_page_renderer.h"
#include "../renderers/max31865_page_renderer.h"
#if ENABLE_PAGE_RECIPIENTS==1
#include "../renderers/recipients_page_renderer.h"
#endif
#if ENABLE_PAGE_ALERTS==1
#include "../renderers/alerts_page_renderer.h"
#endif
#include "../renderers/rs232_page_renderer.h"
#include "../renderers/sensor_config_page_renderer.h"
#include "../renderers/page_render_common.h"
#include "../assets/page_mq135_config_asset.h"
#include "../assets/page_sensor_config_asset.h"
#if ENABLE_STRUCTURES_PAGE==1
#include "../renderers/structures_page_renderer.h"
#endif // ENABLE_STRUCTURES_PAGE

void registerConfigRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/config", handleConfigGet);
    registerApiRoute("POST", "/api/config", handleConfigPost);
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    registerApiRoute("POST", "/api/email-test", handleEmailTestPost);
#if ENABLE_SMTP==1
    registerApiRoute("POST", "/api/email-reset", handleEmailResetPost);
#endif
    registerEmailApiRoutes(server, context);
#endif
#if ENABLE_FTP_CLIENT==1
    registerApiRoute("GET",  "/api/ftp-status", handleFtpStatusGet);
    registerApiRoute("POST", "/api/ftp-test", handleFtpTestPost);
    registerApiRoute("POST", "/api/ftp-upload-now", handleFtpUploadNowPost);
    registerApiRoute("GET",  "/api/ftp-folders", handleFtpFoldersGet);
#endif
    registerConfigSnapshotRoutes(server, context);
    registerSensorApiRoutes(server, context);
#if ENABLE_PAGE_ALERTS==1
    registerApiRoute("GET",  "/api/inactivity-alert-config",   handleInactivityAlertConfigGet);
    registerApiRoute("POST", "/api/inactivity-alert-config",   handleInactivityAlertConfigPost);
    registerApiRoute("POST", "/api/inactivity-alert-test",     handleInactivityAlertTestPost);
#if ENABLE_PROXIMITY==1
    registerApiRoute("GET",  "/api/oee-alert-config",          handleOeeAlertConfigGet);
    registerApiRoute("POST", "/api/oee-alert-config",          handleOeeAlertConfigPost);
#endif
#endif
#if ENABLE_PAGE_RECIPIENTS==1
    registerApiRoute("GET",  "/api/recipient-slot",            handleRecipientSlotGet);
    registerApiRoute("POST", "/api/recipient-slot",            handleRecipientSlotPost);
    registerApiRoute("GET",  "/api/emailday-extra-emails",     handleEmaildayExtraEmailsGet);
    registerApiRoute("POST", "/api/emailday-extra-emails",     handleEmaildayExtraEmailsPost);
    registerApiRoute("GET",  "/api/sensorday-extra-emails",    handleSensordayExtraEmailsGet);
    registerApiRoute("POST", "/api/sensorday-extra-emails",    handleSensordayExtraEmailsPost);
    registerApiRoute("GET",  "/api/logday-extra-emails",      handleLogdayExtraEmailsGet);
    registerApiRoute("POST", "/api/logday-extra-emails",      handleLogdayExtraEmailsPost);
    registerRecipientRoutes(server, context);
#endif
#if ENABLE_RS232==1
    registerApiRoute("GET",  "/api/rs232-status", handleRs232StatusGet);
    registerApiRoute("POST", "/api/test-rs232",   handleTestRs232Post);
    registerApiRoute("POST", "/api/rs232/persist-current-template", handleRs232PersistCurrentTemplatePost);
    registerRs232TemplateRoutes(server, context);
#endif

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    server.on("/email", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamEmailPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/email", bytes, false);
#endif
    });
#endif

#if ENABLE_PAGE_FTP==1
    server.on("/ftp", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamFtpPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ftp", bytes, false);
#endif
    });
#endif

#if ENABLE_STRUCTURES_PAGE==1
    server.on("/structures", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamStructuresPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/structures", bytes, false);
#endif
    });
#endif // ENABLE_STRUCTURES_PAGE

#if ENABLE_PAGE_RECIPIENTS==1
    server.on("/recipients", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-store");
        const size_t bytes = streamRecipientsPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/recipients", bytes, false);
#endif
    });
#endif // ENABLE_PAGE_RECIPIENTS

#if ENABLE_PAGE_ALERTS==1
    server.on("/alerts-config", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        response->addHeader("Cache-Control", "no-store");
        const size_t bytes = streamAlertsPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/alerts-config", bytes, false);
#endif
    });
#endif // ENABLE_PAGE_ALERTS

    server.on("/rs232", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        response->addHeader("Content-Language", "pt");
        const size_t bytes = streamRs232PageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/rs232", bytes, false);
#endif
    });

#if ENABLE_MQ135==1
    server.on("/mq135-config", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static String _lang;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (_lang != activeLangCode) {
            buildPageFrame(_pre, _suf, L(activeLang->pageTitleMq135).c_str(), true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(MQ135_CONFIG_PAGE);
            _lang = activeLangCode;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, MQ135_CONFIG_PAGE, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/mq135-config", pLen + aLen + sLen, false);
#endif
    });
#endif

#if ENABLE_HX711==1
    server.on("/hx711", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamHx711PageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/hx711", bytes, false);
#endif
    });
#endif

#if ENABLE_INA226==1
    server.on("/ina226", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamIna226PageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/ina226", bytes, false);
#endif
    });
#endif

#if ENABLE_SOUND_SENSOR==1
    server.on("/sound", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamSoundPageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/sound", bytes, false);
#endif
    });
#endif

#if ENABLE_MAX31865==1
    server.on("/max31865", HTTP_GET, [&context](AsyncWebServerRequest* request) {
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
        const size_t bytes = streamMax31865PageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/max31865", bytes, false);
#endif
    });
#endif

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    server.on("/sensor-config", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static String _lang;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (_lang != activeLangCode) {
            buildPageFrame(_pre, _suf, L(activeLang->pageTitleAmbient).c_str(), true,
                "<script src='/config-manager.js'></script>");
            _aLen = strlen_P(SENSOR_CONFIG_PAGE);
            _lang = activeLangCode;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, SENSOR_CONFIG_PAGE, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/sensor-config", pLen + aLen + sLen, false);
#endif
    });
#endif
}

// ---- Sensor API route registration (inlined from sensor_handler.cpp) ----------------
// ---- Body accumulators for POST handlers (each guarded by its feature flag) --------
struct SensorBodyAccum {
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    static void sensorConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleSensorConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
#if ENABLE_MQ135==1
    static void mq135Config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleMq135ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
#if ENABLE_HX711==1
    static void hx711Config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleHx711ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
#if ENABLE_INA226==1
    static void ina226Config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleIna226ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
#if ENABLE_MAX31865==1
    static void max31865Config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleMax31865ConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
#if ENABLE_SOUND_SENSOR==1
    static void soundConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body.clear();
        body.reserve(body.length() + len);
        for (size_t i = 0; i < len; i++) body += (char)data[i];
        if (index + len != total) return;
        HttpRequest hreq;
        hreq.body = body;
        hreq.contentType = F("application/json");
        HttpResponse hresp;
        handleSoundConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
#endif
};

void registerSensorApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    static bool _registered = false;
    if (_registered) return;
    _registered = true;

    registerApiRoute("GET",  "/api/sensor-flags", handleSensorFlagsGet);

    // ---- Chart capabilities ---------------------------------------
    server.on("/api/chart-caps", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleChartCapsGet(hreq, hresp);
        AsyncWebServerResponse* resp = request->beginResponse(hresp.code, hresp.contentType, hresp.body);
        resp->addHeader("Cache-Control", "no-cache");
        request->send(resp);
    });

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    registerApiRoute("GET",    "/api/sensor-config",            handleSensorConfigGet);
    registerApiRoute("POST",   "/api/sensor-measure",           handleSensorMeasurePost);
    registerApiRoute("POST",   "/api/sensor-reset-stats",       handleSensorResetStatsPost);
    registerApiRoute("GET",    "/api/sensor-slots",             handleSensorSlotsGet);
    registerApiRoute("POST",   "/api/sensor-flush",             handleSensorFlushPost);
#endif

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
    registerApiRoute("POST",   "/api/sensor-charts-session",    handleSensorChartsSessionPost);
    registerApiRoute("POST",   "/api/sensor-charts-heartbeat",  handleSensorChartsHeartbeatPost);
    registerApiRoute("DELETE", "/api/sensor-charts-session",    handleSensorChartsSessionDelete);
    registerApiRoute("POST",   "/api/sensor-charts-session-end", handleSensorChartsSessionEndPost);
#endif

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
    registerApiRoute("GET",    "/api/sensor-history",           handleSensorHistoryGet);
    registerApiRoute("GET",    "/api/sensor-history-files",     handleSensorHistoryFilesGet);
    registerApiRoute("GET",    "/api/sensor-history-file",      handleSensorHistoryFileGet);
#endif

#if ENABLE_MQ135==1
    registerApiRoute("GET",  "/api/mq135-config",      handleMq135ConfigGet);
    registerApiRoute("POST", "/api/mq135-measure",     handleMq135MeasurePost);
    registerApiRoute("POST", "/api/mq135-calibrate-r0", handleMq135CalibrateR0Post);
#endif

#if ENABLE_HX711==1
    registerApiRoute("GET",  "/api/hx711-config",  handleHx711ConfigGet);
    registerApiRoute("POST", "/api/hx711-measure", handleHx711MeasurePost);
    registerApiRoute("POST", "/api/hx711-tare",    handleHx711TarePost);
#endif

#if ENABLE_INA226==1
    registerApiRoute("GET",  "/api/ina226-config",  handleIna226ConfigGet);
    registerApiRoute("POST", "/api/ina226-measure", handleIna226MeasurePost);
#endif

#if ENABLE_MAX31865==1
    registerApiRoute("GET",  "/api/max31865-config",  handleMax31865ConfigGet);
    registerApiRoute("POST", "/api/max31865-measure", handleMax31865MeasurePost);
#endif

#if ENABLE_SOUND_SENSOR==1
    registerApiRoute("GET",  "/api/sound-config",  handleSoundConfigGet);
    registerApiRoute("POST", "/api/sound-measure", handleSoundMeasurePost);
#endif

    // ---- Body-accum POST routes (stay manual) ----------------------------------------
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    server.on("/api/sensor-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::sensorConfig(request, data, len, index, total);
        });
#endif

#if ENABLE_MQ135==1
    server.on("/api/mq135-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::mq135Config(request, data, len, index, total);
        });
#endif

#if ENABLE_HX711==1
    server.on("/api/hx711-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::hx711Config(request, data, len, index, total);
        });
#endif

#if ENABLE_INA226==1
    server.on("/api/ina226-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::ina226Config(request, data, len, index, total);
        });
#endif

#if ENABLE_MAX31865==1
    server.on("/api/max31865-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::max31865Config(request, data, len, index, total);
        });
#endif

#if ENABLE_SOUND_SENSOR==1
    server.on("/api/sound-config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            SensorBodyAccum::soundConfig(request, data, len, index, total);
        });
#endif
}