#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstddef>

// Standalone reimpl of the Pack Cam stability policies fixed in v1.093:
//   1. MJPEG phase-0 is NON-BLOCKING (web_routes_camera.cpp fillMjpegBuffer):
//      never wait on the frame semaphore inside the AsyncTCP task; yield
//      TRY_AGAIN immediately when no new frame is ready.
//   2. Multi-viewer policy: at most 2 MJPEG clients (shared cache, private
//      pump state each); extras get 503; when the last viewer disconnects
//      the stream pre-capture stops.
//   3. Single frame source (camera_manager.cpp cameraLoop): while streaming,
//      the AVI recorder consumes the same pre-captured cache instead of its
//      own esp_camera_fb_get(); a full write queue drops the newest frame.
//   4. FPS clamp (camera_manager.cpp recommendedMaxFps): configured fps is
//      capped per framesize so WiFi/PSRAM are not saturated on first connect.
//   5. Zero-copy cache pinning (camera_manager.cpp cameraStreamFramePin/Unpin):
//      the MJPEG pump sends straight from the shared cache (no per-viewer
//      PSRAM copy); cameraLoop() only publishes when no reader holds a pin.
//
// Mirrors production constants: STREAM_CACHE_MAX_BYTES = 192KB,
// RESPONSE_TRY_AGAIN sentinel, framesize enum order (QVGA=5 ... UXGA=13).

static const size_t STREAM_CACHE_MAX_BYTES = 192UL * 1024UL;
static const size_t RESPONSE_TRY_AGAIN = (size_t)-1;

// --- 1. Phase-0 decision ----------------------------------------------
enum Phase0Action { PH_WAIT_YIELD, PH_SEND_FRAME, PH_SKIP_EMPTY };

static Phase0Action phase0Decide(bool semaExists, bool semaTakeOk,
                                 bool frameReady, size_t frameLen) {
    // Production: if (!sema || xSemaphoreTake(sema, 0) != pdTRUE) return TRY_AGAIN.
    if (!semaExists || !semaTakeOk) return PH_WAIT_YIELD;
    // Production: if (!avail || availLen == 0 || availLen > 192KB) return TRY_AGAIN.
    if (!frameReady || frameLen == 0 || frameLen > STREAM_CACHE_MAX_BYTES)
        return PH_SKIP_EMPTY;
    return PH_SEND_FRAME;
}

// --- 2. Multi-viewer policy (max 2) -------------------------------------
struct ViewerPolicy {
    int clients = 0;
    static const int MAX_CLIENTS = 2;
    bool streamActive = false;
    // Returns HTTP code for a new /cam/stream request (200 admitted, 503 busy).
    int onNewViewer() {
        if (clients >= MAX_CLIENTS) return 503;
        clients++;
        streamActive = true;
        return 200;
    }
    // Returns true when pre-capture must stop (last viewer left).
    bool onDisconnect() {
        if (clients > 0) clients--;
        if (clients <= 0) {
            clients = 0;
            streamActive = false;
            return true;
        }
        return false;
    }
};

// --- 3. Record-from-cache -----------------------------------------------
struct RecFeed {
    uint32_t fed = 0;
    uint32_t dropped = 0;
    int queueDepth = 0;
    static const int Q_MAX = 4;
    // Mirrors cameraLoop stream branch + cameraRecordFrame drop-newest rule.
    void onCacheFrame(bool recordActive, bool cacheValid, size_t len) {
        if (!recordActive || !cacheValid || len == 0) return;
        if (queueDepth >= Q_MAX) { dropped++; return; }  // drop newest
        queueDepth++;
        fed++;
    }
    void onWriterConsumed() { if (queueDepth > 0) queueDepth--; }
};

// --- 4. FPS clamp --------------------------------------------------------
// Enum values mirror the framework `framesize_t` (sensor.h):
// 6=QVGA, 10=VGA, 11=SVGA, 12=XGA, 13=HD, 15=UXGA.
static uint8_t recommendedMaxFps(int framesize) {
    const int FS_XGA = 12;  // FRAMESIZE_XGA
    const int FS_SVGA = 11;  // FRAMESIZE_SVGA
    const int FS_VGA = 10;  // FRAMESIZE_VGA
    if (framesize >= FS_XGA) return 10;
    if (framesize == FS_SVGA) return 15;
    if (framesize == FS_VGA) return 20;
    return 25;
}

static uint8_t clampFps(int framesize, uint8_t requested) {
    uint8_t maxFps = recommendedMaxFps(framesize);
    return (requested > maxFps) ? maxFps : requested;
}

// --- 5. Single-pump grab policy ------------------------------------------
// Mirrors cameraGrabFileFrame(): stream live -> cache (wait); idle +
// motion sampling -> refuse; idle otherwise -> one direct capture.
enum GrabSource { GRAB_CACHE, GRAB_REFUSE, GRAB_DIRECT };

static GrabSource grabPolicy(bool ready, bool streamActive, bool sampling) {
    if (!ready) return GRAB_REFUSE;
    if (streamActive) return GRAB_CACHE;
    if (sampling) return GRAB_REFUSE;
    return GRAB_DIRECT;
}

// Cache sizing: w*h/5 JPEG estimate clamped to [192KB, 1MB].
static size_t cacheBytesFor(uint16_t w, uint16_t h) {
    size_t need = (size_t)w * (size_t)h / 5;
    const size_t MINB = 192UL * 1024UL, MAXB = 1024UL * 1024UL;
    if (need < MINB) need = MINB;
    if (need > MAXB) need = MAXB;
    return need;
}

TEST_CASE("phase0 never blocks: yields when semaphore unavailable") {
    CHECK(phase0Decide(false, false, true, 50000) == PH_WAIT_YIELD);
    CHECK(phase0Decide(true, false, true, 50000) == PH_WAIT_YIELD);
}

TEST_CASE("phase0 skips empty/oversize frames without blocking") {
    CHECK(phase0Decide(true, true, false, 0) == PH_SKIP_EMPTY);
    CHECK(phase0Decide(true, true, true, 0) == PH_SKIP_EMPTY);
    CHECK(phase0Decide(true, true, true, STREAM_CACHE_MAX_BYTES + 1) == PH_SKIP_EMPTY);
}

TEST_CASE("phase0 sends a ready valid frame") {
    CHECK(phase0Decide(true, true, true, 1) == PH_SEND_FRAME);
    CHECK(phase0Decide(true, true, true, STREAM_CACHE_MAX_BYTES) == PH_SEND_FRAME);
}

TEST_CASE("two viewers: first and second admitted, third rejected") {
    ViewerPolicy v;
    CHECK(v.onNewViewer() == 200);
    CHECK(v.streamActive == true);
    CHECK(v.onNewViewer() == 200);
    CHECK(v.clients == 2);
    CHECK(v.onNewViewer() == 503);
    CHECK(v.clients == 2);
}

TEST_CASE("two viewers: one leaves, stream keeps running") {
    ViewerPolicy v;
    v.onNewViewer();
    v.onNewViewer();
    CHECK(v.onDisconnect() == false);
    CHECK(v.streamActive == true);
    CHECK(v.clients == 1);
    CHECK(v.onDisconnect() == true);
    CHECK(v.streamActive == false);
    CHECK(v.clients == 0);
    // Disconnect with no clients is a no-op, never negative.
    CHECK(v.onDisconnect() == true);
    CHECK(v.clients == 0);
}

TEST_CASE("record-from-cache: idle recorder and empty cache feed nothing") {
    RecFeed r;
    r.onCacheFrame(false, true, 40000);
    CHECK(r.fed == 0);
    r.onCacheFrame(true, false, 0);
    CHECK(r.fed == 0);
    r.onCacheFrame(true, true, 0);
    CHECK(r.fed == 0);
}

TEST_CASE("record-from-cache: streams frames then drops newest when full") {
    RecFeed r;
    for (int i = 0; i < 4; i++) r.onCacheFrame(true, true, 40000);
    CHECK(r.fed == 4);
    CHECK(r.queueDepth == 4);
    r.onCacheFrame(true, true, 40000);
    CHECK(r.fed == 4);
    CHECK(r.dropped == 1);
    r.onWriterConsumed();
    r.onCacheFrame(true, true, 40000);
    CHECK(r.fed == 5);
}

TEST_CASE("fps clamp: high resolutions capped") {
    CHECK(clampFps(15, 30) == 10);  // UXGA -> 10
    CHECK(clampFps(12, 30) == 10);  // XGA -> 10
    CHECK(clampFps(11, 30) == 15);  // SVGA -> 15
    CHECK(clampFps(24, 30) == 10);  // 5MP -> 10
}

TEST_CASE("fps clamp: low resolutions keep request within cap") {
    CHECK(clampFps(10, 12) == 12);  // VGA default 12 stays
    CHECK(clampFps(10, 30) == 20);  // VGA capped at 20
    CHECK(clampFps(6, 12) == 12);   // QVGA untouched
    CHECK(clampFps(6, 30) == 25);   // QVGA capped at 25
}

TEST_CASE("grab policy: live stream always serves from cache") {
    CHECK(grabPolicy(true, true, false) == GRAB_CACHE);
    CHECK(grabPolicy(true, true, true) == GRAB_CACHE);  // motion paused live
    CHECK(grabPolicy(false, true, false) == GRAB_REFUSE);
}

TEST_CASE("grab policy: idle refuses during sampling, direct otherwise") {
    CHECK(grabPolicy(true, false, true) == GRAB_REFUSE);
    CHECK(grabPolicy(true, false, false) == GRAB_DIRECT);
    CHECK(grabPolicy(false, false, false) == GRAB_REFUSE);
}

TEST_CASE("cache sizing: w*h/5 clamped to [192KB, 1MB]") {    CHECK(cacheBytesFor(640, 480) == 192UL * 1024UL);    // VGA floors
    CHECK(cacheBytesFor(320, 240) == 192UL * 1024UL);    // QVGA floors
    CHECK(cacheBytesFor(1280, 720) == 192UL * 1024UL);   // HD floors (184KB->192KB)
    CHECK(cacheBytesFor(1600, 1200) == (size_t)1600 * 1200 / 5);  // UXGA exact
    CHECK(cacheBytesFor(2048, 1536) == (size_t)2048 * 1536 / 5);  // QXGA exact
    CHECK(cacheBytesFor(2592, 1944) == (size_t)2592 * 1944 / 5);  // 5MP fits (~984KB)
}

// --- 6. Dashcam ownership (record tick) --------------------------------------
// Mirrors cameraRecordTick(): the dashcam-off branch only finalises a file
// the dashcam itself started; manual/motion/timelapse sessions are immune.
// A generic idle pump feeds any other active session at its own fps.
struct DashTick {
    bool active = false;
    bool dashOwn = false;
    bool tlActive = false;
    bool dashEnabled = false;
    bool finished = false;
    bool pumped = false;
    void tick() {
        finished = false; pumped = false;
        if (dashEnabled) {
            if (!active) { active = true; dashOwn = true; }
        } else {
            if (active && dashOwn) { active = false; finished = true; }
            dashOwn = false;
        }
        if (active && !dashEnabled && !tlActive) pumped = true;  // generic pump
    }
};

TEST_CASE("dash-off branch never kills manual sessions") {
    DashTick t;
    t.active = true;  // manual start, dashcam disabled
    t.tick();
    CHECK(t.active == true);
    CHECK(t.finished == false);
}

TEST_CASE("dash-off branch closes leftover dash files") {
    DashTick t;
    t.active = true; t.dashOwn = true;  // dash was on, now off
    t.tick();
    CHECK(t.active == false);
    CHECK(t.finished == true);
}

TEST_CASE("generic pump feeds idle manual sessions") {
    DashTick t;
    t.active = true;
    t.tick();
    CHECK(t.pumped == true);
    t.dashEnabled = true;
    t.tick();
    CHECK(t.pumped == false);  // dashcam branch owns the pump
}

// --- 7. Zero-copy cache pin/refcount ----------------------------------------
// Mirrors camera_manager.cpp: readers pin the shared cache for the whole send
// (cameraStreamFramePin/Unpin); cameraLoop() publishes a new frame only when
// s_streamRefs == 0, otherwise the captured frame is dropped and the reader
// keeps resending the frame it holds. cameraStreamStart() resets refs.
struct StreamCache {
    int      refs  = 0;
    uint32_t gen   = 0;
    size_t   len   = 0;
    bool     valid = false;

    void resetForNewSession() { refs = 0; valid = false; len = 0; }

    bool pin(size_t& outLen) {
        if (!valid || len == 0) return false;
        refs++;
        outLen = len;
        return true;
    }
    void unpin() { if (refs > 0) refs--; }

    // Writer: returns true when published, false when dropped (pinned).
    bool publish(size_t newLen) {
        if (refs > 0) return false;
        len = newLen; valid = true; gen++;
        return true;
    }
};

TEST_CASE("zero-copy: pin blocks publish, unpin re-enables it") {
    StreamCache c;
    c.publish(1000);                 // first frame
    CHECK(c.gen == 1);
    size_t n = 0;
    CHECK(c.pin(n) == true);
    CHECK(n == 1000);
    CHECK(c.publish(2000) == false); // reader owns the buffer
    CHECK(c.gen == 1);               // generation unchanged on drop
    CHECK(c.len == 1000);            // reader's frame intact
    c.unpin();
    CHECK(c.publish(2000) == true);
    CHECK(c.gen == 2);
    CHECK(c.len == 2000);
}

TEST_CASE("zero-copy: two viewers hold and release independently") {
    StreamCache c;
    c.publish(500);
    size_t a = 0, b = 0;
    CHECK(c.pin(a) == true);
    CHECK(c.pin(b) == true);
    CHECK(c.refs == 2);
    c.unpin();                       // first viewer done
    CHECK(c.publish(600) == false);  // second still reading
    c.unpin();                       // last viewer done
    CHECK(c.publish(600) == true);
    CHECK(c.gen == 2);
}

TEST_CASE("zero-copy: pin fails before first frame, resets for a new session") {
    StreamCache c;
    size_t n = 0;
    CHECK(c.pin(n) == false);        // nothing published yet
    c.publish(700);
    CHECK(c.pin(n) == true);
    c.resetForNewSession();          // cameraStreamStart()
    CHECK(c.refs == 0);
    CHECK(c.valid == false);
}

TEST_CASE("zero-copy: unpin never underflows") {
    StreamCache c;
    c.publish(100);
    c.unpin();                       // no pin held
    CHECK(c.refs == 0);
    size_t n = 0;
    c.pin(n);
    c.unpin();
    c.unpin();
    CHECK(c.refs == 0);
}

// Writer-side: the recorder is fed from the freshly captured frame BEFORE the
// cache publish, so a pinned cache (publish deferred) never starves recording.
struct WriterTick {
    StreamCache cache;
    int recFed = 0;
    // Returns true if the cache was published this tick.
    bool tick(size_t frameLen, bool recordActive) {
        if (recordActive && frameLen > 0) recFed++;   // fed from src, not cache
        return cache.publish(frameLen);
    }
};

TEST_CASE("zero-copy: recorder is fed even when a pin blocks the publish") {
    WriterTick w;
    size_t n = 0;
    w.tick(1000, false);          // publish frame 1
    CHECK(w.cache.pin(n) == true);
    CHECK(w.tick(2000, true) == false);  // pinned -> publish dropped
    CHECK(w.recFed == 1);                // but recorder still got the frame
    CHECK(w.cache.gen == 1);             // reader's frame unchanged
    w.cache.unpin();
    CHECK(w.tick(2000, true) == true);   // now publishes
    CHECK(w.cache.gen == 2);
    CHECK(w.recFed == 2);
}

// CAM-67: long-lived MJPEG streams must survive routine WiFi TX stalls.
// AsyncClient aborts a connection whose sent data goes unacknowledged for
// CONFIG_ASYNC_TCP_MAX_ACK_TIME (5 s default), which silently killed active
// streams every few minutes (HW: Pi probe died at 168 s mid-transfer).
// Production sets 30 s on /cam/stream + /cam/playback clients; the 20 s
// zombie eviction still reaps genuinely dead viewers, so slots cannot leak.
static const unsigned long STREAM_ACK_TIMEOUT_MS = 30000UL;
static const unsigned long ASYNC_TCP_DEFAULT_ACK_TIMEOUT_MS = 5000UL;
static const unsigned long ZOMBIE_EVICT_SILENCE_MS = 20000UL;

TEST_CASE("stream: ack timeout raised well above the 5 s library default") {
    CHECK(STREAM_ACK_TIMEOUT_MS > ASYNC_TCP_DEFAULT_ACK_TIMEOUT_MS);
    CHECK(STREAM_ACK_TIMEOUT_MS == 30000UL);  // mirrors web_routes_camera.cpp
}

TEST_CASE("stream: zombie eviction still bounds dead slots below the ack timeout") {
    // A truly dead connection moves no bytes, so the 20 s silence eviction
    // frees its slot even though nothing is ever ACK-aborted at 30 s.
    CHECK(ZOMBIE_EVICT_SILENCE_MS < STREAM_ACK_TIMEOUT_MS);
    CHECK(ZOMBIE_EVICT_SILENCE_MS == 20000UL);  // mirrors web_routes_camera.cpp
}
