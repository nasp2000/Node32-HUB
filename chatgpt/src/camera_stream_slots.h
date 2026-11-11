#pragma once
// camera_stream_slots.h — MJPEG frame ring (pure selection logic).
//
// v1.120 kept ONE frame buffer for all viewers and only republished it while
// no viewer held a pin. A single slow client therefore froze every other
// viewer, and a second viewer (PAGES/camera.md CAM-75) measured 0.5-1 fps with
// freeHeap 37-43 KB / maxAlloc 2-4 KB and ended in a panic. The fix is a small
// ring: the writer publishes into the next free slot, each viewer pins the
// newest slot it has not sent yet, and a pinned slot is never overwritten.
//
// Header-only and allocation-free so the decisions are unit-tested on Linux
// (tests/native/test_camera_stream_slots.cpp); camera_manager.cpp owns the
// PSRAM and the mutex.

#include <stdint.h>
#include <stddef.h>

// Slots in the ring. 3 = one being written + one per viewer (max 2) + slack;
// each slot costs one full JPEG buffer in PSRAM.
#define CAM_STREAM_SLOTS 3

// Pick the slot the writer should publish into. `refs[i] > 0` means a viewer
// is still sending from that slot, so it must not be overwritten. Round-robin
// from `cursor` (the last slot used) so consecutive frames land in different
// slots and a reader still sending slot N sees N+1 published, not N clobbered.
// Returns -1 when every slot is pinned (caller drops the frame: newest wins is
// not worth stalling the capture pump for).
static inline int camStreamPickWriteSlot(const int* refs, int n, int cursor) {
    if (!refs || n <= 0) return -1;
    if (cursor < 0 || cursor >= n) cursor = n - 1;
    for (int k = 1; k <= n; k++) {
        int i = (cursor + k) % n;
        if (refs[i] <= 0) return i;
    }
    return -1;
}

// Pick the newest slot strictly newer than `afterGen` (0 = any). Returns -1
// when nothing new was published since the viewer last sent that frame — the
// caller then re-sends what it already holds to keep the ACK clock alive.
// A slot may be shared by several viewers (read-only); the refcount is what
// stops the writer, not the reader.
static inline int camStreamPickNewest(const uint32_t* gens, int n, uint32_t afterGen) {
    if (!gens || n <= 0) return -1;
    int best = -1;
    uint32_t bestGen = afterGen;
    for (int i = 0; i < n; i++) {
        if (gens[i] != 0 && gens[i] > bestGen) { bestGen = gens[i]; best = i; }
    }
    return best;
}
