#include "test_manifest_handler.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../feature_flags.h"
#include "../../psram_helpers.h"
#include "../routes/route_registry_api.h"
#include "../../system_status_payload.h"
#include "../../sdcard.h"

#if ENABLE_PROXIMITY==1
#include "../../sensors/proximity_manager.h"
#endif

#ifndef ENABLE_USB_HOST
  #define ENABLE_USB_HOST 0
#endif

// ── Authored metadata (Phase 3): structured as data, not if-else chains ──
struct ManifestField {
    const char* id;
    const char* page;
    const char* selector;
    const char* type;
    const char* api_get;
    const char* api_set;
    const char* json_path;
    int32_t min;
    int32_t max;
    uint8_t maxlength;
    bool writable;
    bool persistence;
};

struct ManifestAction {
    const char* id;
    const char* page;
    const char* selector;
    const char* action;
    const char* method;
    const char* path;
    const char* risk;
    bool safe;
    bool reversible;
};

struct ManifestInvariant {
    const char* id;
    const char* type;
    const char* feature;
    const char* page;
    const char* api;
    const char* expect;
};

static void writeField(JsonArray& arr, const ManifestField& f, int idx = -1) {
    JsonObject o = arr.createNestedObject();
    if (idx >= 0) {
        o["id"] = String(f.id) + String(idx);
        o["selector"] = String(f.selector) + String(idx);
        char jp[48];
        snprintf(jp, sizeof(jp), f.json_path, idx);
        o["json_path"] = jp;
    } else {
        o["id"] = f.id;
        o["selector"] = f.selector;
        o["json_path"] = f.json_path;
    }
    o["page"] = f.page;
    o["type"] = f.type;
    if (f.api_get[0]) o["api_get"] = f.api_get;
    if (f.api_set[0]) o["api_set"] = f.api_set;
    if (f.min != 0 || f.max != 0) { o["min"] = f.min; o["max"] = f.max; }
    if (f.maxlength) o["maxlength"] = f.maxlength;
    o["writable"] = f.writable;
    o["persistence"] = f.persistence;
}

static void writeAction(JsonArray& arr, const ManifestAction& a) {
    JsonObject o = arr.createNestedObject();
    o["id"] = a.id;
    o["page"] = a.page;
    o["selector"] = a.selector;
    o["action"] = a.action;
    o["method"] = a.method;
    o["path"] = a.path;
    o["risk"] = a.risk;
    o["safe"] = a.safe;
    o["reversible"] = a.reversible;
}

static void writeInvariant(JsonArray& arr, const ManifestInvariant& iv) {
    JsonObject o = arr.createNestedObject();
    o["id"] = iv.id;
    o["type"] = iv.type;
    o["feature"] = iv.feature;
    o["page"] = iv.page;
    o["api"] = iv.api;
    o["expect"] = iv.expect;
}

// ── Proximity page metadata ─────────────────────────────────────────────
#if ENABLE_PROXIMITY==1
static void emitProximityFields(JsonArray& arr) {
    // Per-channel config fields (repeated for each channel).
    static const ManifestField channelBase[] = {
        {"prox.ch.N",       "/proximity", "cn",  "text",   "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].n",  0,     0, 31, true, true},
        {"prox.ch.mode",    "/proximity", "cm",  "select", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].m",  0,     2,  0, true, true},
        {"prox.ch.debounce","/proximity", "cdb", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].db", 0, 65535,  0, true, true},
        {"prox.ch.goal",    "/proximity", "cgl", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].gl", 0,     0,  0, true, true},
        {"prox.ch.dodelay", "/proximity", "cpd", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].pd", 0, 65535,  0, true, true},
        {"prox.ch.dodur",   "/proximity", "cpu", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].pu", 0, 65535,  0, true, true},
        {"prox.ch.mul",     "/proximity", "cmul","select","GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].ml",-20,    20,  0, true, true},
        {"prox.ch.div",     "/proximity", "cdiv","select","GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].dv",-20,    20,  0, true, true},
        {"prox.ch.monitor", "/proximity", "cmo", "checkbox","GET /api/proximity/config","POST /api/proximity/save-all","channels[%d].mo", 0,     1,  0, true, true},
        {"prox.ch.gpioin",  "/proximity", "cgi", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].gi", 0,   255,  0, true, true},
        {"prox.ch.gpioin2", "/proximity", "cgi2","number","GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].gi2",0,   255,  0, true, true},
        {"prox.ch.gpioout", "/proximity", "cgo", "number", "GET /api/proximity/config", "POST /api/proximity/save-all", "channels[%d].go", 0,   255,  0, true, true},
    };
    const int nBase = (int)(sizeof(channelBase) / sizeof(channelBase[0]));
    for (int ch = 0; ch < PROXIMITY_CHANNELS; ch++) {
        for (int k = 0; k < nBase; k++) {
            writeField(arr, channelBase[k], ch);
        }
    }

    // Shared shift / shiftday fields.
    static const ManifestField shared[] = {
        {"prox.shift.en",      "/proximity", "shen",          "checkbox", "GET /api/proximity/shift-config", "POST /api/proximity/save-all", "shift.en",     0, 1, 0, true, true},
        {"prox.shift.autoreset","/proximity","shar",          "checkbox", "GET /api/proximity/shift-config", "POST /api/proximity/save-all", "shift.ar",     0, 1, 0, true, true},
        {"prox.shift.nslots",  "/proximity", "shnsl",         "select",   "GET /api/proximity/shift-config", "POST /api/proximity/save-all", "shift.nSlots", 1, 3, 0, true, true},
        {"prox.shiftday.emails","/proximity","shiftday-emails","text",   "GET /api/proximity/shiftday-emails", "POST /api/proximity/save-all", "emails",      0, 0, 0, true, true},
    };
    const int nShared = (int)(sizeof(shared) / sizeof(shared[0]));
    for (int k = 0; k < nShared; k++) writeField(arr, shared[k]);

    // Shift slot start times (up to 3).
    static const ManifestField slotBase = {"prox.shift.slot", "/proximity", "sh_h", "number", "GET /api/proximity/shift-config", "POST /api/proximity/save-all", "slots[%d].h", 0, 23, 0, true, true};
    for (int s = 0; s < 3; s++) writeField(arr, slotBase, s);
}

static void emitProximityActions(JsonArray& arr) {
    static const ManifestAction actions[] = {
        {"prox.ch.reset",   "/proximity", ".btn-reset",  "reset_channel", "POST", "/api/proximity/reset?ch=%d",  "safe_write", true, true},
        {"prox.save",       "/proximity", "button",      "save_all",      "POST", "/api/proximity/save-all",       "safe_write", true, true},
        {"prox.testemail",  "/proximity", "#btn-test-email", "test_email","POST", "/api/proximity/test-email",     "safe_write", true, true},
    };
    const int n = (int)(sizeof(actions) / sizeof(actions[0]));
    for (int k = 0; k < n; k++) writeAction(arr, actions[k]);
}

static void emitProximityInvariants(JsonArray& arr) {
    static const ManifestInvariant inv[] = {
        {"prox.feature_page",       "feature_page",      "ENABLE_PROXIMITY", "/proximity", "", ""},
        {"prox.channel_count",      "value_consistency", "",                "/proximity", "GET /api/proximity/config", "channels.length==8"},
    };
    const int n = (int)(sizeof(inv) / sizeof(inv[0]));
    for (int k = 0; k < n; k++) writeInvariant(arr, inv[k]);
}
#endif // ENABLE_PROXIMITY

// ── Alerts page metadata ────────────────────────────────────────────────
#if ENABLE_PAGE_ALERTS==1
static void emitAlertsFields(JsonArray& arr) {
    static const ManifestField slotBase[] = {
        {"alert.slot.enabled","/alerts-config","alert_enabled_","checkbox","","POST /api/inactivity-alert-config","slots[%d].enabled",0,1,0,true,true},
        {"alert.slot.timeout","/alerts-config","alert_timeout_","select",  "","POST /api/inactivity-alert-config","slots[%d].timeout_minutes",5,1440,0,true,true},
        {"alert.slot.maxperday","/alerts-config","alert_max_", "select",  "","POST /api/inactivity-alert-config","slots[%d].max_per_day",3,60,0,true,true},
        {"alert.slot.emails","/alerts-config","alert_emails_","text",     "","POST /api/inactivity-alert-config","slots[%d].emails",0,0,0,true,true},
    };
    const int nBase = (int)(sizeof(slotBase) / sizeof(slotBase[0]));
    for (int slot = 0; slot < 6; slot++) {
        for (int k = 0; k < nBase; k++) writeField(arr, slotBase[k], slot);
    }
    static const ManifestField sensorsReset = {"alert.sensors_reset_timer", "/alerts-config", "inactivity_alert_sensors_reset_timer", "checkbox", "", "POST /api/inactivity-alert-config", "sensors_reset_timer", 0, 1, 0, true, true};
    writeField(arr, sensorsReset);
#if ENABLE_PROXIMITY==1
    static const ManifestField oeeBase[] = {
        {"oee_alert.slot.enabled","/alerts-config","oee_enabled_","checkbox","","POST /api/oee-alert-config","slots[%d].enabled",0,1,0,true,true},
        {"oee_alert.slot.metric","/alerts-config","oee_metric_","select",  "","POST /api/oee-alert-config","slots[%d].metric",0,2,0,true,true},
        {"oee_alert.slot.threshold","/alerts-config","oee_threshold_","number","","POST /api/oee-alert-config","slots[%d].threshold",1,100,60,true,true},
        {"oee_alert.slot.persist","/alerts-config","oee_persist_","select",  "","POST /api/oee-alert-config","slots[%d].persist_min",1,240,15,true,true},
        {"oee_alert.slot.maxperday","/alerts-config","oee_max_", "select",  "","POST /api/oee-alert-config","slots[%d].max_per_day",1,100,6,true,true},
        {"oee_alert.slot.emails","/alerts-config","oee_emails_","text",     "","POST /api/oee-alert-config","slots[%d].emails",0,0,0,true,true},
    };
    const int nOee = (int)(sizeof(oeeBase) / sizeof(oeeBase[0]));
    for (int slot = 0; slot < 3; slot++) {
        for (int k = 0; k < nOee; k++) writeField(arr, oeeBase[k], slot);
    }
#endif
}

static void emitAlertsActions(JsonArray& arr) {
    static const ManifestAction actions[] = {
        {"alert.save", "/alerts-config", "#save_inactivity_alert", "save_alerts", "POST", "/api/inactivity-alert-config", "safe_write", true, true},
#if ENABLE_PROXIMITY==1
        {"oee_alert.save", "/alerts-config", "#save_oee_alert", "save_oee_alerts", "POST", "/api/oee-alert-config", "safe_write", true, true},
#endif
    };
    const int n = (int)(sizeof(actions) / sizeof(actions[0]));
    for (int k = 0; k < n; k++) writeAction(arr, actions[k]);
}

static void emitAlertsInvariants(JsonArray& arr) {
    static const ManifestInvariant inv[] = {
        {"alerts.feature_page", "feature_page", "ENABLE_PAGE_ALERTS", "/alerts-config", "", ""},
    };
    const int n = (int)(sizeof(inv) / sizeof(inv[0]));
    for (int k = 0; k < n; k++) writeInvariant(arr, inv[k]);
}
#endif // ENABLE_PAGE_ALERTS

static const char* boardName() {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    return "P4";
#elif defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1
    return "S3-CAM";
#elif defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    return "8DI8DO";
#else
    return "N16R8";
#endif
}

static bool psramAvailable() {
#if (defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT))
    return true;
#else
    return false;
#endif
}

void handleTestManifestGet(HttpRequest& req, HttpResponse& res) {
    (void)req;

    SpiRamJsonDocument doc(65536);

    doc["manifest_version"] = 1;

    // ── device ──
    JsonObject device = doc.createNestedObject("device");
    device["firmware"] = FIRMWARE_VERSION;
    device["pack"] = PACK_SYSTEM_NAME;
    device["brand"] = PACK_BRAND_NAME;
    device["chip"] = boardName();
    device["board"] = boardName();

    // ── capabilities (real, not assumed from chip) ──
    JsonObject cap = doc.createNestedObject("capabilities");
    cap["browser"] = true;
    cap["api"] = true;
    cap["persistence"] = true;
    cap["reboot"] = true;
    cap["hardware"] =
        #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
        true;
        #else
        false;
        #endif
    cap["wifi"] = (ENABLE_WIFI == 1);
    cap["ethernet"] = (ENABLE_ETHERNET == 1);
    cap["psram"] = psramAvailable();
    cap["sdcard"] = (ENABLE_SDCARD == 1 && sdcardIsMounted());

    // ── pages (auto-derived from feature flags) ──
    JsonArray pages = doc.createNestedArray("pages");
    #define PAGE(path, flag) do { \
        JsonObject p = pages.createNestedObject(); \
        p["path"] = path; \
        p["enabled"] = (flag == 1); \
        p["testable"] = (flag == 1); \
    } while(0)
    PAGE("/",                    1);
    PAGE("/proximity",           ENABLE_PROXIMITY);
    PAGE("/proximity-dashboard", ENABLE_PROXIMITY);
    PAGE("/monitor",             ENABLE_PAGE_MONITOR);
    PAGE("/serial-monitor",      ENABLE_PAGE_MONITOR);
    PAGE("/commands",            ENABLE_PAGE_COMMANDS_INTERVAL);
    PAGE("/commands-by-datetime",ENABLE_PAGE_COMMANDS);
    PAGE("/recipients",          ENABLE_PAGE_RECIPIENTS);
    PAGE("/alerts-config",       ENABLE_PAGE_ALERTS);
    PAGE("/files",               ENABLE_PAGE_FILES);
    PAGE("/sdcard",              ENABLE_PAGE_SDCARD);
    PAGE("/access",              ENABLE_PAGE_ACCESS);
    PAGE("/wifi",                ENABLE_WIFI);
    PAGE("/ethernet",            ENABLE_ETHERNET);
    PAGE("/email",               (ENABLE_SMTP || ENABLE_GOOGLE_EMAIL));
    PAGE("/rs232",               (ENABLE_RS232 || ENABLE_SNIFFER));
    PAGE("/lpt-db25",            ENABLE_DB25);
    PAGE("/modbus-rtu",          ENABLE_MODBUS_RTU);
    PAGE("/modbus-tcp",          ENABLE_MODBUS_TCP);
    PAGE("/modbus-live",         ENABLE_MODBUS);
    PAGE("/can",                 ENABLE_CAN);
    PAGE("/camera",              ENABLE_CAMERA);
    PAGE("/buzzer",              ENABLE_BUZZER);
    PAGE("/unikon",              ENABLE_UNIKON);
    PAGE("/ipg-laser",           ENABLE_IPG_LASER);
    PAGE("/uda1334a",            ENABLE_UDA1334A);
    PAGE("/radio-ui",            ENABLE_UDA1334A);
    PAGE("/grbl-controller",     ENABLE_GRBL);
    PAGE("/sensor-config",       (ENABLE_AHT20_BMP280 && ENABLE_SENSOR_CONFIG_PAGE));
    PAGE("/mq135-config",        ENABLE_MQ135);
    PAGE("/hx711",               ENABLE_HX711);
    PAGE("/ina226",              ENABLE_INA226);
    PAGE("/sound",               ENABLE_SOUND_SENSOR);
    PAGE("/max31865",            ENABLE_MAX31865);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    PAGE("/falas",               1);
    PAGE("/ecra",                1);
#endif
    PAGE("/ota",                 ENABLE_OTA);
    PAGE("/networks",            ENABLE_TELNET);
    PAGE("/do",                  BOARD_HAS_TCA9554_DO);
    PAGE("/api-reference",       ENABLE_API_REFERENCE_PAGE);
    PAGE("/structures",          ENABLE_STRUCTURES_PAGE);
    PAGE("/dualfile",            ENABLE_DUALFILE);
    PAGE("/ftp",                 ENABLE_PAGE_FTP);
    #undef PAGE

    // ── api (auto-derived from the route registry, no parallel list) ──
    JsonArray api = doc.createNestedArray("api");
    {
        size_t n = registeredApiRouteCount();
        for (size_t i = 0; i < n; i++) {
            const char* method = nullptr;
            const char* path = nullptr;
            if (!registeredApiRouteAt(i, method, path)) continue;
            JsonObject a = api.createNestedObject();
            // Registry stores PROGMEM string literals; read with *_P helpers.
            a["method"] = FPSTR(method);
            a["path"] = FPSTR(path);
            a["testable"] = true;
            // Default risk by method; authored metadata can refine later.
            bool isGet = (strcmp_P(method, PSTR("GET")) == 0);
            a["risk"] = isGet ? "read_only" : "safe_write";
        }
    }

    // ── authored metadata (Phase 3): per-page fields / actions / invariants ──
    JsonArray fields = doc.createNestedArray("fields");
    JsonArray actions = doc.createNestedArray("actions");
    JsonArray invariants = doc.createNestedArray("invariants");

#if ENABLE_PROXIMITY==1
    emitProximityFields(fields);
    emitProximityActions(actions);
    emitProximityInvariants(invariants);
#endif
#if ENABLE_PAGE_ALERTS==1
    emitAlertsFields(fields);
    emitAlertsActions(actions);
    emitAlertsInvariants(invariants);
#endif

    doc.createNestedArray("tests");

    // Serialize to a PSRAM buffer (minimal DRAM): the JSON body can be large,
    // so keep it entirely in PSRAM and stream it back without a heap String.
    // Falls back to heap when PSRAM is absent.
    size_t len = measureJson(doc) + 1;
    uint8_t* buf = (uint8_t*)(psramFound() ? ps_malloc(len) : malloc(len));
    if (!buf) {
        res.send(500, F("application/json"), F("{\"error\":\"out_of_memory\"}"));
        return;
    }
    serializeJson(doc, (char*)buf, len);

    res.code = 200;
    res.contentType = F("application/json");
    res.isBinary = true;
    res.binaryData = buf;
    res.binaryLen = len - 1;   // exclude trailing NUL written by serializeJson
    res.freeFn = [](const uint8_t* p) { free(const_cast<uint8_t*>(p)); };
}