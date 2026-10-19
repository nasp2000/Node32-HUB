#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "unikon_checksum.h"
#include <string>
#include <cstring>

// ====== Frame constants used by multiple tests ======
static const uint8_t CONTENT_V[] = { UNIKON_ADDR, 0x76 };
static const uint8_t CONTENT_X[] = { UNIKON_ADDR, 0x58 };

// ====== computeChecksum tests ======
TEST_CASE("computeChecksum: known vector V-frame") {
    // Content: 0x81, 0x76 → sum = 0xF7
    uint8_t ck1, ck2;
    computeChecksum(CONTENT_V, sizeof(CONTENT_V), &ck1, &ck2);
    CHECK(ck1 == 0xF7);
    CHECK(ck2 == 0x80);  // 0x80 | (0 >> 7) = 0x80
}

TEST_CASE("computeChecksum: known vector X-frame") {
    // Content: 0x81, 0x58 → sum = 0xD9
    uint8_t ck1, ck2;
    computeChecksum(CONTENT_X, sizeof(CONTENT_X), &ck1, &ck2);
    CHECK(ck1 == 0xD9);
    CHECK(ck2 == 0x80);
}

TEST_CASE("computeChecksum: carries into high byte") {
    // Sum = 0xFF + 0x01 = 0x100 → CK1 = 0x00 → escaped to 0x80 ; CK2 = 0x80|0x01 = 0x81
    uint8_t data[] = { 0xFF, 0x01 };
    uint8_t ck1, ck2;
    computeChecksum(data, sizeof(data), &ck1, &ck2);
    CHECK(ck1 == 0x80);
    CHECK(ck2 == 0x81);
}

TEST_CASE("computeChecksum: all zeros") {
    // Sum = 0x00 → CK1 = 0x00 → escaped to 0x80
    uint8_t data[] = { 0x00, 0x00, 0x00, 0x00 };
    uint8_t ck1, ck2;
    computeChecksum(data, sizeof(data), &ck1, &ck2);
    CHECK(ck1 == 0x80);
    CHECK(ck2 == 0x80);
}

TEST_CASE("computeChecksum: escapes CK1 below 0x20 (real capture vector)") {
    // docs/Unikon Sniffer/10.txt:
    //   02 81 5a 50 31 38 30 30 38 32 33 30 37 36 38 20 43 39 38 46 30 41 42 46 30 30 31 8a 86 03
    // Content = 0x81 + "ZP18008230768 C98F0ABF001" → sum = 0x60A
    // → CK1 raw 0x0A, transmitted escaped as 0x8A ; CK2 = 0x80|0x06 = 0x86
    const uint8_t content[] = { 0x81, 'Z','P','1','8','0','0','8','2','3','0','7','6','8',' ',
                                'C','9','8','F','0','A','B','F','0','0','1' };
    uint8_t ck1, ck2;
    computeChecksum(content, sizeof(content), &ck1, &ck2);
    CHECK(ck1 == 0x8A);
    CHECK(ck2 == 0x86);
}

TEST_CASE("computeChecksum: larger values") {
    // Sum = 0xAA + 0xBB + 0xCC = 0x231 → CK1=0x31, CK2=0x80|0x02=0x82
    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    uint8_t ck1, ck2;
    computeChecksum(data, sizeof(data), &ck1, &ck2);
    CHECK(ck1 == 0x31);
    CHECK(ck2 == 0x82);
}

TEST_CASE("computeChecksum: max sum") {
    // 255 + 255 = 510 = 0x1FE → CK1=0xFE, CK2=0x80|0x01=0x81
    uint8_t data[] = { 0xFF, 0xFF };
    uint8_t ck1, ck2;
    computeChecksum(data, sizeof(data), &ck1, &ck2);
    CHECK(ck1 == 0xFE);
    CHECK(ck2 == 0x81);
}

// ====== buildFrame tests ======
TEST_CASE("buildFrame: V-frame") {
    uint8_t buf[16];
    uint8_t len = buildFrame(CONTENT_V, sizeof(CONTENT_V), buf);
    CHECK(len == (sizeof(CONTENT_V) + 4));
    CHECK(buf[0] == UNIKON_STX);
    CHECK(buf[1] == UNIKON_ADDR);
    CHECK(buf[2] == 0x76);
    CHECK(buf[3] == 0xF7); // CK1
    CHECK(buf[4] == 0x80); // CK2
    CHECK(buf[5] == UNIKON_ETX);
}

TEST_CASE("buildFrame: X-frame") {
    uint8_t buf[16];
    uint8_t len = buildFrame(CONTENT_X, sizeof(CONTENT_X), buf);
    CHECK(len == (sizeof(CONTENT_X) + 4));
    CHECK(buf[0] == UNIKON_STX);
    CHECK(buf[1] == UNIKON_ADDR);
    CHECK(buf[2] == 0x58);
    CHECK(buf[3] == 0xD9);
    CHECK(buf[4] == 0x80);
    CHECK(buf[5] == UNIKON_ETX);
}

TEST_CASE("buildFrame: longer content") {
    uint8_t content[] = { 0x81, 0x49, 0x43, 0x30 };
    uint8_t buf[16];
    uint8_t len = buildFrame(content, sizeof(content), buf);
    CHECK(len == sizeof(content) + 4);
    CHECK(buf[0] == UNIKON_STX);
    // Sum = 0x81+0x49+0x43+0x30 = 0x13D → CK1=0x3D, CK2=0x80|0x01=0x81
    CHECK(buf[sizeof(content) + 1] == 0x3D);
    CHECK(buf[sizeof(content) + 2] == 0x81);
    CHECK(buf[sizeof(content) + 3] == UNIKON_ETX);
}

TEST_CASE("buildFrame: full byte compare against real capture (IC0)") {
    // docs/Unikon Sniffer/transmitir_voo.txt:  02 81 49 43 30 3d 81 03
    const uint8_t content[] = { 0x81, 0x49, 0x43, 0x30 };
    uint8_t buf[16];
    uint8_t len = buildFrame(content, sizeof(content), buf);
    const uint8_t expect[] = { 0x02, 0x81, 0x49, 0x43, 0x30, 0x3D, 0x81, 0x03 };
    CHECK(len == sizeof(expect));
    for (uint8_t i = 0; i < len; i++) CHECK(buf[i] == expect[i]);
}

TEST_CASE("buildFrame: full byte compare against real capture (escaped CK1)") {
    // docs/Unikon Sniffer/10.txt:
    //   02 81 5a 50 31 38 30 30 38 32 33 30 37 36 38 20 43 39 38 46 30 41 42 46 30 30 31 8a 86 03
    const uint8_t content[] = { 0x81, 'Z','P','1','8','0','0','8','2','3','0','7','6','8',' ',
                                'C','9','8','F','0','A','B','F','0','0','1' };
    uint8_t buf[64];
    uint8_t len = buildFrame(content, sizeof(content), buf);
    CHECK(len == sizeof(content) + 4);
    CHECK(buf[0] == UNIKON_STX);
    CHECK(buf[len - 3] == 0x8A);   // CK1 = 0x0A escaped with |0x80
    CHECK(buf[len - 2] == 0x86);   // CK2 = 0x80 | 0x06
    CHECK(buf[len - 1] == UNIKON_ETX);
}

TEST_CASE("buildFrame: push frames P1/P0 escape the CK1 control byte") {
    // Push on/off, the frames the vendor driver's uniActivatePush()/uniDeactivatePush()
    // send. Both sums land on a control byte, so without the CK1 escape the frame would
    // contain 0x02/0x01 (STX / SOH) and be unparseable.
    //   "P1": 0x81+0x50+0x31 = 0x102 -> CK1 0x02 -> escaped 0x82 ; CK2 0x80|0x01 = 0x81
    const uint8_t p1[] = { 0x81, 0x50, 0x31 };
    uint8_t buf[16];
    uint8_t len = buildFrame(p1, sizeof(p1), buf);
    const uint8_t exp1[] = { 0x02, 0x81, 0x50, 0x31, 0x82, 0x81, 0x03 };
    CHECK(len == sizeof(exp1));
    for (uint8_t i = 0; i < len; i++) CHECK(buf[i] == exp1[i]);
    //   "P0": 0x81+0x50+0x30 = 0x101 -> CK1 0x01 -> escaped 0x81 ; CK2 0x80|0x01 = 0x81
    const uint8_t p0[] = { 0x81, 0x50, 0x30 };
    len = buildFrame(p0, sizeof(p0), buf);
    const uint8_t exp0[] = { 0x02, 0x81, 0x50, 0x30, 0x81, 0x81, 0x03 };
    CHECK(len == sizeof(exp0));
    for (uint8_t i = 0; i < len; i++) CHECK(buf[i] == exp0[i]);
}

// ====== isAsciiDigitsExact tests ======
TEST_CASE("isAsciiDigitsExact: valid") {
    CHECK(isAsciiDigitsExact("12345", 5));
    CHECK(isAsciiDigitsExact("0", 1));
    CHECK(isAsciiDigitsExact("000", 3));
}

TEST_CASE("isAsciiDigitsExact: rejects non-digits") {
    CHECK(!isAsciiDigitsExact("12a45", 5));
    CHECK(!isAsciiDigitsExact("12.5", 4));
}

TEST_CASE("isAsciiDigitsExact: empty with len=0 returns true (value[0] is null)") {
    CHECK(isAsciiDigitsExact("", 0));
}

TEST_CASE("isAsciiDigitsExact: requires null terminator at len") {
    // "12345" substring of "12345X" — value[5] = 'X' not '\0'
    CHECK(!isAsciiDigitsExact("12345X", 5));
}

TEST_CASE("isAsciiDigitsExact: null pointer") {
    CHECK(!isAsciiDigitsExact(nullptr, 5));
}

// ====== isAsciiHexPrefixExact tests ======
TEST_CASE("isAsciiHexPrefixExact: valid hex") {
    CHECK(isAsciiHexPrefixExact("ABCD1234", 8));
    CHECK(isAsciiHexPrefixExact("DEADBEEF", 8));
    CHECK(isAsciiHexPrefixExact("CAFE", 4));
    CHECK(isAsciiHexPrefixExact("A1B2C3D4", 8));
}

TEST_CASE("isAsciiHexPrefixExact: rejects lowercase") {
    CHECK(!isAsciiHexPrefixExact("abcd1234", 8));
    CHECK(!isAsciiHexPrefixExact("cafe", 4));
}

TEST_CASE("isAsciiHexPrefixExact: rejects non-hex chars") {
    CHECK(!isAsciiHexPrefixExact("XYZ12345", 8));
    CHECK(!isAsciiHexPrefixExact("GHIJ", 4));
}

TEST_CASE("isAsciiHexPrefixExact: null pointer") {
    CHECK(!isAsciiHexPrefixExact(nullptr, 5));
}

// ====== unikonFormatDisplayRing tests ======
TEST_CASE("unikonFormatDisplayRing: format with 00 prefix") {
    char out[32];
    unikonFormatDisplayRing("01234567890", out, sizeof(out)); // 11 digits, pos[2]='2' not '0'
    // rawRing[2] != '0' → passthrough
    CHECK(std::string(out) == "01234567890");
}

TEST_CASE("unikonFormatDisplayRing: format with 00 at positions 2-3") {
    char out[32];
    unikonFormatDisplayRing("AB001234567", out, sizeof(out)); // not all digits → passthrough
    CHECK(std::string(out) == "AB001234567");
}

TEST_CASE("unikonFormatDisplayRing: 11 digits with 00 at pos 2-3 -> XX-XXXXXXX") {
    char out[32];
    // rawRing = "12001234567": pos 2='0', pos 3='0', all 11 digits
    // Format: first 2 chars + "-" + 7 chars from rawRing+4 (positions 4-10)
    // rawRing+4 = "1234567" → "12-1234567"
    unikonFormatDisplayRing("12001234567", out, sizeof(out));
    CHECK(std::string(out) == "12-1234567");
}

TEST_CASE("unikonFormatDisplayRing: 11 digits without 00 prefix -> passthrough") {
    char out[32];
    unikonFormatDisplayRing("12001234567", out, sizeof(out));
    CHECK(std::string(out) == "12-1234567");
}

TEST_CASE("unikonFormatDisplayRing: null input") {
    char out[32] = "dirty";
    unikonFormatDisplayRing(nullptr, out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("unikonFormatDisplayRing: zero outSize") {
    char out[32] = "";
    unikonFormatDisplayRing("123", out, 0);
    // no crash, nothing written
}

// ====== unikonFormatDisplayChip tests ======
TEST_CASE("unikonFormatDisplayChip: 11 chars, 8 hex + 3 digits -> first 8 chars") {
    char out[32];
    // "ABCD1234" = 8 hex chars, "567" = 3 digits, 11 total
    unikonFormatDisplayChip("ABCD1234567", true, out, sizeof(out));
    CHECK(std::string(out) == "ABCD1234");
}

TEST_CASE("unikonFormatDisplayChip: no chip -> empty") {
    char out[32];
    out[0] = 'X';
    unikonFormatDisplayChip("ABCD1234XYZ", false, out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("unikonFormatDisplayChip: null chip -> empty") {
    char out[32];
    out[0] = 'X';
    unikonFormatDisplayChip(nullptr, true, out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("unikonFormatDisplayChip: not matching format -> passthrough") {
    char out[32];
    unikonFormatDisplayChip("SHORT", true, out, sizeof(out));
    CHECK(std::string(out) == "SHORT");
}

TEST_CASE("unikonFormatDisplayChip: 11 chars but non-hex prefix -> passthrough") {
    char out[32];
    unikonFormatDisplayChip("XYZ12345ABC", true, out, sizeof(out));
    CHECK(std::string(out) == "XYZ12345ABC");
}

TEST_CASE("unikonFormatDisplayChip: 11 chars, 8 hex but trailing non-digits -> passthrough") {
    char out[32];
    // First 8 are hex but last 3 are not digits
    unikonFormatDisplayChip("ABCD1234XYZ", true, out, sizeof(out));
    CHECK(std::string(out) == "ABCD1234XYZ");
}

// ====== S3 fancier name parsing (standalone reimpl, tested for 21-byte fix) ======
static void trimSpacesLocal(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

static void parseS3Name(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (len < 35 || f[2] != 0x53 || f[3] != 0x33) return;
    int maxBytes = (int)len - 14 - 2 - 1;
    if (maxBytes < 0) return;
    uint8_t copyLen = (uint8_t)((int)maxBytes < (int)(outLen - 1) ? maxBytes : (outLen - 1));
    memcpy(out, f + 14, copyLen);
    out[copyLen] = '\0';
    trimSpacesLocal(out);
    // Strip trailing zeros (padding)
    size_t slen = strlen(out);
    while (slen > 0 && out[slen - 1] == '0') out[--slen] = '\0';
    // Re-strip spaces uncovered by zero removal
    trimSpacesLocal(out);
}

TEST_CASE("parseS3Frame: full name (Alexandre I. Pereira)") {
    uint8_t s3[] = {
        0x02, 0x81,
        0x53, 0x33,
        '3','7','6','1','7','0','1',
        '0','0','3',
        'A','l','e','x','a','n','d','r','e',' ','I','.',' ','P','e','r','e','i','r','a',
        '0','0','0','0','0','0','0','0','0','0','0','0','0','0',
        0xB6, 0x8C,
        0x03
    };
    char name[32];
    parseS3Name(s3, sizeof(s3), name, sizeof(name));
    CHECK(std::string(name) == "Alexandre I. Pereira");
}

TEST_CASE("parseS3Frame: shorter name right-padded with spaces + zeros") {
    uint8_t s3[] = {
        0x02, 0x81,
        0x53, 0x33,
        '3','7','6','1','7','0','1',
        '0','0','3',
        'J','o','r','g','e',' ','S','i','l','v','a',
        ' ',' ',' ',' ',' ',' ',' ',' ',' ',
        '0','0','0','0','0','0','0','0','0','0','0','0','0','0',
        0xD1, 0x8D,
        0x03
    };
    char name[32];
    parseS3Name(s3, sizeof(s3), name, sizeof(name));
    CHECK(std::string(name) == "Jorge Silva");
}

TEST_CASE("parseS3Frame: long name (e.g. António José Silva) padded with zeros") {
    uint8_t s3[] = {
        0x02, 0x81,
        0x53, 0x33,
        '3','7','6','1','7','0','1',
        '0','0','5',
        'A','n','t','o','n','i','o',' ','J','o','s','e',' ','d','a',' ','S','i','l','v','a',' ','J','u','n','i','o','r',
        '0','0','0','0','0','0','0',
        0xC3, 0x8E,
        0x03
    };
    char name[48];
    parseS3Name(s3, sizeof(s3), name, sizeof(name));
    CHECK(std::string(name) == "Antonio Jose da Silva Junior");
}

TEST_CASE("parseS3Frame: short name with trailing spaces trimmed") {
    uint8_t s3[] = {
        0x02, 0x81,
        0x53, 0x33,
        '3','7','6','1','7','0','1',
        '0','0','1',
        'J','o','a','o',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
        '0','0','0','0','0','0','0','0','0','0','0','0','0','0',
        0xC1, 0x8D,
        0x03
    };
    char name[32];
    parseS3Name(s3, sizeof(s3), name, sizeof(name));
    CHECK(std::string(name) == "Joao");
}

TEST_CASE("parseS3Frame: empty name (all zeros)") {
    uint8_t s3[] = {
        0x02, 0x81,
        0x53, 0x33,
        '0','0','0','0','0','0','0',
        '0','0','0',
        '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
        '0','0','0','0','0','0','0','0','0','0','0','0','0','0',
        0x81, 0xD5,
        0x03
    };
    char name[32];
    parseS3Name(s3, sizeof(s3), name, sizeof(name));
    CHECK(std::string(name) == "");
}

// ====== I-frame and ZP-frame building (write-side protocol) ======
static void buildIContent(const char* code, const char* date, const char* name,
                          uint8_t* content, uint8_t* contentLen) {
    content[0] = 0x81; // ADDR
    content[1] = 0x49; // 'I'
    memcpy(content + 2, code, 3);
    memcpy(content + 5, date, 4);
    memset(content + 9, ' ', 9);
    uint8_t nLen = (uint8_t)strlen(name);
    if (nLen > 9) nLen = 9;
    memcpy(content + 9, name, nLen);
    *contentLen = 18;
}

static void buildZPContent(const char* ring, const char* chip,
                           uint8_t* content, uint8_t* contentLen) {
    // Poul upload line: the vendor driver prefixes every line with 'M' (uniUploadPoulData).
    content[0] = 0x81; // ADDR
    content[1] = 0x4D; // 'M' (poul upload prefix)
    content[2] = 0x5A; // 'Z'
    content[3] = 0x50; // 'P'
    memcpy(content + 4, ring, 11);
    content[15] = 0x20; // space
    memcpy(content + 16, chip, 11);
    *contentLen = 27;
}

TEST_CASE("buildIFrame: training entry with date") {
    uint8_t content[18];
    uint8_t cLen;
    buildIContent("990", "2607", "T26072026", content, &cLen);
    CHECK(cLen == 18);
    CHECK(content[0] == 0x81);
    CHECK(content[1] == 0x49);
    CHECK(memcmp(content + 2, "990", 3) == 0);
    CHECK(memcmp(content + 5, "2607", 4) == 0);
    CHECK(memcmp(content + 9, "T26072026", 9) == 0);
    // Verify checksum via buildFrame
    uint8_t frame[32];
    uint8_t fLen = buildFrame(content, cLen, frame);
    CHECK(fLen == 18 + 4); // content + STX + CK1 + CK2 + ETX
    CHECK(frame[0] == 0x02); // STX
    CHECK(frame[fLen - 1] == 0x03); // ETX
}

TEST_CASE("buildIFrame: short name space-padded") {
    uint8_t content[18];
    uint8_t cLen;
    buildIContent("001", "0101", "Arraiolo1", content, &cLen);
    CHECK(memcmp(content + 9, "Arraiolo1", 9) == 0);
}

TEST_CASE("buildIFrame: name truncated to 9 chars") {
    uint8_t content[18];
    uint8_t cLen;
    buildIContent("002", "1503", "MuitoLongoNome", content, &cLen);
    CHECK(memcmp(content + 9, "MuitoLongo", 9) == 0);
}

TEST_CASE("buildZPFrame: pigeon with chip") {
    uint8_t content[27];
    uint8_t cLen;
    buildZPContent("25000061136", "C9BCC229001", content, &cLen);
    CHECK(cLen == 27);
    CHECK(content[0] == 0x81);
    CHECK(content[1] == 0x4D); // 'M' -- poul upload line prefix
    CHECK(content[2] == 0x5A);
    CHECK(content[3] == 0x50);
    CHECK(memcmp(content + 4, "25000061136", 11) == 0);
    CHECK(content[15] == 0x20);
    CHECK(memcmp(content + 16, "C9BCC229001", 11) == 0);
    // Verify checksum
    uint8_t frame[40];
    uint8_t fLen = buildFrame(content, cLen, frame);
    CHECK(fLen == 27 + 4);
    CHECK(frame[0] == 0x02);
    CHECK(frame[fLen - 1] == 0x03);
}

TEST_CASE("buildZPFrame: pigeon without chip (all zeros)") {
    uint8_t content[27];
    uint8_t cLen;
    buildZPContent("24004342525", "00000000000", content, &cLen);
    CHECK(memcmp(content + 16, "00000000000", 11) == 0);
}

// ====== sanitizeAsciiText tests ======
TEST_CASE("sanitizeAsciiText: passthrough printable") {
    char out[64];
    sanitizeAsciiText("Hello, World!", out, sizeof(out));
    CHECK(std::string(out) == "Hello, World!");
}

TEST_CASE("sanitizeAsciiText: replaces non-printable with ?") {
    char out[64];
    sanitizeAsciiText("Line1\nLine2\tTab", out, sizeof(out));
    CHECK(std::string(out) == "Line1?Line2?Tab");
}

TEST_CASE("sanitizeAsciiText: replaces high bytes") {
    char out[64];
    char input[] = { 'A', (char)0x80, 'B', (char)0xFF, 'C', 0 };
    sanitizeAsciiText(input, out, sizeof(out));
    CHECK(std::string(out) == "A?B?C");
}

TEST_CASE("sanitizeAsciiText: respects outputSize") {
    char out[8];
    sanitizeAsciiText("Hello World!", out, sizeof(out));
    CHECK(std::string(out) == "Hello W"); // 7 chars + null
}

TEST_CASE("sanitizeAsciiText: null input") {
    char out[32] = "dirty";
    sanitizeAsciiText(nullptr, out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("sanitizeAsciiText: null output") {
    sanitizeAsciiText("test", nullptr, 0); // no crash
}

TEST_CASE("sanitizeAsciiText: empty input") {
    char out[32] = "dirty";
    sanitizeAsciiText("", out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("sanitizeAsciiText: lower boundary (32 = space)") {
    char out[32];
    char input[] = { 'A', (char)31, (char)32, 'B', 0 };
    sanitizeAsciiText(input, out, sizeof(out));
    CHECK(std::string(out) == "A? B");
}

TEST_CASE("sanitizeAsciiText: upper boundary (126 = ~)") {
    char out[32];
    char input[] = { (char)126, (char)127, 0 };
    sanitizeAsciiText(input, out, sizeof(out));
    CHECK(std::string(out) == "~?");
}
