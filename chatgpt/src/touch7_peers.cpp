#include "touch7_peers.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <JPEGDecoder.h>
#include "psram_helpers.h"
#include "touch7_config.h"
#include "touch7_display.h"
#include "touch7_peers_logic.h"

// 256 KB shared PSRAM snapshot slot (largest QXGA/5MP JPEGs fit; bigger ones
// are skipped with a status note instead of growing RAM).
#define TOUCH7_SNAP_SLOT (256UL * 1024UL)

static uint8_t* s_snapBuf = 0;
static unsigned long s_lastPollMs = 0;
static uint8_t s_roundRobin = 0;
static int s_fails[TOUCH7_PEERS_MAX] = {0, 0, 0, 0};
static unsigned long s_nextAt[TOUCH7_PEERS_MAX] = {0, 0, 0, 0};

static String peerBase(const Touch7Peer& p) {
    String h = String(p.host);
    h.trim();
    if (h.startsWith("http://") || h.startsWith("https://")) return h;
    return String("http://") + h;
}

static void setStatus(uint8_t idx, const Touch7Peer& p, const String& st, bool live) {
    touch7Display.setPeerStatus(idx, String(p.name), st, live);
}

static bool fetchStatus(const Touch7Peer& p, String& model, int& fps, long& frames,
                        bool& recording, int& motion, String& note) {
    HTTPClient http;
    http.setTimeout(2500); // short: a dead peer must never stall the loop
    http.setReuse(false);
    if (p.user[0]) http.setAuthorization(p.user, p.pass);
    http.begin(peerBase(p) + "/api/camera/status");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        note = "status " + String(code);
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();
    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, payload)) {
        note = "json invalido";
        return false;
    }
    model = doc["model"] | "?";
    fps = (int)(doc["streamFps"] | 0);
    frames = (long)(doc["streamFrames"] | 0L);
    recording = (bool)(doc["recording"] | false);
    motion = (int)(doc["motionScore"] | 0);
    return true;
}

static bool fetchSnapshot(const Touch7Peer& p) {
    HTTPClient http;
    http.setTimeout(4000); // short: paced polling, backoff covers dead peers
    http.setReuse(false);
    if (p.user[0]) http.setAuthorization(p.user, p.pass);
    http.begin(peerBase(p) + "/cam/snapshot");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    int len = http.getSize();
    if (len <= 0 || (uint32_t)len > TOUCH7_SNAP_SLOT) {
        http.end();
        return false;
    }
    WiFiClient* s = http.getStreamPtr();
    uint32_t got = 0;
    unsigned long t0 = millis();
    while (http.connected() && got < (uint32_t)len && millis() - t0 < 4000) {
        size_t avail = s->available();
        if (!avail) {
            delay(2);
            continue;
        }
        size_t chunk = avail;
        if (got + chunk > (uint32_t)len) chunk = len - got;
        int r = s->readBytes(s_snapBuf + got, chunk);
        if (r <= 0) break;
        got += (uint32_t)r;
    }
    http.end();
    if (got < 1024) return false;
    // Decode headers into JpegDec state; MCU blit happens in blitTile().
    if (!JpegDec.decodeArray(s_snapBuf, got)) return false;
    if (JpegDec.width == 0 || JpegDec.height == 0) {
        JpegDec.abort();
        return false;
    }
    return true;
}

// Blit the decoded JPEG (in JpegDec state) into a buffer with decimation.
static void blitTile(uint16_t* tile, uint16_t tw, uint16_t th) {
    if (!tile) {
        JpegDec.abort();
        return;
    }
    uint16_t iw = (uint16_t)JpegDec.width;
    uint16_t ih = (uint16_t)JpegDec.height;
    int step = 1;
    while ((iw / (step + 1) >= tw) && (ih / (step + 1) >= th)) step++;
    if (step < 1) step = 1;
    // Clear target first (letterbox).
    for (uint32_t i = 0; i < (uint32_t)tw * th; i++) tile[i] = 0x0000;
    uint16_t dw = iw / (uint16_t)step;
    uint16_t dh = ih / (uint16_t)step;
    if (dw > tw) dw = tw;
    if (dh > th) dh = th;
    uint16_t ox = (tw - dw) / 2;
    uint16_t oy = (th - dh) / 2;
    while (JpegDec.read()) {
        uint16_t mx = (uint16_t)JpegDec.MCUx;
        uint16_t my = (uint16_t)JpegDec.MCUy;
        uint16_t mw = (uint16_t)JpegDec.MCUWidth;
        uint16_t mh = (uint16_t)JpegDec.MCUHeight;
        uint16_t* img = JpegDec.pImage;
        if (!img) continue;
        for (uint16_t y = 0; y < mh; y += (uint16_t)step) {
            uint32_t sy = (uint32_t)my + y;
            uint16_t dy = (uint16_t)(sy / (uint32_t)step);
            if (dy >= dh) break;
            for (uint16_t x = 0; x < mw; x += (uint16_t)step) {
                uint32_t sx = (uint32_t)mx + x;
                uint16_t dx = (uint16_t)(sx / (uint32_t)step);
                if (dx >= dw) break;
                tile[(oy + dy) * tw + (ox + dx)] =
                    img[y * mw + x];
            }
        }
    }
    JpegDec.abort();
}

static void pollPeer(uint8_t idx) {
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    if (idx >= TOUCH7_PEERS_MAX) return;
    const Touch7Peer& p = cfg.peers[idx];
    if (!p.enabled || !p.host[0]) return;
    if (!s_snapBuf) return;

    String model, note;
    int fps = 0, motion = 0;
    long frames = 0;
    bool recording = false;
    if (!fetchStatus(p, model, fps, frames, recording, motion, note)) {
        s_fails[idx]++;
        setStatus(idx, p, note, false);
        return;
    }
    // Status OK: try the snapshot (failure here keeps the old tile).
    bool snapOk = false;
    if (fetchSnapshot(p)) {
        uint16_t* tile = touch7Display.peerTileBuffer(idx);
        blitTile(tile, TOUCH7_TILE_W, TOUCH7_TILE_H);
        touch7Display.invalidatePeerTile(idx);
        snapOk = true;
    }
    s_fails[idx] = 0;
    char st[96];
    snprintf(st, sizeof(st), "%s %dfps%s mot%d%%", model.c_str(), fps,
             recording ? " REC" : "", motion);
    if (!snapOk) {
        String s = String(st) + " (tile anterior)";
        setStatus(idx, p, s, true);
    } else {
        setStatus(idx, p, String(st), true);
    }
}

void touch7PeersBegin() {
    if (!psramFound()) return;
    if (!s_snapBuf) {
        s_snapBuf = (uint8_t*)ps_malloc(TOUCH7_SNAP_SLOT);
    }
}

void touch7PeerRefreshNow(uint8_t idx) {
    if (idx < TOUCH7_PEERS_MAX) s_nextAt[idx] = 0;
}

// Fullscreen fetch: snapshot straight into a caller PSRAM buffer (640x480).
bool touch7PeerFetchFull(uint8_t idx, uint16_t* buf, uint16_t w, uint16_t h) {
    if (idx >= TOUCH7_PEERS_MAX || !buf || !s_snapBuf) return false;
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    const Touch7Peer& p = cfg.peers[idx];
    if (!p.enabled || !p.host[0]) return false;
    if (!fetchSnapshot(p)) return false;
    blitTile(buf, w, h);
    return true;
}

void touch7PeersTick() {
    if (!touch7ConfigManager.isInitialized() || !touch7ConfigManager.isEnabled()) return;
    if (touch7FlashHeld()) return; // flash op in flight: no PSRAM decode now
    const Touch7Config& cfg0 = touch7ConfigManager.getConfig();
    // Empty slots show a setup hint once (instead of a stale "---").
    static bool emptyShown[TOUCH7_PEERS_MAX] = {false, false, false, false};
    for (uint8_t i = 0; i < TOUCH7_PEERS_MAX; i++) {
        if ((!cfg0.peers[i].enabled || !cfg0.peers[i].host[0]) && !emptyShown[i]) {
            emptyShown[i] = true;
            touch7Display.setPeerStatus(i, String(cfg0.peers[i].name), "sem config", false);
        } else if (cfg0.peers[i].enabled && cfg0.peers[i].host[0]) {
            emptyShown[i] = false;
        }
    }
    if (WiFi.status() != WL_CONNECTED) return;
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    int n = touch7ConfigManager.enabledPeerCount();
    if (n == 0) return;
    int cadence = touch7PeerPollIntervalSec(n);
    if (cfg.read_interval_sec > cadence) cadence = cfg.read_interval_sec;
    unsigned long now = millis();
    // Round-robin: one peer per tick window so a slow node never blocks others.
    for (int k = 0; k < TOUCH7_PEERS_MAX; k++) {
        s_roundRobin = (uint8_t)((s_roundRobin + 1) % TOUCH7_PEERS_MAX);
        uint8_t idx = s_roundRobin;
        const Touch7Peer& p = cfg.peers[idx];
        if (!p.enabled || !p.host[0]) continue;
        if (now < s_nextAt[idx]) continue;
        if (s_lastPollMs != 0 && now - s_lastPollMs < 1500) return; // pace polls
        s_lastPollMs = now;
        pollPeer(idx);
        int wait = (s_fails[idx] > 0)
            ? touch7PeerBackoffSec(s_fails[idx], idx)
            : cadence;
        s_nextAt[idx] = now + (unsigned long)wait * 1000UL;
        return;
    }
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
