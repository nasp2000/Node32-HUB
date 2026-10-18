#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <vector>

// Decisive reproduction test for the missing-frames bug. The device decodes
// only 9841 of the 11279 frames of teste.mp3 (11336832 vs 12993408 samples),
// i.e. ~12.7% of the audio is lost even though the whole file is read. This
// test feeds the REAL vendored minimp3 through a faithful copy of the rolling
// 16 KB input-buffer algorithm in Minimp3Decoder::loop() using the REAL bytes
// of teste.mp3. If it decodes 11279 frames the decoder+minimp3 path is sound
// and the bug lives upstream (AudioSourceBuffer / SD). If it reproduces 9841
// the bug is reproduced here and can be debugged off-device.

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> v;
    FILE* f = fopen(path, "rb");
    if (!f) return v;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return v; }
    v.resize((size_t)sz);
    if (fread(v.data(), 1, (size_t)sz, f) != (size_t)sz) v.clear();
    fclose(f);
    return v;
}

struct DecodeResult {
    uint64_t frames = 0;
    uint64_t samples = 0;
    uint64_t consumedBytes = 0;
    uint64_t sourceBytes = 0;
    int rate = 0;
    int ch = 0;
};

// Faithful copy of Minimp3Decoder::loop()'s buffer handling, driven by the
// real minimp3 decoder. `readN` caps per-call source reads; `inBufSize` is the
// rolling buffer size (the device uses 16 KB).
//
// Includes the PARTIAL-FRAME guard that fixes the missing-frames bug: when a
// valid MPEG frame header is buffered but the whole frame isn't available yet,
// the decoder compacts + refills and retries instead of feeding minimp3 a
// truncated buffer (which forces a resync, drops the Layer-III bit reservoir,
// and can lock onto a false sync — silently skipping frames).

static uint32_t mpegFrameSize(const uint8_t* h) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    const int version = (h[1] >> 3) & 3;
    const int layer   = (h[1] >> 1) & 3;
    if (layer == 0) return 0;
    const int brIdx = (h[2] >> 4) & 0xF;
    const int srIdx = (h[2] >> 2) & 3;
    const int pad   = (h[2] >> 1) & 1;
    static const uint16_t kBitrate[2][16] = {
        {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0},
        {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}
    };
    static const uint16_t kRate[3][4] = {
        {11025,12000,8000,0},{22050,24000,16000,0},{44100,48000,32000,0}
    };
    const int br = kBitrate[(version == 3) ? 0 : 1][brIdx];
    const int rate = kRate[(version == 3) ? 2 : ((version == 2) ? 1 : 0)][srIdx];
    if (!br || !rate) return 0;
    if (layer == 3) return (12u * br * 1000u / rate + pad) * 4u;
    const uint32_t mult = (version == 3) ? 144u : 72u;
    return mult * br * 1000u / rate + pad;
}

DecodeResult runDeviceLoop(const std::vector<uint8_t>& file, uint32_t readN,
                           uint32_t inBufSize) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_scratch_t scratch;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    std::vector<uint8_t> inBuf(inBufSize, 0);
    uint32_t inLen = 0, inPos = 0;
    uint32_t id3Remain = 0;
    size_t srcPos = 0;
    bool eof = false;
    DecodeResult r;

    for (int iter = 0; iter < 4000000 && srcPos <= file.size(); iter++) {
        // ---- top-up (mirrors the device) ---------------------------------
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
            uint32_t avail = (uint32_t)(file.size() - srcPos);
            uint32_t got = (want < avail) ? want : avail;
            if (got > 0) memcpy(&inBuf[inLen], &file[srcPos], got);
            srcPos += got;
            r.sourceBytes += got;
            if (got == 0) {
                if (srcPos >= file.size()) eof = true;
            } else {
                inLen += got;
            }
        }

        uint32_t availLen = inLen - inPos;
        const uint8_t* availPtr = &inBuf[inPos];

        // ---- ID3v2 bulk skip (mirrors the device) --------------------------
        if (id3Remain == 0 && r.frames == 0 && r.samples == 0 && availLen >= 10 &&
            availPtr[0]=='I' && availPtr[1]=='D' && availPtr[2]=='3') {
            id3Remain = 10u
                | ((uint32_t)(availPtr[6] & 0x7f) << 21)
                | ((uint32_t)(availPtr[7] & 0x7f) << 14)
                | ((uint32_t)(availPtr[8] & 0x7f) << 7)
                | ((uint32_t)(availPtr[9] & 0x7f));
        }
        if (id3Remain > 0) {
            if (availLen == 0) break; // waited for more data at EOF
            uint32_t take = (availLen < id3Remain) ? availLen : id3Remain;
            inPos += take;
            id3Remain -= take;
            if (inPos >= inLen) { inLen = 0; inPos = 0; }
            continue;
        }

        // ---- partial-frame guard (mirrors the device fix) ----------------
        // A complete frame is not enough: minimp3's fast path also validates
        // the NEXT frame header (frame_bytes + 4 <= availLen). So keep the
        // buffer topped up until a full frame PLUS the next header is present,
        // otherwise minimp3 resyncs (drops the Layer-III reservoir) and can
        // lock onto a false sync in garbage near the buffer end.
        if (!eof && availLen >= 4) {
            uint32_t fsz = mpegFrameSize(availPtr);
            if (fsz && availLen < fsz + 4) {
                if (inPos > 0 && inLen > inPos) {
                    memmove(&inBuf[0], &inBuf[inPos], inLen - inPos);
                    inLen -= inPos;
                    inPos = 0;
                } else if (inPos > 0) {
                    inLen = 0;
                    inPos = 0;
                }
                uint32_t before = inLen;
                if (inLen < inBufSize) {
                    uint32_t cap = inBufSize - inLen;
                    uint32_t want = (cap < readN) ? cap : readN;
                    uint32_t avail = (uint32_t)(file.size() - srcPos);
                    uint32_t got = (want < avail) ? want : avail;
                    if (got > 0) memcpy(&inBuf[inLen], &file[srcPos], got);
                    srcPos += got;
                    r.sourceBytes += got;
                    if (got == 0) {
                        if (srcPos >= file.size()) eof = true;
                    } else {
                        inLen += got;
                    }
                }
                if (inLen > before) continue;
                if (!eof) return r; // stalled — matches the device yield
            }
        }

        availLen = inLen - inPos;
        availPtr = &inBuf[inPos];
        if (availLen == 0) {
            if (eof) break;
            continue;
        }

        // ---- decode one frame (mirrors the device) ------------------------
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame_s(&dec, availPtr, availLen,
                                            pcm, &info, &scratch);
        if (info.frame_bytes > 0) {
            // Mirror the failed-scan guard: frame_bytes == availLen with 0
            // samples means find_frame consumed the whole buffer without
            // validating a frame — advancing would discard real audio at an
            // ID3/garbage -> audio transition. Keep the data and refill.
            if (samples > 0 || info.frame_bytes < availLen) {
                inPos += (uint32_t)info.frame_bytes;
                r.consumedBytes += (uint32_t)info.frame_bytes;
                if (inPos >= inLen) { inLen = 0; inPos = 0; }
            }
            if (samples > 0) {
                r.frames++;
                r.samples += (uint64_t)samples;
                r.rate = info.hz;
                r.ch = info.channels;
                continue;
            }
            if (info.frame_bytes < availLen) continue;
        }

        // frame_bytes == 0: need more data or EOF (mirrors the device)
        if (eof) break;
        if (inPos > 0 && inLen == inPos) { inLen = 0; inPos = 0; }
        if (inLen == inBufSize && availLen == inBufSize) inPos++;
        if (inPos > 0 && inLen == inBufSize) {
            memmove(&inBuf[0], &inBuf[inPos], inLen - inPos);
            inLen -= inPos;
            inPos = 0;
        }
    }
    return r;
}

TEST_CASE("real minimp3 + device rolling buffer decodes ALL teste.mp3 frames") {
    std::vector<uint8_t> data = readFile("../../teste.mp3");
    if (data.empty()) {
        // teste.mp3 lives at the repo root (owner-provided); skip when absent
        // so the suite still passes in CI/other machines.
        MESSAGE("teste.mp3 not present at repo root - skipping");
        return;
    }
    REQUIRE(data.size() == 11862010);

    DecodeResult r = runDeviceLoop(data, 16384, 16 * 1024);
    CHECK(r.sourceBytes == data.size());
    CHECK(r.frames == 11279);
    CHECK(r.samples == 11279u * 1152);
    CHECK(r.rate == 44100);
    CHECK(r.ch == 2);
}

TEST_CASE("real minimp3 + rolling buffer, small reads, still decodes all") {
    std::vector<uint8_t> data = readFile("../../teste.mp3");
    if (data.empty()) {
        MESSAGE("teste.mp3 not present at repo root - skipping");
        return;
    }
    REQUIRE(data.size() == 11862010);

    // AudioSourceBuffer hands data out in modest chunks; the decoder must not
    // lose frames regardless of the read size.
    DecodeResult r = runDeviceLoop(data, 997, 16 * 1024);
    CHECK(r.sourceBytes == data.size());
    CHECK(r.frames == 11279);
    CHECK(r.samples == 11279u * 1152);
}
