#include "route_registry_api.h"
#include "web_route_context.h"
#include <ESPAsyncWebServer.h>
#include "../../touch7_display.h" // T7-29 cross-task flash lock (no-op off T7)

// Local-processing heartbeat without pulling webpage_impl.h (which needs the
// full server context): refresh the viewer's multi-track slot so active
// local viewers (e.g. /camera polling /api/camera/status every 5 s) are
// never kicked by the stale-tracker. Declared in webpage_state.cpp.
extern void webLocalPageHeartbeat(AsyncWebServerRequest* request);

struct ApiRouteEntry {
    const char* method;
    const char* path;
    ApiRouteHandler handler;
};

static ApiRouteEntry s_routes[MAX_API_ROUTES];
static size_t s_count = 0;

void registerApiRoute(const char* method, const char* path, ApiRouteHandler handler) {
    if (s_count >= MAX_API_ROUTES) return;
    s_routes[s_count].method = method;
    s_routes[s_count].path = path;
    s_routes[s_count].handler = handler;
    s_count++;
}

void registerApiRoutesToServer(AsyncWebServer& server, const WebRouteContext& context) {
    for (size_t i = 0; i < s_count; i++) {
        auto& r = s_routes[i];
        auto httpMethod = HTTP_GET;
        if (strcmp_P(r.method, PSTR("POST")) == 0) httpMethod = HTTP_POST;
        else if (strcmp_P(r.method, PSTR("DELETE")) == 0) httpMethod = HTTP_DELETE;
        else if (strcmp_P(r.method, PSTR("PUT")) == 0) httpMethod = HTTP_PUT;
        else if (strcmp_P(r.method, PSTR("PATCH")) == 0) httpMethod = HTTP_PATCH;

        server.on(r.path, httpMethod, [&context, handler = r.handler](AsyncWebServerRequest* req) {
            // Raw-body stash (see onBody below) must be freed on EVERY exit.
            auto freeRawBody = [&]() {
                if (req->_tempObject) {
                    delete (String*)req->_tempObject;
                    req->_tempObject = nullptr;
                }
            };
            if (!context.ensureBasicAuth(req)) { freeRawBody(); return; }
            // Local-processing heartbeat: pages like /camera poll their
            // status endpoint every few seconds; refresh the viewer's
            // multi-track slot here so active local viewers are never
            // kicked by the stale-tracker (webLocalPageHeartbeat).
            webLocalPageHeartbeat(req);
            HttpRequest hreq;
            hreq.path = req->url();
            hreq.method = (req->method() == HTTP_POST) ? "POST" : (req->method() == HTTP_GET) ? "GET" : "OTHER";
            for (int i = 0; i < req->params(); i++) {
                auto* p = req->getParam(i);
                if (p->name() != "body" && p->name() != "_html") {
                    if (hreq.query.length() > 0) hreq.query += "&";
                    hreq.query += p->name() + "=" + p->value();
                    if (req->method() == HTTP_POST || req->method() == HTTP_DELETE || req->method() == HTTP_PUT || req->method() == HTTP_PATCH) {
                        if (hreq.body.length() > 0) hreq.body += "&";
                        hreq.body += p->name() + "=" + p->value();
                    }
                }
            }
            if (hreq.body.length() > 0) {
                hreq.contentType = F("application/x-www-form-urlencoded");
            }
            // Raw (non-form) bodies, e.g. JSON APIs: onBody stashed them in
            // _tempObject (form/multipart traffic never touches it). Raw wins
            // over params so handlers see the exact posted document.
            if (req->_tempObject) {
                String* raw = (String*)req->_tempObject;
                req->_tempObject = nullptr;
                if (raw->length() > 0) {
                    hreq.body = *raw;
                    hreq.contentType = req->contentType();
                }
                delete raw;
            }
            HttpResponse hresp;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            // TouchMeteo-7: with the RGB bounce buffer, ANY flash op while the
            // panel streams panics (bounce ISR copies PSRAM during a cache
            // suspend). Suspend (del) the panel around mutations so their flash
            // writes are safe. Frequent non-flash actions (remote tap, peer
            // refresh, weather refresh enqueue) skip the panel blackout.
            const bool t7mut = (req->method() == HTTP_POST || req->method() == HTTP_PUT ||
                                req->method() == HTTP_PATCH || req->method() == HTTP_DELETE);
            const String t7url = req->url();
            const bool t7noflash = t7url.indexOf("/api/touch7-tap") >= 0 ||
                                   t7url.indexOf("/api/touch7-peer-refresh") >= 0 ||
                                   t7url.indexOf("/api/touch7-weather-refresh") >= 0;
            const bool t7hold = t7mut && !t7noflash;
            if (t7hold) touch7PanelSuspend();
#endif
            handler(hreq, hresp);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
            if (t7hold) touch7PanelResume();
#endif
            if (hresp.isBinary && hresp.binaryData) {
                AsyncWebServerResponse* resp = req->beginResponse(hresp.code, hresp.contentType, hresp.binaryData, hresp.binaryLen);
                resp->addHeader("Cache-Control", "no-cache");
                if (hresp.frameHash.length() > 0) resp->addHeader("X-Frame-Hash", hresp.frameHash);
                if (hresp.freeFn) {
                    const uint8_t* data = hresp.binaryData;
                    auto freeFn = hresp.freeFn;
                    req->onDisconnect([data, freeFn]() { freeFn(data); });
                }
                req->send(resp);
            } else if (hresp.isRedirect) {
                req->redirect(hresp.redirectUrl);
            } else if (hresp.isNotModified) {
                req->send(304);
            } else {
                req->send(hresp.code, hresp.contentType, hresp.body);
            }
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            // Stash raw request bodies for the onRequest handler above.
            // Form-urlencoded and multipart traffic keep flowing through the
            // params path untouched (zero extra RAM for those).
            if (index == 0) {
                if (req->_tempObject) {
                    delete (String*)req->_tempObject;
                    req->_tempObject = nullptr;
                }
                const String ct = req->contentType();
                if (ct.startsWith(F("application/x-www-form-urlencoded")) || ct.startsWith(F("multipart/"))) return;
                if (total > 4096) return;  // not ours to buffer; handler reports it
                req->_tempObject = new String();
                if (total > 0) ((String*)req->_tempObject)->reserve(total);
            }
            String* s = (String*)req->_tempObject;
            if (s && data && len > 0) {
                const size_t room = 4096 - s->length();
                if (room > 0) s->concat((const char*)data, len < room ? len : room);
            }
        });
    }
}

ApiRouteHandler findApiRouteHandler(const String& method, const String& path) {
    for (size_t i = 0; i < s_count; i++) {
        if (method == s_routes[i].method && path == s_routes[i].path) {
            return s_routes[i].handler;
        }
    }
    return nullptr;
}

size_t registeredApiRouteCount() {
    return s_count;
}

bool registeredApiRouteAt(size_t i, const char*& method, const char*& path) {
    if (i >= s_count) return false;
    method = s_routes[i].method;
    path = s_routes[i].path;
    return true;
}

// ── Legacy route table (method+path only, no handlers) ──────────────────────
// Originally from route_registry.h/.cpp. Kept here for 405 vs 404 fallback.
// Do NOT add new entries — use registerApiRoute() instead.

struct LegacyRoute {
    const char* method;
    const char* path;
};

static const LegacyRoute s_legacyRoutes[] = {
    {"GET", "/access"},
    {"GET", "/alerts-config"},
    {"GET", "/api/access-config"},
    {"GET", "/api/bootstrap-status"},
    {"GET", "/api/bricon-arrivals"},
    {"GET", "/api/bricon-contests"},
    {"GET", "/api/bricon-pombal"},
    {"GET", "/api/bricon-registrations"},
    {"GET", "/api/bricon-status"},
    {"GET", "/api/buzzer-config"},
    {"GET", "/api/camera/config"},
    {"GET", "/api/camera/recording"},
    {"GET", "/api/camera/status"},
    {"GET", "/api/can/config"},
    {"GET", "/api/can/frames"},
    {"GET", "/api/can/status"},
    {"GET", "/api/chart-caps"},
    {"GET", "/api/config"},
    {"GET", "/api/config/access-point"},
    {"GET", "/api/config/email"},
    {"GET", "/api/config/printer"},
    {"GET", "/api/config/rs232"},
    {"GET", "/api/config/saved_networks"},
    {"GET", "/api/ftp-status"},
    {"GET", "/api/ftp-folders"},
    {"GET", "/api/config/structures"},
    {"GET", "/api/config/wifi"},
    {"GET", "/api/configs"},
    {"GET", "/api/daily-reboot"},
    {"GET", "/api/db25-inject"},
    {"GET", "/api/do/status"},
    {"GET", "/api/emailday-extra-emails"},
    {"GET", "/api/eth-status"},
    {"GET", "/api/grbl/control-token"},
    {"GET", "/api/grbl/debug"},
    {"GET", "/api/grbl/events"},
    {"GET", "/api/grbl/frame-bbox"},
    {"GET", "/api/grbl/list-gcode"},
    {"GET", "/api/grbl/psram-file"},
    {"GET", "/api/grbl/psram-read-text"},
    {"GET", "/api/grbl/read-gcode-text"},
    {"GET", "/api/grbl/status"},
    {"GET", "/api/heap-snapshot"},
    {"GET", "/api/hx711-config"},
    {"GET", "/api/ina226-config"},
    {"GET", "/api/ipg-laser-available"},
    {"GET", "/api/ipg-laser-commands"},
    {"GET", "/api/ipg-laser-log"},
    {"GET", "/api/ipg-laser-status"},
    {"GET", "/api/inactivity-alert-config"},
    {"GET", "/api/oee-alert-config"},
    {"GET", "/api/lang"},
    {"GET", "/api/logday-extra-emails"},
    {"GET", "/api/max31865-config"},
    {"GET", "/api/modbus/config"},
    {"GET", "/api/modbus/live"},
    {"GET", "/api/modbus/scan-status"},
    {"GET", "/api/modbus/stats"},
    {"GET", "/api/modbus-chart-cols"},
    {"GET", "/api/modbus-chart-data"},
    {"GET", "/api/modbus-chart-files"},
    {"GET", "/api/monitor-status"},
    {"GET", "/api/mq135-config"},
    {"GET", "/api/nvs-stats"},
    {"GET", "/api/proximity"},
    {"GET", "/api/proximity/config"},
    {"GET", "/api/proximity/shift-config"},
    {"GET", "/api/proximity/shiftday-emails"},
    {"GET", "/api/proximity/tca9554"},
    {"GET", "/api/proximity-chart-cols"},
    {"GET", "/api/proximity-chart-data"},
    {"GET", "/api/proximity-chart-files"},
    {"GET", "/api/radio/eq"},
    {"GET", "/api/radio/favorites"},
    {"GET", "/api/radio/queue"},
    {"GET", "/api/radio/resume"},
    {"GET", "/api/radio/status"},
    {"GET", "/api/received-text"},
    {"GET", "/api/recipients"},
    {"GET", "/api/recipient-slot"},
    {"GET", "/api/recipient-slots"},
    {"GET", "/api/rs232-cols"},
    {"GET", "/api/rs232-history"},
    {"GET", "/api/rs232-history-files"},
    {"GET", "/api/rs232-status"},
    {"GET", "/api/rs232-templates"},
    {"GET", "/api/scheduled-command-slot"},
    {"GET", "/api/scheduled-command-slots"},
    {"GET", "/api/scheduled-datetime-slot"},
    {"GET", "/api/scheduled-datetime-slots"},
    {"GET", "/api/sd/download"},
    {"GET", "/api/sd-buffers"},
    {"GET", "/api/sdcard-config"},
    {"GET", "/api/sd-status"},
    {"GET", "/api/sensor-config"},
    {"GET", "/api/sensorday-extra-emails"},
    {"GET", "/api/sensor-flags"},
    {"GET", "/api/sensor-history"},
    {"GET", "/api/sensor-history-file"},
    {"GET", "/api/sensor-history-files"},
    {"GET", "/api/sensor-slots"},
    {"GET", "/api/serial-monitor"},
    {"GET", "/api/session-history"},
    {"GET", "/api/self-test"},
    {"GET", "/api/sound-config"},
    {"GET", "/api/status"},
    {"GET", "/api/telnet-status"},
    {"GET", "/api/test-manifest"},
    {"GET", "/api/uda1334a-config"},
    {"GET", "/api/unikon-arrivals"},
    {"GET", "/api/unikon-contests"},
    {"GET", "/api/unikon-pombal"},
    {"GET", "/api/unikon-registrations"},
    {"GET", "/api/unikon-status"},
    {"GET", "/api/usb-server-config"},
    {"GET", "/api/usb-server-devices"},
    {"GET", "/api/usb-server-status"},
    {"GET", "/api/wifi-intelligent-status"},
    {"GET", "/api/wifi-runtime-status"},
    {"GET", "/api/wifi-scan"},
    {"GET", "/api/wifi-status"},
    {"GET", "/api-reference"},
    {"GET", "/auto.js"},
    {"GET", "/bricon"},
    {"GET", "/buffer-viewer"},
    {"GET", "/buzzer"},
    {"GET", "/cam/snapshot"},
    {"GET", "/cam/stream"},
    {"GET", "/camera"},
    {"GET", "/can"},
    {"GET", "/commands"},
    {"GET", "/commands-by-datetime"},
    {"GET", "/config-manager.js"},
    {"GET", "/core.css"},
    {"GET", "/do"},
    {"GET", "/email"},
    {"GET", "/ethernet"},
    {"GET", "/favicon.ico"},
    {"GET", "/files"},
    {"GET", "/ftp"},
    {"GET", "/GoogleAppsScript_EmailHandler.js"},
    {"GET", "/google-script"},
    {"GET", "/grbl-controller"},
    {"GET", "/healthz"},
    {"GET", "/hx711"},
    {"GET", "/ina226"},
    {"GET", "/ipg-laser"},
    {"GET", "/lpt-db25"},
    {"GET", "/max31865"},
    {"GET", "/modbus-live"},
    {"GET", "/modbus-rtu"},
    {"GET", "/modbus-tcp"},
    {"GET", "/monitor"},
    {"GET", "/mq135-config"},
    {"GET", "/networks"},
    {"GET", "/ota"},
    {"GET", "/printer"},
    {"GET", "/proximity"},
    {"GET", "/rambox"},
    {"GET", "/radio-ui"},
    {"GET", "/recipients"},
    {"GET", "/rs232"},
    {"GET", "/sdcard"},
    {"GET", "/sensor-charts"},
    {"GET", "/sensor-config"},
    {"GET", "/serial-monitor"},
    {"GET", "/sound"},
    {"GET", "/status"},
    {"GET", "/structures"},
    {"GET", "/uda1334a"},
    {"GET", "/ui"},
    {"GET", "/unikon"},
    {"GET", "/usb-server"},
    {"GET", "/wifi"},
    {"POST", "/api/access-config"},
    {"POST", "/api/access-kick-all"},
    {"POST", "/api/bricon-config"},
    {"POST", "/api/bricon-poll"},
    {"POST", "/api/bricon-reset"},
    {"POST", "/api/bricon-sync-calendar"},
    {"POST", "/api/bricon-sync-pombal"},
    {"POST", "/api/buzzer-config"},
    {"POST", "/api/buzzer-stop"},
    {"POST", "/api/buzzer-test"},
    {"POST", "/api/camera/config"},
    {"POST", "/api/camera/photo"},
    {"POST", "/api/camera/recording/start"},
    {"POST", "/api/camera/recording/stop"},
    {"POST", "/api/camera/start"},
    {"POST", "/api/camera/stop"},
    {"POST", "/api/can/clear"},
    {"POST", "/api/can/config"},
    {"POST", "/api/can/restart"},
    {"POST", "/api/can/send"},
    {"POST", "/api/clear-ap-lock"},
    {"POST", "/api/clear-buffer"},
    {"POST", "/api/config"},
    {"POST", "/api/config/access-point"},
    {"POST", "/api/config/saved_networks"},
    {"POST", "/api/daily-reboot"},
    {"POST", "/api/do/set"},
    {"POST", "/api/emailday-extra-emails"},
    {"POST", "/api/email-reset"},
    {"POST", "/api/email-test"},
    {"POST", "/api/enable-ota"},
    {"POST", "/api/eth-config"},
    {"POST", "/api/factory-reset"},
    {"POST", "/api/firmware-upload"},
    {"POST", "/api/ftp-test"},
    {"POST", "/api/ftp-upload-now"},
    {"POST", "/api/grbl/auto-detect-baud"},
    {"POST", "/api/grbl/cancel"},
    {"POST", "/api/grbl/create-dir"},
    {"POST", "/api/grbl/delete-path"},
    {"POST", "/api/grbl/pause"},
    {"POST", "/api/grbl/poll-interval"},
    {"POST", "/api/grbl/psram-clear"},
    {"POST", "/api/grbl/psram-stream"},
    {"POST", "/api/grbl/psram-upload"},
    {"POST", "/api/grbl/queue-start"},
    {"POST", "/api/grbl/queue-stop"},
    {"POST", "/api/grbl/override"},
    {"POST", "/api/grbl/resume"},
    {"POST", "/api/grbl/send"},
    {"POST", "/api/grbl/stream"},
    {"POST", "/api/grbl/upload-gcode-stream"},
    {"POST", "/api/grbl/usb-line-coding"},
    {"POST", "/api/grbl/usb-restart"},
    {"POST", "/api/hx711-config"},
    {"POST", "/api/hx711-measure"},
    {"POST", "/api/hx711-tare"},
    {"POST", "/api/ina226-config"},
    {"POST", "/api/ina226-measure"},
    {"POST", "/api/inactivity-alert-config"},
    {"POST", "/api/oee-alert-config"},
    {"POST", "/api/ipg-laser-clear"},
    {"POST", "/api/ipg-laser-send"},
    {"POST", "/api/logday-extra-emails"},
    {"POST", "/api/max31865-config"},
    {"POST", "/api/max31865-measure"},
    {"POST", "/api/modbus/config"},
    {"POST", "/api/modbus/poll-now"},
    {"POST", "/api/modbus/reset-stats"},
    {"POST", "/api/modbus/rtu-write"},
    {"POST", "/api/modbus/scan-rtu"},
    {"POST", "/api/modbus/scan-stop"},
    {"POST", "/api/modbus/scan-tcp"},
    {"POST", "/api/modbus/write"},
    {"POST", "/api/modbus-command"},
    {"POST", "/api/monitor-close"},
    {"POST", "/api/monitor-open"},
    {"POST", "/api/mq135-calibrate-r0"},
    {"POST", "/api/mq135-config"},
    {"POST", "/api/mq135-measure"},
    {"POST", "/api/proximity/config"},
    {"POST", "/api/proximity/reset"},
    {"POST", "/api/proximity/reset-all"},
    {"POST", "/api/proximity/save-all"},
    {"POST", "/api/proximity/shift-config"},
    {"POST", "/api/proximity/shiftday-emails"},
    {"POST", "/api/proximity/tca9554"},
    {"POST", "/api/radio/eq"},
    {"POST", "/api/radio/favorites/add"},
    {"POST", "/api/radio/favorites/play"},
    {"POST", "/api/radio/favorites/remove"},
    {"POST", "/api/radio/next"},
    {"POST", "/api/radio/play-sd"},
    {"POST", "/api/radio/play-url"},
    {"POST", "/api/radio/queue"},
    {"POST", "/api/radio/random"},
    {"POST", "/api/radio/resume"},
    {"POST", "/api/radio/sleep"},
    {"POST", "/api/radio/stop"},
    {"POST", "/api/radio/volume"},
    {"POST", "/api/recipient-slot"},
    {"POST", "/api/restart"},
    {"POST", "/api/rs232/persist-current-template"},
    {"POST", "/api/rs232-apply"},
    {"POST", "/api/rs232-command"},
    {"POST", "/api/rs232-templates"},
    {"POST", "/api/scheduled-command-slot"},
    {"POST", "/api/scheduled-datetime-slot"},
    {"POST", "/api/sd/create-dir"},
    {"POST", "/api/sd/create-file"},
    {"POST", "/api/sd/delete"},
    {"POST", "/api/sd/delete-multiple"},
    {"POST", "/api/sd/reset-write-counter"},
    {"POST", "/api/sd/mount"},
    {"POST", "/api/sd/rename"},
    {"POST", "/api/sd/unmount"},
    {"POST", "/api/sd-action"},
    {"POST", "/api/sdcard-config-save"},
    {"POST", "/api/sensor-charts-heartbeat"},
    {"POST", "/api/sensor-charts-session"},
    {"POST", "/api/sensor-charts-session-end"},
    {"POST", "/api/sensor-config"},
    {"POST", "/api/sensorday-extra-emails"},
    {"POST", "/api/sensor-flush"},
    {"POST", "/api/sensor-measure"},
    {"POST", "/api/sensor-reset-stats"},
    {"POST", "/api/sound-config"},
    {"POST", "/api/sound-measure"},
    {"POST", "/api/storage-retry-sd"},
    {"POST", "/api/system-command"},
    {"POST", "/api/telnet-config"},
    {"POST", "/api/telnet-toggle"},
    {"POST", "/api/test"},
    {"POST", "/api/test-rs232"},
    {"POST", "/api/uda1334a-config"},
    {"POST", "/api/uda1334a-test"},
    {"POST", "/api/uda1334a-timing"},
    {"POST", "/api/unikon-config"},
    {"POST", "/api/unikon-email-pombal"},
    {"POST", "/api/unikon-poll"},
    {"POST", "/api/unikon-poll-w"},
    {"POST", "/api/unikon-reset"},
    {"POST", "/api/unikon-sync-calendar"},
    {"POST", "/api/unikon-sync-pombal"},
    {"POST", "/api/unikon-sync-zp"},
    {"POST", "/api/usb-server-config"},
    {"POST", "/api/wifi-disable"},
    {"POST", "/api/wifi-disable-ap"},
    {"POST", "/api/wifi-enable"},
    {"POST", "/api/wifi-force-ap"},
    {"POST", "/api/wifi-reconnect"},
    {"POST", "/api/wifi-switch-network"},
    {"DELETE", "/api/config/saved_networks"},
    {"DELETE", "/api/rs232-templates"},
    {"DELETE", "/api/sensor-charts-session"},
};

static const size_t s_legacyCount = sizeof(s_legacyRoutes) / sizeof(s_legacyRoutes[0]);

bool isKnownApiPath(const String& path) {
    // Check registered API routes
    for (size_t i = 0; i < s_count; i++) {
        if (path == s_routes[i].path) return true;
    }
    // Check legacy route table
    for (size_t i = 0; i < s_legacyCount; i++) {
        if (path == s_legacyRoutes[i].path) return true;
    }
    return false;
}
