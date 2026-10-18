#pragma once
// camera_psram_ring.h — PSRAM shock-absorber ring for camera frames.
//
// Frames (MJPEG chunks or H.264 NAL blobs) land here when the SD path is
// slower than the capture pump; a background drain moves them to the card.
// Without it a slow/dying SD card throttles or kills the recording fps —
// with it the capture keeps its own pace and the card catches up.
//
// Layout: contiguous byte buffer with framed records [u32 len][payload].
// Overflow drops the OLDEST complete frames first (newest data wins — a
// security camera wants the last seconds, not the first). All decisions are
// pure header logic over caller-owned memory, so Linux/MinGW tests compile
// this directly; the firmware owns the PSRAM buffer and the mutex.
//
// Sizing (runtime, both boards): ring = min(boardCap, freePsram/3), with a
// 256 KB floor below which the ring stays disabled and the old direct path
// runs. P4 (32 MB PSRAM): cap 12 MB. S3 N16R8 (8 MB PSRAM): cap 2 MB —
// still ~15-60 s at S3 byte rates.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// --- Sizing ---------------------------------------------------------------
// Board caps (bytes). S3 N16R8 has 8 MB PSRAM total, shared with the stream
// cache/motion/email — 2 MB is the most the ring may take there.
#define CAM_RING_CAP_P4   (12UL * 1024UL * 1024UL)
#define CAM_RING_CAP_S3   (2UL * 1024UL * 1024UL)
#define CAM_RING_MIN      (256UL * 1024UL)

// Pick the ring size from the free PSRAM seen at record start. 0 = too
// little headroom, keep the direct SD path (old behaviour, no regression).
static inline size_t cameraRingPickSize(size_t freePsram, size_t boardCap) {
    size_t want = freePsram / 3;
    if (want > boardCap) want = boardCap;
    if (want < CAM_RING_MIN) return 0;
    return want;
}

// --- Ring -----------------------------------------------------------------
// Header per frame on the wire: just its length. Readers validate
// len <= bytes-held before touching payload (corrupt/short reads fail shut).
#define CAM_RING_HDR 4u

struct CamRing {
    uint8_t* buf;      // caller-owned storage (PSRAM on device)
    size_t   cap;      // sizeof storage
    size_t   head;     // write offset (next frame header goes here)
    size_t   used;     // bytes held (headers + payloads)
    uint32_t frames;   // complete frames held
    uint32_t dropped;  // frames discarded for lack of room (oldest-first)
    uint32_t pushed;   // lifetime counter (wraps, diagnostics only)
};

static inline void cameraRingInit(CamRing* r, uint8_t* storage, size_t cap) {
    if (!r) return;
    r->buf = storage;
    r->cap = (storage && cap) ? cap : 0;
    r->head = 0;
    r->used = 0;
    r->frames = 0;
    r->dropped = 0;
    r->pushed = 0;
}

static inline size_t cameraRingFree(const CamRing* r) {
    if (!r || !r->buf || !r->cap) return 0;
    return r->cap - r->used;
}

// Tail offset (oldest frame header), derived — nothing extra to keep in sync.
static inline size_t cameraRingTail(const CamRing* r) {
    if (!r || !r->used) return 0;
    return (r->head + r->cap - r->used) % r->cap;
}

// Read one header, handling the wrap. False when fewer than 4 bytes are held.
static inline bool cameraRingPeekLen(const CamRing* r, size_t at, uint32_t* outLen) {
    if (!r || !r->buf || !r->cap || !outLen) return false;
    if (r->used < CAM_RING_HDR) return false;
    uint8_t h[4];
    for (unsigned i = 0; i < 4; i++) h[i] = r->buf[(at + i) % r->cap];
    uint32_t len = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
                   ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
    if (len == 0 || len > r->cap - CAM_RING_HDR) return false;  // corrupt/short
    *outLen = len;
    return true;
}

// Drop the oldest complete frame. False when empty or corrupt.
static inline bool cameraRingDropOldest(CamRing* r) {
    if (!r || !r->buf || !r->cap || !r->frames || !r->used) return false;
    uint32_t len = 0;
    if (!cameraRingPeekLen(r, cameraRingTail(r), &len)) return false;
    size_t rec = CAM_RING_HDR + len;
    if (rec > r->used) return false;
    r->used -= rec;
    r->frames--;
    r->dropped++;
    return true;
}

// Push one frame. Makes room by dropping oldest-first; fails only when a
// single frame cannot ever fit (len + 4 > cap) or there is no storage.
static inline bool cameraRingPush(CamRing* r, const uint8_t* data, size_t len) {
    if (!r || !r->buf || !r->cap || !data || !len) return false;
    if (len > r->cap - CAM_RING_HDR) return false;
    size_t need = CAM_RING_HDR + len;
    while (r->cap - r->used < need) {
        if (!cameraRingDropOldest(r)) return false;
    }
    uint8_t h[4] = { (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF),
                     (uint8_t)((len >> 16) & 0xFF), (uint8_t)((len >> 24) & 0xFF) };
    for (unsigned i = 0; i < 4; i++) r->buf[(r->head + i) % r->cap] = h[i];
    size_t at = (r->head + CAM_RING_HDR) % r->cap;
    size_t first = r->cap - at;
    if (first > len) first = len;
    memcpy(r->buf + at, data, first);
    if (len > first) memcpy(r->buf, data + first, len - first);
    r->head = (r->head + need) % r->cap;
    r->used += need;
    r->frames++;
    r->pushed++;
    return true;
}

// Copy the oldest frame out WITHOUT removing it (caller drains + pops after
// a successful SD write). Returns payload length, 0 when empty/corrupt.
static inline size_t cameraRingPeek(const CamRing* r, uint8_t* out, size_t outCap) {
    if (!r || !r->buf || !r->cap || !out || !outCap || !r->frames) return 0;
    uint32_t len = 0;
    if (!cameraRingPeekLen(r, cameraRingTail(r), &len)) return 0;
    if (len > outCap) return 0;
    size_t at = (cameraRingTail(r) + CAM_RING_HDR) % r->cap;
    size_t first = r->cap - at;
    if (first > len) first = len;
    memcpy(out, r->buf + at, first);
    if (len > first) memcpy(out + first, r->buf, len - first);
    return len;
}

// Remove the oldest frame after it was consumed (pairs with Peek).
static inline bool cameraRingPop(CamRing* r) {
    if (!r || !r->buf || !r->cap || !r->frames) return false;
    uint32_t len = 0;
    if (!cameraRingPeekLen(r, cameraRingTail(r), &len)) return false;
    size_t rec = CAM_RING_HDR + len;
    if (rec > r->used) return false;
    r->used -= rec;
    r->frames--;
    return true;
}
