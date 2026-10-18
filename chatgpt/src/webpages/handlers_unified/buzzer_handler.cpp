#include "buzzer_handler.h"
#include "feature_flags.h"
#include "../../languages/lang_active.h"
#if ENABLE_BUZZER==1
#include <ArduinoJson.h>
#include "../../buzzer.h"
#include "feature_flags.h"

void handleBuzzerConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    BuzzerConfig cfg = buzzerGetConfig();
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"enabled\":%s,\"pin\":%u"
        ",\"on_boot\":%s,\"on_save\":%s"
        ",\"on_alarm\":%s,\"on_sensor_alert\":%s"
        ",\"on_network\":%s,\"on_error\":%s}",
        cfg.enabled          ? "true" : "false",
        (unsigned)cfg.pin,
        cfg.on_boot          ? "true" : "false",
        cfg.on_save          ? "true" : "false",
        cfg.on_alarm         ? "true" : "false",
        cfg.on_sensor_alert  ? "true" : "false",
        cfg.on_network       ? "true" : "false",
        cfg.on_error         ? "true" : "false"
    );
    res.sendJson(String(buf));
}

void handleBuzzerConfigPost(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, F("application/json"), String(F("{\"ok\":false,\"msg\":\"")) + L(activeLang->apiJsonInvalid) + F("\"}"));
        return;
    }
    bool anyChanged = false;
    BuzzerConfig cfg = buzzerGetConfig();
    if (doc.containsKey(F("enabled")))         { cfg.enabled         = doc["enabled"].as<bool>();         anyChanged = true; }
    if (doc.containsKey(F("pin")))             { cfg.pin             = (uint8_t)(doc["pin"].as<int>());  anyChanged = true; }
    if (doc.containsKey(F("on_boot")))         { cfg.on_boot         = doc["on_boot"].as<bool>();         anyChanged = true; }
    if (doc.containsKey(F("on_save")))         { cfg.on_save         = doc["on_save"].as<bool>();         anyChanged = true; }
    if (doc.containsKey(F("on_alarm")))        { cfg.on_alarm        = doc["on_alarm"].as<bool>();        anyChanged = true; }
    if (doc.containsKey(F("on_sensor_alert"))) { cfg.on_sensor_alert = doc["on_sensor_alert"].as<bool>(); anyChanged = true; }
    if (doc.containsKey(F("on_network")))      { cfg.on_network      = doc["on_network"].as<bool>();      anyChanged = true; }
    if (doc.containsKey(F("on_error")))        { cfg.on_error        = doc["on_error"].as<bool>();        anyChanged = true; }
    if (!anyChanged) {
        res.send(400, F("application/json"), F("{\"ok\":false,\"msg\":\"Missing buzzer config fields\"}"));
        return;
    }
    buzzerApplyConfig(cfg);
    buzzerOnSave();
    res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiCfgSaved) + F("\"}"));
}

void handleBuzzerTestPost(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<64> doc;
    deserializeJson(doc, req.body);
    int pattern = doc["pattern"] | 1;
    buzzerBeep((BuzzerPattern)pattern);
    res.sendJson(F("{\"ok\":true}"));
}

void handleBuzzerStopPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    buzzerSilence();
    res.sendJson(F("{\"ok\":true}"));
}

#endif
