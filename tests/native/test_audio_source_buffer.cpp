#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <vector>
#include <cstdio>

// Compiles the production first-party AudioSourceBuffer against a mock source.
#include "audio_source_buffer.h"

// In-memory source: exposes a fixed byte array through the AudioSource API.
// Optional read failures simulate a network hiccup / EOF for the refill path.
class MockSource : public AudioSource {
public:
    MockSource(const std::vector<uint8_t>& data, uint32_t chunk = 0)
        : m_data(data), m_pos(0), m_closed(false), m_readNonBlockUsed(0), m_chunk(chunk) {
    }

    virtual bool isOpen() override { return !m_closed; }
    virtual uint32_t getSize() override { return (uint32_t)m_data.size(); }
    virtual uint32_t getPos() override { return m_pos; }

    virtual uint32_t read(void* data, uint32_t len) override {
        if (m_closed || m_pos >= m_data.size()) return 0;
        uint32_t avail = (uint32_t)m_data.size() - m_pos;
        if (len > avail) len = avail;
        if (m_chunk && len > m_chunk) len = m_chunk;
        memcpy(data, &m_data[m_pos], len);
        m_pos += len;
        return len;
    }

    virtual uint32_t readNonBlock(void* data, uint32_t len) override {
        m_readNonBlockUsed++;
        return read(data, len);
    }

    virtual bool seek(int32_t pos, int dir) override {
        int32_t target;
        if (dir == SEEK_SET) target = pos;
        else if (dir == SEEK_CUR) target = (int32_t)m_pos + pos;
        else target = (int32_t)m_data.size() + pos;
        if (target < 0 || (uint32_t)target > m_data.size()) return false;
        m_pos = (uint32_t)target;
        return true;
    }

    virtual bool close() override { m_closed = true; return true; }

    uint32_t m_pos;
    bool m_closed;
    uint32_t m_readNonBlockUsed;

private:
    std::vector<uint8_t> m_data;
    uint32_t m_chunk;
};

static std::vector<uint8_t> makeData(uint32_t n) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + 3);
    return v;
}

TEST_CASE("AudioSourceBuffer: prebuffer + small reads") {
    std::vector<uint8_t> data = makeData(4096);
    MockSource src(data, 1024); // source delivers in 1 KB chunks
    uint8_t* pbuf = new uint8_t[2048];
    {
        AudioSourceBuffer buf(&src, pbuf, 2048);

        // First read must block until the whole 2 KB buffer is filled.
        uint8_t out[128];
        uint32_t got = buf.read(out, sizeof(out));
        CHECK(got == 128);
        for (size_t i = 0; i < 128; i++) CHECK(out[i] == data[i]);

        // Second read served from the buffer, still contiguous data.
        uint8_t out2[128];
        got = buf.read(out2, sizeof(out2));
        CHECK(got == 128);
        for (size_t i = 0; i < 128; i++) CHECK(out2[i] == data[128 + i]);

        // Buffer is topped up opportunistically by fill(): stays near-full
        // (buffSize-1 safety gap) even after the caller's reads.
        CHECK(buf.getFillLevel() >= 2048 - 128);
        // Wrapped data is still served correctly after the top-up.
        uint8_t out3[128];
        got = buf.read(out3, sizeof(out3));
        CHECK(got == 128);
        for (size_t i = 0; i < 128; i++) CHECK(out3[i] == data[256 + i]);
    }
    delete[] pbuf;
}

TEST_CASE("AudioSourceBuffer: read larger than buffer underflows to source") {
    std::vector<uint8_t> data = makeData(4096);
    MockSource src(data, 4096); // whole file in one read
    uint8_t* pbuf = new uint8_t[512];
    {
        AudioSourceBuffer buf(&src, pbuf, 512);

        // A request bigger than the 512 B buffer drains the buffer, then reads
        // the remainder directly from the source and forces a full refill.
        uint8_t out[1200];
        uint32_t got = buf.read(out, sizeof(out));
        CHECK(got == 1200);
        for (size_t i = 0; i < 1200; i++) CHECK(out[i] == data[i]);

        // After the large read, fill() tops the buffer back up to the full 512 B.
        CHECK(buf.getFillLevel() == 512);

        // Next read triggers a fresh prebuffer of 512 B from pos 1200.
        uint8_t out2[64];
        got = buf.read(out2, sizeof(out2));
        CHECK(got == 64);
        for (size_t i = 0; i < 64; i++) CHECK(out2[i] == data[1200 + i]);
    }
    delete[] pbuf;
}

TEST_CASE("AudioSourceBuffer: wrap-around serves across the buffer end") {
    // Craft a buffer where consumed bytes push readPtr past the end so the
    // remaining data wraps: prebuffer 64 B, consume 40 (readPtr=40), then
    // consume 40 more -> the first 24 come from tail, last 16 wrap to front.
    std::vector<uint8_t> data = makeData(256);
    MockSource src(data, 256);
    uint8_t* pbuf = new uint8_t[64];
    {
        AudioSourceBuffer buf(&src, pbuf, 64);

        uint8_t out[40];
        CHECK(buf.read(out, 40) == 40);
        for (int i = 0; i < 40; i++) CHECK(out[i] == data[i]);

        // Consume more than the tail: 64-40=24 tail bytes, then 16 wrapped.
        uint8_t out2[40];
        CHECK(buf.read(out2, 40) == 40);
        for (int i = 0; i < 40; i++) CHECK(out2[i] == data[40 + i]);

        // Buffer is topped back up, leaving a 1-byte safety gap.
        CHECK(buf.getFillLevel() == 63);
    }
    delete[] pbuf;
}

TEST_CASE("AudioSourceBuffer: seek within buffered data and passthrough") {
    std::vector<uint8_t> data = makeData(1024);
    MockSource src(data, 1024);
    uint8_t* pbuf = new uint8_t[256];
    {
        AudioSourceBuffer buf(&src, pbuf, 256);
        uint8_t out[64];
        CHECK(buf.read(out, 64) == 64);

        // Seek forward 32 bytes within the buffered region (readPtr 64..128).
        CHECK(buf.seek(32, SEEK_CUR));
        uint8_t out2[16];
        CHECK(buf.read(out2, 16) == 16);
        for (int i = 0; i < 16; i++) CHECK(out2[i] == data[64 + 32 + i]);

        // Seek to the source start (outside buffer) -> invalidates + forwards.
        CHECK(buf.seek(0, SEEK_SET));
        uint8_t out3[16];
        CHECK(buf.read(out3, 16) == 16);
        for (int i = 0; i < 16; i++) CHECK(out3[i] == data[0 + i]);
    }
    delete[] pbuf;
}

TEST_CASE("AudioSourceBuffer: EOF returns 0 and getSize/getPos passthrough") {
    std::vector<uint8_t> data = makeData(64);
    MockSource src(data, 64);
    uint8_t* pbuf = new uint8_t[256];
    {
        AudioSourceBuffer buf(&src, pbuf, 256);
        CHECK(buf.getSize() == 64);
        CHECK(buf.isOpen());

        uint8_t out[128];
        CHECK(buf.read(out, 128) == 64);   // only 64 bytes available
        CHECK(buf.getPos() == 64);

        // Everything consumed -> next read is 0 (EOF).
        CHECK(buf.read(out, 128) == 0);

        src.m_closed = true;
        CHECK(!buf.isOpen());
    }
    delete[] pbuf;
}
