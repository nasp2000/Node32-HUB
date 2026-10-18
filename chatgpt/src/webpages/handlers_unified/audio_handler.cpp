#include "audio_handler.h"

#include "../../psram_helpers.h"
#include <Arduino.h>
#include <ArduinoJson.h>

#include "../../psram_helpers.h"
#include "feature_flags.h"

#include "../../psram_helpers.h"
#if ENABLE_UDA1334A==1

#include <vector>

#include "../../psram_helpers.h"
#include "../../audio_dac_uda1334a.h"
#include "../../audio_dac_uda1334a_config.h"
#include "../../audio_i2s_poc.h"
#include "../../audio_mp3_decoder.h"
#include "../../commands.h"
#include "../../radio_audio_runtime.h"
#include "../../radio_schedule.h"
#include "../../upnp_client.h"
#include "feature_flags.h"

#include "../../psram_helpers.h"
extern UDA1334AConfigManager uda1334aConfig;
extern UDA1334ADAC udaDac;

// ---- Helper: is the radio pack active? ------------------------------------
static bool _isRadioPackActive() {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
    return true;
#else
    return false;
#endif
}

// ---- Helper: apply UDA1334A runtime config to hardware --------------------
static bool _applyUda1334aRuntimeConfig(String& messageOut) {
    const UDA1334AConfig& cfg = uda1334aConfig.getConfig();
    AudioFormat fmt(cfg.sampleRate, cfg.bitDepth, 2);

    if (!udaDac.isInitialized()) {
        if (!udaDac.begin(fmt)) {
            messageOut = F("Failed to initialize DAC");
            return false;
        }
        messageOut = F("Config saved");
        return true;
    }

    if (!udaDac.setAudioFormat(fmt)) {
        messageOut = F("Failed to apply DAC format");
        return false;
    }

    messageOut = F("Config saved");
    return true;
}

// ---- Helper: get form/query value from HttpRequest -------------------------
static String _getParam(HttpRequest& req, const char* name) {
    int start = 0;
    while (start < (int)req.query.length()) {
        int amp = req.query.indexOf('&', start);
        String seg = (amp < 0) ? req.query.substring(start) : req.query.substring(start, amp);
        int eq = seg.indexOf('=');
        if (eq > 0) {
            String k = seg.substring(0, eq);
            if (k == name) return seg.substring(eq + 1);
        }
        if (amp < 0) break;
        start = amp + 1;
    }
    if (req.contentType.indexOf("application/x-www-form-urlencoded") >= 0) {
        start = 0;
        while (start < (int)req.body.length()) {
            int amp = req.body.indexOf('&', start);
            String seg = (amp < 0) ? req.body.substring(start) : req.body.substring(start, amp);
            int eq = seg.indexOf('=');
            if (eq > 0) {
                String k = seg.substring(0, eq);
                if (k == name) return seg.substring(eq + 1);
            }
            if (amp < 0) break;
            start = amp + 1;
        }
    }
    return String();
}

static int _hexVal(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// Percent-decode a query value (query params arrive raw from the route layer).
static String _urlDecode(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '%' && i + 2 < in.length()) {
            int hi = _hexVal(in[i + 1]);
            int lo = _hexVal(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += c;
    }
    return out;
}

// ========== UDA1334A ==========================================================

void handleUda1334aConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(uda1334aConfig.toJson());
}

void handleUda1334aConfigPost(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    if (!uda1334aConfig.updateFromJson(doc.as<JsonObject>())) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save config\"}");
        return;
    }
    String message;
    if (!_applyUda1334aRuntimeConfig(message)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + message + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + message + '"' + '}';
    res.sendJson(json);
}

static void udaTestSoundTask(void*) {
    udaDac.playTestSound();
    vTaskDelete(nullptr);
}

void handleUda1334aTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    // The task is self-contained: playTestSound() initializes the DAC on demand,
    // falls back to an existing I2S driver (radio pack), and releases the port
    // when done so the radio output can take it over later.
    xTaskCreate(udaTestSoundTask, "uda_test", 4096, nullptr, 1, nullptr);
    res.sendJson("{\"success\":true,\"message\":\"Test sound playing\"}");
}

static void udaTimingTask(void* param) {
    uint32_t seconds = 60;
    if (param) { seconds = *(uint32_t*)param; delete (uint32_t*)param; }
    // Stop any active radio playback first: the timing test must be the only
    // writer to the I2S port, otherwise concurrent decode writes corrupt the
    // measurement. Deferred stop is executed in the loop task (cross-task
    // safety); wait until the radio reports inactive before starting.
    radioAudioStopDeferred();
    uint32_t waited = 0;
    while (radioAudioIsActive() && waited < 3000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }
    udaDac.playTimingTest(seconds);
    vTaskDelete(nullptr);
}

void handleUda1334aTimingPost(HttpRequest& req, HttpResponse& res) {
    uint32_t seconds = 60;
    StaticJsonDocument<64> doc;
    if (req.body.length() && deserializeJson(doc, req.body) == DeserializationError::Ok) {
        if (doc["seconds"].is<uint32_t>()) {
            uint32_t s = doc["seconds"].as<uint32_t>();
            if (s >= 10 && s <= 300) seconds = s;
        }
    }
    uint32_t* sec = new uint32_t(seconds);
    xTaskCreate(udaTimingTask, "uda_timing", 4096, sec, 1, nullptr);
    String json = String(F("{\"success\":true,\"message\":\"Timing test ("));
    json += String(seconds) + F("s) started\"}");
    res.sendJson(json);
}

// ========== ESP_I2S Phase 1 PoC =============================================

static void i2sPocTask(void*) {
    audioI2sPocRun();
    vTaskDelete(nullptr);
}

void handleI2sPocPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    // The PoC takes the I2S port (BCLK/LRCK/DOUT) while it runs. If the radio
    // is actively playing it owns the same pins via the legacy driver — refuse
    // instead of corrupting both streams.
    if (radioAudioIsActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Stop the radio first (radio is playing)\"}");
        return;
    }
    // If the radio ever played, the legacy I2S driver still holds the port even
    // when idle (udaDac is initialized). Release it so the ESP_I2S PoC can claim
    // the pins. Safe while idle: the radio re-initializes on the next play.
    if (udaDac.isInitialized()) {
        udaDac.end();
        sendVirtualMessage(F("[I2S-POC] Released legacy I2S driver (radio had played)"), INFO, false, true);
    }
    // Runs in its own task. The PoC takes the I2S port while it runs and
    // releases it on return, so the radio runtime can claim it afterwards.
    xTaskCreate(i2sPocTask, "i2s_poc", 4096, nullptr, 1, nullptr);
    res.sendJson("{\"success\":true,\"message\":\"ESP_I2S PoC started\"}");
}

// ========== MP3 decoder (Phase 2) ============================================

static void mp3DecodeTask(void* param) {
    String* path = static_cast<String*>(param);
    if (path) {
        audioMp3PlaySd(path->c_str());
        delete path;
    }
    vTaskDelete(nullptr);
}

void handleMp3TestPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String path = _getParam(req, "path");
    if (!path.length() && req.contentType.indexOf("application/json") >= 0) {
        SpiRamJsonDocument doc(256);
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            path = doc["path"].as<String>();
        }
    }
    if (!path.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing path\"}");
        return;
    }
    if (radioAudioIsActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Stop the radio first (radio is playing)\"}");
        return;
    }
    if (udaDac.isInitialized()) {
        udaDac.end();
        sendVirtualMessage(F("[MP3] Released legacy I2S driver (radio had played)"), INFO, false, true);
    }
    String* param = new String(path);
    if (!param) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
        return;
    }
    xTaskCreate(mp3DecodeTask, "mp3_dec", 8192, param, 1, nullptr);
    res.sendJson("{\"success\":true,\"message\":\"MP3 decode started\"}");
}

// ========== Radio =============================================================

void handleRadioStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(radioAudioStatusJson());
}

void handleRadioQueueGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(radioAudioQueueJson());
}

void handleRadioFavoritesGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(radioAudioFavoritesJson());
}

void handleRadioStopPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    radioAudioStopDeferred();
    res.sendJson("{\"success\":true,\"message\":\"Stopped\"}");
}

void handleRadioTogglePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    radioAudioTogglePlayDeferred();
    res.sendJson("{\"success\":true,\"message\":\"Toggle\"}");
}

void handleRadioNextPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    radioAudioPlayNextDeferred();
    res.sendJson("{\"success\":true,\"message\":\"Next\"}");
}

void handleRadioRandomPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    radioAudioPlayRandomDeferred();
    res.sendJson("{\"success\":true,\"message\":\"Random\"}");
}

void handleRadioVolumePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String v = _getParam(req, "value");
    if (!v.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            v = doc["value"].as<String>();
        }
    }
    int value = v.toInt();
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    radioAudioSetVolume((uint8_t)value);
    res.sendJson("{\"success\":true,\"message\":\"Volume updated\"}");
}

void handleRadioSeekPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String v = _getParam(req, "position");
    if (!v.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok)
            v = doc["position"].as<String>();
    }
    float pos = v.toFloat();
    if (pos < 0.0f || pos > 1.0f) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Position must be 0.0-1.0\"}");
        return;
    }
    if (radioAudioSeekPosition(pos))
        res.sendJson("{\"success\":true,\"message\":\"Seek OK\"}");
    else
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Cannot seek (stream or no playback)\"}");
}

void handleRadioPlayUrlPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String url = _getParam(req, "url");
    String favicon = _getParam(req, "favicon");
    String name = _getParam(req, "name");
    String mime = _getParam(req, "mime");
    if (!url.length() && req.contentType.indexOf("application/json") >= 0) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            url = doc["url"].as<String>();
            favicon = doc["favicon"].as<String>();
            name = doc["name"].as<String>();
            mime = doc["mime"].as<String>();
        }
    }
    if (!url.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing url\"}");
        return;
    }
    radioAudioSetStationFavicon(favicon.length() ? favicon.c_str() : "");
    radioAudioSetStationName(name.length() ? name.c_str() : "");
    if (mime.length()) radioAudioPlayUrlMimeDeferred(url, mime);
    else               radioAudioPlayUrlDeferred(url);
    res.sendJson("{\"success\":true,\"message\":\"Playing\"}");
}

void handleRadioPlaySdPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String path = _getParam(req, "path");
    if (!path.length() && req.contentType.indexOf("application/json") >= 0) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            path = doc["path"].as<String>();
        }
    }
    if (!path.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing path\"}");
        return;
    }
    radioAudioSetStationName("");
    radioAudioPlaySdDeferred(path);
    res.sendJson("{\"success\":true,\"message\":\"Playing SD\"}");
}

void handleRadioQueuePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    SpiRamJsonDocument doc(12288);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    if (!doc.containsKey("items")) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing items array\"}");
        return;
    }
    std::vector<String> items;
    JsonArray arr = doc["items"].as<JsonArray>();
    for (JsonVariant v : arr) {
        String p = v.as<String>();
        if (p.length()) items.push_back(p);
        if (items.size() >= 256) break;
    }
    int idx = doc["index"] | 0;
    String msg;
    if (!radioAudioSetQueue(items, idx, msg)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioFavoritesAddPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String name = _getParam(req, "name");
    String url = _getParam(req, "url");
    String favicon = _getParam(req, "favicon");
    if (!url.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            url  = doc["url"].as<String>();
            name = doc["name"].as<String>();
            favicon = doc["favicon"].as<String>();
        }
    }
    if (!url.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing url\"}");
        return;
    }
    String msg;
    if (!radioAudioAddFavorite(name, url, msg, favicon)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioFavoritesRemovePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String is = _getParam(req, "index");
    if (!is.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            is = doc["index"].as<String>();
        }
    }
    int index = is.toInt();
    String msg;
    if (!radioAudioRemoveFavorite(index, msg)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioFavoritesPlayPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String is = _getParam(req, "index");
    if (!is.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            is = doc["index"].as<String>();
        }
    }
    int index = is.toInt();
    if (index < 0) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid index\"}");
        return;
    }
    radioAudioPlayFavoriteDeferred(index);
    res.sendJson("{\"success\":true,\"message\":\"Playing favorite\"}");
}

// ========== New features ======================================================

void handleRadioArtworkGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    uint32_t size = 0;
    const uint8_t* data = radioAudioGetArtworkData(size);
    if (!data || size == 0) {
        res.send(404, "application/json", "{\"success\":false,\"message\":\"No artwork\"}");
        return;
    }
    const char* mime = radioAudioGetArtworkMime();
    if (strlen(mime) == 0) mime = "image/jpeg";
    res.code = 200;
    res.contentType = mime;
    res.isBinary = true;
    res.binaryData = data;
    res.binaryLen = size;
}

void handleRadioSearchGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String q = _getParam(req, "q");
    if (!q.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing query\"}");
        return;
    }
    res.sendJson(radioAudioSearch(q.c_str()));
}

void handleRadioStationsSearchGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String q = _getParam(req, "q");
    String genre = _getParam(req, "genre");
    String country = _getParam(req, "countrycode");
    int offset = _getParam(req, "offset").toInt();
    int limit = _getParam(req, "limit").toInt();
    if (limit <= 0) limit = 50;
    if (limit > 50) limit = 50;

    String json, err;
    bool hasMore = false;
    if (!radioAudioSearchStations(q, genre, country, offset, limit, json, err, &hasMore)) {
        sendVirtualMessage(err.length() ? err : String(F("Radio search failed")), WARN);
        res.sendJson(String(F("{\"success\":false,\"results\":[]}")));
        return;
    }
    res.sendJson(String(F("{\"success\":true,\"has_more\":")) + (hasMore ? F("true") : F("false")) +
                 String(F(",\"results\":")) + json + "}");
}

void handleRadioStationsTagsGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    (void)req;
    String json, err;
    if (!radioAudioGetTags(json, err)) {
        sendVirtualMessage(err.length() ? err : String(F("Radio tags failed")), WARN);
        res.sendJson("{\"success\":false,\"tags\":[]}");
        return;
    }
    res.sendJson(String(F("{\"success\":true,\"tags\":")) + json + "}");
}

void handleRadioStationsCountriesGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    (void)req;
    String json, err;
    if (!radioAudioGetCountries(json, err)) {
        sendVirtualMessage(err.length() ? err : String(F("Radio countries failed")), WARN);
        res.sendJson("{\"success\":false,\"countries\":[]}");
        return;
    }
    res.sendJson(String(F("{\"success\":true,\"countries\":")) + json + "}");
}

void handleRadioStationsDefaultGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    (void)req;
    res.sendJson(radioAudioDefaultStationsJson());
}

void handleRadioLibraryGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String path = _getParam(req, "path");
    String queue = _getParam(req, "queue");
    if (queue == "1") {
        res.sendJson(radioAudioListFolderMusic(path));
        return;
    }
    res.sendJson(radioAudioListLibrary(path));
}

// GET /api/radio/library/art?path=... — serves a folder cover image
// (cover.jpg/folder.jpg) from the SD /library tree.
void handleRadioLibraryArtGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String path = _urlDecode(_getParam(req, "path"));
    if (path.length() == 0) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing path\"}");
        return;
    }
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    char mime[16] = "";
    if (!radioAudioLoadLibraryArt(path, data, size, mime, sizeof(mime))) {
        res.send(404, "application/json", "{\"success\":false,\"message\":\"No artwork\"}");
        return;
    }
    res.code = 200;
    res.contentType = mime;
    res.isBinary = true;
    res.binaryData = data;
    res.binaryLen = size;
    res.freeFn = [](const uint8_t* p) { if (p) heap_caps_free((void*)p); };
}

void handleRadioPlaylistsGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(radioAudioListPlaylists());
}

void handleRadioPlaylistSavePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String name = _getParam(req, "name");
    if (!name.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            name = doc["name"].as<String>();
        }
    }
    if (!name.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing name\"}");
        return;
    }
    String msg;
    if (!radioAudioSavePlaylist(name.c_str(), msg)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioPlaylistLoadPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String name = _getParam(req, "name");
    if (!name.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            name = doc["name"].as<String>();
        }
    }
    if (!name.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing name\"}");
        return;
    }
    String msg;
    if (!radioAudioLoadPlaylist(name.c_str(), msg)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioPlaylistDeletePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String name = _getParam(req, "name");
    if (!name.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            name = doc["name"].as<String>();
        }
    }
    if (!name.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing name\"}");
        return;
    }
    String msg;
    if (!radioAudioDeletePlaylist(name.c_str(), msg)) {
        String json = String(F("{\"success\":false,\"message\":")) + '"' + msg + '"' + '}';
        res.send(500, "application/json", json);
        return;
    }
    String json = String(F("{\"success\":true,\"message\":")) + '"' + msg + '"' + '}';
    res.sendJson(json);
}

void handleRadioShufflePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String vs = _getParam(req, "value");
    if (!vs.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            vs = doc["value"].as<String>();
        }
    }
    bool on = (vs == "1" || vs == "true");
    radioAudioSetShuffle(on);
    res.sendJson("{\"success\":true,\"message\":\"Shuffle updated\"}");
}

void handleRadioRepeatPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String ms = _getParam(req, "mode");
    if (!ms.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            ms = doc["mode"].as<String>();
        }
    }
    uint8_t mode = (uint8_t)ms.toInt();
    if (mode > 2) mode = 0;
    radioAudioSetRepeatMode(mode);
    res.sendJson("{\"success\":true,\"message\":\"Repeat mode updated\"}");
}

void handleRadioResumeGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    bool a = true, f = false;
    radioAudioGetResume(a, f);
    String json = F("{\"success\":true,\"autoResume\":");
    json += (a ? "true" : "false");
    json += F(",\"resumeFirst\":");
    json += (f ? "true" : "false");
    json += "}";
    res.sendJson(json);
}

void handleRadioResumePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    bool a = true, f = false;
    if (req.contentType.indexOf("application/json") >= 0 && req.body.length()) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            if (doc["autoResume"].is<bool>()) a = doc["autoResume"].as<bool>();
            if (doc["resumeFirst"].is<bool>()) f = doc["resumeFirst"].as<bool>();
        }
    } else {
        String vs = _getParam(req, "autoResume");
        if (vs.length()) a = (vs == "1" || vs == "true");
        String vs2 = _getParam(req, "resumeFirst");
        if (vs2.length()) f = (vs2 == "1" || vs2 == "true");
    }
    radioAudioSetResume(a, f);
    res.sendJson("{\"success\":true,\"message\":\"Auto-resume updated\"}");
}

void handleRadioSleepPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String ms = _getParam(req, "minutes");
    if (!ms.length() && req.contentType.indexOf("application/json") >= 0) {
        StaticJsonDocument<64> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            ms = doc["minutes"].as<String>();
        }
    }
    int minutes = ms.toInt();
    if (minutes < 0) minutes = 0;
    if (minutes > 480) minutes = 480;
    radioAudioSetSleep(minutes);
    if (minutes <= 0) {
        res.sendJson("{\"success\":true,\"message\":\"Sleep timer off\"}");
    } else {
        res.sendJson(String(F("{\"success\":true,\"message\":\"Sleep timer set to ")) + String(minutes) + " min\"}");
    }
}

void handleRadioEqGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    SpiRamJsonDocument doc(256);
    doc["success"] = true;
    doc["bands"] = radioAudioGetEqBands();
    JsonArray arr = doc.createNestedArray("gains");
    for (uint8_t i = 0; i < radioAudioGetEqBands(); i++) {
        arr.add(radioAudioGetEqBandGain(i));
    }
    String out;
    serializeJson(doc, out);
    res.sendJson(out);
}

void handleRadioEqPost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    SpiRamJsonDocument doc(256);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    if (!doc.containsKey("gains")) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing gains array\"}");
        return;
    }
    JsonArray arr = doc["gains"].as<JsonArray>();
    uint8_t bands = radioAudioGetEqBands();
    uint8_t idx = 0;
    for (JsonVariant v : arr) {
        if (idx >= bands) break;
        radioAudioSetEqBandGain(idx, (int8_t)v.as<int>());
        idx++;
    }
    res.sendJson("{\"success\":true,\"message\":\"EQ updated\"}");
}

void handleRadioScheduleGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(radioScheduleJson());
}

void handleRadioSchedulePost(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String message;
    if (!radioScheduleSet(req.body, message)) {
        String json = String(F("{\"success\":false,\"message\":\"")) + message + "\"}";
        res.send(400, "application/json", json);
        return;
    }
    res.sendJson(String(F("{\"success\":true,\"message\":\"")) + message + "\"}");
}

// ========== UPnP / DLNA client ================================================

void handleUpnpServersGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    res.sendJson(upnpServersJson());
}

void handleUpnpBrowseGet(HttpRequest& req, HttpResponse& res) {
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    String uuid   = _getParam(req, "server");
    String object = _getParam(req, "object");
    int    start  = _getParam(req, "start").toInt();
    int    count  = _getParam(req, "count").toInt();
    bool   metadata = (_getParam(req, "flag") == "metadata");
    if (!uuid.length()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"Missing server uuid\"}");
        return;
    }
    res.sendJson(upnpBrowseJson(uuid, object, start, count, metadata));
}

void handleUpnpDiscoverPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!_isRadioPackActive()) {
        res.send(400, "application/json", "{\"success\":false,\"message\":\"PACK_RADIO only\"}");
        return;
    }
    upnpClientDiscover();
    res.sendJson("{\"success\":true,\"message\":\"Discovery started\"}");
}

#endif // ENABLE_UDA1334A==1
