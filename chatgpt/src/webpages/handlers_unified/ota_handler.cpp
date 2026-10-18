#include "../../languages/lang_active.h"
#include "ota_handler.h"
#include "../../feature_flags.h"

#if ENABLE_OTA==1
#include <Arduino.h>
extern unsigned long otaEnabledUntil;

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

void handleEnableOtaPost(HttpRequest& req, HttpResponse& res) {
    unsigned long ttl = 300000UL;
    int v = queryInt(req.query, "ttl_ms", 0);
    if (v > 0) ttl = (unsigned long)v;
    if (!v && req.body.length() > 0) {
        v = queryInt(req.body, "ttl_ms", 0);
        if (v > 0) ttl = (unsigned long)v;
    }

    unsigned long enabledFor = ttl;
    otaEnabledUntil = otaEnabledUntil < millis() ? millis() + enabledFor : otaEnabledUntil + enabledFor;

    res.code = 200;
    res.contentType = F("application/json");
    res.body = String(F("{\"status\":\"ok\",\"enabled_until\":")) + String(otaEnabledUntil) +
               String(F(",\"ttl_ms\":")) + String(enabledFor) + String(F("}"));
}

#else
void handleEnableOtaPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.code = 503;
    res.contentType = F("application/json");
    res.body = String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiOtaNotCompiled) + F("\"}");
}
#endif
