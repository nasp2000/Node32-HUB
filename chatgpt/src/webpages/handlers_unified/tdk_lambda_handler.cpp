// tdk_lambda_handler.cpp — unified handlers for the /tdk-lambda page.
// Registered with registerApiRoute() so WiFi and Ethernet (W5500) both serve
// them: ethernet_bootstrap_routes.h dispatches unknown /api/ paths through
// findApiRouteHandler().
//
// String building rule (Arduino WString): never start an expression with
// F("...") — append pieces to a String instead, or the operands are ambiguous.
#include "tdk_lambda_handler.h"

#include "../../feature_flags.h"
#include "../../ConfigManager.h"
#include <ArduinoJson.h>

extern ConfigManager configManager;

#if ENABLE_TDK_LAMBDA == 1
#include "../../tdk_lambda.h"
#include "../../sdcard.h"
#include "../../littlefs_manager.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

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

static bool tdkTruthy(const String& v) {
    return (v == "1" || v == "true" || v == "on");
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

    String j;
    j.reserve(1400);
    j += F("{\"success\":true,\"status\":{");
    j += F("\"polling\":");           j += st.polling ? F("true") : F("false");
    j += F(",\"csvEnabled\":");       j += st.csvEnabled ? F("true") : F("false");
    j += F(",\"csvSensorFormat\":");  j += st.csvSensorFormat ? F("true") : F("false");
    j += F(",\"autoMap\":");          j += st.autoMap ? F("true") : F("false");
    j += F(",\"listen\":");           j += st.listen ? F("true") : F("false");
    j += F(",\"allowWrite\":");       j += tdkLambdaAllowWrite() ? F("true") : F("false");
    j += F(",\"pollIntervalMs\":");   j += String(st.pollIntervalMs);
    j += F(",\"lastRxAgeMs\":");      j += String(st.lastRxAgeMs);
    j += F(",\"logCount\":");         j += String(st.logCount);
    j += F(",\"csvRows\":");          j += String(st.csvRows);
    j += F(",\"csvOnSd\":");          j += st.csvOnSd ? F("true") : F("false");
    j += F(",\"address\":");          j += String(st.address);
    j += F(",\"inject\":");           j += st.inject ? F("true") : F("false");
    j += F(",\"alertActive\":");      j += st.alertActive ? F("true") : F("false");
    j += F(",\"lastErrorAgeMs\":");   j += String(st.lastErrorAgeMs);
    j += F(",\"scaleVUsed\":");       j += String(st.scaleVUsed, 4);
    j += F(",\"scaleIUsed\":");       j += String(st.scaleIUsed, 4);

    j += F(",\"csvPath\":\"");        j += tdkJsonEscape(st.csvPath);     j += '"';
    j += F(",\"csvEvery\":");        j += String(st.csvEvery);
    j += F(",\"csvRowMs\":");        j += String(st.csvRowMs);
    j += F(",\"aggCount\":");        j += String(st.aggCount);
    j += F(",\"sessionWh\":");       j += String(st.sessionWh, 3);
    j += F(",\"sessionCcMs\":");     j += String(st.sessionCcMs);
    j += F(",\"sessionLimitMs\":");  j += String(st.sessionLimitMs);
    j += F(",\"sessionOffMs\":");    j += String(st.sessionOffMs);
    j += F(",\"cvccTransitions\":"); j += String(st.cvccTransitions);
    j += F(",\"rateV\":");           j += String(st.rateV, 2);
    j += F(",\"rateI\":");           j += String(st.rateI, 3);
    j += F(",\"unattributed\":");    j += String(st.unattributed);
    j += F(",\"selfTest\":{\"running\":"); j += st.selfTestActive ? F("true") : F("false");
    j += F(",\"step\":");   j += String((unsigned)st.selfTestStep);
    j += F(",\"steps\":");  j += String((unsigned)st.selfTestTotal);
    j += F(",\"ok\":");     j += String((unsigned)st.selfTestOk);
    j += F(",\"fail\":");   j += String((unsigned)st.selfTestFail);
    j += F(",\"result\":\""); j += tdkJsonEscape(st.selfTestVerdict); j += '"';
    j += F(",\"detail\":\""); j += tdkJsonEscape(st.selfTestDetail); j += '"';
    j += F("}");
    j += F(",\"lastCommand\":\"");    j += tdkJsonEscape(st.lastCommand); j += '"';
    j += F(",\"lastLine\":\"");       j += tdkJsonEscape(st.lastLine);    j += '"';
    j += F(",\"profileMode\":\"");    j += tdkJsonEscape(st.profileMode); j += '"';
    j += F(",\"autoNote\":\"");       j += tdkJsonEscape(st.autoNote);    j += '"';
    j += F(",\"mapUsed\":\"");        j += tdkJsonEscape(st.mapUsed);     j += '"';
    j += F(",\"idn\":\"");            j += tdkJsonEscape(st.idn);         j += '"';
    j += F(",\"rev\":\"");            j += tdkJsonEscape(st.rev);         j += '"';
    j += F(",\"sn\":\"");             j += tdkJsonEscape(st.sn);          j += '"';
    j += F(",\"lastError\":\"");      j += tdkJsonEscape(st.lastError);   j += '"';
    j += F(",\"alertText\":\"");      j += tdkJsonEscape(st.alertText);   j += '"';

    j += F("},\"values\":{");
    j += F("\"valid\":");   j += v.valid ? F("true") : F("false");
    j += F(",\"vMeas\":");  j += String(v.vMeas, 2);
    j += F(",\"vSet\":");   j += String(v.vSet, 2);
    j += F(",\"iMeas\":");  j += String(v.iMeas, 3);
    j += F(",\"iSet\":");   j += String(v.iSet, 3);
    j += F(",\"ovp\":");    j += String(v.ovp, 1);
    j += F(",\"uvl\":");    j += String(v.uvl, 1);
    j += F(",\"mode\":\""); j += tdkJsonEscape(v.mode); j += '"';
    j += F(",\"outOn\":");  j += v.outOn ? F("true") : F("false");
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
    String j;
    j.reserve(256 + n * 220);
    j += F("{\"success\":true,\"commands\":[");
    for (size_t i = 0; i < n; ++i) {
        if (i) j += ',';
        j += F("{\"cmd\":\"");     j += tdkJsonEscape(cat[i].cmd);   j += '"';
        j += F(",\"group\":\"");   j += tdkJsonEscape(cat[i].group); j += '"';
        j += F(",\"usage\":\"");   j += tdkJsonEscape(cat[i].usage); j += '"';
        j += F(",\"desc\":\"");    j += tdkJsonEscape(cat[i].desc);  j += '"';
        j += F(",\"reply\":\"");   j += tdkJsonEscape(cat[i].reply); j += '"';
        j += F(",\"notes\":\"");   j += tdkJsonEscape(cat[i].notes); j += F("\"}");
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
    String j;
    j.reserve(128 + n * 190);
    j += F("{\"success\":true,\"entries\":[");
    for (uint16_t i = 0; i < n; ++i) {
        if (i) j += ',';
        j += F("{\"dir\":\"");  j += tdkJsonEscape(entries[i].direction); j += '"';
        j += F(",\"ageMs\":");  j += String(entries[i].ageMs);
        j += F(",\"text\":\""); j += tdkJsonEscape(entries[i].text);      j += F("\"}");
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
        res.sendJson(F("{\"success\":false,\"message\":\"listen mode: TX disabled (the PLC owns the line)\"}"));
        return;
    }
    if (!tdkLambdaAllowWrite() && !tdkIsReadOnlyCommand(cmd)) {
        res.code = 403;
#if TDK_READ_ONLY
        res.sendJson(F("{\"success\":false,\"message\":\"read-only module: only queries are sent\"}"));
#else
        res.sendJson(F("{\"success\":false,\"message\":\"write command blocked (enable 'allow write')\"}"));
#endif
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
    String j;
    j.reserve(768);
    j += F("{\"success\":true");
    j += F(",\"poll\":");        j += configManager.getTdkLambdaPoll() ? F("true") : F("false");
    j += F(",\"poll_s\":");      j += String(configManager.getTdkLambdaPollSec());
    j += F(",\"csv_every\":");   j += String(configManager.getTdkLambdaCsvEvery());
    j += F(",\"csv\":");         j += configManager.getTdkLambdaCsv() ? F("true") : F("false");
    j += F(",\"format\":\"");    j += configManager.getTdkLambdaCsvFormat() ? F("sensor") : F("table");  j += '"';
    j += F(",\"auto_map\":");    j += configManager.getTdkLambdaAutoMap() ? F("true") : F("false");
    j += F(",\"listen\":");      j += configManager.getTdkLambdaListen() ? F("true") : F("false");
    j += F(",\"allow_write\":"); j += tdkLambdaAllowWrite() ? F("true") : F("false");
#if TDK_READ_ONLY
    j += F(",\"ro\":true");     // this build never writes: the page hides the switch
#else
    j += F(",\"ro\":false");
#endif
    j += F(",\"addr\":");        j += String(configManager.getTdkLambdaAddr());
    j += F(",\"inject\":");      j += configManager.getTdkLambdaInject() ? F("true") : F("false");
    j += F(",\"mode\":\"");      j += configManager.getTdkLambdaMode();                    j += '"';
    j += F(",\"cmd_v\":\"");     j += tdkJsonEscape(configManager.getTdkLambdaCmdV().c_str());     j += '"';
    j += F(",\"cmd_i\":\"");     j += tdkJsonEscape(configManager.getTdkLambdaCmdI().c_str());     j += '"';
    j += F(",\"cmd_state\":\""); j += tdkJsonEscape(configManager.getTdkLambdaCmdState().c_str()); j += '"';
    j += F(",\"map\":\"");       j += tdkJsonEscape(configManager.getTdkLambdaMap().c_str());      j += '"';
    j += F(",\"scale_v\":");     j += String(configManager.getTdkLambdaScaleV(), 4);
    j += F(",\"scale_i\":");     j += String(configManager.getTdkLambdaScaleI(), 4);
    j += F(",\"off_v\":");       j += String(configManager.getTdkLambdaOffV(), 4);
    j += F(",\"off_i\":");       j += String(configManager.getTdkLambdaOffI(), 4);
    j += F(",\"rate_v\":");      j += String(configManager.getTdkLambdaRateV(), 2);
    j += F(",\"rate_i\":");      j += String(configManager.getTdkLambdaRateI(), 3);
    j += F(",\"limits\":{");
    j += F("\"enabled\":");      j += configManager.getTdkLambdaLimitsEnabled() ? F("true") : F("false");
    j += F(",\"v_min\":");       j += String(configManager.getTdkLambdaLimitVMin(), 2);
    j += F(",\"v_max\":");       j += String(configManager.getTdkLambdaLimitVMax(), 2);
    j += F(",\"i_max\":");       j += String(configManager.getTdkLambdaLimitIMax(), 3);
    j += F("}}");
    res.sendJson(j);
#else
    res.sendJson(F("{\"success\":false}"));
#endif
}

void handleTdkLambdaConfigPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_TDK_LAMBDA == 1
    // --- extras: multi-drop address, monitor injection, limit alerts ---------
    const String addrS = tdkFormValue(req.body, "addr");
    const String injS  = tdkFormValue(req.body, "inject");
    const String limS  = tdkFormValue(req.body, "limits_enabled");
    if (addrS.length() || injS.length() || limS.length() ||
        req.body.indexOf("limit_v_min=") >= 0) {
        const int  addr = addrS.length() ? addrS.toInt() : configManager.getTdkLambdaAddr();
        const bool inj  = injS.length() ? tdkTruthy(injS) : configManager.getTdkLambdaInject();
        const bool lim  = limS.length() ? tdkTruthy(limS) : configManager.getTdkLambdaLimitsEnabled();
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

    // --- safety: listen / allow-write --------------------------------------
    const String listenS = tdkFormValue(req.body, "listen");
    const String allowS  = tdkFormValue(req.body, "allow_write");
    if (listenS.length() || allowS.length()) {
        const bool listenOn = listenS.length() ? tdkTruthy(listenS) : configManager.getTdkLambdaListen();
        const bool allowOn  = allowS.length()  ? tdkTruthy(allowS)  : configManager.getTdkLambdaAllowWrite();
        configManager.setTdkLambdaSafety(listenOn, allowOn);
        tdkLambdaSetListen(listenOn);
    }

    // --- auto-detect toggle ------------------------------------------------
    const String autoS = tdkFormValue(req.body, "auto_map");
    if (autoS.length()) {
        const bool autoOn = tdkTruthy(autoS);
        configManager.setTdkLambdaAutoMap(autoOn);
        tdkLambdaSetAutoMap(autoOn);
    }

    // --- polling / CSV -----------------------------------------------------
    const String pollS   = tdkFormValue(req.body, "poll");
    const String pollSec = tdkFormValue(req.body, "poll_s");
    const String csvS    = tdkFormValue(req.body, "csv");
    const String fmtS    = tdkFormValue(req.body, "format");
    const bool poll = tdkTruthy(pollS);
    const int  sec  = pollSec.length() ? pollSec.toInt() : configManager.getTdkLambdaPollSec();
    const bool csv  = tdkTruthy(csvS);
    const String fmt = (fmtS == "table") ? String("table") : String("sensor");
    const String everyS = tdkFormValue(req.body, "csv_every");
    if (everyS.length()) {
        int n = everyS.toInt();
        if (n < 1) n = 1;
        if (n > 60) n = 60;
        configManager.setTdkLambdaCsvEvery(n);
        tdkLambdaSetCsvEvery((uint16_t)n);
    }

    if (pollS.length() || pollSec.length() || csvS.length() || fmtS.length()) {
        configManager.setTdkLambda(poll, sec, csv, fmt);
        tdkLambdaSetPoll(poll, (uint32_t)((sec < 60 ? 60 : (sec > 3600 ? 3600 : sec))) * 1000UL);
        tdkLambdaSetCsvFormat(fmt == "sensor");
        tdkLambdaSetCsv(csv);
    }

    // --- device profile (optional fields) ----------------------------------
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
        const String sv = tdkFormValue(req.body, "scale_v");
        const String si = tdkFormValue(req.body, "scale_i");
        const String ov = tdkFormValue(req.body, "off_v");
        const String oi = tdkFormValue(req.body, "off_i");
        const float fsv = sv.length() ? sv.toFloat() : configManager.getTdkLambdaScaleV();
        const float fsi = si.length() ? si.toFloat() : configManager.getTdkLambdaScaleI();
        const float fov = ov.length() ? ov.toFloat() : configManager.getTdkLambdaOffV();
        const float foi = oi.length() ? oi.toFloat() : configManager.getTdkLambdaOffI();
        configManager.setTdkLambdaProfile(mode, cmdV, cmdI, cmdS, map, fsv, fsi, fov, foi);
        tdkLambdaApplyProfile(mode.c_str(), cmdV.c_str(), cmdI.c_str(), cmdS.c_str(),
                              map.c_str(), fsv, fsi, fov, foi);
    }

    // --- unit rating (used by the layout detection, never to clamp readings) --
    const String rateVS = tdkFormValue(req.body, "rate_v");
    const String rateIS = tdkFormValue(req.body, "rate_i");
    if (rateVS.length() || rateIS.length()) {
        const float rv = rateVS.length() ? rateVS.toFloat() : configManager.getTdkLambdaRateV();
        const float ri = rateIS.length() ? rateIS.toFloat() : configManager.getTdkLambdaRateI();
        if (rv > 0.0f && rv <= 2000.0f && ri > 0.0f && ri <= 5000.0f) {
            configManager.setTdkLambdaRates(rv, ri);
            tdkLambdaSetRates(rv, ri);
        }
    }

    String j;
    j.reserve(160);
    j += F("{\"success\":true");
    j += F(",\"poll\":");    j += poll ? F("true") : F("false");
    j += F(",\"poll_s\":");  j += String(sec);
    j += F(",\"csv\":");     j += csv ? F("true") : F("false");
    j += F(",\"format\":\""); j += fmt; j += '"';
    j += F(",\"csv_every\":"); j += String(configManager.getTdkLambdaCsvEvery());
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
    if (want > TDK_SAMPLES_MAX) want = TDK_SAMPLES_MAX;   // 4 h at 1/min

    // Static, not stack/heap: the ring is copied here and freed with the TU.
    static TdkSample samples[TDK_SAMPLES_MAX];
    const uint16_t n = tdkLambdaCopySamples(samples, (uint16_t)want);
    const uint32_t now = millis();
    String j;
    j.reserve(64 + n * 26);
    j += F("{\"success\":true,\"samples\":[");
    for (uint16_t i = 0; i < n; ++i) {
        if (i) j += ',';
        j += '['; j += String((now - samples[i].ts) / 1000UL);
        j += ','; j += String(samples[i].v, 2);
        j += ','; j += String(samples[i].i, 3);
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
    // The rows wait in the module's RAM buffer until the configured window:
    // flush first so a download carries everything up to now (no-op when empty).
    (void)tdkLambdaCsvFlushNow();

    const String path = tdkLambdaCsvPath();
    const size_t kMaxCsvBytes = 1024UL * 1024UL;   // a full day at 1/min fits

    File f;
    bool lockedSd = false, lockedFs = false;
    if (sdcardShouldUseSD()) {
#if ENABLE_SDCARD==1
        if (sdMutexLock(pdMS_TO_TICKS(1500))) {
            f = SD_MMC.open(path.c_str(), FILE_READ);
            lockedSd = true;
        }
#endif
    } else {
#if ENABLE_FILESYSTEM==1
        if (lfsMutexLock(pdMS_TO_TICKS(2000))) {
            f = LittleFS.open(path.c_str(), FILE_READ);
            lockedFs = true;
        }
#endif
    }

    const size_t fileSize = (f && !f.isDirectory()) ? (size_t)f.size() : 0;
    if (fileSize == 0) {
        if (f) f.close();
#if ENABLE_SDCARD==1
        if (lockedSd) sdMutexUnlock();
#endif
#if ENABLE_FILESYSTEM==1
        if (lockedFs) { extern void lfsMutexUnlock(); lfsMutexUnlock(); }
#endif
        res.code = 404;
        res.sendJson(F("{\"success\":false,\"message\":\"no CSV for today yet\"}"));
        return;
    }

    // One PSRAM buffer (internal RAM only as a last resort, capped): never a
    // 200 KB+ String on the heap.
    size_t want = (fileSize > kMaxCsvBytes) ? kMaxCsvBytes : fileSize;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(want + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        want = (want > (256UL * 1024UL)) ? (256UL * 1024UL) : want;
        buf = (uint8_t*)heap_caps_malloc(want + 1, MALLOC_CAP_8BIT);
    }
    if (!buf) {
        if (f) f.close();
#if ENABLE_SDCARD==1
        if (lockedSd) sdMutexUnlock();
#endif
#if ENABLE_FILESYSTEM==1
        if (lockedFs) { extern void lfsMutexUnlock(); lfsMutexUnlock(); }
#endif
        res.code = 503;
        res.sendJson(F("{\"success\":false,\"message\":\"not enough memory for the CSV\"}"));
        return;
    }

    size_t got = 0;
    while (got < want && f.available()) {
        const int n = f.read(buf + got, want - got);
        if (n <= 0) break;
        got += (size_t)n;
        esp_task_wdt_reset();
    }
    f.close();
#if ENABLE_SDCARD==1
    if (lockedSd) sdMutexUnlock();
#endif
#if ENABLE_FILESYSTEM==1
    if (lockedFs) { extern void lfsMutexUnlock(); lfsMutexUnlock(); }
#endif

    if (got == 0) {
        heap_caps_free(buf);
        res.code = 404;
        res.sendJson(F("{\"success\":false,\"message\":\"no CSV for today yet\"}"));
        return;
    }

    // Truncated (beyond the cap or short of memory): end on a line boundary so
    // the last row is never half a row, and say so on the monitor.
    if (got < fileSize) {
        size_t cut = got;
        while (cut > 0 && buf[cut - 1] != '\n') --cut;
        if (cut > 0) got = cut;
        char msg[96];
        snprintf(msg, sizeof(msg), "CSV download truncated: %u of %u bytes",
                 (unsigned)got, (unsigned)fileSize);
        sendVirtualMessage(String(F("[TDK] ")) + msg, 1, true, false);
    }

    res.code = 200;
    res.contentType = F("text/csv");
    res.isBinary = true;
    res.binaryData = buf;
    res.binaryLen = got;
    res.freeFn = [](const uint8_t* p) { heap_caps_free((void*)p); };
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}

void handleTdkLambdaSelfTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_TDK_LAMBDA == 1
    if (configManager.getTdkLambdaListen()) {
        res.code = 409;
        res.sendJson(F("{\"success\":false,\"message\":\"listen mode: the line test needs TX\"}"));
        return;
    }
    if (!tdkLambdaStartSelfTest()) {
        res.code = 409;
        res.sendJson(F("{\"success\":false,\"message\":\"a line test is already running\"}"));
        return;
    }
    res.sendJson(F("{\"success\":true,\"running\":true}"));
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"module disabled\"}"));
#endif
}
