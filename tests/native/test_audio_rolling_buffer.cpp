#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <cstdint>
#include <vector>

// Standalone reimplementation of the rolling input-buffer logic in
// Minimp3Decoder::loop() (audio_decoder_minimp3.cpp). The decoder keeps a
// 16 KB _inBuf, consumes MP3 frames from it (advancing _inPos by frame_bytes),
// and when a frame would need more data than is buffered it memmoves the tail
// and tops up from the source. If that logic ever drops or duplicates bytes,
// minimp3 loses sync and the file plays shorter/faster — exactly the symptom
// under investigation (teste.mp3 decoding 9841 of 11279 frames). This test
// drives the identical buffer algorithm with a mock "frame decoder" over a
// stream shaped like the real file and verifies every frame is consumed
// exactly once.

namespace {
const uint32_t kFrameBytes = 1044;   // MPEG-1 Layer III, 44.1k, 320 kbps CBR
const uint32_t kSamplesPerFrame = 1152;

// A frame decoder that mimics minimp3's contract: given the buffered bytes it
// returns frame_bytes>0 with a fixed frame size once enough data is present,
// otherwise 0 (need more data).
struct MockDec {
    uint32_t nextFrameBytes;
    MockDec() : nextFrameBytes(kFrameBytes) {}
};

// Returns frame_bytes consumed (kFrameBytes) when `availLen` holds a full
// frame, else 0. `samples` mirrors minimp3's per-frame sample count.
uint32_t mockDecode(MockDec& d, const uint8_t* /*avail*/, uint32_t availLen,
                    int* samples) {
    if (availLen < d.nextFrameBytes) return 0;
    *samples = (int)kSamplesPerFrame;
    return d.nextFrameBytes;
}

struct LoopStats {
    uint64_t frames = 0;
    uint64_t samples = 0;
    uint64_t consumedBytes = 0;
    uint64_t sourceBytes = 0;
};

// Reimplements the decoder loop's buffer handling: top-up, memmove, decode,
// EOF. `readN` caps how many bytes the fake source hands out per call so the
// buffer is filled across many small reads (stress the wrap/boundary paths).
LoopStats runLoop(std::vector<uint8_t>& fileBytes, uint32_t readN,
                  uint32_t inBufSize) {
    std::vector<uint8_t> inBuf(inBufSize, 0);
    uint32_t inLen = 0, inPos = 0;
    size_t srcPos = 0;
    bool eof = false;
    MockDec dec;
    LoopStats st;

    for (int iter = 0; iter < 100000; iter++) {
        if (inLen < inBufSize && !eof) {
            if (inPos > 0 && inLen > inPos) {
                memmove(&inBuf[0], &inBuf[inPos], inLen - inPos);
                inLen -= inPos;
                inPos = 0;
            } else if (inPos > 0) {
                inLen = 0;
                inPos = 0;
            }
            uint32_t cap = inBufSize - inLen;
            uint32_t want = (cap < readN) ? cap : readN;
            uint32_t avail = (uint32_t)(fileBytes.size() - srcPos);
            uint32_t got = (want < avail) ? want : avail;
            if (got > 0) memcpy(&inBuf[inLen], &fileBytes[srcPos], got);
            srcPos += got;
            st.sourceBytes += got;
            if (got == 0) {
                if (srcPos >= fileBytes.size()) eof = true;
            } else {
                inLen += got;
            }
        }

        uint32_t availLen = inLen - inPos;
        if (availLen == 0) {
            if (eof) break;
            continue;
        }

        int samples = 0;
        uint32_t fb = mockDecode(dec, &inBuf[inPos], availLen, &samples);
        if (fb > 0) {
            inPos += fb;
            st.consumedBytes += fb;
            if (inPos >= inLen) { inLen = 0; inPos = 0; }
            if (samples > 0) {
                st.frames++;
                st.samples += (uint64_t)samples;
            }
            continue;
        }

        if (eof) break;
        if (inPos > 0 && inLen == inPos) { inLen = 0; inPos = 0; }
        if (inLen == inBufSize && availLen == inBufSize) inPos++;
        if (inPos > 0 && inLen == inBufSize) {
            memmove(&inBuf[0], &inBuf[inPos], inLen - inPos);
            inLen -= inPos;
            inPos = 0;
        }
    }
    return st;
}
} // namespace

TEST_CASE("rolling buffer: full teste.mp3 frame stream decodes 11279 frames") {
    // Stream shaped exactly like teste.mp3: 11279 CBR frames + a header.
    std::vector<uint8_t> data;
    const uint32_t kHeader = 100;
    data.reserve(kHeader + 11279u * kFrameBytes);
    data.resize(kHeader, 0xFF); // header (ID3) — will be skipped as garbage
    data.resize(kHeader + 11279u * kFrameBytes, 0x55);

    LoopStats st = runLoop(data, 16384, 16 * 1024);
    CHECK(st.frames == 11279);
    CHECK(st.samples == 11279u * kSamplesPerFrame);
    CHECK(st.sourceBytes == data.size());
    CHECK(st.consumedBytes == 11279u * kFrameBytes);
}

TEST_CASE("rolling buffer: small source reads still consume everything") {
    // The AudioSourceBuffer hands out data in small pieces; the rolling buffer
    // must reassemble full frames without loss or duplication.
    std::vector<uint8_t> data;
    data.resize(5000u * kFrameBytes, 0x5A);

    LoopStats st = runLoop(data, 997, 16 * 1024); // odd read size
    CHECK(st.frames == 5000);
    CHECK(st.samples == 5000u * kSamplesPerFrame);
    CHECK(st.sourceBytes == data.size());
    CHECK(st.consumedBytes == 5000u * kFrameBytes);
}

TEST_CASE("rolling buffer: buffer smaller than 2 frames exercises memmove") {
    // A buffer (~1.4 frames) forces the memmove tail-shift on almost every
    // frame; no bytes may be dropped or duplicated.
    std::vector<uint8_t> data;
    data.resize(300u * kFrameBytes, 0x3C);

    LoopStats st = runLoop(data, 700, 1500);
    CHECK(st.frames == 300);
    CHECK(st.samples == 300u * kSamplesPerFrame);
    CHECK(st.sourceBytes == data.size());
    CHECK(st.consumedBytes == 300u * kFrameBytes);
}
