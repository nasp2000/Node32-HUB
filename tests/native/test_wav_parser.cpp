#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstring>
#include <vector>

// Compiles the production first-party WAV header parser.
#include "wav_parser.h"

static void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
}

// Canonical 44-byte PCM WAV header.
static std::vector<uint8_t> makeWavHeader(uint32_t rate, uint16_t channels,
                                          uint16_t bits, uint32_t dataBytes) {
    std::vector<uint8_t> b;
    b.insert(b.end(), { 'R', 'I', 'F', 'F' });
    putU32(b, 36 + dataBytes);
    b.insert(b.end(), { 'W', 'A', 'V', 'E' });
    b.insert(b.end(), { 'f', 'm', 't', ' ' });
    putU32(b, 16);
    putU16(b, 1);           // PCM
    putU16(b, channels);
    putU32(b, rate);
    putU32(b, rate * channels * bits / 8);  // byte rate
    putU16(b, (uint16_t)(channels * bits / 8)); // block align
    putU16(b, bits);
    b.insert(b.end(), { 'd', 'a', 't', 'a' });
    putU32(b, dataBytes);
    return b;
}

TEST_CASE("wav parse: canonical 44.1kHz stereo 16-bit") {
    std::vector<uint8_t> h = makeWavHeader(44100, 2, 16, 1000);
    wav::Format f = wav::parse(h.data(), h.size());

    CHECK(f.valid);
    CHECK(f.sampleRate == 44100);
    CHECK(f.channels == 2);
    CHECK(f.bitsPerSample == 16);
    CHECK(f.blockAlign == 4);
    CHECK(f.dataOffset == 44);
    CHECK(f.dataSize == 1000);
}

TEST_CASE("wav parse: 8kHz mono 16-bit") {
    std::vector<uint8_t> h = makeWavHeader(8000, 1, 16, 500);
    wav::Format f = wav::parse(h.data(), h.size());

    CHECK(f.valid);
    CHECK(f.sampleRate == 8000);
    CHECK(f.channels == 1);
    CHECK(f.blockAlign == 2);
    CHECK(f.dataOffset == 44);
    CHECK(f.dataSize == 500);
}

TEST_CASE("wav parse: rejects non-RIFF / non-PCM / truncated") {
    uint8_t junk[64];
    memset(junk, 0x41, sizeof(junk));
    wav::Format f1 = wav::parse(junk, sizeof(junk));
    CHECK(!f1.valid);

    // Valid RIFF but non-PCM fmt (audioFormat=3 IEEE float).
    std::vector<uint8_t> h = makeWavHeader(44100, 2, 16, 100);
    h[20] = 3; h[21] = 0; // audioFormat field (offset 20-21)
    wav::Format f2 = wav::parse(h.data(), h.size());
    CHECK(!f2.valid);

    // Truncated (< 44 bytes).
    std::vector<uint8_t> shortH(h.begin(), h.begin() + 30);
    wav::Format f3 = wav::parse(shortH.data(), shortH.size());
    CHECK(!f3.valid);

    // Null buffer.
    wav::Format f4 = wav::parse(nullptr, 0);
    CHECK(!f4.valid);
}

TEST_CASE("wav parse: skips LIST chunk before data") {
    // RIFF + fmt + LIST (some metadata) + data.
    std::vector<uint8_t> b;
    b.insert(b.end(), { 'R', 'I', 'F', 'F' });
    putU32(b, 36 + 12 + 40);
    b.insert(b.end(), { 'W', 'A', 'V', 'E' });

    b.insert(b.end(), { 'f', 'm', 't', ' ' });
    putU32(b, 16);
    putU16(b, 1);
    putU16(b, 2);
    putU32(b, 48000);
    putU32(b, 48000 * 4);
    putU16(b, 4);
    putU16(b, 16);

    b.insert(b.end(), { 'L', 'I', 'S', 'T' });
    putU32(b, 10);
    b.insert(b.end(), { 'I', 'N', 'F', 'O', 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 });

    b.insert(b.end(), { 'd', 'a', 't', 'a' });
    putU32(b, 640);

    wav::Format f = wav::parse(b.data(), b.size());
    CHECK(f.valid);
    CHECK(f.sampleRate == 48000);
    CHECK(f.channels == 2);
    CHECK(f.dataOffset == 12 + 8 + 16 + 8 + 10 + 8); // 62 (LIST payload is even, no pad)
    CHECK(f.dataSize == 640);
}
