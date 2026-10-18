#include "touch7_config.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include "touch7_peers_logic.h"
#include "touch7_weather.h"
#include <ArduinoJson.h>
#include "psram_helpers.h"
#include <string.h>

Touch7ConfigManager touch7ConfigManager;

static void copyStr(char* dst, size_t n, const char* src) {
    if (!dst || n == 0) return;
    if (!src) src = "";
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

bool Touch7ConfigManager::begin(const String& filePath) {
    initFilesystem(filePath);
    // Clamp loaded values (never trust flash).
    _config.theme = touch7ClampInt(_config.theme, 0, 5); // 0..5 (see TOUCH7_THEMES)
    _config.clock_style = touch7ClampInt(_config.clock_style, 0, 4);
    _config.volume = touch7ClampInt(_config.volume, 0, 10);
    _config.backlight_pct = touch7ClampInt(_config.backlight_pct, 5, 100);
    _config.read_interval_sec = touch7ClampInt(_config.read_interval_sec, 5, 60);
    if (_config.wx_lat < -90.0f || _config.wx_lat > 90.0f) _config.wx_lat = 38.72f;
    if (_config.wx_lon < -180.0f || _config.wx_lon > 180.0f) _config.wx_lon = -9.14f;
    if (_config.wx_place[0] == 0) copyStr(_config.wx_place, sizeof(_config.wx_place), "Lisboa");
    _initialized = true;
    Serial.println(F("[TOUCH7_CFG] Ready /config/touch7.json"));
    return true;
}

int Touch7ConfigManager::enabledPeerCount() const {
    int n = 0;
    for (int i = 0; i < TOUCH7_PEERS_MAX; i++) {
        if (_config.peers[i].enabled && _config.peers[i].host[0]) n++;
    }
    return n;
}

String Touch7ConfigManager::toJson() const {
    SpiRamJsonDocument doc(2048);
    doc["sensor_enabled"] = _config.sensor_enabled;
    doc["read_interval_sec"] = _config.read_interval_sec;
    doc["theme"] = _config.theme;
    doc["clock_style"] = _config.clock_style;
    doc["volume"] = _config.volume;
    doc["t_max"] = _config.t_max;
    doc["t_min"] = _config.t_min;
    doc["h_max"] = _config.h_max;
    doc["pir_dim_sec"] = _config.pir_dim_sec;
    doc["screensaver_sec"] = _config.screensaver_sec;
    doc["backlight_auto"] = _config.backlight_auto;
    doc["backlight_pct"] = _config.backlight_pct;
    doc["owm_key_set"] = (_config.owm_key[0] != 0);
    doc["wx_lat"] = _config.wx_lat;
    doc["wx_lon"] = _config.wx_lon;
    doc["wx_place"] = _config.wx_place;
    JsonArray peers = doc.createNestedArray("peers");
    for (int i = 0; i < TOUCH7_PEERS_MAX; i++) {
        JsonObject p = peers.createNestedObject();
        p["name"] = _config.peers[i].name;
        p["host"] = _config.peers[i].host;
        p["user"] = _config.peers[i].user;
        p["enabled"] = _config.peers[i].enabled;
        p["pass_set"] = (_config.peers[i].pass[0] != 0);
    }
    String out;
    serializeJson(doc, out);
    return out;
}

bool Touch7ConfigManager::updateFromJson(const String& json) {
    if (!applyJson(json)) return false;
    if (!saveToFile()) return false;
    _deferredDirty = false;
    _deferredSinceMs = 0;
    _deferredLastAttemptMs = 0;
    return true;
}

bool Touch7ConfigManager::updateFromJsonDeferred(const String& json) {
    if (!applyJson(json)) return false;
    _deferredDirty = true;
    _deferredSinceMs = millis();
    _deferredLastAttemptMs = 0;
    return true;
}

bool Touch7ConfigManager::deferredSaveDue(bool displayHidden) const {
    if (!_deferredDirty) return false;
    return touch7DeferredSaveDue(millis(), _deferredSinceMs, _deferredLastAttemptMs, displayHidden);
}

bool Touch7ConfigManager::flushDeferredSave() {
    if (!_deferredDirty) return true;
    _deferredLastAttemptMs = millis();
    if (!saveToFile()) return false;
    _deferredDirty = false;
    _deferredSinceMs = 0;
    _deferredLastAttemptMs = 0;
    return true;
}

bool Touch7ConfigManager::applyJson(const String& json) {
    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;
    const float oldLat = _config.wx_lat;
    const float oldLon = _config.wx_lon;
    char oldPlace[sizeof(_config.wx_place)];
    copyStr(oldPlace, sizeof(oldPlace), _config.wx_place);
    if (doc.containsKey("sensor_enabled")) _config.sensor_enabled = doc["sensor_enabled"];
    if (doc.containsKey("read_interval_sec"))
        _config.read_interval_sec = touch7ClampInt((int)doc["read_interval_sec"], 5, 60);
    if (doc.containsKey("theme") && touch7ThemeValid((int)doc["theme"]))
        _config.theme = (int)doc["theme"];
    if (doc.containsKey("clock_style") && touch7ClockValid((int)doc["clock_style"]))
        _config.clock_style = (int)doc["clock_style"];
    if (doc.containsKey("volume"))
        _config.volume = touch7ClampInt((int)doc["volume"], 0, 10);
    if (doc.containsKey("t_max")) _config.t_max = (float)doc["t_max"];
    if (doc.containsKey("t_min")) _config.t_min = (float)doc["t_min"];
    if (doc.containsKey("h_max")) _config.h_max = (float)doc["h_max"];
    if (doc.containsKey("pir_dim_sec"))
        _config.pir_dim_sec = touch7ClampInt((int)doc["pir_dim_sec"], 0, 3600);
    if (doc.containsKey("screensaver_sec"))
        _config.screensaver_sec = touch7ClampInt((int)doc["screensaver_sec"], 0, 3600);
    if (doc.containsKey("backlight_auto")) _config.backlight_auto = doc["backlight_auto"];
    if (doc.containsKey("backlight_pct"))
        _config.backlight_pct = touch7ClampInt((int)doc["backlight_pct"], 5, 100);
    if (doc.containsKey("owm_key")) copyStr(_config.owm_key, sizeof(_config.owm_key), doc["owm_key"] | "");
    if (doc.containsKey("wx_lat")) {
        float la = (float)doc["wx_lat"];
        if (la < -90.0f) la = -90.0f;
        if (la > 90.0f) la = 90.0f;
        _config.wx_lat = la;
    }
    if (doc.containsKey("wx_lon")) {
        float lo = (float)doc["wx_lon"];
        if (lo < -180.0f) lo = -180.0f;
        if (lo > 180.0f) lo = 180.0f;
        _config.wx_lon = lo;
    }
    if (doc.containsKey("wx_place")) copyStr(_config.wx_place, sizeof(_config.wx_place), doc["wx_place"] | "");
    if (doc.containsKey("peers") && doc["peers"].is<JsonArray>()) {
        int i = 0;
        for (JsonObject p : doc["peers"].as<JsonArray>()) {
            if (i >= TOUCH7_PEERS_MAX) break;
            if (p.containsKey("name")) copyStr(_config.peers[i].name, sizeof(_config.peers[i].name), p["name"] | "");
            if (p.containsKey("host")) copyStr(_config.peers[i].host, sizeof(_config.peers[i].host), p["host"] | "");
            if (p.containsKey("user")) copyStr(_config.peers[i].user, sizeof(_config.peers[i].user), p["user"] | "");
            if (p.containsKey("pass")) copyStr(_config.peers[i].pass, sizeof(_config.peers[i].pass), p["pass"] | "");
            if (p.containsKey("enabled")) _config.peers[i].enabled = p["enabled"];
            i++;
        }
    }
    if (_config.wx_lat != oldLat || _config.wx_lon != oldLon ||
        strcmp(_config.wx_place, oldPlace) != 0) {
        touch7WeatherInvalidate();
    }
    return true;
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
