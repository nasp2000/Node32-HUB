#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "charset_conversion.h"

TEST_CASE("decodeUtf8Byte - ASCII single bytes") {
    resetUtf8Decoder();
    for (uint32_t c = 0x20; c <= 0x7E; c++) {
        CHECK(decodeUtf8Byte((uint8_t)c) == c);
    }
    CHECK(decodeUtf8Byte((uint8_t)'A') == 0x41);
    CHECK(decodeUtf8Byte((uint8_t)'z') == 0x7A);
    CHECK(decodeUtf8Byte((uint8_t)'0') == 0x30);
}

TEST_CASE("decodeUtf8Byte - two-byte sequences") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xC3) == 0);       // start of 2-byte
    CHECK(decodeUtf8Byte(0xA9) == 0x00E9);  // é = U+00E9
}

TEST_CASE("decodeUtf8Byte - three-byte sequences") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xE2) == 0);       // start of 3-byte
    CHECK(decodeUtf8Byte(0x82) == 0);       // continuation
    CHECK(decodeUtf8Byte(0xAC) == 0x20AC);  // € = U+20AC
}

TEST_CASE("decodeUtf8Byte - four-byte sequences") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xF0) == 0);       // start of 4-byte
    CHECK(decodeUtf8Byte(0x9F) == 0);       // continuation
    CHECK(decodeUtf8Byte(0x98) == 0);       // continuation
    CHECK(decodeUtf8Byte(0x8E) == 0x1F60E); // 😎 = U+1F60E
}

TEST_CASE("decodeUtf8Byte - invalid continuation byte") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xC3) == 0);
    CHECK(decodeUtf8Byte(0x00) == 0xFFFD);  // 0x00 is not a valid continuation
}

TEST_CASE("decodeUtf8Byte - invalid first byte") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0x80) == 0xFFFD);  // continuation byte with no start
    // 0xC0/0xC1 are accepted as 2-byte starters (overlong lenient)
    CHECK(decodeUtf8Byte(0xF8) == 0xFFFD);  // 5-byte start (not supported)
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xFE) == 0xFFFD);  // invalid
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xFF) == 0xFFFD);  // invalid
}

TEST_CASE("decodeUtf8Byte - reset between sequences") {
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0xC3) == 0);
    resetUtf8Decoder();
    CHECK(decodeUtf8Byte(0x41) == 0x41);    // 'A', not affected by prior state
}

TEST_CASE("unicodeToLatin1 - direct mapping") {
    CHECK(unicodeToLatin1(0x00) == 0x00);
    CHECK(unicodeToLatin1(0x41) == 0x41);   // 'A'
    CHECK(unicodeToLatin1(0xFF) == 0xFF);   // ÿ in Latin-1
    CHECK(unicodeToLatin1(0x80) == 0x80);   // control char in Latin-1
}

TEST_CASE("unicodeToLatin1 - out of range") {
    CHECK(unicodeToLatin1(0x100) == 0x3F);  // '?' for beyond Latin-1
    CHECK(unicodeToLatin1(0xFFFD) == 0x3F);
    CHECK(unicodeToLatin1(0x20AC) == 0x3F); // € not in Latin-1
}

TEST_CASE("unicodeToCP850 - Portuguese characters") {
    CHECK(unicodeToCP850(0x00E7) == 0x87);  // ç
    CHECK(unicodeToCP850(0x00E9) == 0x82);  // é
    CHECK(unicodeToCP850(0x00E3) == 0x3F);  // ã -> not in CP850 table, expect '?'
    CHECK(unicodeToCP850(0x00F5) == 0x3F);  // õ -> not in CP850 table, expect '?'
    CHECK(unicodeToCP850(0x00C7) == 0x80);  // Ç
    CHECK(unicodeToCP850(0x00E1) == 0xA0);  // á
    CHECK(unicodeToCP850(0x00ED) == 0xA1);  // í
    CHECK(unicodeToCP850(0x00F3) == 0xA2);  // ó
    CHECK(unicodeToCP850(0x00FA) == 0xA3);  // ú
    CHECK(unicodeToCP850(0x00F1) == 0xA4);  // ñ
    CHECK(unicodeToCP850(0x00D1) == 0xA5);  // Ñ
}

TEST_CASE("unicodeToCP850 - box drawing") {
    CHECK(unicodeToCP850(0x2502) == 0xB3);  // │
    CHECK(unicodeToCP850(0x2500) == 0xC4);  // ─
    CHECK(unicodeToCP850(0x2514) == 0xC0);  // └
    CHECK(unicodeToCP850(0x250C) == 0xDC);  // ┌
    CHECK(unicodeToCP850(0x2510) == 0xBF);  // ┐
}

TEST_CASE("unicodeToCP850 - Greek letters") {
    CHECK(unicodeToCP850(0x03B1) == 0xE1);  // α
    CHECK(unicodeToCP850(0x03C0) == 0xE4);  // π
    CHECK(unicodeToCP850(0x03A3) == 0xE5);  // Σ
    CHECK(unicodeToCP850(0x03A9) == 0xEB);  // Ω
}

TEST_CASE("unicodeToCP850 - Math symbols") {
    CHECK(unicodeToCP850(0x221E) == 0xED);  // ∞
    CHECK(unicodeToCP850(0x2265) == 0xF3);  // ≥
    CHECK(unicodeToCP850(0x2264) == 0xF4);  // ≤
    CHECK(unicodeToCP850(0x00B1) == 0xF2);  // ±
    CHECK(unicodeToCP850(0x00F7) == 0xF7);  // ÷
    CHECK(unicodeToCP850(0x221A) == 0xFC);  // √
    CHECK(unicodeToCP850(0x00B0) == 0xF9);  // °
}

TEST_CASE("unicodeToCP850 - ASCII passthrough") {
    for (uint32_t c = 0x20; c <= 0x7E; c++) {
        CHECK(unicodeToCP850(c) == (uint8_t)c);
    }
}

TEST_CASE("unicodeToCP850 - unmapped characters") {
    CHECK(unicodeToCP850(0x1F600) == 0x3F); // emoji -> '?'
    CHECK(unicodeToCP850(0x0000) == 0x3F);  // null -> '?'
    CHECK(unicodeToCP850(0x0008) == 0x3F);  // backspace -> '?'
}

TEST_CASE("unicodeToCP850 - non-breaking space to normal space") {
    CHECK(unicodeToCP850(0x00A0) == 0x20);  // NBSP -> space
}
