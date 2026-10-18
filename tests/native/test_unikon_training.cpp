#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>
#include <cstring>
#include <cstdint>

// ====== Standalone reimpls of the training-send pure logic ======
// Mirror chatgpt/src/unikon_protocol.cpp (sendTrainingSession + cacheSFrame)
// without Arduino/RS232 dependencies.

static const uint8_t TRAIN_STX = 0x02;
static const uint8_t TRAIN_ADDR = 0x81;
static const uint8_t TRAIN_ETX = 0x03;
static const int TRAIN_MAX_SFRAME = 64;

// Training contest name: "TDDMMHHMM" (I-frame name holds 9 chars + null),
// date+time from NTP-fed clock. Fallback "TREINO_NO".
static void buildTrainingName(int day, int month, int hour, int minute,
                              bool clockOk, char* out, size_t outSize) {
    if (outSize < 10) return;
    if (clockOk) {
        snprintf(out, outSize, "T%02d%02d%02d%02d", day, month, hour, minute);
    } else {
        memcpy(out, "TREINO_NO", 9);
        out[9] = '\0';
    }
}

// ZP content: ADDR 'Z' 'P' RING[11] ' ' CHIP[11] (26 bytes, no STX/ETX/checks).
static uint8_t buildTrainingZP(const char* ring, const char* chip, bool hasChip,
                               uint8_t* content) {
    content[0] = TRAIN_ADDR;
    content[1] = 0x5A; // 'Z'
    content[2] = 0x50; // 'P'
    memcpy(content + 3, ring, 11);
    content[14] = 0x20; // space
    if (hasChip) memcpy(content + 15, chip, 11);
    else memset(content + 15, '0', 11);
    return 26;
}

// S-frame content cache (ADDR+payload, no STX/ETX/checks), one slot per subtype.
struct SCache {
    uint8_t s1[TRAIN_MAX_SFRAME];
    uint8_t s1Len = 0;
    uint8_t s2[TRAIN_MAX_SFRAME];
    uint8_t s2Len = 0;
    uint8_t s3[TRAIN_MAX_SFRAME];
    uint8_t s3Len = 0;
};

static bool cacheS(SCache& c, uint8_t subtype, const uint8_t* content, uint8_t len) {
    if (len == 0 || len > TRAIN_MAX_SFRAME) return false;
    if (subtype == 0x31) { memcpy(c.s1, content, len); c.s1Len = len; return true; }
    if (subtype == 0x32) { memcpy(c.s2, content, len); c.s2Len = len; return true; }
    if (subtype == 0x33) { memcpy(c.s3, content, len); c.s3Len = len; return true; }
    return false;
}

// Planned frame count for a training session: IC0 + I + cached S + N ZP.
// Returns 0 when there is nothing to send (no registrations).
static uint16_t trainingPlanFrames(uint16_t registrations, bool hasS1, bool hasS2, bool hasS3) {
    if (registrations == 0) return 0;
    uint16_t n = (uint16_t)(2 + registrations);
    if (hasS1) n++;
    if (hasS2) n++;
    if (hasS3) n++;
    return n;
}

// Padded copies: short/corrupt registrations are zero-padded, never leak.
static void padField(const char* src, char* out11) {
    memset(out11, '0', 11);
    out11[11] = '\0';
    size_t n = strlen(src);
    if (n > 11) n = 11;
    memcpy(out11, src, n);
}

// ====== training name tests (3 variations, NTP date+time) ======
TEST_CASE("trainingName: normal date+time") {
    char name[10];
    buildTrainingName(26, 7, 14, 30, true, name, sizeof(name));
    CHECK(std::string(name) == "T26071430");
}

TEST_CASE("trainingName: single-digit fields zero-padded") {
    char name[10];
    buildTrainingName(5, 3, 8, 5, true, name, sizeof(name));
    CHECK(std::string(name) == "T05030805");
}

TEST_CASE("trainingName: no clock falls back to TREINO_NO") {
    char name[10];
    buildTrainingName(0, 0, 0, 0, false, name, sizeof(name));
    CHECK(std::string(name) == "TREINO_NO");
}

// ====== ZP builder tests ======
TEST_CASE("trainingZP: pigeon with chip") {
    uint8_t content[26];
    uint8_t len = buildTrainingZP("25000061136", "C9BCC229001", true, content);
    CHECK(len == 26);
    CHECK(content[0] == TRAIN_ADDR);
    CHECK(content[1] == 0x5A);
    CHECK(content[2] == 0x50);
    CHECK(memcmp(content + 3, "25000061136", 11) == 0);
    CHECK(content[14] == 0x20);
    CHECK(memcmp(content + 15, "C9BCC229001", 11) == 0);
}

TEST_CASE("trainingZP: pigeon without chip gets zeros") {
    uint8_t content[26];
    uint8_t len = buildTrainingZP("24004342525", "ignored", false, content);
    CHECK(len == 26);
    CHECK(memcmp(content + 3, "24004342525", 11) == 0);
    CHECK(memcmp(content + 15, "00000000000", 11) == 0);
}

TEST_CASE("trainingPad: short ring is zero-padded to 11") {
    char out[12];
    padField("12345", out);
    CHECK(std::string(out) == "12345000000");
}

TEST_CASE("trainingPad: long input is truncated to 11") {
    char out[12];
    padField("12345678901234", out);
    CHECK(std::string(out) == "12345678901");
}

// ====== S cache tests ======
TEST_CASE("sCache: store and retrieve S1") {
    SCache c;
    uint8_t payload[] = { TRAIN_ADDR, 0x53, '1', '3', '7', '6', '1', '7' };
    CHECK(cacheS(c, 0x31, payload, sizeof(payload)));
    CHECK(c.s1Len == sizeof(payload));
    CHECK(memcmp(c.s1, payload, sizeof(payload)) == 0);
    CHECK(c.s2Len == 0);
    CHECK(c.s3Len == 0);
}

TEST_CASE("sCache: S1/S2/S3 independent + overwrite") {
    SCache c;
    uint8_t p1[] = { TRAIN_ADDR, 0x53, '1' };
    uint8_t p2[] = { TRAIN_ADDR, 0x53, '2', 'x' };
    uint8_t p2b[] = { TRAIN_ADDR, 0x53, '2', 'y' };
    CHECK(cacheS(c, 0x31, p1, sizeof(p1)));
    CHECK(cacheS(c, 0x32, p2, sizeof(p2)));
    CHECK(cacheS(c, 0x32, p2b, sizeof(p2b)));  // overwrite keeps latest
    CHECK(memcmp(c.s2, p2b, sizeof(p2b)) == 0);
    CHECK(c.s1Len == sizeof(p1));
    CHECK(c.s3Len == 0);
}

TEST_CASE("sCache: rejects empty, oversize and unknown subtype") {
    SCache c;
    uint8_t payload[] = { TRAIN_ADDR };
    CHECK(!cacheS(c, 0x31, payload, 0));
    uint8_t big[TRAIN_MAX_SFRAME + 1] = { 0 };
    CHECK(!cacheS(c, 0x32, big, sizeof(big)));
    CHECK(!cacheS(c, 0x34, payload, sizeof(payload)));  // '4' not cached
    CHECK(c.s1Len == 0);
    CHECK(c.s2Len == 0);
}

// ====== plan count tests ======
TEST_CASE("trainingPlan: full session counts IC0+I+S*3+ZP") {
    CHECK(trainingPlanFrames(135, true, true, true) == (uint16_t)(2 + 3 + 135));
}

TEST_CASE("trainingPlan: zero registrations aborts") {
    CHECK(trainingPlanFrames(0, true, true, true) == 0);
}

TEST_CASE("trainingPlan: no cached S-frames sends IC0+I+ZP only") {
    CHECK(trainingPlanFrames(2, false, false, false) == 4);
}

// Training entry filter: which rings to program. Empty = whole pombal.
static const int TRAIN_FILTER_MAX = 500; // mirrors UNIKON_MAX_ARRIVALS
static char s_filterRings[TRAIN_FILTER_MAX][12];
static int s_filterCount = 0;

// Mirror of parseTrainingEntryLine (unikon_protocol.cpp).
static bool parseTrainingEntryLine(const char* line, char* outRing) {
    if (!line || !outRing) return false;
    while (*line == ' ' || *line == '\t' || *line == '\r') line++;
    if (line[0] == '\0' || line[0] == '\n') return false;
    if (line[0] == 'S' && line[1] == '4') {
        size_t len = strlen(line);
        if (len < 24) return false;
        for (int i = 0; i < 11; i++) {
            char c = line[13 + i];
            if (c < '0' || c > '9') return false;
        }
        memcpy(outRing, line + 13, 11);
        outRing[11] = '\0';
        return true;
    }
    int i = 0;
    while (i < 11 && line[i] >= '0' && line[i] <= '9') { outRing[i] = line[i]; i++; }
    if (i != 11) return false;
    outRing[11] = '\0';
    return true;
}

static uint16_t setTrainingFilterReimpl(const char* text) {
    s_filterCount = 0;
    if (!text || text[0] == '\0') return 0;
    const char* p = text;
    char line[64];
    while (*p && s_filterCount < TRAIN_FILTER_MAX) {
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof(line) - 1) { line[n] = p[n]; n++; }
        line[n] = '\0';
        p += n;
        if (*p == '\n') p++;
        char ring[12];
        if (parseTrainingEntryLine(line, ring)) {
            memcpy(s_filterRings[s_filterCount], ring, 12);
            s_filterCount++;
        }
    }
    return (uint16_t)s_filterCount;
}

static bool trainingFilterAllowsReimpl(const char* ring) {
    if (s_filterCount == 0) return true;
    for (int i = 0; i < s_filterCount; i++) {
        if (memcmp(s_filterRings[i], ring, 11) == 0) return true;
    }
    return false;
}

// ====== training contest match tests ======
// Matching is by the fixed contest code (\"990\"), not by name: the code is a
// compiled constant, present in both frame-3 flavours, and needs no persistence.
// The code matcher itself is covered by the trainingCodeMatch cases below.

// ====== S4 / entry-list filter tests ======
TEST_CASE("trainingEntry: full S4 line extracts ring") {
    char ring[12];
    CHECK(parseTrainingEntryLine("S43760701017P02002830633WCD2669D3001", ring));
    CHECK(std::string(ring) == "02002830633");
}

TEST_CASE("trainingEntry: plain 11-digit ring") {
    char ring[12];
    CHECK(parseTrainingEntryLine("24004342525", ring));
    CHECK(std::string(ring) == "24004342525");
}

TEST_CASE("trainingEntry: rejects short/truncated/garbage") {
    char ring[12];
    CHECK(!parseTrainingEntryLine("12345", ring));
    CHECK(!parseTrainingEntryLine("", ring));
    CHECK(!parseTrainingEntryLine("S4376070", ring));
    CHECK(!parseTrainingEntryLine("S43760701017P02002830ABC", ring));
    CHECK(!parseTrainingEntryLine(nullptr, ring));
}

TEST_CASE("trainingFilter: text with S4 + plain rings, skips junk") {
    const char* text = "S43760701017P02002830633WCD2669D3001\njunk line\n24004342525\n\n  \n";
    CHECK(setTrainingFilterReimpl(text) == 2);
    CHECK(trainingFilterAllowsReimpl("02002830633"));
    CHECK(trainingFilterAllowsReimpl("24004342525"));
    CHECK(!trainingFilterAllowsReimpl("99999999999"));
}

TEST_CASE("trainingFilter: empty text clears (whole pombal passes)") {
    setTrainingFilterReimpl("24004342525");
    CHECK(setTrainingFilterReimpl("") == 0);
    CHECK(trainingFilterAllowsReimpl("anything....."));
}

// ====== Frame '3' contest code (unikon_protocol.cpp parseFrame3Code) ======
// Real bytes: capture 17.txt ("003", no race data) and 9.txt ("001", with data).
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
    strncpy(out, tmp, outLen - 1);
    out[outLen - 1] = '\0';
}

static bool isTrainingContestCodeReimpl(const char* raceCode, const char* trainingCode) {
    if (!raceCode || !trainingCode) return false;
    if (raceCode[0] == '\0' || trainingCode[0] == '\0') return false;
    return strcmp(raceCode, trainingCode) == 0;
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

TEST_CASE("trainingCodeMatch: 990 matches the programmed training code") {
    CHECK(isTrainingContestCodeReimpl("990", "990"));
    CHECK(!isTrainingContestCodeReimpl("003", "990"));
    CHECK(!isTrainingContestCodeReimpl("", "990"));
    CHECK(!isTrainingContestCodeReimpl("990", ""));
    CHECK(!isTrainingContestCodeReimpl(nullptr, "990"));
}
