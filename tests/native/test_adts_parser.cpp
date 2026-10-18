#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <vector>

// Compiles the production first-party ADTS frame parser header.
#include "adts_parser.h"

// Build a minimal ADTS header for the given config.
// Returns a 7-byte header (no CRC). frameLength includes the 7-byte header.
static std::vector<uint8_t> makeAdtsHeader(uint8_t profileMinus1, uint8_t sfIdx,
                                           uint8_t chanCfg, uint16_t frameLength) {
    std::vector<uint8_t> h(7, 0);
    h[0] = 0xFF;
    h[1] = 0xF1;                                  // sync + layer 00 + no CRC
    h[2] = (uint8_t)((profileMinus1 << 6) | (sfIdx << 2) | ((chanCfg >> 2) & 0x01));
    h[3] = (uint8_t)(((chanCfg & 0x03) << 6) | ((frameLength >> 11) & 0x03));
    h[4] = (uint8_t)((frameLength >> 3) & 0xFF);
    h[5] = (uint8_t)(((frameLength & 0x07) << 5) | 0x1F);  // buffer fullness + 1 full frame
    h[6] = 0xFC;
    return h;
}

TEST_CASE("adts findAndParse: 48kHz stereo AAC-LC header") {
    // AAC-LC = profile 1, sfIdx 3 = 48000, chanCfg 2 = stereo.
    std::vector<uint8_t> h = makeAdtsHeader(1, 3, 2, 512);
    size_t off = 999;
    adts::Frame f = adts::findAndParse(h.data(), h.size(), &off);

    CHECK(f.valid);
    CHECK(off == 0);
    CHECK(f.profile == 2);          // profile field stored as ObjectType (1+1)
    CHECK(f.sampleRate == 48000);
    CHECK(f.channels == 2);
    CHECK(f.headerLen == 7);
    CHECK(f.frameLength == 512);
}

TEST_CASE("adts findAndParse: 44100Hz mono with leading garbage") {
    // sfIdx 4 = 44100, chanCfg 1 = mono, frameLength 300.
    std::vector<uint8_t> payload = { 0x00, 0x11, 0x22, 0x33, 0x44 };
    std::vector<uint8_t> h = makeAdtsHeader(1, 4, 1, 300);
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), payload.begin(), payload.end());
    buf.insert(buf.end(), h.begin(), h.end());

    size_t off = 0;
    adts::Frame f = adts::findAndParse(buf.data(), buf.size(), &off);

    CHECK(f.valid);
    CHECK(off == 5);                // sync found after the garbage
    CHECK(f.sampleRate == 44100);
    CHECK(f.channels == 1);
    CHECK(f.frameLength == 300);
}

TEST_CASE("adts findAndParse: rejects invalid / too-short buffers") {
    size_t off = 0;
    adts::Frame f1 = adts::findAndParse(nullptr, 0, &off);
    CHECK(!f1.valid);

    uint8_t junk[3] = { 0xFF, 0xF0, 0x00 };   // not enough bytes for a header
    adts::Frame f2 = adts::findAndParse(junk, sizeof(junk), &off);
    CHECK(!f2.valid);

    uint8_t badSync[7] = { 0xFE, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00 };  // 0xFE != 0xFF
    adts::Frame f3 = adts::findAndParse(badSync, sizeof(badSync), &off);
    CHECK(!f3.valid);
}

TEST_CASE("adts findAndParse: 8kHz and 96kHz edge rates") {
    // sfIdx 11 = 8000, sfIdx 0 = 96000.
    std::vector<uint8_t> h8 = makeAdtsHeader(1, 11, 2, 64);
    adts::Frame f8 = adts::findAndParse(h8.data(), h8.size(), nullptr);
    CHECK(f8.sampleRate == 8000);

    std::vector<uint8_t> h96 = makeAdtsHeader(1, 0, 2, 128);
    adts::Frame f96 = adts::findAndParse(h96.data(), h96.size(), nullptr);
    CHECK(f96.sampleRate == 96000);
}

TEST_CASE("adts findSync: locates sync across a multi-frame window") {
    std::vector<uint8_t> h = makeAdtsHeader(1, 3, 2, 100);
    std::vector<uint8_t> buf;
    buf.push_back(0x01); buf.push_back(0x02);
    buf.insert(buf.end(), h.begin(), h.end());
    buf.insert(buf.end(), h.begin(), h.end());

    // Header is 7 physical bytes: sync1 at 2, sync2 at 2+7=9.
    size_t s1 = adts::findSync(buf.data(), buf.size());
    CHECK(s1 == 2);

    size_t s2 = adts::findSync(buf.data() + 2, buf.size() - 2);
    CHECK(s2 == 0);

    size_t s3 = adts::findSync(buf.data() + 9, buf.size() - 9);
    CHECK(s3 == 0);

    // No sync in a window without 0xFFF.
    uint8_t noSync[10] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0x00 };
    CHECK(adts::findSync(noSync, sizeof(noSync)) == (size_t)-1);
}
