#include "do_handler.h"
#include "../../tca9554_do.h"
#include "../../feature_flags.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../arduinojson_fix.h"
#include "../../ConfigManager.h"
#include "../../config_manager_runtime.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

void handleDoStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t mask = tca9554GetMask();
    char buf[128];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "{\"ch_count\":%d,\"mask\":%u,\"outputs\":[",
                    TCA9554_DO_CHANNELS, (unsigned)mask);
    for (int i = 0; i < TCA9554_DO_CHANNELS; i++) {
        if (i > 0) buf[pos++] = ',';
        const char* v = (mask & (1 << i)) ? "true" : "false";
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", v);
    }
    if (pos < (int)sizeof(buf) - 3) { buf[pos++] = ']'; buf[pos++] = '}'; buf[pos] = '\0'; }
    res.code = 200;
    res.contentType = F("application/json");
    res.body = String(buf);
}

void handleDoConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    SpiRamJsonDocument doc(512);
    JsonVariant doSec = configManager.getSection("do");
    doc["ch_count"] = TCA9554_DO_CHANNELS;
    doc["invert"] = (uint8_t)(doSec["invert"] | 0);
    JsonArray names = doc.createNestedArray("names");
    for (int i = 0; i < TCA9554_DO_CHANNELS; i++) {
        names.add(doSec["names"][i] | "");
    }
    String out;
    serializeJson(doc, out);
    res.code = 200;
    res.contentType = F("application/json");
    res.body = out;
}

#else
void handleDoStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.code = 404;
    res.contentType = F("application/json");
    res.body = F("{\"error\":\"not found\"}");
}
void handleDoConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.code = 404;
    res.contentType = F("application/json");
    res.body = F("{\"error\":\"not found\"}");
}
#endif
