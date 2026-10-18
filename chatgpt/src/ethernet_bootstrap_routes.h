// ethernet_bootstrap_routes.h - Request dispatch for the Ethernet bootstrap server
// Implementation-include: include ONLY from ethernet.cpp inside #if ENABLE_ETHERNET==1
// Do NOT add include guards - this file must be included exactly once.
#include "webpages/routes/route_registry_api.h"
#include "webpages/renderers/proximity_dashboard_renderer.h"
#include "webpages/renderers/alerts_page_renderer.h"
#include "psram_helpers.h"
static String ethernetNormalizeEtagToken(String token) {
    token.trim();
    if (token.startsWith("W/")) token = token.substring(2);
    token.trim();
    if (token.length() >= 2 && token.startsWith("\"") && token.endsWith("\"")) {
        token = token.substring(1, token.length() - 1);
    }
    token.trim();
    return token;
}

static bool ethernetIfNoneMatchContainsEtag(const String& ifNoneMatch, const String& expected) {
    if (expected.length() == 0) return false;
    String raw = ifNoneMatch;
    raw.trim();
    if (raw.length() == 0) return false;
    if (raw == "*") return true;

    int start = 0;
    while (start < raw.length()) {
        int comma = raw.indexOf(',', start);
        String token = (comma < 0) ? raw.substring(start) : raw.substring(start, comma);
        token = ethernetNormalizeEtagToken(token);
        if (token == expected) return true;
        if (comma < 0) break;
        start = comma + 1;
    }
    return false;
}

static bool ethernetBootstrapHandleClient() {
    EthernetClient client = _ethHttpServer.available();
    if (!client) {
        return false;
    }

    // 100 ms covers the rare case where headers arrive in two TCP segments.
    // 500 ms (old value) blocked loop() for up to 500 ms per header line.
    client.setTimeout(100);
    // Adapter that splits large writes into 512-byte chunks, preventing W5500 SSIZE truncation.
    EthernetChunkedPrint chunked(client);
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();
    if (requestLine.length() == 0) {
        client.stop();
        return false;
    }

    int firstSpace = requestLine.indexOf(' ');
    int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    if (firstSpace <= 0 || secondSpace <= firstSpace) {
        client.stop();
        return false;
    }

    String method = requestLine.substring(0, firstSpace);
    String pathWithQuery = requestLine.substring(firstSpace + 1, secondSpace);
    String path = pathWithQuery;
    String query;
    int queryIndex = pathWithQuery.indexOf('?');
    if (queryIndex >= 0) {
        path = pathWithQuery.substring(0, queryIndex);
        query = pathWithQuery.substring(queryIndex + 1);
    }

    String authorizationHeader;
    String contentTypeHeader;
    String filenameHeader;
    String filesizeHeader;
    String ifNoneMatchHeader;
    String lastEventIdHeader;
    int contentLength = 0;

    while (client.connected()) {
        esp_task_wdt_reset(); // readStringUntil blocks up to setTimeout(100ms) per header line
        String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r" || headerLine.length() == 0) {
            break;
        }
        headerLine.trim();
        if (headerLine.startsWith(F("Authorization:"))) {
            authorizationHeader = headerLine.substring(strlen("Authorization:"));
            authorizationHeader.trim();
        } else if (headerLine.startsWith(F("Content-Type:"))) {
            contentTypeHeader = headerLine.substring(strlen("Content-Type:"));
            contentTypeHeader.trim();
        } else if (headerLine.startsWith(F("Content-Length:"))) {
            String value = headerLine.substring(strlen("Content-Length:"));
            value.trim();
            contentLength = value.toInt();
        } else if (headerLine.startsWith(F("X-Filename:"))) {
            filenameHeader = headerLine.substring(strlen("X-Filename:"));
            filenameHeader.trim();
        } else if (headerLine.startsWith(F("X-Filesize:"))) {
            filesizeHeader = headerLine.substring(strlen("X-Filesize:"));
            filesizeHeader.trim();
        } else if (headerLine.startsWith(F("If-None-Match:"))) {
            ifNoneMatchHeader = headerLine.substring(strlen("If-None-Match:"));
            ifNoneMatchHeader.trim();
            if (ifNoneMatchHeader.length() >= 2 && ifNoneMatchHeader.startsWith("\"") && ifNoneMatchHeader.endsWith("\"")) {
                ifNoneMatchHeader = ifNoneMatchHeader.substring(1, ifNoneMatchHeader.length() - 1);
            }
        } else if (headerLine.startsWith(F("Last-Event-ID:"))) {
            lastEventIdHeader = headerLine.substring(strlen("Last-Event-ID:"));
            lastEventIdHeader.trim();
        }
    }

    String body;
    bool isRawFirmwareUpload = method == F("POST") && path == F("/api/firmware-upload");
    bool isRawGrblUpload = method == F("POST") && path == F("/api/grbl/upload-gcode-stream");
    bool isRawGrblPsramUpload = method == F("POST") && path == F("/api/grbl/psram-upload");
    if (contentLength > 0 && !isRawFirmwareUpload && !isRawGrblUpload && !isRawGrblPsramUpload) {
        body.reserve(contentLength);
        // API JSON payloads arrive in one TCP segment (< 1460 B); 300 ms is very generous.
        // The old 1500 ms deadline wasted time on misbehaving or slow clients.
        unsigned long deadline = millis() + 300UL;
        while ((int)body.length() < contentLength && millis() < deadline) {
            while (client.available() && (int)body.length() < contentLength) {
                body += (char)client.read();
            }
            if ((int)body.length() >= contentLength) {
                break;
            }
            delay(1);
        }
    }

    bool authRequired = !(path == F("/healthz") || path == F("/favicon.ico"));
    if (authRequired && !ethernetIsAuthorized(authorizationHeader)) {
        ethernetSendUnauthorized(client);
        delay(1);
        client.stop();
        return false;
    }

    // Reset the task WDT before entering the route handler.
    // Page streaming holds loop() for multiple seconds (many small W5500 SPI transfers);
    // without this reset the TWDT fires after 120 s even if the system is healthy.
    esp_task_wdt_reset();

    if (method == F("GET") && path == F("/favicon.ico")) {
        client.println(F("HTTP/1.1 204 No Content"));
        client.println(F("Content-Type: image/x-icon"));
        client.println(F("Connection: close"));
        client.println(F("Content-Length: 0"));
        client.println();
    } else if (method == F("GET") && (path == F("/") || path == F("/index.html"))) {
        String homeEtag = buildHomePageEtag();
        if (ethernetIfNoneMatchContainsEtag(ifNoneMatchHeader, homeEtag)) {
            ethernetBootstrapSendNotModified(client, homeEtag.c_str(), "private, max-age=15, must-revalidate");
        } else {
            ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8", "private, max-age=15, must-revalidate", homeEtag.c_str(), "pt");
            streamSharedHomePage(chunked);
        }
    } else if (method == F("GET") && path == F("/status")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedStatusPage(chunked);
    } else if (method == F("GET") && path == F("/ethernet")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedEthernetPage(chunked);
    } else if (method == F("GET") && path == F("/usb-server")) {
#if ENABLE_USBIP_SERVER==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedUsbServerPage(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/sdcard")) {
#if ENABLE_PAGE_SDCARD==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedSdcardPage(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/monitor")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedMonitorPage(chunked);
    } else if (method == F("GET") && path == F("/grbl-controller")) {
#if ENABLE_GRBL
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedGrblControllerPage(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/buffer-viewer")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedBufferViewerPage(chunked);
    } else if (method == F("GET") && path == F("/serial-monitor")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedSerialMonitorPage(chunked);
    } else if (method == F("GET") && path == F("/files")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        ethernetStreamFilesPage(chunked, query);
    } else if (method == F("GET") && path == F("/wifi")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedWifiPage(chunked);
    } else if (method == F("GET") && path == F("/commands")) {
#if ENABLE_PAGE_COMMANDS_INTERVAL==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedCommandsPage(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/commands-by-datetime")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedCommandsByDatetimePage(chunked);
    } else if (method == F("GET") && path == F("/recipients")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedRecipientsPage(chunked);
    } else if (method == F("GET") && path == F("/access")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedAccessPage(chunked);
    } else if (method == F("GET") && path == F("/alerts-config")) {
        #if ENABLE_PAGE_ALERTS==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamAlertsPageToPrint(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/networks")) {
        #if ENABLE_TELNET==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedNetworksPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/api-reference")) {
        #if ENABLE_API_REFERENCE_PAGE==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedApiReferencePage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/proximity-dashboard")) {
        #if ENABLE_PROXIMITY==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamProximityDashboardPageToPrint(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/proximity")) {
        #if ENABLE_PROXIMITY==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedProximityPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/modbus-rtu")) {
        #if ENABLE_MODBUS_RTU==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedModbusRtuPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/modbus-tcp")) {
        #if ENABLE_MODBUS_TCP==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedModbusTcpPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/modbus-live")) {
        #if ENABLE_MODBUS==1
        {
            JsonVariant _mbSec = configManager.getSection("modbus");
            bool _mbActive = _mbSec["enabled"].as<bool>() &&
                             (_mbSec["rtu_enabled"].as<bool>() || _mbSec["tcp_enabled"].as<bool>());
            if (!_mbActive) {
                // Redirect home if Modbus is not active in config.
                ethernetBootstrapSendResponse(client, "302 Found", "text/html",
                    F("<html><head><meta http-equiv='refresh' content='0;url=/'></head></html>"));
            } else {
                ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
                streamSharedModbusLivePage(chunked);
            }
        }
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/do")) {
        #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedDoPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/rs232")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedRs232Page(chunked);
    } else if (method == F("GET") && path == F("/structures")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedStructuresPage(chunked);
    } else if (method == F("GET") && path == F("/printer")) {
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedPrinterPage(chunked);
    } else if (method == F("GET") && path == F("/lpt-db25")) {
#if ENABLE_DB25==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedLptDb25Page(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/email")) {
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedEmailPage(chunked);
#else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
#endif
    } else if (method == F("GET") && path == F("/google-script")) {
        #if ENABLE_PAGE_GOOGLE_SCRIPT==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedGoogleScriptPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/ftp")) {
        #if ENABLE_PAGE_FTP==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedFtpPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/ota")) {
        #if ENABLE_OTA==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedOtaPage(chunked, true);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/sensor-config")) {
        #if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedSensorConfigPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/sensor-charts")) {
        #if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
        if (configManager.getChartsPageEnabled()) {
            ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
            streamSharedSensorChartsPage(chunked);
        } else {
            ethernetBootstrapSendRedirect(client, String(F("/")));
        }
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/mq135-config")) {
        #if ENABLE_MQ135==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedMq135Page(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/buzzer")) {
        #if ENABLE_BUZZER==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedBuzzerPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/can")) {
        #if ENABLE_CAN==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedCanPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/hx711")) {
        #if ENABLE_HX711==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedHx711Page(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/ina226")) {
        #if ENABLE_INA226==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedIna226Page(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/sound")) {
        #if ENABLE_SOUND_SENSOR==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedSoundPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/uda1334a")) {
        #if ENABLE_UDA1334A==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedUda1334aPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/radio-ui")) {
        #if ENABLE_UDA1334A==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedRadioUiPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/max31865")) {
        #if ENABLE_MAX31865==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedMax31865Page(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/unikon")) {
        #if ENABLE_UNIKON==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedUnikonPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/ipg-laser")) {
        #if ENABLE_IPG_LASER==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedIpgLaserPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/camera")) {
        #if ENABLE_CAMERA==1
        ethernetBootstrapBeginStreamResponse(client, "200 OK", "text/html; charset=utf-8");
        streamSharedCameraPage(chunked);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("GET") && path == F("/api/lang")) {
        ethernetHandleLangGet(client, ifNoneMatchHeader);
    } else if (method == F("GET") && path == F("/api/bootstrap-status")) {
        ethernetBootstrapSendStatus(client);
    } else if (method == F("GET") && path == F("/api/status")) {
        ethernetBootstrapSendApiStatus(client);
    } else if (method == F("GET") && path == F("/api/nvs-stats")) {
        ethernetSendNvsStats(client);
    } else if (method == F("GET") && path == F("/core.css")) {
        if (ethernetIfNoneMatchContainsEtag(ifNoneMatchHeader, String(FIRMWARE_BUILD))) {
            ethernetBootstrapSendNotModified(client, FIRMWARE_BUILD);
        } else {
            ethernetBootstrapSendProgmemResponse(client, "200 OK", "text/css; charset=utf-8", CORE_CSS, strlen_P(CORE_CSS), FIRMWARE_BUILD);
        }
    } else if (method == F("GET") && path == F("/auto.js")) {
        if (ethernetIfNoneMatchContainsEtag(ifNoneMatchHeader, String(FIRMWARE_BUILD))) {
            ethernetBootstrapSendNotModified(client, FIRMWARE_BUILD);
        } else {
            ethernetBootstrapSendProgmemResponse(client, "200 OK", "application/javascript; charset=utf-8", AUTO_JS_BODY, strlen_P(AUTO_JS_BODY), FIRMWARE_BUILD);
        }
    } else if (method == F("GET") && path == F("/config-manager.js")) {
        if (ethernetIfNoneMatchContainsEtag(ifNoneMatchHeader, String(FIRMWARE_BUILD))) {
            ethernetBootstrapSendNotModified(client, FIRMWARE_BUILD);
        } else {
            ethernetBootstrapSendProgmemResponse(client, "200 OK", "application/javascript; charset=utf-8", CONFIG_MANAGER_JS_BODY, strlen_P(CONFIG_MANAGER_JS_BODY), FIRMWARE_BUILD);
        }
    } else if (method == F("GET") && path == F("/GoogleAppsScript_EmailHandler.js")) {
        ethernetBootstrapSendProgmemAttachmentResponse(client, "200 OK", "application/javascript; charset=utf-8", "GoogleAppsScript_EmailHandler.js", GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS, strlen_P(GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS));
    } else if (method == F("GET") && path == F("/api/wifi-status")) {
        ethernetBootstrapSendWifiStatus(client);
    } else if (method == F("GET") && path == F("/api/chart-caps")) {
        ethernetHandleChartCapsGet(client);
    } else if (method == F("GET") && path == F("/api/wifi-scan")) {
        ethernetHandleWifiScanGet(client);
    } else if (method == F("GET") && path == F("/api/config/wifi")) {
        // Narrow endpoint: only wifi_ssid (~50 bytes vs 130 KB for /api/configs).
        String ssid = configManager.getWifiSSID();
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        ethernetBootstrapSendResponse(client, "200 OK", "application/json",
            String("{\"wifi_ssid\":\"") + ssid + "\"}");
    } else if (method == F("GET") && path == F("/api/config/email")) {
        String s;
        {
            SpiRamJsonDocument doc(512);
            doc["smtp_server"]       = configManager.getSmtpServer();
            doc["smtp_port"]         = configManager.getSmtpPort();
            doc["smtp_user"]         = configManager.getSmtpUsername();
            doc["smtp_tls_insecure"] = configManager.getSmtpTlsInsecure();
            doc["google_script_url"] = configManager.getGoogleScriptUrl();
            doc["email_destination"] = configManager.getEmailDestination();
            JsonObject email = doc.createNestedObject("email");
            email["method"]      = configManager.getEmailMethod();
            email["destination"] = configManager.getEmailDestination();
            serializeJson(doc, s);
        }
        ethernetBootstrapSendResponse(client, "200 OK", "application/json", s);
    } else if (method == F("GET") && path == F("/api/config/rs232")) {
        String s;
        {
            SpiRamJsonDocument doc(2048);
#if ENABLE_RS232==1
            doc["rs232_enabled"] = configManager.getRs232Enabled();
            doc["sniffer_mode"]  = false;
#elif ENABLE_SNIFFER==1
            doc["rs232_enabled"] = false;
            doc["sniffer_mode"]  = true;
#else
            doc["rs232_enabled"] = false;
            doc["sniffer_mode"]  = false;
#endif
            int idx = configManager.getRs232BaudrateIndex();
            doc["rs232_baudrate_index"] = idx;
            doc["rs232_baudrate"] = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;
            JsonArray list = doc.createNestedArray("rs232_baudrate_list");
            for (int i = 0; i < RS232_BAUDRATES_COUNT; ++i) list.add(RS232_BAUDRATES[i]);
            doc["rs232_data_bits"]    = configManager.getRs232DataBits();
            doc["rs232_parity"]       = configManager.getRs232Parity();
            doc["rs232_stop_bits"]    = configManager.getRs232StopBits();
            doc["rs232_invert_pins"]  = configManager.getRs232InvertPins();
            doc["rs232_read_timeout"] = configManager.getRs232ReadTimeoutMs();
            doc["rs232_use_xon_xoff"] = configManager.getRs232UseXonXoff();
            doc["rs232_current_template"] = configManager.getRs232CurrentTemplate();
            JsonArray src = configManager.getRs232Templates();
            JsonArray dst = doc.createNestedArray("rs232_templates");
            for (JsonObject t : src) {
                JsonObject n = dst.createNestedObject();
                for (JsonPair kv : t) n[kv.key().c_str()] = kv.value();
            }
            serializeJson(doc, s);
        }
        ethernetBootstrapSendResponse(client, "200 OK", "application/json", s);
    } else if (method == F("GET") && path == F("/api/config/printer")) {
        String s;
        {
            SpiRamJsonDocument doc(512);
            doc["printer_emulation_mode"]   = configManager.getPrinterEmulationMode();
            doc["charset_mode"]             = configManager.getPrinterCharsetMode();
            doc["ntp_timezone"]             = configManager.getNtpTimezone();
            doc["timezone"]                 = configManager.getNtpTimezone();
            doc["ntp_server"]               = configManager.getNtpServer();
            doc["system_name"]              = configManager.getSystemName();
            doc["system_language"]          = configManager.getSystemLanguage();
            doc["autoEmailEnabledTimeout"]  = configManager.getTimeoutAutoEmailEnabled();
            doc["autoEmailDisabledTimeout"] = configManager.getTimeoutAutoEmailDisabled();
            JsonObject ntp = doc.createNestedObject("ntp");
            ntp["timezone"] = configManager.getNtpTimezone();
            ntp["server"]   = configManager.getNtpServer();
            JsonObject email = doc.createNestedObject("email");
            email["auto_send"]          = configManager.getEmailAutoSend();
            email["max_lines"]          = configManager.getEmailMaxLines();
            email["min_content_length"] = configManager.getEmailMinContentLength();
            serializeJson(doc, s);
        }
        ethernetBootstrapSendResponse(client, "200 OK", "application/json", s);
    } else if (method == F("GET") && path == F("/api/config/structures")) {
        String s;
        {
            SpiRamJsonDocument doc(256);
            JsonObject st = doc.createNestedObject("structures");
            st["auto_detect"]          = configManager.getStructuresAutoDetectEnabled();
            st["amada_filter_headers"] = configManager.getAmadaFilterHeaders();
            st["amada_header_every"]   = configManager.getAmadaHeaderEvery();
            JsonObject mtf1 = st.createNestedObject("amada_isq20_mtf1");
            mtf1["enabled"] = configManager.getAmadaISQ20MTF1Enabled();
            JsonObject awse = st.createNestedObject("amada_isq20_awse");
            awse["enabled"] = configManager.getAmadaISQ20AWSEEnabled();
            serializeJson(doc, s);
        }
        ethernetBootstrapSendResponse(client, "200 OK", "application/json", s);
    } else if (method == F("GET") && path == F("/api/sd/download")) {
        ethernetHandleSdDownloadGet(client, query);
    } else if (method == F("GET") && path == F("/api/config/saved_networks")) {
        ethernetHandleSavedNetworksGet(client);
    } else if (method == F("GET") && path == F("/api/config/access-point")) {
        ethernetHandleAccessPointGet(client);
    } else if (method == F("GET") && path == F("/api/heap-snapshot")) {
        ethernetHandleHeapSnapshotGet(client);
    } else if (method == F("GET") && path == F("/api/self-test")) {
        ethernetHandleSelfTestGet(client);
    } else if (method == F("GET") && path == F("/api/test-manifest")) {
        ethernetHandleTestManifestGet(client);
    #if ENABLE_MODBUS==1
    #if ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
    } else if (method == F("GET") && path == F("/api/modbus-chart-files")) {
        ethernetHandleModbusChartFilesGet(client);
    } else if (method == F("GET") && path == F("/api/modbus-chart-cols")) {
        ethernetHandleModbusChartColsGet(client, query);
    } else if (method == F("GET") && path == F("/api/modbus-chart-data")) {
        ethernetHandleModbusChartDataGet(client, query);
    #endif // ENABLE_RS232_CHARTS && ENABLE_SDCARD
    #endif // ENABLE_MODBUS==1
    // Note: sensor GET routes migrated to RouteRegistry
    #if ENABLE_UDA1334A==1
    #endif
    #if ENABLE_IPG_LASER==1
    } else if (method == F("GET") && path == F("/api/ipg-laser-status")) {
        ethernetHandleIpgLaserStatusGet(client);
    } else if (method == F("GET") && path == F("/api/ipg-laser-commands")) {
        ethernetHandleIpgLaserCommandsGet(client);
    } else if (method == F("GET") && path == F("/api/ipg-laser-log")) {
        ethernetHandleIpgLaserLogGet(client);
    #endif
    #if ENABLE_CAMERA==1
    } else if (method == F("GET") && path == F("/cam/snapshot")) {
        ethernetHandleCameraSnapshotGet(client);
    } else if (method == F("GET") && path == F("/cam/motion/diff")) {
        ethernetHandleCameraMotionDiffGet(client);
    } else if (method == F("GET") && path == F("/cam/stream")) {
        ethernetHandleCameraStreamGet(client);
    #endif // ENABLE_CAMERA
    } else if (method == F("GET") && path == F("/api/sd-buffers")) {
        ethernetHandleSdBuffersGet(client);
    } else if (method == F("GET") && path == F("/api/rs232-cols")) {
        #if ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
        ethernetHandleRs232ColsGet(client, query);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("GET") && path == F("/api/rs232-history-files")) {
        #if ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
        ethernetHandleRs232HistoryFilesGet(client);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("GET") && path == F("/api/rs232-history")) {
        #if ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
        ethernetHandleRs232HistoryGet(client, query);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("GET") && path == F("/api/rs232-templates")) {
        ethernetSendRs232Templates(client);
    } else if (method == F("GET") && path == F("/api/recipients")) {
        ethernetSendRecipientsList(client, query);
    } else if (method == F("GET") && path == F("/api/recipient-slots")) {
        ethernetSendRecipientSlots(client);
    } else if (method == F("GET") && path == F("/api/configs")) {
        ethernetSendSavedConfigs(client);
    } else if (method == F("GET") && path == F("/api/config")) {
        ethernetSendUnifiedConfig(client, query);
    } else if (method == F("GET") && path == F("/api/grbl/read-gcode-text")) {
        ethernetHandleGrblReadGcodeTextGet(client, query);
    } else if (method == F("GET") && path == F("/api/monitor-status")) {
        ethernetSendMonitorStatus(client);
    } else if (method == F("GET") && path == F("/api/serial-monitor")) {
        ethernetHandleSerialMonitorGet(client, query);
    } else if (method == F("GET") && path == F("/api/received-text")) {
        ethernetHandleReceivedTextGet(client, query);
    #if ENABLE_UDA1334A==1
    } else if (method == F("POST") && path == F("/api/radio/queue")) {
        ethernetHandleRadioQueuePost(client, body);
    } else if (method == F("POST") && path == F("/api/radio/eq")) {
        ethernetHandleRadioEqPost(client, body);
    } else if (method == F("POST") && path == F("/api/radio/schedule")) {
        ethernetHandleRadioSchedulePost(client, body);
    #endif
    } else if (method == F("POST") && path == F("/api/sd/delete-multiple")) {
        ethernetHandleSdDeleteMultiplePost(client, body, query);
    } else if (method == F("POST") && path == F("/api/test")) {
        ethernetHandleTestPost(client);
    } else if (method == F("POST") && path == F("/api/system-command")) {
        ethernetHandleSystemCommandPost(client, body);
    } else if (method == F("POST") && path == F("/api/rs232-command")) {
        ethernetHandleRs232CommandPost(client, body);
#if ENABLE_MODBUS==1
    } else if (method == F("POST") && path == F("/api/modbus-command")) {
        ethernetHandleModbusCommandPost(client, body);
#endif
    } else if (method == F("POST") && path == F("/api/rs232-templates")) {
        ethernetHandleRs232TemplatesPost(client, body);
    } else if (method == F("POST") && path == F("/api/rs232-apply")) {
        ethernetHandleRs232ApplyPost(client, body);
    } else if (method == F("POST") && path == F("/api/config")) {
        ethernetHandleUnifiedConfigPost(client, body);
    } else if (method == F("POST") && path == F("/api/config/saved_networks")) {
        ethernetHandleSavedNetworksPost(client, body);
    } else if (method == F("POST") && path == F("/api/config/access-point")) {
        ethernetHandleAccessPointPost(client, body);
    } else if (method == F("POST") && path == F("/api/sensor-config")) {
        #if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
        ethernetHandleSensorConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif


    } else if (method == F("POST") && path == F("/api/mq135-config")) {
        #if ENABLE_MQ135==1
        ethernetHandleMq135ConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("POST") && path == F("/api/hx711-config")) {
        #if ENABLE_HX711==1
        ethernetHandleHx711ConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("POST") && path == F("/api/ina226-config")) {
        #if ENABLE_INA226==1
        ethernetHandleIna226ConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("POST") && path == F("/api/sound-config")) {
        #if ENABLE_SOUND_SENSOR==1
        ethernetHandleSoundConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    } else if (method == F("POST") && path == F("/api/max31865-config")) {
        #if ENABLE_MAX31865==1
        ethernetHandleMax31865ConfigPost(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    #if ENABLE_BUZZER==1
    } else if (method == F("POST") && path == F("/api/buzzer-config")) {
        ethernetHandleBuzzerConfigPost(client, body);
    } else if (method == F("POST") && path == F("/api/buzzer-test")) {
        ethernetHandleBuzzerTestPost(client, body);
    #endif
    #if ENABLE_UDA1334A==1
    } else if (method == F("POST") && path == F("/api/uda1334a-config")) {
        ethernetHandleUda1334aConfigPost(client, body);
    #endif
    #if ENABLE_CAN==1
    } else if (method == F("POST") && path == F("/api/can/send")) {
        ethernetHandleCanSendPost(client, body);
    } else if (method == F("POST") && path == F("/api/can/config")) {
        ethernetHandleCanConfigPost(client, body);
    #endif
    #if ENABLE_UNIKON==1
    } else if (method == F("POST") && path == F("/api/unikon-config")) {
        ethernetHandleUnikonConfigPost(client, body);
    #endif
    #if ENABLE_IPG_LASER==1
    } else if (method == F("POST") && path == F("/api/ipg-laser-send")) {
        ethernetHandleIpgLaserSendPost(client, body);
    #endif
    } else if (method == F("POST") && path == F("/api/wifi-switch-network")) {
        ethernetHandleWifiSwitchNetworkPost(client, body);
    } else if (method == F("POST") && path == F("/api/wifi-disable")) {
        ethernetHandleWifiDisablePost(client);
    } else if (method == F("POST") && path == F("/api/access-config")) {
        ethernetHandleAccessConfigPost(client, body);
    #if ENABLE_PROXIMITY==1
    } else if (method == F("POST") && path == F("/api/proximity/config")) {
        ethernetHandleProximityConfigPost(client, body);
    } else if (method == F("POST") && path == F("/api/proximity/shift-config")) {
        ethernetHandleProximityShiftConfigPost(client, body);
    } else if (method == F("POST") && path == F("/api/proximity/shiftday-emails")) {
        ethernetHandleProximityShiftdayEmailsPost(client, body);
    } else if (method == F("POST") && path == F("/api/proximity/save-all")) {
        ethernetHandleProximitySaveAll(client, body);
    } else if (method == F("POST") && path == F("/api/proximity/tca9554")) {
        #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
        ethernetHandleProximityTca9554Post(client, body);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "application/json", String(F("{\"error\":\"not found\"}")));
        #endif
    #endif // ENABLE_PROXIMITY==1
    #if ENABLE_MODBUS==1
    } else if (method == F("POST") && path == F("/api/modbus/config")) {
        ethernetHandleModbusConfigPost(client, body);
    } else if (method == F("POST") && path == F("/api/modbus/poll-now")) {
        ethernetHandleModbusPollNowPost(client, body);
    } else if (method == F("POST") && path == F("/api/modbus/rtu-write")) {
        ethernetHandleModbusRtuWritePost(client, body);
    } else if (method == F("POST") && path == F("/api/modbus/scan-rtu")) {
        ethernetHandleModbusScanRtuPost(client, body);
    } else if (method == F("POST") && path == F("/api/modbus/scan-tcp")) {
        ethernetHandleModbusScanTcpPost(client, body);
    } else if (method == F("POST") && path == F("/api/modbus/write")) {
        ethernetHandleModbusWritePost(client, body);
    #endif // ENABLE_MODBUS==1
    #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
    } else if (method == F("POST") && path == F("/api/do/set")) {
        ethernetHandleDoSetPost(client, body);
    } else if (method == F("POST") && path == F("/api/do/config")) {
        ethernetHandleDoConfigPost(client, body);
    #endif // BOARD_HAS_TCA9554_DO

    } else if (method == F("POST") && path == F("/api/firmware-upload")) {
        #if ENABLE_OTA==1
        (void)contentTypeHeader;
        ethernetHandleFirmwareUpload(client, contentLength, filenameHeader, filesizeHeader);
        #else
        ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
        #endif
    } else if (method == F("POST") && path == F("/api/restart")) {
        ethernetHandleRestartPost(client);
    } else if (method == F("POST") && path == F("/api/factory-reset")) {
        ethernetHandleFactoryResetPost(client);
    } else if (method == F("POST") && path == F("/api/sd-action")) {
        ethernetHandleSdActionPost(client, body);
    } else if (method == F("POST") && path == F("/api/grbl/upload-gcode-stream")) {
        (void)contentTypeHeader;
        ethernetHandleGrblUploadGcodeStreamPost(client, query, contentLength, filenameHeader, filesizeHeader);
    } else if (method == F("POST") && path == F("/api/grbl/psram-upload")) {
        (void)contentTypeHeader;
        ethernetHandleGrblPsramUploadPost(client, query, contentLength, filenameHeader);
    } else if (method == F("DELETE") && path == F("/api/rs232-templates")) {
        ethernetHandleRs232TemplatesDelete(client, query);
    } else if (method == F("DELETE") && path == F("/api/config/saved_networks")) {
        ethernetHandleSavedNetworksDelete(client, body, query);
    } else if (method == F("GET") && path == F("/ui")) {
        String fullUiUrl = ethernetBootstrapGetUiUrl();
        if (fullUiUrl.length() > 0) {
            ethernetBootstrapSendRedirect(client, fullUiUrl);
        } else {
            ethernetBootstrapSendResponse(client, "503 Service Unavailable", "application/json", String(F("{\"status\":\"unavailable\",\"reason\":\"full_ui_offline\"}")));
        }
    } else if (method == F("GET") && path == F("/healthz")) {
        ethernetBootstrapSendResponse(client, "200 OK", "text/plain", String(F("ok")));
    } else {
        // API route registry dispatch (replaces per-module if/else chains)
        ApiRouteHandler apiHandler = findApiRouteHandler(method, path);
        if (apiHandler) {
            HttpRequest hreq;
            hreq.method = method;
            hreq.path = path;
            hreq.query = query;
            hreq.body = body;
            // Form bodies arrive URL-encoded; decode once so unified handlers (which
            // expect decoded values, like the WiFi path) work on Ethernet too.
            if (contentTypeHeader.indexOf(F("application/x-www-form-urlencoded")) >= 0) {
                hreq.body = ethernetUrlDecode(body);
            }
            hreq.contentType = contentTypeHeader;
            HttpResponse hresp;
            apiHandler(hreq, hresp);
            if (hresp.isBinary && hresp.binaryData) {
                client.println(F("HTTP/1.1 200 OK"));
                client.print(F("Content-Type: ")); client.println(hresp.contentType);
                client.println(F("Cache-Control: no-cache, no-store"));
                client.println(F("Connection: close"));
                client.print(F("Content-Length: ")); client.println((unsigned long)hresp.binaryLen);
                client.println();
                client.write(hresp.binaryData, hresp.binaryLen);
                if (hresp.freeFn) hresp.freeFn(hresp.binaryData);
            } else if (hresp.isRedirect) {
                ethernetBootstrapSendRedirect(client, hresp.redirectUrl);
            } else if (hresp.isNotModified) {
                ethernetBootstrapSendNotModified(client, "", "private, max-age=15, must-revalidate");
            } else {
                ethernetBootstrapSendResponse(client, httpStatusToString(hresp.code).c_str(), hresp.contentType.c_str(), hresp.body);
            }
        } else {
            // Path existence lookup for correct 405 vs 404
            if (isKnownApiPath(path)) {
                ethernetBootstrapSendResponse(client, "405 Method Not Allowed", "text/plain", String(F("method not allowed")));
            } else {
                ethernetBootstrapSendResponse(client, "404 Not Found", "text/plain", String(F("not found")));
            }
        }
    }

    chunked.flushBuf();
    // Flush the W5500 TX buffer before sending FIN so the peer receives all data
    // before the socket closes. Without this, the W5500 may send RST instead of FIN
    // if there is still unacknowledged data in the TX buffer, leaving the socket in
    // CLOSE_WAIT longer and slowing down socket reuse.
    client.flush();
    delay(15); // Give W5500 more time to queue final bytes before TCP FIN on large pages.
    client.stop();
    return true;
}
