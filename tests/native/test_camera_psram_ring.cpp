#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>
#include <cstdint>
#include <vector>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_psram_ring.h is the pure shock-absorber behind the camera recorder:
// frames land here when the SD is slower than the capture pump.
#include "../../chatgpt/src/camera_psram_ring.h"

static std::vector<uint8_t> frameBytes(uint8_t seed, size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(seed + i);
    return v;
}

TEST_CASE("ring sizing: board caps, free/3 rule, 256KB floor") {
    CHECK(cameraRingPickSize(48UL * 1024 * 1024, CAM_RING_CAP_P4) == 12UL * 1024 * 1024);  // capped P4
    CHECK(cameraRingPickSize(9UL * 1024 * 1024, CAM_RING_CAP_P4) == 3UL * 1024 * 1024);    // free/3
    CHECK(cameraRingPickSize(6UL * 1024 * 1024, CAM_RING_CAP_S3) == 2UL * 1024 * 1024);    // capped S3
    CHECK(cameraRingPickSize(3UL * 1024 * 1024, CAM_RING_CAP_S3) == 1UL * 1024 * 1024);    // S3 typical
    CHECK(cameraRingPickSize(700UL * 1024, CAM_RING_CAP_S3) == 0);                        // below floor
    CHECK(cameraRingPickSize(0, CAM_RING_CAP_P4) == 0);
}

TEST_CASE("ring push/peek/pop round-trips frames in order") {
    uint8_t mem[4096];
    CamRing r;
    cameraRingInit(&r, mem, sizeof(mem));
    CHECK(cameraRingFree(&r) == sizeof(mem));
    std::vector<uint8_t> a = frameBytes(1, 100), b = frameBytes(2, 200);
    CHECK(cameraRingPush(&r, a.data(), a.size()));
    CHECK(cameraRingPush(&r, b.data(), b.size()));
    CHECK(r.frames == 2);
    uint8_t out[512];
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 100);   // peek does not remove
    CHECK(r.frames == 2);
    CHECK(memcmp(out, a.data(), 100) == 0);
    CHECK(cameraRingPop(&r));
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 200);   // then the second
    CHECK(memcmp(out, b.data(), 200) == 0);
    CHECK(cameraRingPop(&r));
    CHECK(r.frames == 0);
    CHECK(cameraRingFree(&r) == sizeof(mem));
    CHECK(cameraRingPop(&r) == false);                    // empty pops fail
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 0);
}

TEST_CASE("ring overflow drops oldest-first, newest data wins") {
    uint8_t mem[256];
    CamRing r;
    cameraRingInit(&r, mem, sizeof(mem));
    std::vector<uint8_t> a = frameBytes(1, 100), b = frameBytes(2, 100), c = frameBytes(3, 100);
    CHECK(cameraRingPush(&r, a.data(), a.size()));
    CHECK(cameraRingPush(&r, b.data(), b.size()));   // 208/256 used
    CHECK(cameraRingPush(&r, c.data(), c.size()));   // forces one drop
    CHECK(r.dropped == 1);
    CHECK(r.frames == 2);
    uint8_t out[256];
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 100);
    CHECK(memcmp(out, b.data(), 100) == 0);          // oldest (a) is gone
    CHECK(cameraRingPop(&r));
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 100);
    CHECK(memcmp(out, c.data(), 100) == 0);
}

TEST_CASE("ring wrap: headers and payloads split across the end survive") {
    uint8_t mem[64];
    CamRing r;
    cameraRingInit(&r, mem, sizeof(mem));
    std::vector<uint8_t> pad = frameBytes(9, 40);    // 44 bytes: head at 44
    std::vector<uint8_t> v = frameBytes(7, 16);      // 20 bytes: header splits at 60
    CHECK(cameraRingPush(&r, pad.data(), pad.size()));
    CHECK(cameraRingPop(&r));                         // head now at 44, empty
    CHECK(cameraRingPush(&r, v.data(), v.size()));    // header 44..47, wraps payload
    uint8_t out[64];
    CHECK(cameraRingPeek(&r, out, sizeof(out)) == 16);
    CHECK(memcmp(out, v.data(), 16) == 0);
}

TEST_CASE("ring rejects: oversize single frame, null storage, short dest") {
    uint8_t mem[64];
    CamRing r;
    cameraRingInit(&r, mem, sizeof(mem));
    std::vector<uint8_t> big = frameBytes(1, 100);
    CHECK_FALSE(cameraRingPush(&r, big.data(), big.size()));  // 104 > 64
    CHECK(r.frames == 0);
    CamRing dead;
    cameraRingInit(&dead, nullptr, 0);
    CHECK(cameraRingFree(&dead) == 0);
    CHECK_FALSE(cameraRingPush(&dead, big.data(), 10));
    std::vector<uint8_t> ok = frameBytes(5, 50);
    CHECK(cameraRingPush(&r, ok.data(), ok.size()));
    uint8_t tiny[8];
    CHECK(cameraRingPeek(&r, tiny, sizeof(tiny)) == 0);       // dest too short
    CHECK(r.frames == 1);                                      // still held
}
