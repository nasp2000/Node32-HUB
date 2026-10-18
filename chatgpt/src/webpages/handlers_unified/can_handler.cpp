#include "can_handler.h"
#include "../../feature_flags.h"
#include "../../languages/lang_active.h"

#if ENABLE_CAN==1
#include <Arduino.h>
#include "../../can_runtime.h"

static const char* stateStr(CanBusState s) {
    switch (s) {
        case CAN_BUS_RUNNING:   return "RUNNING";
        case CAN_BUS_BUS_OFF:   return "BUS_OFF";
        case CAN_BUS_RECOVERING: return "RECOVERING";
        case CAN_BUS_ERROR:     return "ERROR";
        default:                return "STOPPED";
    }
}

static int queryInt(const String& q, const char* key, int def) {
    String prefix = String(key) + "=";
    int start = 0;
    while (start <= (int)q.length()) {
        int amp = q.indexOf('&', start);
        if (amp < 0) amp = q.length();
        String pair = q.substring(start, amp);
        if (pair.startsWith(prefix)) {
            return pair.substring(prefix.length()).toInt();
        }
        start = amp + 1;
    }
    return def;
}

void handleCanStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    CanBusStats bs = canRuntimeGetBusStats();
    CanRuntimeStats rs = canRuntimeGetStats();
    const CanConfig& cfg = canRuntimeGetConfig();

    String json;
    json.reserve(220);
    json  = F("{\"state\":\"");
    json += stateStr(bs.state);
    json += F("\",\"mode\":");
    json += (int)cfg.mode;
    json += F(",\"bitrate_kbps\":");
    json += cfg.bitrate_kbps;
    json += F(",\"tec\":");
    json += bs.tec;
    json += F(",\"rec\":");
    json += bs.rec;
    json += F(",\"total_rx\":");
    json += rs.total_rx;
    json += F(",\"total_tx\":");
    json += rs.total_tx;
    json += F(",\"rx_errors\":");
    json += bs.rx_errors;
    json += F(",\"tx_errors\":");
    json += bs.tx_errors;
    json += F(",\"bus_off_count\":");
    json += rs.bus_off_events;
    json += F(",\"buffer_drops\":");
    json += rs.buffer_drops;
    json += F(",\"buffer_frames\":");
    json += canRuntimeFrameCount();
    json += F(",\"msgs_to_rx\":");
    json += bs.msgs_to_rx;
    json += F(",\"success\":true}");

    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleCanFramesGet(HttpRequest& req, HttpResponse& res) {
    size_t limit = (size_t)queryInt(req.query, "limit", 128);
    if (limit == 0 || limit > CAN_FRAME_BUFFER_SIZE) limit = 128;

    CanFrame* frames = (CanFrame*)malloc(limit * sizeof(CanFrame));
    if (!frames) {
        res.sendJson(F("{\"success\":false,\"message\":\"OOM\"}"));
        return;
    }

    size_t n = canRuntimeGetFrames(frames, limit);

    String json;
    json.reserve(64 + n * 80);
    json = F("{\"success\":true,\"count\":");
    json += n;
    json += F(",\"frames\":[");

    for (size_t i = 0; i < n; i++) {
        const CanFrame& f = frames[i];
        if (i > 0) json += ',';
        json += F("{\"ts\":");
        json += f.timestamp_ms;
        json += F(",\"id\":");
        json += f.id;
        json += F(",\"dlc\":");
        json += f.dlc;
        json += F(",\"ext\":");
        json += f.ext ? "true" : "false";
        json += F(",\"rtr\":");
        json += f.rtr ? "true" : "false";
        json += F(",\"data\":[");
        for (uint8_t b = 0; b < f.dlc && b < 8; b++) {
            if (b > 0) json += ',';
            json += f.data[b];
        }
        json += F("]}");
    }
    json += F("]}");

    free(frames);

    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleCanConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    const CanConfig& c = canRuntimeGetConfig();
    String json;
    json.reserve(180);
    json  = F("{\"success\":true,\"config\":{\"enabled\":");
    json += c.enabled ? "true" : "false";
    json += F(",\"bitrate_kbps\":");
    json += c.bitrate_kbps;
    json += F(",\"mode\":");
    json += (int)c.mode;
    json += F(",\"tx_pin\":");
    json += c.tx_pin;
    json += F(",\"rx_pin\":");
    json += c.rx_pin;
    json += F(",\"filter_id\":");
    json += c.filter_id;
    json += F(",\"filter_mask\":");
    json += c.filter_mask;
    json += F(",\"filter_ext\":");
    json += c.filter_ext ? "true" : "false";
    json += F(",\"log_to_sd\":");
    json += c.log_to_sd ? "true" : "false";
    json += F("}}");

    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleCanClearPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    canRuntimeClearFrames();
    res.sendJson(F("{\"success\":true,\"message\":\"Buffer limpo\"}"));
}

void handleCanRestartPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool ok = canRuntimeApplyConfig(canRuntimeGetConfig(), false);
    if (ok) {
        res.sendJson(F("{\"success\":true,\"message\":\"Driver CAN reiniciado\"}"));
    } else {
        res.code = 200;
        res.contentType = F("application/json");
        res.body = (String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanRestartFail) + "\"}");
    }
}

#else
void handleCanStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
}
void handleCanFramesGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
}
void handleCanConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
}
void handleCanClearPost(HttpRequest& req, HttpResponse& res) {
    (void)req; res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
}
void handleCanRestartPost(HttpRequest& req, HttpResponse& res) {
    (void)req; res.sendJson(String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiCanNotCompiled) + F("\"}"));
}
#endif
