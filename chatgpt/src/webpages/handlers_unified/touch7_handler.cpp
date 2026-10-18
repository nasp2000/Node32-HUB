#include "../../languages/lang_active.h"
#include "touch7_handler.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_rom_crc.h>
#include "../../sensors/sensor_bh1750_config.h"
#include "../../touch7_config.h"
#include "../../touch7_display.h"
#include "../../touch7_peers_logic.h"
#include "../../touch7_peers.h"
#include "../../touch7_audio.h"
#include "../../touch7_weather.h"
#include "../../touch7_geo.h"
// Touch7FlashGuard reused from touch7_display.h (shared, refcounted).

// ===== BH1750 ========================================================================
#if ENABLE_BH1750==1
void handleBh1750ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(bh1750ConfigManager.toJson());
}

void handleBh1750ConfigPost(HttpRequest& req, HttpResponse& res) {
    Touch7FlashGuard hold_;
    (void)hold_;
    bool ok = bh1750ConfigManager.updateFromJson(req.body);
    if (ok) {
        res.sendJson("{\"success\":true}");
    } else {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\""+L(activeLang->apiErrSaveBh1750)+"\"}");
    }
}

void handleBh1750MeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bh1750ConfigManager.performReading();
    res.sendJson(bh1750ConfigManager.toJson());
}
#endif // ENABLE_BH1750==1

// ===== Touch7 config / peers ===========================================================
void handleTouch7ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(touch7ConfigManager.toJson());
}

void handleTouch7ConfigPost(HttpRequest& req, HttpResponse& res) {
    Touch7PanelHold hold_;
    (void)hold_;
    bool ok = touch7ConfigManager.updateFromJson(req.body);
    if (ok) {
        const Touch7Config& cfg = touch7ConfigManager.getConfig();
        touch7Display.applyTheme((uint8_t)cfg.theme);
        touch7Display.applyClockStyle((uint8_t)cfg.clock_style);
        res.sendJson("{\"success\":true}");
    } else {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"JSON invalido\"}");
    }
}

void handleTouch7PeerRefreshPost(HttpRequest& req, HttpResponse& res) {
    // ?idx=N refreshes one peer; no idx refreshes all enabled peers.
    int idx = -1;
    int p = req.query.indexOf("idx=");
    if (p >= 0) idx = req.query.substring(p + 4).toInt();
    if (idx >= 0 && idx < TOUCH7_PEERS_MAX) {
        touch7PeerRefreshNow((uint8_t)idx);
    } else {
        for (uint8_t i = 0; i < TOUCH7_PEERS_MAX; i++) touch7PeerRefreshNow(i);
    }
    res.sendJson("{\"success\":true}");
}

// ===== Geo: place search + IP auto-locate (keyless providers) ==============
// GET /api/touch7-geo?name=X -> Open-Meteo geocoding candidates (trimmed).
// GET /api/touch7-autoloc   -> ip-api.com city suggestion. Saving is done via
// the existing POST /api/touch7-config {wx_lat,wx_lon,wx_place}.
static void touch7GeoJsonEscape(const char* in, char* out, size_t n) {
    size_t o = 0;
    if (!in || !out || n == 0) return;
    for (size_t i = 0; in[i] != 0; i++) {
        if (o + 2 >= n) break;
        if (in[i] == '"' || in[i] == '\\') out[o++] = '\\';
        out[o++] = in[i] < 32 ? ' ' : in[i];
    }
    out[o] = 0;
}

void handleTouch7GeoGet(HttpRequest& req, HttpResponse& res) {
    String q = req.query;
    String name;
    int p = q.indexOf("name=");
    if (p >= 0) {
        name = q.substring((size_t)p + 5);
        int a = name.indexOf('&');
        if (a >= 0) name = name.substring(0, (size_t)a);
    }
    name.trim();
    if (name.length() == 0 || name.length() > 80) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"nome vazio\"}");
        return;
    }
    String payload;
    if (!touch7GeoSearchFetch(name, payload)) {
        res.send(502, "application/json", "{\"success\":false,\"msg\":\"geocoding falhou\"}");
        return;
    }
    Touch7GeoCand c[TOUCH7_GEO_MAX];
    int n = touch7GeoParseSearch(payload.c_str(), c, TOUCH7_GEO_MAX);
    String out = "{\"success\":true,\"results\":[";
    for (int i = 0; i < n; i++) {
        char nm[64], co[48], ad[48];
        touch7GeoJsonEscape(c[i].name, nm, sizeof(nm));
        touch7GeoJsonEscape(c[i].country, co, sizeof(co));
        touch7GeoJsonEscape(c[i].admin1, ad, sizeof(ad));
        char item[256];
        snprintf(item, sizeof(item),
                 "%s{\"name\":\"%s\",\"country\":\"%s\",\"admin1\":\"%s\",\"lat\":%.4f,\"lon\":%.4f}",
                 i ? "," : "", nm, co, ad, c[i].lat, c[i].lon);
        out += item;
        if (out.length() > 3000) break;
    }
    out += "]}";
    res.sendJson(out);
}

void handleTouch7AutoLocGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String payload;
    if (!touch7GeoAutoFetch(payload)) {
        res.send(502, "application/json", "{\"success\":false,\"msg\":\"autoloc falhou\"}");
        return;
    }
    Touch7GeoAuto a;
    if (!touch7GeoParseAuto(payload.c_str(), &a)) {
        res.send(502, "application/json", "{\"success\":false,\"msg\":\"sem localizacao\"}");
        return;
    }
    char city[64], co[48];
    touch7GeoJsonEscape(a.city, city, sizeof(city));
    touch7GeoJsonEscape(a.country, co, sizeof(co));
    char out[256];
    snprintf(out, sizeof(out),
             "{\"success\":true,\"city\":\"%s\",\"country\":\"%s\",\"lat\":%.4f,\"lon\":%.4f}",
             city, co, a.lat, a.lon);
    res.sendJson(String(out));
}

void handleTouch7ForecastGet(HttpRequest& req, HttpResponse& res) {    (void)req;
    Touch7WeatherStatus status;
    touch7WeatherGetStatus(status);
    String days = "[";
    if (status.hasData) {
        String d[5];
        touch7WeatherDays(d);
        for (int i = 0; i < 5; i++) {
            if (i) days += ",";
            days += "\"" + d[i] + "\"";
        }
    }
    days += "]";
    String out = "{\"has\":";
    out += status.hasData ? "true" : "false";
    out += ",\"code\":";
    out += String(touch7WeatherCode());
    out += ",\"isDay\":";
    out += touch7WeatherIsDay() ? "true" : "false";
    out += ",\"today\":\"";
    out += touch7WeatherToday();
    out += "\",\"days\":";
    out += days;
    out += ",\"refreshing\":";
    out += status.refreshing ? "true" : "false";
    out += ",\"stale\":";
    out += status.stale ? "true" : "false";
    out += ",\"age_sec\":";
    out += String(status.ageSeconds);
    out += ",\"last_error\":\"";
    out += status.lastError;
    out += "\"";
    out += "}";
    res.sendJson(out);
}

void handleTouch7WeatherRefreshPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    touch7WeatherInvalidate();
    res.sendJson("{\"success\":true,\"refreshing\":true}");
}

void handleTouch7TapPost(HttpRequest& req, HttpResponse& res) {
    // Legacy tap injects one LVGL press. /ecra supplies state=down|move|up to
    // keep the virtual pointer pressed while dragging controls such as sliders.
    int x = -1, y = -1;
    int px = req.query.indexOf("x=");
    int py = req.query.indexOf("y=");
    if (px >= 0) x = req.query.substring(px + 2).toInt();
    if (py >= 0) y = req.query.substring(py + 2).toInt();
    int cx = 0, cy = 0;
    if (!touch7ClampTap(x, y, &cx, &cy)) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"x 0..799, y 0..479\"}");
        return;
    }
    int ps = req.query.indexOf("state=");
    if (ps >= 0) {
        String state = req.query.substring((size_t)ps + 6);
        int amp = state.indexOf('&');
        if (amp >= 0) state = state.substring(0, (size_t)amp);
        state.toLowerCase();
        if (state != "down" && state != "move" && state != "up") {
            res.send(400, "application/json", "{\"success\":false,\"msg\":\"state down|move|up\"}");
            return;
        }
        touch7Display.remotePointer((uint16_t)cx, (uint16_t)cy, state != "up");
    } else {
        touch7Display.remoteTap((uint16_t)cx, (uint16_t)cy);
    }
    res.sendJson("{\"success\":true}");
}

// ===== Live LCD frame mirror (/ecra) =================================================
// Serves the last presented RGB565 framebuffer (800x480x2 raw bytes). The
// /ecra page supplies the prior hash; unchanged frames return 304 without
// transferring the 768 KB image.
void handleTouch7FrameGet(HttpRequest& req, HttpResponse& res) {
    String requestedHash;
    int hashPos = req.query.indexOf("hash=");
    if (hashPos >= 0) {
        requestedHash = req.query.substring((size_t)hashPos + 5);
        int amp = requestedHash.indexOf('&');
        if (amp >= 0) requestedHash = requestedHash.substring(0, (size_t)amp);
    }
    const size_t len = touch7Display.frameBytes();
    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        res.send(503, "application/json", String("{\"success\":false,\"msg\":\"") + L(activeLang->apiTouch7NoMemory) + "\"}");
        return;
    }
    if (!touch7Display.captureFrame(buf, len)) {
        heap_caps_free(buf);
        res.send(503, "application/json", "{\"success\":false,\"msg\":\"ecra indisponivel\"}");
        return;
    }
    const uint32_t hashA = esp_rom_crc32_le(0, buf, (uint32_t)len);
    const uint32_t hashB = esp_rom_crc32_le(0xA5A5A5A5UL, buf, (uint32_t)len);
    char hashText[17];
    snprintf(hashText, sizeof(hashText), "%08lx%08lx",
             (unsigned long)hashA, (unsigned long)hashB);
    if (requestedHash == hashText) {
        heap_caps_free(buf);
        res.sendNotModified();
        return;
    }
    res.code = 200;
    res.contentType = F("application/octet-stream");
    res.isBinary = true;
    res.binaryData = buf;
    res.binaryLen = len;
    res.frameHash = hashText;
    res.freeFn = [](const uint8_t* p) { heap_caps_free((void*)p); };
}

// ===== Runtime PCLK tuning (debug) ===================================================
// GET /api/touch7-pclk?hz=14000000 — set the RGB pixel clock live so the panel
// can be tuned (lock vs PSRAM underrun) without a 20-minute rebuild.
void handleTouch7PclkGet(HttpRequest& req, HttpResponse& res) {
    int p = req.query.indexOf("hz=");
    if (p < 0) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"use ?hz=12000000..18000000\"}");
        return;
    }
    uint32_t hz = (uint32_t)strtoul(req.query.substring(p + 3).c_str(), nullptr, 10);
    if (hz < 4000000UL || hz > 30000000UL) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"hz out of range\"}");
        return;
    }
    touch7Display.setPclkHz(hz);
    char b[64];
    snprintf(b, sizeof(b), "{\"success\":true,\"hz\":%lu}", (unsigned long)hz);
    res.sendJson(b);
}

// ===== Falas (local WAV phrases in LittleFS /falas) ======================================
static String falasListJson() {
    String out = "{\"files\":[";
    bool first = true;
    File root = LittleFS.open("/falas");
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            String name = String(f.name());
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                if (!first) out += ",";
                first = false;
                out += "{\"name\":\"" + name + "\",\"size\":" + String((unsigned long)f.size()) + "}";
            }
            f = root.openNextFile();
        }
    }
    out += "]}";
    return out;
}

static bool falasSafePath(const String& q, String& full) {
    int p = q.indexOf("file=");
    if (p < 0) return false;
    String name = q.substring(p + 5);
    int amp = name.indexOf('&');
    if (amp >= 0) name = name.substring(0, amp);
    name.trim();
    if (name.length() == 0 || name.length() > 64) return false;
    if (name.indexOf("..") >= 0 || name.indexOf('/') >= 0) return false;
    if (!name.endsWith(".wav") && !name.endsWith(".WAV")) return false;
    full = "/falas/" + name;
    return true;
}

void handleFalasListGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    Touch7FlashGuard hold_;
    (void)hold_;
    res.sendJson(falasListJson());
}

void handleFalasPlayPost(HttpRequest& req, HttpResponse& res) {
    Touch7FlashGuard hold_;
    (void)hold_;
    String full;
    if (!falasSafePath(req.query, full)) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"ficheiro invalido\"}");
        return;
    }
    if (!LittleFS.exists(full)) {
        res.send(404, "application/json", "{\"success\":false,\"msg\":\"ficheiro inexistente\"}");
        return;
    }
    if (!touch7AudioPlay(full)) {
        res.send(503, "application/json", "{\"success\":false,\"msg\":\"audio ocupado\"}");
        return;
    }
    res.sendJson("{\"success\":true}");
}

void handleFalasDeletePost(HttpRequest& req, HttpResponse& res) {
    Touch7PanelHold hold_;
    (void)hold_;
    String full;
    if (!falasSafePath(req.query, full)) {
        res.send(400, "application/json", "{\"success\":false,\"msg\":\"ficheiro invalido\"}");
        return;
    }
    touch7AudioStop();
    if (!LittleFS.remove(full)) {
        res.send(404, "application/json", "{\"success\":false,\"msg\":\""+L(activeLang->apiNotDeleted)+"\"}");
        return;
    }
    res.sendJson("{\"success\":true}");
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
