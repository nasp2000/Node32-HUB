#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <strings.h>

// ============================================================================
// Unikon READ-ONLY helpers (the write path was removed on 2026-10-25).
//
// The hub may only READ the RAMBOX: the training/pombal WRITE (IC0 + I-frame +
// S1/S2/S3 replay + "M ZP" pigeon lines) erased the clock's loft during a bench
// test, so it was deleted from the firmware together with its API and buttons.
// What remains here is the read-side classification: the contest code carried
// by frame '3' and the rule that tags a race as training.
// ============================================================================

// Mirror of parseFrame3Code() in unikon_protocol.cpp: the contest code lives at
// bytes [9..11] of frame '3' in BOTH flavours (with and without race data).
static void parseFrame3CodeReimpl(const uint8_t* f, int len, char* out, size_t outLen) {
    if (!out || outLen < 2) return;
    out[0] = '\0';
    if (len < 12 || f[2] != 0x33) return;
    char tmp[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        uint8_t c = f[9 + i];
        if (c < '0' || c > '9') return;
        tmp[i] = (char)c;
    }
    snprintf(out, outLen, "%s", tmp);
}

// Mirror of isTrainingContestCode() in unikon_protocol.cpp.
static bool isTrainingContestCodeReimpl(const char* raceCode, const char* trainingCode) {
    if (!raceCode || !trainingCode) return false;
    if (raceCode[0] == '\0' || trainingCode[0] == '\0') return false;
    while (*raceCode == ' ') raceCode++;
    if (strcmp(raceCode, trainingCode) == 0) return true;
    if (trainingCode[0] == '0' && strcmp(raceCode, trainingCode + 1) == 0) return true;
    return false;
}

TEST_CASE("frame3code: capture 17 (no race data) yields 003") {
    // 02 81 33 37 36 31 37 30 31 30 30 33 30 30 30 30 81 37 61 70 .. 03
    const uint8_t f[] = {0x02,0x81,0x33,0x37,0x36,0x31,0x37,0x30,0x31,0x30,0x30,0x33,
                         0x30,0x30,0x30,0x30,0x81,0x37,0x61,0x70,0x03};
    char code[4];
    parseFrame3CodeReimpl(f, sizeof(f), code, sizeof(code));
    CHECK(std::string(code) == "003");
}

TEST_CASE("frame3code: capture 9 (with race data) yields 001") {
    const uint8_t f[] = {0x02,0x81,0x33,0x37,0x36,0x31,0x37,0x30,0x31,0x30,0x30,0x31,
                         0x30,0x30,0x30,0x32,0x81,0x37,0x61,0x70,0x03};
    char code[4];
    parseFrame3CodeReimpl(f, sizeof(f), code, sizeof(code));
    CHECK(std::string(code) == "001");
}

TEST_CASE("frame3code: short/garbage frames yield empty") {
    char code[4] = "xx";
    const uint8_t shortf[] = {0x02,0x81,0x33,0x30,0x30,0x03};
    parseFrame3CodeReimpl(shortf, sizeof(shortf), code, sizeof(code));
    CHECK(std::string(code) == "");
    const uint8_t nondigit[] = {0x02,0x81,0x33,'3','7','6','1','7','0','1','A','B','C',0x03};
    parseFrame3CodeReimpl(nondigit, sizeof(nondigit), code, sizeof(code));
    CHECK(std::string(code) == "");
    const uint8_t not3[] = {0x02,0x81,0x50,'3','7','6','1','7','0','1','0','0','3',0x03};
    parseFrame3CodeReimpl(not3, sizeof(not3), code, sizeof(code));
    CHECK(std::string(code) == "");
}

TEST_CASE("frame3code: the 2026-10-25 short flavour (59 B) still yields its code") {
    // Same device as the bench capture: frame '3' arrives with 59 bytes, so the
    // date/name fields (offsets 59/65) do not exist -- the code still must be read.
    uint8_t f[59];
    memset(f, '0', sizeof(f));
    f[0] = 0x02; f[1] = 0x81; f[2] = 0x33;
    f[9] = '0'; f[10] = '0'; f[11] = '3';
    char code[4];
    parseFrame3CodeReimpl(f, sizeof(f), code, sizeof(code));
    CHECK(std::string(code) == "003");
}

TEST_CASE("trainingCodeMatch: 090 matches the programmed training code") {
    CHECK(isTrainingContestCodeReimpl("090", "090"));
    CHECK(!isTrainingContestCodeReimpl("003", "090"));
    CHECK(!isTrainingContestCodeReimpl("", "090"));
    CHECK(!isTrainingContestCodeReimpl("090", ""));
    CHECK(!isTrainingContestCodeReimpl(nullptr, "090"));
}

TEST_CASE("trainingCodeMatch: tolerates the bare race number and space padding") {
    // The device may echo the race number without the leading literal '0', or pad the
    // 3-char field with a space -- both must still tag the race as training.
    CHECK(isTrainingContestCodeReimpl("90", "090"));
    CHECK(isTrainingContestCodeReimpl(" 90", "090"));
    CHECK(!isTrainingContestCodeReimpl("91", "090"));
    CHECK(!isTrainingContestCodeReimpl("003", "090"));
}
