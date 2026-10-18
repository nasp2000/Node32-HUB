#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <cstdint>
#include <vector>

// Compiles the production ring buffer + producer pump (pure, no ESP32 deps).
#include "audio_ring.h"

namespace {

// Mock producer: hands out bytes from a fixed array, optionally in capped
// slices (simulating SD/network chunking), and returns -1 once exhausted.
struct MockSource {
    const std::vector<uint8_t>* data;
    size_t pos;
    uint32_t sliceCap;   // max bytes per call (0 = unlimited)
    int calls;
    MockSource(const std::vector<uint8_t>* d, uint32_t cap)
        : data(d), pos(0), sliceCap(cap), calls(0) {}
};

int mockRead(void* ctx, uint8_t* buf, uint32_t len) {
    MockSource* m = (MockSource*)ctx;
    m->calls++;
    if (m->pos >= m->data->size()) return -1;   // EOF
    if (m->sliceCap && len > m->sliceCap) len = m->sliceCap;
    size_t remain = m->data->size() - m->pos;
    if (len > remain) len = (uint32_t)remain;
    memcpy(buf, &(*m->data)[m->pos], len);
    m->pos += len;
    return (int)len;
}

std::vector<uint8_t> makeData(uint32_t n) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 31 + 7);
    return v;
}

} // namespace

TEST_CASE("ring buffer empty/full accounting") {
    uint8_t mem[8];
    RingBuffer r;
    r.init(mem, 8);
    CHECK(r.cap() == 8);
    CHECK(r.used() == 0);
    CHECK(r.space() == 7);   // one slot reserved to tell full from empty

    uint8_t in[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(r.write(in, 7) == 7);
    CHECK(r.used() == 7);
    CHECK(r.space() == 0);

    // Full: write returns 0 and the buffer is unchanged.
    CHECK(r.write(in, 3) == 0);

    uint8_t out[8] = { 0 };
    CHECK(r.read(out, 4) == 4);
    CHECK(r.used() == 3);
    CHECK(r.space() == 4);
    CHECK(memcmp(out, in, 4) == 0);

    // Read on empty returns 0.
    uint8_t tmp[8] = { 0 };
    CHECK(r.read(tmp, 8) == 3);   // the remaining 3
    CHECK(r.used() == 0);
    CHECK(r.read(tmp, 8) == 0);
}

TEST_CASE("ring buffer wrap-around keeps byte order") {
    // Cap 8: write 6, read 5 -> next write wraps past the end.
    uint8_t mem[8];
    RingBuffer r;
    r.init(mem, 8);
    uint8_t a[6] = { 10, 11, 12, 13, 14, 15 };
    CHECK(r.write(a, 6) == 6);
    uint8_t b[5];
    CHECK(r.read(b, 5) == 5);
    CHECK(memcmp(b, a, 5) == 0);

    // Now write 6 bytes, wrapping the write pointer across the end of the
    // buffer (used=1, so space=6 — one slot stays reserved). FIFO order must
    // hold: the leftover 15 comes out first, then the new 20..25.
    uint8_t c[6] = { 20, 21, 22, 23, 24, 25 };
    CHECK(r.write(c, 6) == 6);
    uint8_t out[7];
    CHECK(r.read(out, 7) == 7);
    uint8_t expect[7] = { 15, 20, 21, 22, 23, 24, 25 };
    CHECK(memcmp(out, expect, 7) == 0);
    CHECK(r.used() == 0);
}

TEST_CASE("ring buffer writeSpan/commitWrite direct-fill path") {
    uint8_t mem[8];
    RingBuffer r;
    r.init(mem, 8);
    uint8_t first[3] = { 1, 2, 3 };
    CHECK(r.write(first, 3) == 3);
    uint8_t drain[2];
    CHECK(r.read(drain, 2) == 2);   // used=1, write index now at 3

    // Direct fill through the contiguous free region.
    CHECK(r.writeSpan() == 5);       // 4 tail bytes (idx3..6) + 1 head byte (idx0)
    memcpy(r.raw() + r.writeIndex(), "ABCDE", 5);
    r.commitWrite(5);
    CHECK(r.used() == 6);

    uint8_t out[6];
    CHECK(r.read(out, 6) == 6);
    CHECK(memcmp(out, "\x03" "ABCDE", 6) == 0);   // leftover 3 then ABCDE
}

TEST_CASE("ringFillFrom pumps the whole source exactly once") {
    const uint32_t kData = 100000;   // larger than any ring below
    std::vector<uint8_t> data = makeData(kData);
    for (uint32_t capPow2 : { (uint32_t)256, (uint32_t)4096 }) {
        uint8_t* mem = new uint8_t[capPow2];
        RingBuffer r;
        r.init(mem, capPow2);
        MockSource src(&data, 0);   // unlimited slices
        int eof = 0;

        // Mirror the real consumer: drain whatever is buffered, then let the
        // fill task top up — until EOF and the ring is empty.
        std::vector<uint8_t> got;
        got.reserve(kData);
        uint8_t buf[512];
        while (!eof || r.used() > 0) {
            uint32_t rd = r.read(buf, sizeof(buf));
            if (rd > 0) got.insert(got.end(), buf, buf + rd);
            if (!eof) ringFillFrom(r, mockRead, &src, 16 * 1024, 0, &eof);
        }
        CHECK(eof == 1);
        CHECK(src.pos == kData);
        CHECK(got.size() == kData);
        CHECK(got == data);
        delete[] mem;
    }
}

TEST_CASE("ringFillFrom chunks, short reads and maxTotal stop early") {
    const std::vector<uint8_t> data = makeData(4096);
    uint8_t mem[1024];
    RingBuffer r;
    r.init(mem, 1024);

    // maxTotal caps the fill (preload-style).
    MockSource a(&data, 0);
    int eof = 0;
    uint32_t n = ringFillFrom(r, mockRead, &a, 1024, 300, &eof);
    CHECK(n == 300);
    CHECK(eof == 0);
    CHECK(r.used() == 300);

    // maxChunk limits each producer call (mutex-held-for-short-burst style).
    MockSource b(&data, 0);
    RingBuffer r2;
    r2.init(mem, 1024);
    uint32_t n2 = ringFillFrom(r2, mockRead, &b, 64, 0, &eof);
    CHECK(n2 == 1023);   // ring full (cap-1)
    CHECK(b.calls >= (int)(1023 / 64));   // many small calls, not one big one

    // EOF mid-pump stops cleanly and reports it.
    std::vector<uint8_t> small = makeData(50);
    MockSource c(&small, 7);   // 7-byte slices -> short reads too
    RingBuffer r3;
    r3.init(mem, 1024);
    int eof3 = 0;
    uint32_t n3 = ringFillFrom(r3, mockRead, &c, 1024, 0, &eof3);
    CHECK(n3 == 50);
    CHECK(eof3 == 1);
    CHECK(c.pos == 50);
}
