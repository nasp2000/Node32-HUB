#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

// Compiles the production ICY demuxer (pure, no ESP32 deps).
#include "icy_demux.h"

using namespace icy_demux;

// Build a full ICY stream: [metaint audio][meta block][metaint audio][meta block]
// per the ICY spec (the interleave inserts a length byte + block AFTER each
// metaint run of audio; the stream starts with audio).
static std::vector<uint8_t> buildStream(uint32_t metaint, const char* title1, const char* title2) {
    std::vector<uint8_t> out;
    char blocks[2][512];
    size_t lens[2] = {0, 0};
    snprintf(blocks[0], sizeof(blocks[0]), "StreamTitle='%s';", title1 ? title1 : "");
    snprintf(blocks[1], sizeof(blocks[1]), "StreamTitle='%s';", title2 ? title2 : "");
    lens[0] = strlen(blocks[0]);
    lens[1] = strlen(blocks[1]);

    // Pad each metadata block to a multiple of 16 (length byte = bytes/16).
    auto appendAudio = [&](uint8_t fill) {
        for (size_t i = 0; i < metaint; i++) out.push_back(fill);
    };
    auto appendBlock = [&](int idx) {
        size_t padded = (lens[idx] + 15) & ~(size_t)15;
        uint8_t lenByte = (uint8_t)(padded / 16);
        out.push_back(lenByte);
        for (size_t i = 0; i < padded; i++) out.push_back((uint8_t)blocks[idx][i]);
    };

    appendAudio(0x11);
    appendBlock(0);
    appendAudio(0x22);
    appendBlock(1);
    return out;
}

// Feed a stream through the demux in `chunk`-sized slices, collecting audio.
// chunk==0 feeds one 512-byte slice at a time, mirroring the production ring
// (fillFromSocket reads the socket in ~512-byte chunks).
static std::vector<uint8_t> demuxStream(const std::vector<uint8_t>& raw, uint32_t metaint,
                                        size_t chunk, std::vector<std::string>* titles) {
    Demux d;
    d.reset(metaint);
    std::vector<uint8_t> audio;
    size_t i = 0;
    while (i < raw.size()) {
        size_t n = (chunk == 0) ? 512 : chunk;
        if (n > raw.size() - i) n = raw.size() - i;
        uint8_t in[512];
        uint8_t out[512];
        char title[256];
        memcpy(in, &raw[i], n);
        bool metaSeen = false;
        uint32_t a = d.feed(in, n, out, sizeof(out), title, sizeof(title), &metaSeen);
        audio.insert(audio.end(), out, out + a);
        if (metaSeen) titles->push_back(std::string(title));
        i += n;
    }
    return audio;
}

TEST_CASE("icy_demux parseTitle extracts Artist - Title and bounds") {
    char out[256];
    const uint8_t block[] = "StreamTitle='AC/DC - Thunderstruck';StreamUrl='x';";
    CHECK(parseTitle(block, sizeof(block) - 1, out, sizeof(out)) > 0);
    CHECK(std::string(out) == "AC/DC - Thunderstruck");

    // No StreamTitle in the block -> 0 and empty output.
    const uint8_t block2[] = "StreamUrl='only';";
    CHECK(parseTitle(block2, sizeof(block2) - 1, out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');

    // Small output buffer truncates safely and stays NUL-terminated.
    const uint8_t block3[] = "StreamTitle='A very long song name';";
    char tiny[10];
    int n = parseTitle(block3, sizeof(block3) - 1, tiny, sizeof(tiny));
    CHECK(n == 9);
    CHECK(tiny[9] == '\0');
    CHECK(std::string(tiny) == "A very lo");
}

TEST_CASE("icy_demux passthrough with metaint=0 returns every byte as audio") {
    uint8_t in[40];
    for (size_t i = 0; i < sizeof(in); i++) in[i] = (uint8_t)(i * 7);
    Demux d;
    d.reset(0);   // no metadata
    CHECK(!d.active());
    uint8_t out[40];
    bool seen = false;
    char title[64];
    uint32_t a = d.feed(in, sizeof(in), out, sizeof(out), title, sizeof(title), &seen);
    CHECK(a == sizeof(in));
    CHECK(memcmp(in, out, sizeof(in)) == 0);
    CHECK(!seen);
}

TEST_CASE("icy_demux strips metadata, keeps only audio, surfaces both titles across slices") {
    uint32_t metaint = 32;
    std::vector<uint8_t> raw = buildStream(metaint, "AC/DC - Thunderstruck", "Queen - Radio Ga Ga");
    std::vector<std::string> titles;
    // Small slices: each feed() call completes at most one block -> both titles surface.
    std::vector<uint8_t> audio = demuxStream(raw, metaint, 10, &titles);
    // 2 audio runs of 32 bytes each: [0x11 x32][0x22 x32]
    CHECK(audio.size() == 2 * metaint);
    CHECK(audio[0] == 0x11);
    CHECK(audio[31] == 0x11);
    CHECK(audio[32] == 0x22);
    CHECK(audio[63] == 0x22);
    CHECK(titles.size() == 2);
    CHECK(titles[0] == "AC/DC - Thunderstruck");
    CHECK(titles[1] == "Queen - Radio Ga Ga");
}

TEST_CASE("icy_demux one large slice surfaces only the last block (ring consumer contract)") {
    uint32_t metaint = 32;
    std::vector<uint8_t> raw = buildStream(metaint, "Song A", "Song B");
    std::vector<std::string> titles;
    // chunk=0 -> one 512-byte slice holds the whole stream (both blocks), so the
    // reported title is the LAST one (the current song), matching the header doc.
    std::vector<uint8_t> audio = demuxStream(raw, metaint, 0, &titles);
    CHECK(audio.size() == 2 * metaint);
    CHECK(audio[0] == 0x11);
    CHECK(audio[63] == 0x22);
    CHECK(titles.size() == 1);
    CHECK(titles[0] == "Song B");
}

TEST_CASE("icy_demux handles chunk boundaries straddling a metadata block") {
    uint32_t metaint = 64;
    std::vector<uint8_t> raw = buildStream(metaint, "Song One", "Song Two");
    // Feed in 10-byte slices so block length byte, block body and audio all
    // straddle feed() boundaries.
    std::vector<std::string> titles;
    std::vector<uint8_t> audio = demuxStream(raw, metaint, 10, &titles);
    CHECK(audio.size() == 2 * metaint);
    CHECK(titles.size() == 2);
    CHECK(titles[0] == "Song One");
    CHECK(titles[1] == "Song Two");
}

TEST_CASE("icy_demux zero-length metadata block advances without a title") {
    // [metaint audio][0x00 lenByte][metaint audio][StreamTitle block]
    uint32_t metaint = 16;
    std::vector<uint8_t> raw;
    for (size_t i = 0; i < metaint; i++) raw.push_back(0xAA);
    raw.push_back(0); // empty metadata block
    char block[64];
    snprintf(block, sizeof(block), "StreamTitle='Band - Track';");
    size_t tlen = strlen(block);
    size_t padded = (tlen + 15) & ~(size_t)15;
    for (size_t i = 0; i < metaint; i++) raw.push_back(0xBB);
    raw.push_back((uint8_t)(padded / 16));
    for (size_t i = 0; i < padded; i++) raw.push_back((uint8_t)block[i]);

    Demux d;
    d.reset(metaint);
    uint8_t out[512];
    char titleBuf[256];
    bool seen = false;
    std::vector<uint8_t> audio;
    size_t i = 0;
    while (i < raw.size()) {
        size_t n = raw.size() - i;
        if (n > 512) n = 512;
        uint32_t a = d.feed(&raw[i], n, out, sizeof(out), titleBuf, sizeof(titleBuf), &seen);
        audio.insert(audio.end(), out, out + a);
        if (seen) { CHECK(std::string(titleBuf) == "Band - Track"); seen = false; }
        i += n;
    }
    CHECK(audio.size() == 2 * metaint);
    CHECK(audio[0] == 0xAA);
    CHECK(audio[metaint - 1] == 0xAA);
    CHECK(audio[metaint] == 0xBB);
    CHECK(audio[2 * metaint - 1] == 0xBB);
}
