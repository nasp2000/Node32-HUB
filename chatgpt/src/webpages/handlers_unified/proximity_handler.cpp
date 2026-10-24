#include "proximity_handler.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../../ConfigManager.h"
#include "../../sd_layout.h"
#include "../../littlefs_manager.h" // no-SD /oee mirror reads (begin/mounted)
#include <LittleFS.h>
#include "../../chart_history.h"
#include "../../commands.h"
#include "../../config_manager_runtime.h"
#include "../../sdcard.h"
#include "../../sensors/proximity_manager.h"
#include "../../languages/lang_active.h"

#include "../../feature_flags.h"
#if ENABLE_PROXIMITY==1

extern ConfigManager configManager;

// ---- Print adapter for streaming into a String ----
struct _ProxStringPrint : public Print {
    String* s;
    _ProxStringPrint(String& str) : s(&str) {}
    size_t write(uint8_t c) override { *s += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; i++) *s += (char)buf[i];
        return len;
    }
};

// ========== GET /api/proximity ===============================================
void handleProximityGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t n = proximityChannelCount();
    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"channels\":[");
    for (uint8_t i = 0; i < n; i++) {
        ProximityChannel ch = proximityGetChannel(i);
        if (i > 0) buf[pos++] = ',';
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"name\":\"%s\",\"mode\":%u,"
            "\"count_in\":%lu,\"count_out\":%lu,"
            "\"total_in\":%lu,\"total_out\":%lu,"
            "\"cycle_ms\":%lu,\"ppm\":%.2f,"
            "\"state\":%s,\"goal\":%lu,"
            "\"setup_count\":%lu,"
            "\"dir\":%u,"
            "\"elapsed_sec\":%lu}",
            ch.name ? ch.name : "",
            (unsigned)ch.mode,
            (unsigned long)ch.count_in,
            (unsigned long)ch.count_out,
            (unsigned long)ch.total_in,
            (unsigned long)ch.total_out,
            (unsigned long)ch.cycle_time_ms,
            ch.rate_ppm,
            ch.state ? "true" : "false",
            (unsigned long)ch.goal,
            (unsigned long)ch.setup_count,
            (unsigned)ch.direction,
            (unsigned long)proximityCountElapsedSec(i)
        );
    }
    if (pos < (int)sizeof(buf) - 3) { buf[pos++]=']'; buf[pos++]='}'; buf[pos]='\0'; }
    res.sendJson(String(buf));
}

// ---- Local helper: extract param from query or body ----
static String _getParam(const HttpRequest& req, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)req.query.length()) {
        int amp = req.query.indexOf('&', start);
        if (amp < 0) amp = req.query.length();
        String pair = req.query.substring(start, amp);
        if (pair.startsWith(prefix)) return pair.substring(prefix.length());
        start = amp + 1;
    }
    start = 0;
    while (start <= (int)req.body.length()) {
        int amp = req.body.indexOf('&', start);
        if (amp < 0) amp = req.body.length();
        String pair = req.body.substring(start, amp);
        if (pair.startsWith(prefix)) return pair.substring(prefix.length());
        start = amp + 1;
    }
    return String();
}

// ========== POST /api/proximity/reset ========================================
void handleProximityResetPost(HttpRequest& req, HttpResponse& res) {
    String val = _getParam(req, "ch");
    if (val.length() == 0) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing ?ch param\"}");
        return;
    }
proximityResetChannel((uint8_t)val.toInt());
    saveProximityCounters();
    sendVirtualMessage(String(F("Prox: DI")) + String(val.toInt() + 1) + F(" reset via web"), INFO);
    res.sendJson("{\"ok\":true}");
}

// ========== POST /api/proximity/reset-all ====================================
void handleProximityResetAllPost(HttpRequest& req, HttpResponse& res) {
(void)req;
    proximityResetAll();
    sendVirtualMessage(F("Prox: all counters reset via web"), INFO);
    res.sendJson("{\"ok\":true}");
}

// ========== POST /api/proximity/reset-total ===================================
void handleProximityResetTotalPost(HttpRequest& req, HttpResponse& res) {
    String val = _getParam(req, "ch");
    if (val.length() == 0) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing ?ch param\"}");
        return;
    }
    proximityResetTotal((uint8_t)val.toInt());
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity/config ========================================
void handleProximityConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t n = proximityChannelCount();
    SpiRamJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("channels");
    bool fixedPins = false;
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    fixedPins = true;   // 8DI8DO: DI1..DI8 hardwired — no GPIO config needed
#endif
    for (uint8_t i = 0; i < n; i++) {
        ProximityChannel ch = proximityGetChannel(i);
        JsonObject obj = arr.createNestedObject();
        obj["fixedPins"] = fixedPins;
        obj["n"]   = ch.name;
        obj["m"]   = (uint8_t)ch.mode;
        obj["gi"]  = ch.gpio_in;
        obj["dgi"] = proximityDefaultGpioIn(i);   // board pin for this DI (fixed wiring)
        obj["gi2"] = ch.gpio_in2;
        obj["go"]  = ch.gpio_out;
        obj["db"]  = ch.debounce_ms;
        obj["gl"]  = ch.goal;
        obj["pd"]  = ch.pulse_delay_ms;
        obj["pu"]  = ch.pulse_dur_ms;
        obj["mo"]  = ch.monitor_only;
        obj["ml"]  = ch.multiplier;
        obj["dv"]  = ch.divider;
        obj["tp"]  = ch.target_ppm;
        obj["om"]  = ch.oee_manual;
        obj["gc"]  = ch.gate_ch;
        obj["sc"]  = ch.scrap_ch;
    }
    { ProximityChannel _c0 = proximityGetChannel(0);
      char _dbg[160]; snprintf(_dbg, sizeof(_dbg),
        "[prox:diag] GET config: n=%u ch0: n=%s m=%u db=%lu gl=%lu mo=%d",
        (unsigned)n, _c0.name, (unsigned)_c0.mode,
        (unsigned long)_c0.debounce_ms, (unsigned long)_c0.goal, (int)_c0.monitor_only);
      sendVirtualMessage(String(_dbg), INFO, false, true); }
    String out;
    out.reserve(1536);
    serializeJson(doc, out);
    res.sendJson(out);
}

// ========== POST /api/proximity/config =======================================
void handleProximityConfigPost(HttpRequest& req, HttpResponse& res) {
    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"JSON invalido\"}");
        return;
    }
    JsonArray arr = doc["channels"].as<JsonArray>();
    if (arr.isNull()) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing channels\"}");
        return;
    }
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= proximityChannelCount()) break;
        JsonObject obj = v.as<JsonObject>();
        ProximityChannel cur = proximityGetChannel(i);
        proximityConfigureChannel(i,
            obj["n"]   | cur.name,
            obj["m"]   | cur.mode,
            obj["gi"]  | cur.gpio_in,
            obj["go"]  | cur.gpio_out,
            (uint8_t)(obj["gi2"] | (int)cur.gpio_in2),
            obj["db"]  | cur.debounce_ms,
            obj["gl"]  | cur.goal,
            obj["pd"]  | cur.pulse_delay_ms,
            obj["pu"]  | cur.pulse_dur_ms,
            obj["mo"]  | cur.monitor_only,
            (int32_t)(obj["ml"] | (int32_t)cur.multiplier),
            obj["tp"]  | cur.target_ppm,
            obj["om"]  | cur.oee_manual,
            (uint8_t)(obj["gc"] | (int)cur.gate_ch),
            (uint8_t)(obj["sc"] | (int)cur.scrap_ch),
            (int32_t)(obj["dv"] | (int32_t)cur.divider)
        );
        i++;
    }
    if (!saveProximityConfig()) {
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\"LittleFS write failed (config)\"}");
        return;
    }
    sendVirtualMessage(F("Prox: config saved via web"), INFO, false, true);
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity/shift-config ==================================
void handleProximityShiftConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ProximityShiftConfig sc = proximityGetShiftConfig();
    sendVirtualMessage(String(F("[prox:shift] GET: en=")) + String(sc.enabled ? 1 : 0) +
        F(" nSlots=") + String((int)sc.nSlots) +
        F(" h0=") + String((int)sc.slots[0].hour) +
        F(" m0=") + String((int)sc.slots[0].minute) +
        F(" h1=") + String((int)sc.slots[1].hour), COMMAND_RESPONSE, true);
    SpiRamJsonDocument doc(2048);
    doc["en"] = sc.enabled;
    doc["ar"] = sc.auto_reset_on_shift;
    doc["nSlots"] = sc.nSlots;
    JsonArray slots = doc.createNestedArray("slots");
    for (uint8_t i = 0; i < sc.nSlots && i < 3; i++) {
        JsonObject o = slots.createNestedObject();
        o["h"] = sc.slots[i].hour;
        o["m"] = sc.slots[i].minute;
    }
    JsonArray stops = doc.createNestedArray("stops");
    ProximityPlannedStop cur[PROX_MAX_PLANNED_STOPS];
    uint8_t nCur = proximityGetPlannedStops(cur, PROX_MAX_PLANNED_STOPS);
    for (uint8_t i = 0; i < nCur; i++) {
        JsonObject o = stops.createNestedObject();
        o["sh"] = cur[i].sh;
        o["sm"] = cur[i].sm;
        o["eh"] = cur[i].eh;
        o["em"] = cur[i].em;
    }
    doc["emails"] = sc.shiftday_emails;
    String out;
    out.reserve(512);
    serializeJson(doc, out);
    res.sendJson(out);
}

// ========== POST /api/proximity/shift-config =================================
void handleProximityShiftConfigPost(HttpRequest& req, HttpResponse& res) {
    SpiRamJsonDocument doc(1024);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"JSON invalido\"}");
        return;
    }
    ProximityShiftConfig sc = proximityGetShiftConfig();
    sc.enabled             = doc["en"] | sc.enabled;
    sc.auto_reset_on_shift = doc["ar"] | sc.auto_reset_on_shift;
    JsonArray arr = doc["slots"].as<JsonArray>();
    if (!arr.isNull()) {
        sc.nSlots = 0;
        for (JsonVariant v : arr) {
            if (sc.nSlots >= 3) break;
            sc.slots[sc.nSlots].hour   = v["h"] | 6;
            sc.slots[sc.nSlots].minute = v["m"] | 0;
            sc.nSlots++;
        }
    }
    JsonArray stopsArr = doc["stops"].as<JsonArray>();
    if (!stopsArr.isNull()) {
        ProximityPlannedStop stops[PROX_MAX_PLANNED_STOPS];
        uint8_t nStops = 0;
        for (JsonVariant v : stopsArr) {
            if (nStops >= PROX_MAX_PLANNED_STOPS) break;
            JsonObject o = v.as<JsonObject>();
            int sh = o["sh"] | -1, sm = o["sm"] | -1;
            int eh = o["eh"] | -1, em = o["em"] | -1;
            if (sh < 0 || sh > 23 || eh < 0 || eh > 23 ||
                sm < 0 || sm > 59 || em < 0 || em > 59) {
                res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid planned stop time (HH 0-23, MM 0-59)\"}");
                return;
            }
            if (sh == eh && sm == em) {
                res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Planned stop with zero length (start == end)\"}");
                return;
            }
            stops[nStops].sh = (uint8_t)sh;
            stops[nStops].sm = (uint8_t)sm;
            stops[nStops].eh = (uint8_t)eh;
            stops[nStops].em = (uint8_t)em;
            nStops++;
        }
        if (!proximitySetPlannedStops(stops, nStops)) {
            res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Overlapping planned stops\"}");
            return;
        }
    }
    if (doc.containsKey("emails")) {
        const char* em = doc["emails"] | "";
        strncpy(sc.shiftday_emails, em, sizeof(sc.shiftday_emails) - 1);
        sc.shiftday_emails[sizeof(sc.shiftday_emails) - 1] = '\0';
}
    proximitySetShiftConfig(sc);
    if (!saveProximityShiftConfig()) {
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\"LittleFS write failed\"}");
        return;
    }
    sendVirtualMessage(F("Prox: shift config saved via web"), INFO, false, true);
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity/shiftday-emails ===============================
void handleProximityShiftdayEmailsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ProximityShiftConfig sc = proximityGetShiftConfig();
    SpiRamJsonDocument doc(1024);
    doc["emails"] = sc.shiftday_emails;
    String out;
    serializeJson(doc, out);
    res.sendJson(out);
}

// ========== POST /api/proximity/shiftday-emails ==============================
void handleProximityShiftdayEmailsPost(HttpRequest& req, HttpResponse& res) {
    SpiRamJsonDocument doc(1024);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"JSON invalido\"}");
        return;
    }
    ProximityShiftConfig sc = proximityGetShiftConfig();
    const char* em = doc["emails"] | "";
    strncpy(sc.shiftday_emails, em, sizeof(sc.shiftday_emails) - 1);
sc.shiftday_emails[sizeof(sc.shiftday_emails) - 1] = '\0';
    proximitySetShiftConfig(sc);
    if (!saveProximityShiftConfig()) {
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\"LittleFS write failed\"}");
        return;
    }
    sendVirtualMessage(F("Prox: shiftday emails saved via web"), INFO, false, true);
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity/tca9554 =======================================
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
void handleProximityTca9554Get(HttpRequest& req, HttpResponse& res) {
    (void)req;
    char buf[64];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"map\":[");
    uint8_t n = proximityChannelCount();
    for (uint8_t i = 0; i < n; i++) {
        if (i > 0) buf[pos++] = ',';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%u", (unsigned)proximityGetTCA9554Ch(i));
    }
    if (pos < (int)sizeof(buf) - 3) { buf[pos++] = ']'; buf[pos++] = '}'; buf[pos] = '\0'; }
    res.sendJson(String(buf));
}

// ========== POST /api/proximity/tca9554 ======================================
void handleProximityTca9554Post(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"JSON invalido\"}");
        return;
    }
    JsonArray arr = doc["map"].as<JsonArray>();
    if (arr.isNull()) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing map\"}");
        return;
    }
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= proximityChannelCount()) break;
proximitySetTCA9554Ch(i, (uint8_t)(v | 255));
        i++;
    }
    if (!saveProximityConfig()) {
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\"LittleFS write failed (config)\"}");
        return;
    }
    sendVirtualMessage(F("Prox: TCA relay map saved via web"), INFO, false, true);
    res.sendJson("{\"ok\":true}");
}
#endif

// ========== POST /api/proximity/save-all ====================================
void handleProximitySaveAllPost(HttpRequest& req, HttpResponse& res) {
    SpiRamJsonDocument doc(4096);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        sendVirtualMessage(F("[prox:save-all] JSON parse failed"), COMMAND_RESPONSE, true);
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"JSON invalido\"}");
        return;
    }
    sendVirtualMessage(F("[prox:save-all] JSON parsed OK"), COMMAND_RESPONSE, true);

    // --- 1. Apply channel configs to in-memory state ---
    JsonArray chArr = doc["channels"].as<JsonArray>();
    if (!chArr.isNull()) {
        sendVirtualMessage(F("[prox:save-all] channels section present, applying..."), COMMAND_RESPONSE, true);
        uint8_t i = 0;
        for (JsonVariant v : chArr) {
            if (i >= proximityChannelCount()) break;
            JsonObject obj = v.as<JsonObject>();
            ProximityChannel cur = proximityGetChannel(i);
            proximityConfigureChannel(i,
                obj["n"]   | cur.name,
                obj["m"]   | cur.mode,
                obj["gi"]  | cur.gpio_in,
                obj["go"]  | cur.gpio_out,
                (uint8_t)(obj["gi2"] | (int)cur.gpio_in2),
                obj["db"]  | cur.debounce_ms,
                obj["gl"]  | cur.goal,
                obj["pd"]  | cur.pulse_delay_ms,
                obj["pu"]  | cur.pulse_dur_ms,
                obj["mo"]  | cur.monitor_only,
                (int32_t)(obj["ml"] | (int32_t)cur.multiplier),
                obj["tp"]  | cur.target_ppm,
                obj["om"]  | cur.oee_manual,
                (uint8_t)(obj["gc"] | (int)cur.gate_ch),
                (uint8_t)(obj["sc"] | (int)cur.scrap_ch),
                (int32_t)(obj["dv"] | (int32_t)cur.divider)
            );
            i++;
        }
    }
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    JsonArray tcaArr = doc["tca"].as<JsonArray>();
    if (!tcaArr.isNull()) {
        uint8_t i = 0;
        for (JsonVariant v : tcaArr) {
            if (i >= proximityChannelCount()) break;
            proximitySetTCA9554Ch(i, (uint8_t)(v | 255));
            i++;
        }
    }
#endif

    // --- 2. Apply shift + shiftday-emails to in-memory BEFORE saving ---
    //    (saveProximityConfig() serialises the in-memory shift into the unified
    //     file, so the shift state MUST be updated first to avoid a second
    //     round-trip that could fail silently and lose the shift data.)
    {
        ProximityShiftConfig sc = proximityGetShiftConfig();
        JsonObject shiftObj = doc["shift"].as<JsonObject>();
        bool shiftApplied = false;
        if (!shiftObj.isNull()) {
            sendVirtualMessage(F("[prox:save-all] shift section present, applying..."), COMMAND_RESPONSE, true);
            sc.enabled             = shiftObj["en"] | sc.enabled;
            sc.auto_reset_on_shift = shiftObj["ar"] | sc.auto_reset_on_shift;
            JsonArray sArr = shiftObj["slots"].as<JsonArray>();
            if (!sArr.isNull()) {
                sc.nSlots = 0;
                for (JsonVariant sv : sArr) {
                    if (sc.nSlots >= 3) break;
                    sc.slots[sc.nSlots].hour   = sv["h"] | 6;
                    sc.slots[sc.nSlots].minute = sv["m"] | 0;
                    sc.nSlots++;
                }
            }
            JsonArray stopsArr = shiftObj["stops"].as<JsonArray>();
            if (!stopsArr.isNull()) {
                ProximityPlannedStop stops[PROX_MAX_PLANNED_STOPS];
                uint8_t nStops = 0;
                for (JsonVariant sv : stopsArr) {
                    if (nStops >= PROX_MAX_PLANNED_STOPS) break;
                    JsonObject o = sv.as<JsonObject>();
                    int sh = o["sh"] | -1, sm = o["sm"] | -1;
                    int eh = o["eh"] | -1, em = o["em"] | -1;
                    if (sh < 0 || sh > 23 || eh < 0 || eh > 23 ||
                        sm < 0 || sm > 59 || em < 0 || em > 59 ||
                        (sh == eh && sm == em)) {
                        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid planned stops\"}");
                        return;
                    }
                    stops[nStops].sh = (uint8_t)sh;
                    stops[nStops].sm = (uint8_t)sm;
                    stops[nStops].eh = (uint8_t)eh;
                    stops[nStops].em = (uint8_t)em;
                    nStops++;
                }
                if (!proximitySetPlannedStops(stops, nStops)) {
                    res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Overlapping planned stops\"}");
                    return;
                }
            }
            shiftApplied = true;
            { char _nb[64]; snprintf(_nb, sizeof(_nb), "[prox:save-all] shift: %u slots received", (unsigned)sc.nSlots); sendVirtualMessage(String(_nb), COMMAND_RESPONSE, true); }
        }
        if (doc.containsKey("emails")) {
            const char* em = doc["emails"] | "";
            strncpy(sc.shiftday_emails, em, sizeof(sc.shiftday_emails) - 1);
            sc.shiftday_emails[sizeof(sc.shiftday_emails) - 1] = '\0';
            shiftApplied = true;
        }
        if (!shiftApplied) {
            sendVirtualMessage(F("[prox:save-all] shift section MISSING or empty"), COMMAND_RESPONSE, true);
            res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing shift or emails section\"}");
            return;
        }
        proximitySetShiftConfig(sc);
        sendVirtualMessage(F("[prox:save-all] shift applied to memory"), COMMAND_RESPONSE, true);
    }

    // --- 3. Single atomic write: channels + shift in one file ---
    if (!saveProximityConfig()) {
        sendVirtualMessage(F("[prox:save-all] saveProximityConfig FAILED"), COMMAND_RESPONSE, true);
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\"LittleFS write failed (config)\"}");
        return;
    }
    sendVirtualMessage(F("[prox:save-all] channels + shift saved OK (unified)"), COMMAND_RESPONSE, true);
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity-chart-files ===================================
void handleProximityChartFilesGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
#if ENABLE_PROXIMITY_CHARTS==1
    String safeName = chartHistorySanitizeSystemName(configManager.getSystemName());
    String dir = "/" + safeName;
    String prefix = safeName + "_PROXIMITY_";
    String first = "true";
    String buf = "[";
    buf.reserve(512);
    if (sdcardShouldUseSD() && sdMutexLock(pdMS_TO_TICKS(3000))) {
        File root = SD_MMC.open(dir.c_str());
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                String fname = String(entry.name());
                int sl = fname.lastIndexOf('/');
                String base = sl >= 0 ? fname.substring(sl + 1) : fname;
                if (base.startsWith(prefix) && base.endsWith(".chart")) {
                    String datePart = base.substring(prefix.length(), base.length() - 6);
                    if (datePart.length() == 10 &&
                        datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                        if (first != "true") buf += ",";
                        buf += "\"" + datePart + "\"";
                        first = "false";
                    }
                }
                entry.close();
                entry = root.openNextFile();
            }
            root.close();
        }
        sdMutexUnlock();
    }
    buf += "]";
    res.sendJson(buf);
#else
    res.send(404, "application/json", "{\"error\":\"charts not enabled\"}");
#endif
}

// ========== GET /api/proximity-chart-cols ====================================
void handleProximityChartColsGet(HttpRequest& req, HttpResponse& res) {
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
#if ENABLE_PROXIMITY_CHARTS==1
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char today[11];
    snprintf(today, sizeof(today), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String requestedDate;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        requestedDate = req.query.substring(eq + 1, end);
    }
    String resolvedDate = (requestedDate.length() == 10) ? requestedDate : String(today);
    String chartPath = chartHistoryProximityPathForDate(configManager.getSystemName(), resolvedDate);
    uint8_t colCount = 0;
    ChartColumnDef defs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS] = {};
    chartHistoryReadProximityMeta(chartPath, colCount, defs);
    String buf = "{\"cols\":" + String(colCount) + ",\"date\":\"" + resolvedDate + "\",\"col_names\":[";
    for (uint8_t i = 0; i < colCount; i++) {
        if (i > 0) buf += ",";
        buf += "\"";
        if (defs[i].key[0]) buf += String(defs[i].key);
        else { buf += "col_" + String(i + 1); }
        buf += "\"";
    }
    buf += "]}";
    res.sendJson(buf);
#else
    res.send(404, "application/json", "{\"error\":\"charts not enabled\"}");
#endif
}

// ========== GET /api/proximity-chart-data ====================================
void handleProximityChartDataGet(HttpRequest& req, HttpResponse& res) {
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
#if ENABLE_PROXIMITY_CHARTS==1
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char today[11];
    snprintf(today, sizeof(today), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String requestedDate, mode, fromStr;
    bool wantStops = false;
    int start = 0;
    while (start < (int)req.query.length()) {
        int amp = req.query.indexOf('&', start);
        String seg = (amp < 0) ? req.query.substring(start) : req.query.substring(start, amp);
        int eqs = seg.indexOf('=');
        if (eqs > 0) {
            String k = seg.substring(0, eqs);
            String v = seg.substring(eqs + 1);
            if (k == "date") requestedDate = v;
            if (k == "mode") mode = v;
            if (k == "from") fromStr = v;
            if (k == "stops") wantStops = (v != "0");
        }
        if (amp < 0) break;
        start = amp + 1;
    }
    // OEE timeline squares (10-min stop seconds of the production day, RAM
    // only): a compact side-channel of the same route — the .chart series is
    // served at the configured cadence, far too coarse for a 10-min square.
    if (wantStops) {
        String sbuf;
        sbuf.reserve(2048);
        _ProxStringPrint sbp(sbuf);
        extern bool proxChartStopBinsJson(Print& out);
        if (!proxChartStopBinsJson(sbp)) sbuf = F("{\"day_start\":0,\"bins\":0,\"stops\":{}}");
        res.send(200, "application/json", sbuf);
        return;
    }
    String resolvedDate = (requestedDate.length() == 10) ? requestedDate : String(today);
    if (mode.length() == 0) mode = "day";
    // Production-day window (same day the CSV/totals use: label D covers
    // [D-1 b:00, D b:00)). The .chart file is named by the same label, so the
    // axis and the totals can no longer disagree by the boundary offset.
    const uint32_t prodDayStart =
        proxShiftDayStartFromCivilMidnight(chartHistoryDayStartFromRs232DateId(resolvedDate),
                                           proximityShiftDayBoundaryHour());
    uint32_t fromTs = 0;
    if (fromStr.length()) {
        fromTs = (uint32_t)strtoul(fromStr.c_str(), nullptr, 10);
    } else {
        time_t nowTs = time(nullptr);
        if      (mode == "live") fromTs = (uint32_t)(nowTs > 900   ? nowTs - 900   : 0);
        else if (mode == "8h")   fromTs = (uint32_t)(nowTs > 28800 ? nowTs - 28800 : 0);
        else                     fromTs = prodDayStart;
    }
    uint32_t rangeSeconds = (uint32_t)(now > (time_t)fromTs ? now - (time_t)fromTs : 86400UL);
    const int maxOut = (rangeSeconds <= 3600UL) ? 72 : ((rangeSeconds <= 28800UL) ? 144 : 288);
    String chartPath = chartHistoryProximityPathForDate(configManager.getSystemName(), resolvedDate);
    String buf;
    buf.reserve(8192);
    _ProxStringPrint sp(buf);
    // Merged serving (file history + PSRAM ring tail, always minute-fresh —
    // same envelope as the file stream, one stride budget over the union).
    // untilTs = end of the requested PRODUCTION day, so the live tail never
    // leaks into a past date's series (that gap made the JS delta chain dump
    // hours of production into a single bar).
    uint32_t untilTs = prodDayStart ? prodDayStart + 86400UL
                                    : chartHistoryDayStartFromRs232DateId(resolvedDate) + 86400UL;
    extern bool proxChartServeMergedJson(Print& out, const String& path, uint32_t fromTs,
                                         uint16_t maxOut, uint32_t untilTs);
    if (!proxChartServeMergedJson(sp, chartPath, fromTs, (uint16_t)maxOut, untilTs)) {
        buf = "{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}";
    }
    res.send(200, "application/json", buf);
#else
    res.send(404, "application/json", "{\"error\":\"charts not enabled\"}");
#endif
}

// ========== GET /api/proximity/events =========================================
void handleProximityEventsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t n = proximityChannelCount();
    SpiRamJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t ch = 0; ch < n; ch++) {
        JsonObject chObj = arr.createNestedObject();
        chObj["ch"] = ch;
        JsonArray evts = chObj.createNestedArray("events");
        ProximityChannel pc = proximityGetChannel(ch);
        (void)pc;
        for (uint8_t i = 0; i < PROX_RECENT_EVENTS; i++) {
            (void)i;
            // Events are filled in the manager — we just expose what's available.
        }
    }
    String out;
    out.reserve(1024);
    serializeJson(doc, out);
    res.sendJson(out);
}

// ========== GET /api/proximity/oee ============================================
void handleProximityOeeGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t n = proximityChannelCount();
    SpiRamJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < n; i++) {
        ProximityChannel ch = proximityGetChannel(i);
        uint32_t countInBase = 0, runSecBase = 0, downtSecBase = 0, goodPartsBase = 0;
        proximityGetShiftBaselines(i, countInBase, runSecBase, downtSecBase, goodPartsBase);
        uint32_t cIn = (ch.count_in >= countInBase) ? ch.count_in - countInBase : 0;
        uint32_t runSec = (ch.running_seconds >= runSecBase) ? ch.running_seconds - runSecBase : 0;
        uint32_t downtSec = (ch.downtime_seconds >= downtSecBase) ? ch.downtime_seconds - downtSecBase : 0;
        uint32_t goodP = (ch.good_parts >= goodPartsBase) ? ch.good_parts - goodPartsBase : 0;
        uint32_t scrapIn = 0;
        if (ch.scrap_ch < PROXIMITY_CHANNELS && ch.scrap_ch != i) {
            ProximityChannel scrapSnap = proximityGetChannel(ch.scrap_ch);
            uint32_t sb1 = 0, sb2 = 0, sb3 = 0, sb4 = 0;
            proximityGetShiftBaselines(ch.scrap_ch, sb1, sb2, sb3, sb4);
            uint32_t scrapInFull = scrapSnap.count_in;
            scrapIn = (scrapInFull >= sb1) ? scrapInFull - sb1 : 0;
        }
        uint32_t effectiveGood = (scrapIn > 0 && cIn > scrapIn)
            ? (cIn - scrapIn) : goodP;
        JsonObject obj = arr.createNestedObject();
        obj["ch"]               = i;
        obj["name"]             = ch.name;
        obj["gate_ch"]          = ch.gate_ch;
        obj["scrap_ch"]         = ch.scrap_ch;
        obj["scrap_count"]      = scrapIn;
        obj["running_sec"]      = runSec;
        obj["downtime_sec"]     = downtSec;
        obj["good_parts"]       = goodP;
        obj["effective_good"]   = effectiveGood;
        obj["count_in"]         = cIn;
        obj["target_ppm"]       = ch.target_ppm;
        obj["oee_manual"]       = ch.oee_manual;
        // Wall-clock shift window first (reboot-immune); millis fallback when
        // the clock is unsynced, shifts are off, or the shift just started.
        uint32_t shElapsed = proximityShiftElapsedSec();
        uint32_t shPlanned;
        if (shElapsed == 0) {
            shElapsed = proximityCountElapsedSec(i);
            shPlanned = proximityChannelPlannedStopSec(i);
        } else {
            shPlanned = proximityShiftPlannedStopSec();
        }
        ProximityOeeResult r = proximityComputeOee(runSec, shElapsed, cIn, effectiveGood, ch.target_ppm, shPlanned);
        obj["theoretical"]      = r.theoretical;
        obj["availability"]     = r.availability;
        obj["performance"]      = r.performance;
        obj["quality"]          = r.quality;
        obj["oee"]              = r.oee;
        // Day-so-far OEE (deltas since the shift-aligned day boundary, carried
        // across shift auto-resets so the day always spans [boundary, now]).
        uint32_t dIn = 0, dRun = 0, dDown = 0, dGood = 0;
        proximityGetDayDeltas(i, dIn, dRun, dDown, dGood);
        (void)dDown;
        uint32_t dScrapIn = 0;
        if (ch.scrap_ch < PROXIMITY_CHANNELS && ch.scrap_ch != i) {
            uint32_t sdIn = 0, sdRun = 0, sdDown = 0, sdGood = 0;
            proximityGetDayDeltas(ch.scrap_ch, sdIn, sdRun, sdDown, sdGood);
            dScrapIn = sdIn;
        }
        uint32_t dEffectiveGood = (dScrapIn > 0 && dIn > dScrapIn) ? (dIn - dScrapIn) : dGood;
        ProximityOeeResult dr = proximityComputeOee(dRun, proximityDayElapsedSec(), dIn, dEffectiveGood, ch.target_ppm, proximityDayPlannedStopSec());
        obj["day_oee"] = dr.oee;
        obj["day_a"]   = dr.availability;
        obj["day_p"]   = dr.performance;
        obj["day_q"]   = dr.quality;
        // Day-so-far time split for the dashboard donut (production / planned /
        // unplanned fractions of the day window).
        obj["day_run"]     = dRun;
        obj["day_planned"] = proximityDayPlannedStopSec();
        obj["day_elapsed"] = proximityDayElapsedSec();
    }
    String out;
    out.reserve(4096);
    serializeJson(doc, out);
    res.sendJson(out);
}

// ========== POST /api/proximity/oee-reset =====================================
void handleProximityOeeResetPost(HttpRequest& req, HttpResponse& res) {
    String val = _getParam(req, "ch");
    if (val.length() == 0) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing ?ch param\"}");
        return;
    }
    uint8_t ch = (uint8_t)val.toInt();
    if (ch >= proximityChannelCount()) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid channel\"}");
        return;
    }
    proximityResetOee(ch);
    saveProximityCounters();
    res.sendJson("{\"ok\":true}");
}

// ========== GET /api/proximity/shift-history?days=7 ============================
void handleProximityShiftHistoryGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    String daysStr = _getParam(req, "days");
    int days = (daysStr.length() > 0) ? daysStr.toInt() : 7;
    if (days < 1 || days > 30) days = 7;

    extern ConfigManager configManager;
    String sdName = configManager.getSdcardName();
    if (sdName.isEmpty()) sdName = "SD_LOG";

    time_t now = time(nullptr);
    if (now < 1000000000L) { res.sendJson("{\"error\":\"clock not synced\"}"); return; }

    SpiRamJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (int d = 0; d < days; d++) {
        time_t dayTs = now - (time_t)d * 86400UL;
        struct tm ti;
        localtime_r(&dayTs, &ti);
        char dateStr[16];
        snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        String filePath = sd_layout::dayFile(sdName, "PROXIMITY", String(dateStr), "csv");
        File f;
        bool haveFile = false;
        bool sdLocked = false;
        if (sdcardShouldUseSD()) {
            // FTP holds the SD exclusively during its cycle (HISTORIC #57): answer
            // "sd_busy" fast instead of interleaving with the scan/upload.
            if (!sdMutexLock(pdMS_TO_TICKS(2000))) {
                res.sendJson("{\"error\":\"sd_busy\"}");
                return;
            }
            sdLocked = true;
            if (SD_MMC.exists(filePath.c_str())) {
                f = SD_MMC.open(filePath.c_str(), FILE_READ);
                haveFile = (bool)f;
            }
            if (!haveFile) { sdMutexUnlock(); sdLocked = false; continue; }
        } else {
            // No SD: LittleFS /oee mirror (same layout, written at shift-end).
            if (!littlefsBegin() || !littlefsMounted()) continue;
            String fbPath = String("/oee/PROXIMITY_") + dateStr + ".csv";
            if (!LittleFS.exists(fbPath.c_str())) continue;
            f = LittleFS.open(fbPath.c_str(), FILE_READ);
            haveFile = (bool)f;
            if (!haveFile) continue;
        }
        // Read last 20 lines looking for shift_end rows
        f.seek(0);
        String lastLines[20];
        int lineIdx = 0;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (line.length() > 0 && line.indexOf("shift_end") >= 0) {
                lastLines[lineIdx % 20] = line;
                lineIdx++;
            }
        }
        f.close();
        if (sdLocked) { sdMutexUnlock(); sdLocked = false; }
        int count = min(lineIdx, 20);
        for (int li = 0; li < count; li++) {
            int idx = (lineIdx >= 20) ? ((lineIdx - 20 + li) % 20) : li;
            if (lastLines[idx].length() == 0) continue;
            arr.add(lastLines[idx]);
        }
    }

    String out;
    out.reserve(4096);
    serializeJson(doc, out);
    res.sendJson(out);
#else
    (void)req;
    (void)res;
    res.sendJson("{\"error\":\"SD card not enabled\"}");
#endif
}

// ========== POST /api/proximity/test-email =====================================
void handleProximityTestEmailPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    // Trigger a SHIFTDAY email: sends today's proximity CSV folder to shiftday_emails.
    ProximityShiftConfig sc = proximityGetShiftConfig();
    if (sc.shiftday_emails[0] == '\0') {
        res.sendJson(F("{\"ok\":false,\"msg\":\"No shiftday emails configured\"}"));
        return;
    }
    if (!isEmailConfiguredAndWifiConnected()) {
        res.sendJson(F("{\"ok\":false,\"msg\":\"Email not configured or WiFi disconnected\"}"));
        return;
    }
    if (!storageIsAvailable()) {
        res.sendJson(F("{\"ok\":false,\"msg\":\"No storage available\"}"));
        return;
    }
    time_t now = time(nullptr);
    if (now < 1000000000L) {
        res.sendJson(F("{\"ok\":false,\"msg\":\"Clock not synced (NTP)\"}"));
        return;
    }
    struct tm ti;
    proximityShiftDayAlignedDate(now, &ti);
    char dateSuf[16];
    snprintf(dateSuf, sizeof(dateSuf), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String sdName = configManager.getSdcardName();
    if (sdName.isEmpty()) sdName = "SD_LOG";
    String csvPath = sd_layout::dayFile(sdName, "PROXIMITY", String(dateSuf), "csv");

#if ENABLE_SDCARD==1
    bool usingSD = (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted());
    // FTP holds the SD exclusively during its cycle (HISTORIC #57): answer
    // fast instead of interleaving with the scan/upload.
    if (usingSD) {
        if (!sdMutexLock(pdMS_TO_TICKS(2000))) {
            res.sendJson(F("{\"ok\":false,\"msg\":\"sd_busy\"}"));
            return;
        }
        bool csvMissing = !SD_MMC.exists(csvPath.c_str());
        sdMutexUnlock();
        if (csvMissing) {
            res.sendJson(F("{\"ok\":false,\"msg\":\"Today's proximity CSV not found on SD\"}"));
            return;
        }
    }
#endif

    extern bool startEmailDayAsync(const String&, const String&, const String&);
    extern String emailDayCmdExtra;
    emailDayCmdExtra = "";
    String subject = L(activeLang->emailShiftDayTest) + String(F(" - ")) + dateSuf;
    // Same body format as the auto shift-end email, showing the running shift up to now.
    uint32_t pieces = 0;
    float oee = 0.0f, avail = 0.0f, perf = 0.0f, qual = 0.0f;
    proximityGetRunningShiftSummary(pieces, oee, avail, perf, qual);
    // Get scrap count from channel 0's scrap_ch if configured
    uint32_t scrapCount = 0;
    uint32_t setupCount = 0;
    {
        ProximityChannel ch0 = proximityGetChannel(0);
        if (ch0.scrap_ch < PROXIMITY_CHANNELS && ch0.scrap_ch != 0) {
            ProximityChannel scrapSnap = proximityGetChannel(ch0.scrap_ch);
            scrapCount = scrapSnap.count_in;
        }
        setupCount = ch0.setup_count;
    }
    int chartStartHour = 0, durHours = 0;
    int8_t curShift = proximityGetCurrentShiftIndex();
    if (curShift >= 0) proximityGetShiftWindow(curShift, chartStartHour, durHours, true);
    // Time-bar inputs from the shift baselines (same window as the summary above).
    // Planned/elapsed use the absolute running-shift window [start, now):
    // reboot-immune (the millis count-start window restarts at boot and hides
    // stops between shift start and boot).
    uint32_t tRunSec = 0, tPlannedSec = 0, tElapsedSec = 0;
    {
        ProximityChannel ch0 = proximityGetChannel(0);
        uint32_t b1 = 0, b2 = 0, b3 = 0, b4 = 0;
        proximityGetShiftBaselines(0, b1, b2, b3, b4);
        tRunSec = (ch0.running_seconds >= b2) ? ch0.running_seconds - b2 : 0;
        tPlannedSec = proximityChannelPlannedStopSec(0);
        tElapsedSec = proximityCountElapsedSec(0);
        if (curShift >= 0 && now > 120) {
            time_t stW = 0, enW = 0;
            if (proxShiftWindowSec(sc, curShift, now - 60, stW, enW) > 0 && stW < now) {
                tPlannedSec = proximityPlannedStopSec(stW, now);
                tElapsedSec = (uint32_t)(now - stW);
                // Recompute the OEE line from the same absolute inputs so the
                // line and the bar agree (same rule as the auto shift-end email).
                uint32_t goodP = (ch0.good_parts >= b4) ? ch0.good_parts - b4 : 0;
                uint32_t effectiveGood = (scrapCount > 0 && pieces > scrapCount) ? pieces - scrapCount : goodP;
                ProximityOeeResult pr = proximityComputeOee(tRunSec, tElapsedSec, pieces, effectiveGood, ch0.target_ppm, tPlannedSec);
                oee = pr.oee; avail = pr.availability; perf = pr.performance; qual = pr.quality;
            }
        }
    }
    String body = proximityBuildShiftEmailBody(csvPath, chartStartHour, durHours, pieces, oee, avail, perf, qual, scrapCount, setupCount, tRunSec, tPlannedSec, tElapsedSec, now);
    if (startEmailDayAsync(csvPath, subject, body)) {
        res.sendJson(F("{\"ok\":true,\"msg\":\"ShiftDay email started\"}"));
    } else {
        res.sendJson(F("{\"ok\":false,\"msg\":\"Email already in progress\"}"));
    }
}

#endif // ENABLE_PROXIMITY==1


