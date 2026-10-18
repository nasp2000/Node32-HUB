#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdint>
#include <cstring>

// Validates the Opus identification + TOC helpers in chatgpt/src/ogg_parser.h
// (first-party, RFC 7845 section 5.1 / RFC 6716 section 3.1) that
// audio_decoder_opus.cpp relies on: OpusHead channel/pre-skip/rate parsing
// and per-packet frame counts from the TOC byte.

#include "ogg_parser.h"

// OpusHead from the real test vector (tone.opus: mono, pre-skip 312, 48 kHz).
static const uint8_t kRealHead[19] = {
    0x4f, 0x70, 0x75, 0x73, 0x48, 0x65, 0x61, 0x64,  // "OpusHead"
    0x01,        // version 1
    0x01,        // channels = 1
    0x38, 0x01,  // pre-skip = 312
    0x80, 0xbb, 0x00, 0x00,  // sample rate = 48000
    0x00, 0x00, 0x00  // output gain + mapping family
};

TEST_CASE("opus parseHead accepts the real mono header") {
    opus::Head h = opus::parseHead(kRealHead, sizeof(kRealHead));
    CHECK(h.valid == true);
    CHECK(h.channels == 1);
    CHECK(h.preSkip == 312);
    CHECK(h.sampleRate == 48000);
}

TEST_CASE("opus parseHead accepts a synthetic stereo header") {
    uint8_t stereo[19];
    memcpy(stereo, kRealHead, sizeof(stereo));
    stereo[9] = 0x02;    // channels = 2
    stereo[10] = 0x00; stereo[11] = 0x00;  // pre-skip = 0
    stereo[12] = 0x44; stereo[13] = 0xac; stereo[14] = 0x00; stereo[15] = 0x00;  // 44100
    opus::Head h = opus::parseHead(stereo, sizeof(stereo));
    CHECK(h.valid == true);
    CHECK(h.channels == 2);
    CHECK(h.preSkip == 0);
    CHECK(h.sampleRate == 44100);
}

TEST_CASE("opus parseHead rejects corrupt identification packets") {
    // Wrong magic.
    uint8_t badMagic[19];
    memcpy(badMagic, kRealHead, sizeof(badMagic));
    badMagic[0] = 'X';
    CHECK(opus::parseHead(badMagic, sizeof(badMagic)).valid == false);
    // Version out of range (RFC 7845: 0..15).
    uint8_t badVer[19];
    memcpy(badVer, kRealHead, sizeof(badVer));
    badVer[8] = 16;
    CHECK(opus::parseHead(badVer, sizeof(badVer)).valid == false);
    // Channel count 0 and 3 are illegal (Opus supports 1..2 here).
    uint8_t badCh[19];
    memcpy(badCh, kRealHead, sizeof(badCh));
    badCh[9] = 0;
    CHECK(opus::parseHead(badCh, sizeof(badCh)).valid == false);
    badCh[9] = 3;
    CHECK(opus::parseHead(badCh, sizeof(badCh)).valid == false);
    // Truncated header.
    CHECK(opus::parseHead(kRealHead, 18).valid == false);
    CHECK(opus::parseHead(nullptr, 0).valid == false);
}

TEST_CASE("opus frameCount decodes every TOC code") {
    // Code 0 (bits 1:0 = 00): exactly 1 frame.
    uint8_t single[4] = {0x78, 0x11, 0x22, 0x33};  // 0x78 & 3 == 0
    CHECK(opus::frameCount(single, sizeof(single)) == 1);
    // Code 1 (01): 2 frames, equal size. Code 2 (10): 2 frames, sizes follow.
    uint8_t twoA[4] = {0x79, 0x11, 0x22, 0x33};  // 0x79 & 3 == 1
    uint8_t twoB[6] = {0x7a, 0x11, 0x22, 0x33, 0x44, 0x55};
    CHECK(opus::frameCount(twoA, sizeof(twoA)) == 2);
    CHECK(opus::frameCount(twoB, sizeof(twoB)) == 2);
    // Code 3 (11): frame count M in the next byte (here: 5).
    uint8_t multi[4] = {0x7f, 0x05, 0x11, 0x22};
    CHECK(opus::frameCount(multi, sizeof(multi)) == 5);
}

TEST_CASE("opus frameCount rejects malformed TOC packets") {
    CHECK(opus::frameCount(nullptr, 0) == -1);
    // Code 3 without the M byte.
    uint8_t noM[1] = {0x7f};
    CHECK(opus::frameCount(noM, sizeof(noM)) == -1);
    // M == 0 and M > 120 are illegal counts.
    uint8_t mZero[2] = {0x7f, 0x00};
    uint8_t mHuge[2] = {0x7f, 121};
    CHECK(opus::frameCount(mZero, sizeof(mZero)) == -1);
    CHECK(opus::frameCount(mHuge, sizeof(mHuge)) == -1);
    // Upper bound M == 120 is legal.
    uint8_t mMax[2] = {0x7f, 120};
    CHECK(opus::frameCount(mMax, sizeof(mMax)) == 120);
}
