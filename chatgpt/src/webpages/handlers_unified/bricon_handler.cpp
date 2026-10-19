#include "bricon_handler.h"
#include "../../feature_flags.h"
#include "../../languages/lang_active.h"

#if ENABLE_BRICON == 1

#include <Arduino.h>
#include "../../bricon_protocol.h"
#include "../../psram_helpers.h"

// The poll runs on the main loop (blocking RS-232 I/O must not run in the web
// task), so the endpoints schedule it and answer immediately.
static void briconScheduledJson(HttpResponse& res) {
    res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiBriconPending) + F("\"}"));
}

void handleBriconStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    BriconConfig cc = briconGetConfig();
    BriconStatus st = briconGetStatus();
    uint32_t agoSec = (st.lastRxMs > 0) ? (millis() - st.lastRxMs) / 1000UL : 0;
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"connected\":%s,\"hardwarePending\":%s,\"enabled\":%s,"
        "\"intervalSec\":%u,\"baud\":%d,\"dataBits\":%d,\"parity\":\"%s\",\"stopBits\":%d,"
        "\"country\":\"%s\",\"baAddress\":%d,\"clubId\":\"%s\",\"flightId\":\"%s\","
        "\"baAddress\":%u,\"lastNewCount\":%u,\"lastPollAgoSec\":%lu,"
        "\"lastRxAgoSec\":%lu,\"bytesRx\":%lu,\"framesSeen\":%u,"
        "\"activeModule\":\"%s\"}",
        st.connected ? "true" : "false",
        st.hardwarePending ? "true" : "false",
        cc.enabled ? "true" : "false",
        (unsigned)cc.pollIntervalSec,
        cc.baudrate, cc.dataBits, cc.parity, cc.stopBits,
        cc.country, cc.baAddress, cc.clubId, cc.flightId,
        (unsigned)st.baAddress, (unsigned)st.lastNewCount,
        (unsigned long)((st.lastPollMs > 0) ? (millis() - st.lastPollMs) / 1000UL : 0),
        (unsigned long)agoSec,
        (unsigned long)st.bytesRx,
        (unsigned)st.framesSeen,
        briconActiveModuleName());
    res.sendJson(String(buf));
}

void handleBriconArrivalsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    static BriconArrival* buf = nullptr;
    if (!buf) {
        buf = (BriconArrival*)(psramFound() ? ps_malloc(BRICON_MAX_ARRIVALS * sizeof(BriconArrival))
                                            : malloc(BRICON_MAX_ARRIVALS * sizeof(BriconArrival)));
    }
    String json = F("{\"count\":0,\"arrivals\":[]}");
    if (buf) {
        uint16_t count = briconGetArrivals(buf, BRICON_MAX_ARRIVALS);
        json.reserve(count * 64 + 20);
        json = "{\"count\":";
        json += count;
        json += ",\"arrivals\":[";
        for (uint16_t i = 0; i < count; i++) {
            if (i > 0) json += ',';
            json += "{\"ring\":\"";
            json += buf[i].ring;
            json += "\",\"chip\":\"";
            json += buf[i].chip;
            json += "\",\"time\":\"";
            json += buf[i].arrivalTime;
            json += "\",\"date\":\"";
            json += buf[i].arrivalDate;
            json += "\",\"kind\":\"";
            json += buf[i].isTraining ? "training" : "race";
            json += "\",\"race\":\"";
            json += buf[i].race;
            json += "\",\"sex\":\"";
            json += buf[i].sex ? buf[i].sex : '?';
            json += "\",\"color\":\"";
            json += buf[i].color;
            json += "\",\"category\":";
            json += (unsigned)buf[i].category;
            json += ",\"speed\":";
            json += (unsigned)buf[i].speed;
            json += ",\"speed2\":";
            json += (unsigned)buf[i].speed2;
            json += ",\"antenna\":";
            json += (unsigned)buf[i].antenna;
            json += ",\"posArriveClub\":";
            json += (unsigned)buf[i].posArriveClub;
            json += ",\"posEnterClub\":";
            json += (unsigned)buf[i].posEnterClub;
            json += ",\"evaluation\":\"";
            json += buf[i].evaluation ? buf[i].evaluation : '?';
            json += "\"";
            json += "}";
        }
        json += "]}";
    }
    res.sendJson(json);
}

void handleBriconRegistrationsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    static BriconArrival* buf = nullptr;
    if (!buf) {
        buf = (BriconArrival*)(psramFound() ? ps_malloc(BRICON_MAX_ARRIVALS * sizeof(BriconArrival))
                                            : malloc(BRICON_MAX_ARRIVALS * sizeof(BriconArrival)));
    }
    String json = F("{\"count\":0,\"registrations\":[]}");
    if (buf) {
        uint16_t count = briconGetRegistrations(buf, BRICON_MAX_ARRIVALS);
        json.reserve(count * 48 + 20);
        json = "{\"count\":";
        json += count;
        json += ",\"registrations\":[";
        for (uint16_t i = 0; i < count; i++) {
            if (i > 0) json += ',';
            json += "{\"ring\":\"";
            json += buf[i].ring;
            json += "\",\"chip\":\"";
            json += buf[i].chip;
            json += "\"}";
        }
        json += "]}";
    }
    res.sendJson(json);
}

void handleBriconContestsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    BriconContest list[BRICON_MAX_CONTESTS];
    uint16_t count = briconGetContests(list, BRICON_MAX_CONTESTS);
    String json = F("{\"count\":0,\"contests\":[]}");
    if (count > 0) {
        json = "{\"count\":";
        json += count;
        json += ",\"contests\":[";
        for (uint16_t i = 0; i < count; i++) {
            if (i > 0) json += ',';
            json += "{\"code\":\"";
            json += list[i].code;
            json += "\",\"date\":\"";
            json += list[i].date;
            json += "\",\"name\":\"";
            json += list[i].name;
            json += "\",\"club\":\"";
            json += list[i].club;
            json += "\",\"distance\":";
            json += (unsigned long)list[i].distance;
            json += ",\"pigeonCount\":";
            json += (unsigned)list[i].pigeonCount;
            json += ",\"emailSent\":";
            json += list[i].emailSent ? "true" : "false";
            json += ",\"webSent\":";
            json += list[i].webSent ? "true" : "false";
            json += "}";
        }
        json += "]}";
    }
    res.sendJson(json);
}

void handleBriconPombalGet(HttpRequest& req, HttpResponse& res) {
    handleBriconRegistrationsGet(req, res);
}

void handleBriconConfigPost(HttpRequest& req, HttpResponse& res) {
    // urlencoded body: enabled, interval, baud (+ parity/data/stop defaults).
    bool hasEnabled = req.body.indexOf("enabled=") >= 0;
    bool wantOn = hasEnabled && req.body.indexOf("enabled=1") >= 0;
    BriconConfig cfg = briconGetConfig();
    if (hasEnabled) cfg.enabled = wantOn;
    {
        int ii = req.body.indexOf("interval=");
        if (ii >= 0) cfg.pollIntervalSec = (uint16_t)req.body.substring((size_t)ii + 9).toInt();
    }
    {
        int bi = req.body.indexOf("baud=");
        if (bi >= 0) {
            int b = req.body.substring((size_t)bi + 5).toInt();
            if (b > 0) cfg.baudrate = b;
        }
    }
    {
        // REA-derived profile fields: the serial framing and the layout country.
        int di = req.body.indexOf("data=");
        if (di >= 0) { int v = req.body.substring((size_t)di + 5).toInt(); if (v >= 5 && v <= 8) cfg.dataBits = v; }
        int si = req.body.indexOf("stop=");
        if (si >= 0) { int v = req.body.substring((size_t)si + 5).toInt(); if (v == 1 || v == 2) cfg.stopBits = v; }
        int pi = req.body.indexOf("parity=");
        if (pi >= 0) {
            String p = req.body.substring((size_t)pi + 7);
            int amp = p.indexOf('&'); if (amp >= 0) p = p.substring(0, (unsigned)amp);
            p.toLowerCase();
            if (p == "even" || p == "odd" || p == "none") strlcpy(cfg.parity, p.c_str(), sizeof(cfg.parity));
        }
        int ci = req.body.indexOf("country=");
        if (ci >= 0) {
            String c = req.body.substring((size_t)ci + 8);
            int amp = c.indexOf('&'); if (amp >= 0) c = c.substring(0, (unsigned)amp);
            c.toUpperCase(); c.trim();
            if (c.length() >= 2 && c.length() <= 3) strlcpy(cfg.country, c.c_str(), sizeof(cfg.country));
        }
        int li = req.body.indexOf("club=");
        if (li >= 0) {
            String v = req.body.substring((size_t)li + 5);
            int amp = v.indexOf('&'); if (amp >= 0) v = v.substring(0, (unsigned)amp);
            v.trim();
            if (v.length() == 4) strlcpy(cfg.clubId, v.c_str(), sizeof(cfg.clubId));
        }
        int fi = req.body.indexOf("flight=");
        if (fi >= 0) {
            String v = req.body.substring((size_t)fi + 7);
            int amp = v.indexOf('&'); if (amp >= 0) v = v.substring(0, (unsigned)amp);
            v.trim();
            if (v.length() == 4) strlcpy(cfg.flightId, v.c_str(), sizeof(cfg.flightId));
        }
        int ai = req.body.indexOf("ba=");
        if (ai >= 0) {
            String a = req.body.substring((size_t)ai + 3);
            int amp = a.indexOf('&'); if (amp >= 0) a = a.substring(0, (unsigned)amp);
            long v = strtol(a.c_str(), nullptr, 0);   // accepts 0, 0x82, 130
            if (v == 0 || v == 0x10 || (v >= 0x80 && v <= 0x85)) cfg.baAddress = (int)v;
        }
    }
    briconSetConfig(cfg);
    if (cfg.enabled) briconSetEnabled(true);
    else briconSetEnabled(false);
    if (cfg.enabled)
        res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiBriconCfgActive) + F("\"}"));
    else
        res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiBriconCfgOff) + F("\"}"));
}

void handleBriconPollPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!briconIsActive()) {
        res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiBriconDisabled) + F("\"}"));
        return;
    }
    briconRequestWOnlyPoll();
    briconScheduledJson(res);
}

void handleBriconSyncPombalPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!briconIsActive()) {
        res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiBriconDisabled) + F("\"}"));
        return;
    }
    briconRequestXOnlyPoll();
    briconScheduledJson(res);
}

void handleBriconSyncCalendarPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!briconIsActive()) {
        res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiBriconDisabled) + F("\"}"));
        return;
    }
    briconRequestIcOnlyPoll();
    briconScheduledJson(res);
}

void handleBriconResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiBriconNothingReset) + F("\"}"));
}

void handleBriconSendTrainingPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!briconIsActive()) {
        res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiBriconDisabled) + F("\"}"));
        return;
    }
    briconScheduledJson(res);
}

void handleBriconClearTrainingPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!briconIsActive()) {
        res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiBriconDisabled) + F("\"}"));
        return;
    }
    briconScheduledJson(res);
}

#else

#include "../http_abstraction.h"

static void stubNotFound(HttpResponse& res) {
    res.code = 404;
    res.contentType = F("application/json");
    res.body = F("{\"error\":\"Bricon not enabled\"}");
}
void handleBriconStatusGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconArrivalsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconRegistrationsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconContestsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconPombalGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconConfigPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconPollPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconSyncPombalPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconSyncCalendarPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconResetPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconSendTrainingPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleBriconClearTrainingPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }

#endif
