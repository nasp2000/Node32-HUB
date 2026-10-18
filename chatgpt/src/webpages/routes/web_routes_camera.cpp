#include "web_routes_camera.h"
#include "route_registry_api.h"
#include "../assets/page_camera_asset.h"
#include "../assets/page_hub_asset.h"
#include "../http_abstraction.h"
#include "../handlers_unified/camera_handler.h"
#include "../renderers/page_render_common.h"
#include "../../feature_flags.h"
#include "../../camera_manager.h"
#include "../../camera_record.h"
#include "../../psram_helpers.h"
#include "../../direct_log.h"

// Local-processing heartbeat without pulling webpage_impl.h (which needs the
// full server context): refresh the viewer's multi-track slot so active
// local viewers (e.g. /camera polling /api/camera/status every 5 s) are
// never kicked by the stale-tracker. Declared in webpage_state.cpp.
extern void webLocalPageHeartbeat(AsyncWebServerRequest* request);
#if ENABLE_SDCARD==1
#include "../../sdcard.h"
#endif

#if ENABLE_CAMERA==1

// ── MJPEG streaming state machine ────────────────────────────────
// Phase progression: 0=wait_frame → 1=header → 2=jpeg_data → 3=trailer → 0
struct MjpegStreamState {
    int     phase    = 0;
    size_t  offset   = 0;
    size_t  frameLen = 0;
    const uint8_t* frameBuf = nullptr;  // pinned pointer into the shared cache (zero copy)
    bool    pinned   = false;           // frameBuf held via cameraStreamFramePin()
    char    hdr[96];              // stack-built header: zero heap churn per frame
    size_t  hdrLen    = 0;

    ~MjpegStreamState() { if (pinned) cameraStreamFrameUnpin(); }
};

// Last millis() at which fillMjpegBuffer() pushed bytes to any viewer.
static volatile uint32_t s_lastMjpegByteMs = 0;
static size_t fillMjpegBuffer(MjpegStreamState* s, uint8_t* buf, size_t maxLen) {
    if (s->phase == 0) {
        // Bounded wait (not a spin): yields core 0 so other HTTP/TCP keeps
        // flowing, and — critically — every invocation below SENDS a frame.
        // A pure TRY_AGAIN spin deadlocks here: the chunked framework only
        // retries on TCP activity, so one empty poll with no unacked data
        // in flight freezes the stream forever. On timeout we re-pin and
        // resend the last frame to keep the ACK clock ticking; the fps dedup
        // in cameraStreamFrameSent() keeps the counter honest.
        //
        // Zero copy: pin the shared cache instead of memcpy-ing a private
        // buffer. Release the previous pin first so cameraLoop() can publish
        // a fresh frame while we wait.
        if (s->pinned) {
            cameraStreamFrameUnpin();
            s->pinned = false;
            s->frameBuf = nullptr;
            s->frameLen = 0;
        }
        SemaphoreHandle_t sema = cameraStreamFrameSemaphore();
        if (sema) xSemaphoreTake(sema, pdMS_TO_TICKS(250));
        size_t availLen = 0;
        const uint8_t* avail = cameraStreamFramePin(availLen);
        if (avail && availLen > 0 && availLen <= (1024UL * 1024UL)) {
            s->frameBuf = avail;
            s->frameLen = availLen;
            s->pinned   = true;
            cameraStreamFrameSent();
        }
        if (!s->frameBuf || s->frameLen == 0) return RESPONSE_TRY_AGAIN;
        s->hdrLen = snprintf(s->hdr, sizeof(s->hdr),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            (unsigned)s->frameLen);
        s->phase = 1; s->offset = 0;
    }
    if (s->phase == 1) {
        size_t rem = (s->hdrLen > s->offset) ? (s->hdrLen - s->offset) : 0;
        size_t n = (rem < maxLen) ? rem : maxLen;
        if (n > 0) memcpy(buf, s->hdr + s->offset, n);
        s->offset += n;
        if (s->offset >= s->hdrLen) { s->phase = 2; s->offset = 0; }
        if (n > 0) { s_lastMjpegByteMs = millis(); return n; }
        return RESPONSE_TRY_AGAIN;
    }
    if (s->phase == 2) {
        size_t rem = s->frameLen - s->offset;
        size_t n = (rem < maxLen) ? rem : maxLen;
        if (n > 0) memcpy(buf, s->frameBuf + s->offset, n);
        s->offset += n;
        if (s->offset >= s->frameLen) { s->phase = 3; s->offset = 0; }
        if (n > 0) { s_lastMjpegByteMs = millis(); return n; }
        return RESPONSE_TRY_AGAIN;
    }
    if (s->phase == 3) {
        buf[0] = '\r'; buf[1] = '\n';
        s->phase = 0; s->offset = 0;
        s_lastMjpegByteMs = millis();
        return 2;
    }
    return RESPONSE_TRY_AGAIN;
}

// Active MJPEG viewer count. The S3 serves up to MAX_MJPEG_CLIENTS parallel
// viewers; each pins an Async client and pins the shared cache only while it
// is being sent from. Last byte pushed to ANY viewer (CAM-63): a live stream
// always moves bytes — fresh frames or re-sent last frames — so silence here
// means the counted viewers are dead TCP without FIN (zombie slots).
// is sending a frame (zero-copy), so no extra capture or per-viewer copy. Extras get 503.
#define MAX_MJPEG_CLIENTS 2
static volatile int s_mjpegClients = 0;
// ACK timeout for long-lived MJPEG responses (CAM-67): AsyncClient aborts a
// connection whose sent data goes unacknowledged for
// CONFIG_ASYNC_TCP_MAX_ACK_TIME (5 s default). Any WiFi TX stall longer than
// that — interference bursts on 2.4 GHz are routine — silently kills an
// actively-transferring stream every few minutes (HW-proven: a Pi probe died
// at 168 s mid-transfer). 30 s survives real-world stalls; genuinely dead
// clients are still reaped by the 20 s zombie eviction, so slots cannot leak.

// Diagnostics for /api/camera/status (proves single-viewer policy live).
int cameraStreamClients() {
    return s_mjpegClients;
}

void cameraStreamResetClients() {
    s_mjpegClients = 0;
}

static void registerCameraApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server;
    (void)context;

    // Snapshot and stream remain manual (binary response, non-blocking MJPEG pump)
    server.on("/cam/snapshot", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleCameraSnapshotGet(hreq, hresp);
        if (hresp.isBinary && hresp.binaryData) {
            AsyncWebServerResponse* resp = req->beginResponse_P(hresp.code, hresp.contentType, hresp.binaryData, hresp.binaryLen);
            resp->addHeader("Cache-Control", "no-cache");
            if (hresp.freeFn) {
                const uint8_t* data = hresp.binaryData;
                auto freeFn = hresp.freeFn;
                req->onDisconnect([data, freeFn]() { freeFn(data); });
            }
            req->send(resp);
        } else {
            req->send(hresp.code, hresp.contentType, hresp.body);
        }
    });

    // Motion diff visualisation (same binary pattern as snapshot)
    server.on("/cam/motion/diff", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        HttpRequest hreq;
        HttpResponse hresp;
        handleCameraMotionDiffGet(hreq, hresp);
        if (hresp.isBinary && hresp.binaryData) {
            AsyncWebServerResponse* resp = req->beginResponse_P(hresp.code, hresp.contentType, hresp.binaryData, hresp.binaryLen);
            resp->addHeader("Cache-Control", "no-cache");
            if (hresp.freeFn) {
                const uint8_t* data = hresp.binaryData;
                auto freeFn = hresp.freeFn;
                req->onDisconnect([data, freeFn]() { freeFn(data); });
            }
            req->send(resp);
        } else {
            req->send(hresp.code, hresp.contentType, hresp.body);
        }
    });

#if ENABLE_SDCARD==1
    // ── AVI playback state machine ───────────────────────────────────
// Serves a recorded /CAM/*.avi as MJPEG by walking '00dc' chunks after the
// 224-byte header. Phases: 0=chunk header → 1=part header → 2=jpeg → 3=pad+CRLF.
// Paced to the recorded fps (TRY_AGAIN until the frame interval elapses).
// Returning 0 ends the chunked response (EOF or corrupt chunk).
struct AviPlayState {
    File     file;
    int      phase    = 0;
    size_t   offset   = 0;
    size_t   frameLen = 0;
    uint32_t frameMs  = 0;   // interval per frame at recorded fps
    uint32_t lastMs   = 0;
    uint8_t  hdrBuf[8];
    size_t   hdrGot   = 0;
    char     partHdr[96];
    size_t   partLen  = 0;

    ~AviPlayState() { if (file) file.close(); }
};

static auto fillAviPlayBuffer = [](AviPlayState* s, uint8_t* buf, size_t maxLen) -> size_t {
    if (s->phase == 0) {
        // No pacing waits here: a TRY_AGAIN with no unacked data in flight
        // stalls this chunked pump the same way it stalled the live stream.
        // Playback runs as fast as the SD allows; browsers pace rendering.
        // Accumulate the 8-byte chunk header across calls.
        while (s->hdrGot < 8) {
            int r = s->file.read(s->hdrBuf + s->hdrGot, 8 - s->hdrGot);
            if (r <= 0) return 0;  // EOF
            s->hdrGot += (size_t)r;
        }
        if (!(s->hdrBuf[0] == '0' && s->hdrBuf[1] == '0' &&
              s->hdrBuf[2] == 'd' && s->hdrBuf[3] == 'c')) {
            return 0;  // corrupt chunk: end playback cleanly
        }
        uint32_t jl = ((uint32_t)s->hdrBuf[4]) | ((uint32_t)s->hdrBuf[5] << 8) |
                      ((uint32_t)s->hdrBuf[6] << 16) | ((uint32_t)s->hdrBuf[7] << 24);
        if (jl == 0 || jl > (1024UL * 1024UL)) return 0;
        s->frameLen = jl;
        s->hdrGot = 0;
        s->partLen = snprintf(s->partHdr, sizeof(s->partHdr),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            (unsigned)jl);
        s->phase = 1; s->offset = 0;
    }
    if (s->phase == 1) {
        size_t rem = (s->partLen > s->offset) ? (s->partLen - s->offset) : 0;
        size_t n = (rem < maxLen) ? rem : maxLen;
        if (n > 0) memcpy(buf, s->partHdr + s->offset, n);
        s->offset += n;
        if (s->offset >= s->partLen) { s->phase = 2; s->offset = 0; }
        return n > 0 ? n : RESPONSE_TRY_AGAIN;
    }
    if (s->phase == 2) {
        size_t rem = s->frameLen - s->offset;
        size_t n = (rem < maxLen) ? rem : maxLen;
        int r = (n > 0) ? s->file.read(buf, n) : 0;
        if (r <= 0) return 0;  // truncated file
        s->offset += (size_t)r;
        if (s->offset >= s->frameLen) { s->phase = 3; s->offset = 0; }
        return (size_t)r;
    }
    if (s->phase == 3) {
        // Skip WORD pad byte for odd sizes, then emit trailing CRLF.
        if ((s->frameLen & 1) && s->offset == 0) {
            uint8_t pad = 0;
            if (s->file.read(&pad, 1) <= 0) return 0;
            s->offset = 1;
            return RESPONSE_TRY_AGAIN;
        }
        buf[0] = '\r'; buf[1] = '\n';
        s->phase = 0; s->offset = 0;
        return 2;
    }
    return RESPONSE_TRY_AGAIN;
};
#endif // ENABLE_SDCARD==1 (AVI playback pump)

    // ── MJPEG stream (multipart/x-mixed-replace) ────────────────
    server.on("/cam/stream", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) { req->send(401); return; }
        if (!cameraIsReady()) { req->send(503, "text/plain", "Camera not ready"); return; }
        // Multi-viewer policy (max 2): each viewer owns a private pump state
        // fed from the shared cache — no extra captures, ~1 frameBuf in PSRAM
        // each. Extras get 503 instead of starving WiFi/heap.
        // Zombie eviction (CAM-63): dead TCP without FIN leaks slots, so every
        // in-page retry 503s forever and only a full refresh (page unload
        // closes the browser sockets) recovers. A live stream always moves
        // bytes — fresh or re-sent frames — so no bytes for 20 s means the
        // counted viewers are gone: reset the counter (live response objects
        // keep working untouched) and accept the newcomer.
        if (s_mjpegClients >= MAX_MJPEG_CLIENTS) {
            if (s_lastMjpegByteMs != 0 && (millis() - s_lastMjpegByteMs) > 20000UL) {
                directLogPrintln("[CAM] Stream slots stale 20s: evicting zombie viewers");
                s_mjpegClients = 0;
            } else {
                req->send(503, "text/plain", "Stream busy");
                return;
            }
        }

        // Latency/throughput: disable Nagle for this viewer so every chunked
        // fill leaves the socket immediately instead of waiting to coalesce.
        // This is the TCP-side equivalent of a larger output chunk and needs no
        // library change (AsyncClient::setNoDelay).
        // Survival: raise the ACK timeout (CAM-67, default 5 s aborts the
        // stream on any WiFi TX stall) alongside setNoDelay.
        if (req->client()) { req->client()->setNoDelay(true); req->client()->setAckTimeout(30000); }

        if (!cameraStreamIsActive()) {
            if (!req->client() || !req->client()->connected()) return;
            cameraStreamStart();
        }
        s_mjpegClients++;

        MjpegStreamState* state = new MjpegStreamState();

        AsyncWebServerResponse* resp = req->beginChunkedResponse(
            "multipart/x-mixed-replace; boundary=--frame",
            [state](uint8_t* buf, size_t maxLen, size_t /*index*/) -> size_t {
                return fillMjpegBuffer(state, buf, maxLen);
            });

        if (!resp) { s_mjpegClients--; delete state; req->send(500, "text/plain", "Internal Server Error"); return; }

        resp->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        resp->addHeader("Pragma", "no-cache");
        resp->addHeader("Expires", "0");
        resp->addHeader("X-Accel-Buffering", "no");

        directLogPrintln("[CAM] Stream viewer connected");
        req->onDisconnect([state]() {
            delete state;
            if (s_mjpegClients > 0) s_mjpegClients--;
            // Last viewer left: stop pre-capture so cameraLoop() idles and
            // the snapshot path falls back to single direct captures.
            if (s_mjpegClients <= 0) {
                s_mjpegClients = 0;
                cameraStreamStop();
            }
            char msg[48];
            snprintf(msg, sizeof(msg), "[CAM] Stream viewer left (clients=%d)", s_mjpegClients);
            directLogPrintln(msg);
        });
        req->send(resp);
    });

    // ── Raw stream page: zero-chrome fullscreen MJPEG for a second PC ──
    // No auto.js, no polls, no shell: the only cost on the ESP32 is the same
    // shared-cache pump as /cam/stream. Counts toward MAX_MJPEG_CLIENTS.
    // Local-processing page by construction: no inactivity script exists here
    // (nothing to redirect), the route takes no session slot, and the tiny
    // inline retry below revives the img after a network hiccup — so the raw
    // view never closes while the "local pages never expire" choice is on
    // (or off — it simply has no expiry path at all).
    server.on("/cam/raw", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        static const char kRaw[] PROGMEM =
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Cam raw</title>"
            "<style>html,body{margin:0;height:100%;background:#000}"
            "img{width:100vw;height:100vh;object-fit:contain;display:block}</style>"
            "</head><body><img id='rawImg' src='/cam/stream'>"
            "<div id='rawMsg' style='position:fixed;top:8px;left:8px;color:#f87171;font:12px monospace;display:none'></div>"
            "<script>var r=document.getElementById('rawImg'),g=document.getElementById('rawMsg');"
            "r.onerror=function(){setTimeout(function(){r.src='/cam/stream?_='+Date.now()},2000)};"
            // Frozen-stream watchdog (CAM-63): like the /camera page, watch the
            // monotonic streamFrames counter and reconnect in-page when it
            // freezes — the view must never need a full refresh to come back.
            "var lastFr=-1,frozenStrikes=0;"
            "setInterval(function(){"
            "fetch('/api/camera/status',{cache:'no-store'}).then(function(x){return x.json()}).then(function(d){"
            "if(!d||!d.ready){lastFr=-1;frozenStrikes=0;if(g)g.style.display='none';return};"
            "var fr=(d.streamFrames===undefined)?-1:d.streamFrames;"
            "if(lastFr>=0&&fr>=0){"
            "if(fr===lastFr){frozenStrikes++;if(frozenStrikes>=3){frozenStrikes=0;if(g){g.textContent='reconnecting…';g.style.display='block'}r.src='/cam/stream?_='+Date.now()}}"
            "else{frozenStrikes=0;if(g)g.style.display='none'}}"
            "lastFr=fr}).catch(function(){})"
            "},10000);"
            "</script></body></html>";
        req->send(200, "text/html; charset=utf-8", FPSTR(kRaw));
    });

    // ── AVI playback (recorded file as MJPEG) ────────────────────
#if ENABLE_SDCARD==1
    server.on("/cam/playback", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) { req->send(401); return; }
        String file = req->hasArg("file") ? req->arg("file") : String();
        file.replace("%2F", "/"); file.replace("%2f", "/");
        bool badPath = true;
        if (file.startsWith("/CAM/") && file.indexOf("..") < 0 &&
            (int)file.length() <= 128 && file.endsWith(".avi")) badPath = false;
        if (badPath) { req->send(400, "text/plain", "Bad file path"); return; }
        if (cameraRecordActive()) { req->send(503, "text/plain", "Recording in progress"); return; }
#if ENABLE_SDCARD==1
        if (!sdcardShouldUseSD()) { req->send(503, "text/plain", "SD not available"); return; }

        extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
        extern void releaseSDAccess();
        if (!acquireSDAccess("camera-playback", 2000UL)) {
            req->send(503, "text/plain", "SD busy"); return;
        }
        File f = SD_MMC.open(file.c_str(), FILE_READ);
        if (!f) { releaseSDAccess(); req->send(404, "text/plain", "File not found"); return; }
        if (f.size() <= 224) { f.close(); releaseSDAccess(); req->send(400, "text/plain", "File too small"); return; }
        // Pace from filename: ..._<RES>_<FPS>.avi (also _<FPS>_<DUR>s_<TAG>).
        // Robust: find a known RES label token, fps is the next 1..30 token.
        uint8_t playFps = 10;
        {
            static const char* kRes[] = {"QVGA","HVGA","VGA","SVGA","XGA","HD",
                "SXGA","UXGA","FHD","QXGA","5MP","CIF","VID", nullptr};
            int start = 0;
            while (start < (int)file.length()) {
                int u = file.indexOf('_', start);
                String tok = (u < 0) ? file.substring(start)
                                     : file.substring(start, u);
                for (int k = 0; kRes[k]; k++) {
                    if (tok == kRes[k]) {
                        int n0 = (u < 0) ? -1 : u + 1;
                        int n1 = (n0 < 0) ? -1 : file.indexOf('_', n0);
                        String num = (n1 < 0) ? String() : file.substring(n0, n1);
                        int v = num.toInt();
                        if (v >= 1 && v <= 30) playFps = (uint8_t)v;
                        start = (int)file.length();
                        break;
                    }
                }
                if (u < 0) break;
                start = u + 1;
            }
        }
        f.seek(224);  // skip RIFF/hdrl/movi header
        releaseSDAccess();

        AviPlayState* state = new AviPlayState();        state->file = f;
        state->frameMs = 1000UL / playFps;

        AsyncWebServerResponse* resp = req->beginChunkedResponse(
            "multipart/x-mixed-replace; boundary=--frame",
            [state](uint8_t* buf, size_t maxLen, size_t /*index*/) -> size_t {
                return fillAviPlayBuffer(state, buf, maxLen);
            });

        if (!resp) { delete state; req->send(500, "text/plain", "Internal Server Error"); return; }
        resp->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        resp->addHeader("Pragma", "no-cache");
        // Same long-lived-stream survival as /cam/stream (CAM-67): a WiFi TX
        // stall longer than the 5 s default ACK timeout aborts playback.
        if (req->client()) req->client()->setAckTimeout(30000);
        req->onDisconnect([state]() { delete state; });
        req->send(resp);
#else
        req->send(503, "text/plain", "SD not available");
#endif
    });
#endif // ENABLE_SDCARD==1 (AVI playback route)

    // Simple JSON API routes — auto-registered on both WiFi and Ethernet

    // Simple JSON API routes — auto-registered on both WiFi and Ethernet
    registerApiRoute("GET",    "/api/camera/status",         handleCameraStatusGet);
    registerApiRoute("GET",    "/api/camera/config",         handleCameraConfigGet);
    registerApiRoute("GET",    "/api/camera/recording",      handleCameraRecordingStatusGet);
    registerApiRoute("POST",   "/api/camera/start",           handleCameraStartPost);
    registerApiRoute("POST",   "/api/camera/stop",            handleCameraStopPost);
    registerApiRoute("POST",   "/api/camera/config",          handleCameraConfigPost);
    registerApiRoute("GET",    "/api/camera/last-photo",      handleCameraLastPhotoGet);
    registerApiRoute("POST",   "/api/camera/photo",           handleCameraPhotoPost);
    registerApiRoute("POST",   "/api/camera/recording/start", handleCameraRecordingStartPost);
    registerApiRoute("POST",   "/api/camera/recording/stop",  handleCameraRecordingStopPost);
    registerApiRoute("GET",    "/api/camera/files",          handleCameraFilesGet);
    registerApiRoute("POST",   "/api/camera/delete",         handleCameraDeletePost);
    registerApiRoute("POST",   "/api/camera/motion/reset",   handleCameraMotionResetPost);
    registerApiRoute("POST",   "/api/camera/vision/burst",   handleCameraVisionBurstPost);
    registerApiRoute("POST",   "/api/camera/vision/reset",   handleCameraVisionResetPost);
}
#else
static void registerCameraApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server; (void)context;
}
#endif

void registerCameraRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_CAMERA==1
    server.on("/camera", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        if (sendNotModifiedIfCached(req)) return;
        if (!context.allowHtmlClient(req)) return;
        webLocalPageHeartbeat(req);

        static bool _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xF0\x9F\x93\xB7 C\xC3\xA2mara", true);
            // Camera is a local-processing page (live MJPEG runs in the
            // browser): never expire its session on inactivity timeout.
            _pre += F("<script>window._localPagesNeverExpire=true;</script>");
            _aLen = strlen_P(CAMERA_PAGE);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char* pPtr = _pre.c_str(), *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = req->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index, pPtr, pLen, CAMERA_PAGE, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); req->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        req->send(response);
    });

    registerCameraApiRoutes(server, context);

    // ── Camera Hub (other Node32-HUB cameras, browser-side only) ──
    server.on("/hub", HTTP_GET, [&context](AsyncWebServerRequest* req) {
        if (!context.ensureBasicAuth(req)) return;
        if (sendNotModifiedIfCached(req)) return;
        if (!context.allowHtmlClient(req)) return;

        static bool _hinit = false;
        static String _hpre, _hsuf;
        static size_t _haLen = 0;
        if (!_hinit) {
            buildPageFrame(_hpre, _hsuf, "\xF0\x9F\x93\xB7 Hub", true);
            // Local-processing page: ALWAYS exempt from the inactivity
            // watchdog — flag goes in the prefix so it precedes auto.js.
            _hpre += F("<script>window._localPagesNeverExpire=true;</script>");
            _haLen = strlen_P(HUB_PAGE);
            _hinit = true;
        }
        const size_t pLen = _hpre.length(), aLen = _haLen, sLen = _hsuf.length();
        const char* pPtr = _hpre.c_str(), *sPtr = _hsuf.c_str();

        AsyncWebServerResponse* response = req->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index, pPtr, pLen, HUB_PAGE, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); req->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        req->send(response);
    });
#else
    (void)server;
    (void)context;
#endif
}
