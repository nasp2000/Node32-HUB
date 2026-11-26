// tdk_lambda_handler.cpp — unified handlers for the /tdk-lambda page.
// Registered with registerApiRoute() so WiFi and Ethernet (W5500) both serve
// them: ethernet_bootstrap_routes.h dispatches unknown /api/ paths through
// findApiRouteHandler().
#include "tdk_lambda_handler.h"

#include "../../feature_flags.h"
#include "../../ConfigManager.h"
#include <ArduinoJson.h>
#include "../../psram_helpers.h"

extern ConfigManager configManager;

#if ENABLE_TDK_LAMBDA == 1
#include "../../tdk_lambda.h"
#include "../../sdcard.h"
#include "../../littlefs_manager.h"

static String tdkJsonEscape(const char* s) {
    String o;
    if (!s) return o;
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += F("\\n");
        else if (c == '\r') { /* skip */ }
        else if ((unsigned char)c < 0x20) { /* skip */ }
        else o += c;
    }
    return o;
}

// Read-only commands the console may send with allow_write off. Everything
// else (PV, PC, OUT, OVP, UVL, RMT, SAV, RCL, global G*, MD bytes) changes the
// unit and needs the explicit "allow write" toggle.
static bool tdkIsReadOnlyCommand(const String& command) {
    static const char* const kReadOnly[] = {
        "IDN?", "REV?", "SN?", "DATE?", "MDAV?", "RMT?", "MS?", "FILTER?",
        "MV?", "MC?", "PV?", "PC?", "DVC?", "MODE?", "OUT?", "FLD?", "AST?",
        "OVP?", "UVL?", "FBD?", "STT?", "STAT?", "FLT?", "SEVE?", "FEVE?",
        "SENA?", "FENA?", nullptr
    };
    char up[16];
    tdkUpperTrim(command.c_str(), up, sizeof(up));
    for (size_t i = 0; kReadOnly[i]; ++i) {
        if (!strcmp(up, kReadOnly[i])) return true;
    }
    return false;
}

// Minimal form-field reader: "a=1&b=2" (the dispatcher already url-encodes).
static String tdkFormValue(const String& body, const char* key) {
    String k = String(key) + "=";
    int i = body.indexOf(k);
    if (i < 0) return String();
    i += k.length();
    int j = body.indexOf('&', i);
    String v = (j < 0) ? body.substring(i) : body.substring(i, j);
    v.replace("+", " ");
    v.replace("%20", " ");
    return v;
}
#endif

void handleTdkLambdaAvailableGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    res.sendJson(F("{\"available\":true,\"message\":\"TDK-Lambda module is enabled.\"}"));
#else
    res.sendJson(F("{\"available\":false,\"message\":\"TDK-Lambda module is disabled. Enable ENABLE_TDK_LAMBDA in user_config.h.\"}"));
#endif
}

void handleTdkLambdaStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    TdkStatus st = tdkLambdaGetStatus();
    TdkValues v  = tdkLambdaGetValues();
    String j = F("{\"success\":true,\"status\":{");
    j += String(F("\"polling\":"))       + (st.polling ? F("true") : F("false"));
    j += String(F(",\"csvEnabled\":"))   + (st.csvEnabled ? F("true") : F("false"));
    j += String(F(",\"pollIntervalMs\":")) + String(st.pollIntervalMs);
    j += String(F(",\"lastRxAgeMs\":"))  + String(st.lastRxAgeMs);
    j += String(F(",\"logCount\":"))     + String(st.logCount);
    j += String(F(",\"csvRows\":"))      + String(st.csvRows);
    j += String(F(",\"csvOnSd\":"))      + (st.csvOnSd ? F("true") : F("false"));
    j += String(F(",\"csvSensorFormat\":")) + (st.csvSensorFormat ? F("true") : F("false"));
    j += String(F(",\"autoMap\":")) + (st.autoMap ? F("true") : F("false"));
    j += String(F(",\"listen\":")) + (st.listen ? F("true") : F("false"));
    j += String(F(",\"allowWrite\":")) + (configManager.getTdkLambdaAllowWrite() ? F("true") : F("false"));
    j += F(",\"autoNote\":\"") + tdkJsonEscape(st.autoNote) + F("\"");
    j += F(",\"mapUsed\":\"") + tdkJsonEscape(st.mapUsed) + F("\"");
    j += F(",\"idn\":\"") + tdkJsonEscape(st.idn) + F("\"");
    j += F(",\"rev\":\"") + tdkJsonEscape(st.rev) + F("\"");
    j += F(",\"sn\":\"") + tdkJsonEscape(st.sn) + F("\"");
    j += F(",\"lastError\":\"") + tdkJsonEscape(st.lastError) + F("\"");
    j += String(F(",\"lastErrorAgeMs\":")) + String(st.lastErrorAgeMs);
    j += String(F(",\"address\":")) + String(st.address);
    j += String(F(",\"inject\":")) + (st.inject ? F("true") : F("false"));
    j += String(F(",\"alertActive\":")) + (st.alertActive ? F("true") : F("false"));
    j += F(",\"alertText\":\"") + tdkJsonEscape(st.alertText) + F("\"");
    j += String(F(",\"scaleVUsed\":")) + String(st.scaleVUsed, 4);
    j += String(F(",\"scaleIUsed\":")) + String(st.scaleIUsed, 4);
    j += F(",\"csvPath\":\"")  + tdkJsonEscape(st.csvPath)  + F("\"");
    j += F(",\"lastCommand\":\"") + tdkJsonEscape(st.lastCommand) + F("\"");
    j += F(",\"lastLine\":\"") + tdkJsonEscape(st.lastLine) + F("\"");
    j += F("},\"values\":{");
    j += String(F("\"valid\":")) + (v.valid ? F("true") : F("false"));
    j += String(F(",\"vMeas\":")) + String(v.vMeas, 2);
    j += String(F(",\"vSet\":"))  + String(v.vSet, 2);
    j += String(F(",\"iMeas\":")) + String(v.iMeas, 3);
    j += String(F(",\"iSet\":"))  + String(v.iSet, 3);
    j += String(F(",\"ovp\":"))   + String(v.ovp, 1);
    j += String(F(",\"uvl\":"))   + String(v.uvl, 1);
    j += F(",\"mode\":\"") + tdkJsonEscape(v.mode) + F("\"");
    j += String(F(",\"outOn\":")) + (v.outOn ? F("true") : F("false"));
    j += F("}}");
    res.sendJson(j);
#else
    res.sendJson(F("{\"success\":false,\"message\":\"TDK-Lambda module disabled\"}"));
#endif
}

void handleTdkLambdaCommandsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    size_t n = 0;
    const TdkCommandInfo* cat = tdkLambdaCatalog(&n);
    String j = F("{\"success\":true,\"commands\":[");
    for (size_t i = 0; i < n; ++i) {
        if (i) j += ',';
        j += F("{\"cmd\":\"")   + tdkJsonEscape(cat[i].cmd)   + F("\",\"group\":\"") + tdkJsonEscape(cat[i].group) + F("\",\"usage\":\"")
           + tdkJsonEscape(cat[i].usage) + F("\",\"desc\":\"") + tdkJsonEscape(cat[i].desc) + F("\",\"reply\":\"")
           + tdkJsonEscape(cat[i].reply) + F("\",\"notes\":\"") + tdkJsonEscape(cat[i].notes) + F("\"}");
    }
    j += F("]}");
    res.sendJson(j);
#else
    res.sendJson(F("{\"success\":false,\"commands\":[]}"));
#endif
}

void handleTdkLambdaLogGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    static TdkLogEntry entries[32];
    const uint16_t n = tdkLambdaCopyLog(entries, 32);
    String j = F("{\"success\":true,\"entries\":[");
    for (uint16_t i = 0; i < n; ++i) {
        if (i) j += ',';
        j += F("{\"dir\":\"") + tdkJsonEscape(entries[i].direction) + F("\",\"ageMs\":")
           + String(entries[i].ageMs) + F(",\"text\":\"") + tdkJsonEscape(entries[i].text) + F("\"}");
    }
    j += F("]}");
    res.sendJson(j);
#else
    res.sendJson(F("{\"success\":false,\"entries\":[]}"));
#endif
}

void handleTdkLambdaSendPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TDK_LAMBDA == 1
    String cmd = tdkFormValue(req.body, "cmd");
    cmd.trim();
    if (cmd.isEmpty()) {
        res.code = 400;
        res.sendJson(F("{\"success\":false,\"message\":\"missing cmd\"}"));
        return;
    }
    if (cmd.length() > 32) cmd = cmd.substring(0, 32);
    if (configManager.getTdkLambdaListen()) {
        res.code = 409;
        res.sendJson(F("{\"success\":false,\"message\":\"modo escuta: TX desactivado (o PLC e o mestre da linha)\"}"));
        return;
    }
    if (!configManager.getTdkLambdaAllowWrite() && !tdkIsReadOnlyCommand(cmd)) {
        res.code = 403;
        res.sendJson(F("{\"success\":false,\"message\":\"comando de escrita bloqueado (activa 'permitir escrita')\"}"));
        return;
    }
    const bool ok = tdkLambdaSend(cmd);
    res.sendJson(ok ? F("{\"success\":true,\"message\":\"queued\"}")
                    : F("{\"success\":false,\"message\":\"queue full or RS232 disabled\"}"));
#else
    (void)req;
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}

void handleTdkLambdaClearPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    tdkLambdaClearLog();
    res.sendJson(F("{\"success\":true,\"message\":\"log cleared\"}"));
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}

void handleTdkLambdaConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    String j = F("{\"success\":true");
    j += String(F(",\"poll\":")) + (configManager.getTdkLambdaPoll() ? F("true") : F("false"));
    j += String(F(",\"poll_s\":")) + String(configManager.getTdkLambdaPollSec());
    j += String(F(",\"csv\":")) + (configManager.getTdkLambdaCsv() ? F("true") : F("false"));
    j += String(F(",\"format\":\"")) + (configManager.getTdkLambdaCsvFormat() ? F("sensor") : F("table")) + F("\"");
    // Device profile (which queries, reply map, scales)
    j += F(",\"mode\":\"")      + configManager.getTdkLambdaMode()     + F("\"");
    j += F(",\"cmd_v\":\"")     + tdkJsonEscape(configManager.getTdkLambdaCmdV().c_str())     + F("\"");
    j += F(",\"cmd_i\":\"")     + tdkJsonEscape(configManager.getTdkLambdaCmdI().c_str())     + F("\"");
    j += F(",\"cmd_state\":\"") + tdkJsonEscape(configManager.getTdkLambdaCmdState().c_str()) + F("\"");
    j += F(",\"map\":\"")       + tdkJsonEscape(configManager.getTdkLambdaMap().c_str())      + F("\"");
    j += String(F(",\"scale_v\":")) + String(configManager.getTdkLambdaScaleV(), 4);
    j += String(F(",\"scale_i\":")) + String(configManager.getTdkLambdaScaleI(), 4);
    j += String(F(",\"off_v\":"))   + String(configManager.getTdkLambdaOffV(), 4);
    j += String(F(",\"off_i\":"))   + String(configManager.getTdkLambdaOffI(), 4);
    j += String(F(",\"auto_map\":")) + (configManager.getTdkLambdaAutoMap() ? F("true") : F("false"));
    j += String(F(",\"listen\":"))     + (configManager.getTdkLambdaListen() ? F("true") : F("false"));
    j += String(F(",\"allow_write\":")) + (configManager.getTdkLambdaAllowWrite() ? F("true") : F("false"));
    j += String(F(",\"addr\":")) + String(configManager.getTdkLambdaAddr());
    j += String(F(",\"inject\":")) + (configManager.getTdkLambdaInject() ? F("true") : F("false"));
    j += F(",\"limits\":{");
    j += String(F("\"enabled\":")) + (configManager.getTdkLambdaLimitsEnabled() ? F("true") : F("false"));
    j += String(F(",\"v_min\":")) + String(configManager.getTdkLambdaLimitVMin(), 2);
    j += String(F(",\"v_max\":")) + String(configManager.getTdkLambdaLimitVMax(), 2);
    j += String(F(",\"i_max\":")) + String(configManager.getTdkLambdaLimitIMax(), 3);
    j += F("}");
    j += F("}");
    res.sendJson(j);
#else
    res.sendJson(F("{\"success\":false}"));
#endif
}

void handleTdkLambdaConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TDK_LAMBDA == 1
    const String pollS  = tdkFormValue(req.body, "poll");
    const String pollSec= tdkFormValue(req.body, "poll_s");
    const String csvS   = tdkFormValue(req.body, "csv");
    const bool poll = (pollS == "1" || pollS == "true" || pollS == "on");
    const int  sec  = pollSec.length() ? pollSec.toInt() : 60;
    const bool csv  = (csvS == "1" || csvS == "true" || csvS == "on");
    // Extras (multi-drop address, monitor injection, limit alerts)
    const String addrS = tdkFormValue(req.body, "addr");
    const String injS  = tdkFormValue(req.body, "inject");
    const String limS  = tdkFormValue(req.body, "limits_enabled");
    if (addrS.length() || injS.length() || limS.length() ||
        req.body.indexOf("limit_v_min=") >= 0) {
        const int  addr = addrS.length() ? addrS.toInt() : configManager.getTdkLambdaAddr();
        const bool inj  = injS.length()
            ? (injS == "1" || injS == "true" || injS == "on")
            : configManager.getTdkLambdaInject();
        const bool lim  = limS.length()
            ? (limS == "1" || limS == "true" || limS == "on")
            : configManager.getTdkLambdaLimitsEnabled();
        const String vmin = tdkFormValue(req.body, "limit_v_min");
        const String vmax = tdkFormValue(req.body, "limit_v_max");
        const String imax = tdkFormValue(req.body, "limit_i_max");
        const float fvmin = vmin.length() ? vmin.toFloat() : configManager.getTdkLambdaLimitVMin();
        const float fvmax = vmax.length() ? vmax.toFloat() : configManager.getTdkLambdaLimitVMax();
        const float fimax = imax.length() ? imax.toFloat() : configManager.getTdkLambdaLimitIMax();
        configManager.setTdkLambdaExtras(addr, inj, lim, fvmin, fvmax, fimax);
        tdkLambdaSetAddress((uint8_t)addr);
        tdkLambdaSetInject(inj);
        tdkLambdaSetLimits(fvmin, fvmax, fimax, lim);
    }

    const String listenS = tdkFormValue(req.body, "listen");
    const String allowS  = tdkFormValue(req.body, "allow_write");
    if (listenS.length() || allowS.length()) {
        const bool listenOn = listenS.length()
            ? (listenS == "1" || listenS == "true" || listenS == "on")
            : configManager.getTdkLambdaListen();
        const bool allowOn = allowS.length()
            ? (allowS == "1" || allowS == "true" || allowS == "on")
            : configManager.getTdkLambdaAllowWrite();
        configManager.setTdkLambdaSafety(listenOn, allowOn);
        tdkLambdaSetListen(listenOn);
    }
    const String autoS = tdkFormValue(req.body, "auto_map");
    if (autoS.length()) {
        const bool autoOn = (autoS == "1" || autoS == "true" || autoS == "on");
        configManager.setTdkLambdaAutoMap(autoOn);
        tdkLambdaSetAutoMap(autoOn);
    }
    const String fmtS = tdkFormValue(req.body, "format");
    const String fmt  = (fmtS == "table") ? String("table") : String("sensor");

    configManager.setTdkLambda(poll, sec, csv, fmt);
    tdkLambdaSetPoll(poll, (uint32_t)((sec < 5 ? 5 : (sec > 3600 ? 3600 : sec))) * 1000UL);
    tdkLambdaSetCsvFormat(fmt == "sensor");
    tdkLambdaSetCsv(csv);

    // Optional device-profile fields: applied only when present, so a page that
    // only sends poll/csv keeps the stored profile untouched.
    const String pMode = tdkFormValue(req.body, "mode");
    const String pV    = tdkFormValue(req.body, "cmd_v");
    const String pI    = tdkFormValue(req.body, "cmd_i");
    const String pSt   = tdkFormValue(req.body, "cmd_state");
    const String pMap  = tdkFormValue(req.body, "map");
    if (pMode.length() || pV.length() || pI.length() || pSt.length() || pMap.length()) {
        const String mode = pMode.length() ? pMode : configManager.getTdkLambdaMode();
        const String cmdV = pV.length()    ? pV    : configManager.getTdkLambdaCmdV();
        const String cmdI = pI.length()    ? pI    : configManager.getTdkLambdaCmdI();
        const String cmdS = pSt.length()   ? pSt   : configManager.getTdkLambdaCmdState();
        const String map  = pMap.length()  ? pMap  : configManager.getTdkLambdaMap();
        String sv = tdkFormValue(req.body, "scale_v");
        String si = tdkFormValue(req.body, "scale_i");
        String ov = tdkFormValue(req.body, "off_v");
        String oi = tdkFormValue(req.body, "off_i");
        const float fsv = sv.length() ? sv.toFloat() : configManager.getTdkLambdaScaleV();
        const float fsi = si.length() ? si.toFloat() : configManager.getTdkLambdaScaleI();
        const float fov = ov.length() ? ov.toFloat() : configManager.getTdkLambdaOffV();
        const float foi = oi.length() ? oi.toFloat() : configManager.getTdkLambdaOffI();
        configManager.setTdkLambdaProfile(mode, cmdV, cmdI, cmdS, map, fsv, fsi, fov, foi);
        tdkLambdaApplyProfile(mode.c_str(), cmdV.c_str(), cmdI.c_str(), cmdS.c_str(),
                              map.c_str(), fsv, fsi, fov, foi);
    }

    String j = F("{\"success\":true");
    j += String(F(",\"poll\":"))   + (poll ? F("true") : F("false"));
    j += String(F(",\"poll_s\":")) + String(sec);
    j += String(F(",\"csv\":"))    + (csv ? F("true") : F("false"));
    j += F(",\"format\":\"") + fmt + F("\"");
    j += F("}");
    res.sendJson(j);
#else
    (void)req;
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}

void handleTdkLambdaCsvFlushPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    const bool ok = tdkLambdaCsvFlushNow();
    res.sendJson(ok ? F("{\"success\":true,\"message\":\"flushed\"}")
                    : F("{\"success\":false,\"message\":\"write failed (SD/flash)\"}"));
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}

void handleTdkLambdaSamplesGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TDK_LAMBDA == 1
    int want = 60;
    const int q = req.query.indexOf("n=");
    if (q >= 0) want = req.query.substring(q + 2).toInt();
    if (want < 2) want = 2;
    if (want > 120) want = 120;

    static TdkSample samples[120];
    const uint16_t n = tdkLambdaCopySamples(samples, (uint16_t)want);
    const uint32_t now = millis();
    String j = F("{\"success\":true,\"samples\":[");
    for (uint16_t i = 0; i < n; ++i) {
        if (i) j += ',';
        // [age_seconds, volts, amps] — compact, the page computes the axis.
        j += '[';
        j += String((now - samples[i].ts) / 1000UL);
        j += ',';
        j += String(samples[i].v, 2);
        j += ',';
        j += String(samples[i].i, 3);
        j += ']';
    }
    j += F("]}");
    res.sendJson(j);
#else
    (void)req;
    res.sendJson(F("{\"success\":false,\"samples\":[]}"));
#endif
}

void handleTdkLambdaCsvGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    const String path = tdkLambdaCsvPath();
    String body;
    const size_t kMaxCsvBytes = 220UL * 1024UL;

    if (sdcardShouldUseSD()) {
#if ENABLE_SDCARD==1
        if (sdMutexLock(pdMS_TO_TICKS(1500))) {
            File f = SD_MMC.open(path.c_str(), FILE_READ);
            if (f) {
                while (f.available() && body.length() < kMaxCsvBytes) body += (char)f.read();
                f.close();
            }
            sdMutexUnlock();
        }
#endif
    } else {
#if ENABLE_FILESYSTEM==1
        if (lfsMutexLock(pdMS_TO_TICKS(2000))) {
            File f = LittleFS.open(path.c_str(), FILE_READ);
            if (f) {
                while (f.available() && body.length() < kMaxCsvBytes) body += (char)f.read();
                f.close();
            }
            lfsMutexUnlock();
        }
#endif
    }

    if (body.length() == 0) {
        res.code = 404;
        res.sendJson(F("{\"success\":false,\"message\":\"no CSV for today yet\"}"));
        return;
    }
    res.send(200, F("text/csv"), body);
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}
