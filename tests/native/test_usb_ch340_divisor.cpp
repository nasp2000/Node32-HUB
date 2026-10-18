// test_usb_ch340_divisor.cpp
// Unit tests for the CH340/CH341 baud divisor encoding (usb_ch340_divisor.h).
// Locks the exact divisor values the WCH devices expect (115200 -> 0xCC03,
// 9600 -> 0xB202) so the baud math cannot silently regress again.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "usb_ch340_divisor.h"

TEST_CASE("reference encodings from the WCH/MacOS/Windows driver table") {
    // Values documented in the header and required by P4_CNC acceptance:
    // GRBL must receive a real 115200 (0xCC03), not a doubled 230400.
    CHECK(ch340CalcDivisor(115200) == 0xCC03);
    CHECK(ch340CalcDivisor(9600)   == 0xB202);
}

TEST_CASE("standard RS232 rates encode consistent ps families") {
    // ps=3 family (fast), clk_div = 2^3.  div=26 -> high byte 0x100-26=0xE6.
    CHECK(ch340CalcDivisor(230400) == 0xE603);
    // div=104 -> high byte 0x100-104=0x98.
    CHECK(ch340CalcDivisor(57600)  == 0x9803);
    // ps=2 family (mid), clk_div = 2^6.  div=39 -> high byte 0x100-39=0xD9.
    CHECK(ch340CalcDivisor(19200)  == 0xD902);
    // div=156 -> high byte 0x100-156=0x64.
    CHECK(ch340CalcDivisor(4800)   == 0x6402);
    // ps=1 family (slow), clk_div = 2^9.  div=156 -> 0x6401.
    CHECK(ch340CalcDivisor(600)    == 0x6401);
    // Kernel round-to-nearest + even-divisor halving (fact 1 -> 0): 48e6/4/48 =
    // 48 (even) -> div 24, ps=3 -> 0x100-24=0xE8, real rate exactly 250000.
    CHECK(ch340CalcDivisor(250000) == 0xE803);
    // ps=3, div=156 -> 0x100-156=0x64 (real 38461, 0.16% off).
    CHECK(ch340CalcDivisor(38400)  == 0x6403);
}

TEST_CASE("edge clamps: divisor never below 2 and never above 0xFE") {
    // Very low rate sits at ps=0 with clk_div=2^12: div=117 -> 0x100-117=0x8B.
    CHECK(ch340CalcDivisor(100) == 0x8B00);
    // Absurdly high rates clamp to the minimum divisor: div=2, ps=3 -> 0xFE03.
    CHECK(ch340CalcDivisor(9216000) == 0xFE03);
    CHECK(ch340CalcDivisor(4000000) == 0xFE03);
}

TEST_CASE("encoded fields decode back to a sane real baud (within 3%)") {
    // Recompute the actual bit rate the encoded value produces and confirm it is
    // within 3% of the requested baud (the CH340 sub-clock resolution).
    auto realBaud = [](unsigned long baud) -> long {
        const uint16_t v = ch340CalcDivisor(baud);
        const uint32_t d     = 0x100 - (v >> 8);
        const uint32_t ps    = v & 0x03;
        const uint32_t fact  = (v >> 2) & 0x03;
        const uint64_t clkDiv = (uint64_t)1u << (12 - 3 * ps - fact);
        return (long)(48000000ULL / (clkDiv * d));
    };
    const long r115200 = realBaud(115200);
    CHECK(r115200 > 112000);
    CHECK(r115200 < 119000);
    const long r9600 = realBaud(9600);
    CHECK(r9600 > 9400);
    CHECK(r9600 < 9900);
    const long r57600 = realBaud(57600);
    CHECK(r57600 > 57000);
    CHECK(r57600 < 59000);
    const long r19200 = realBaud(19200);
    CHECK(r19200 > 18500);
    CHECK(r19200 < 20000);
    const long r4800 = realBaud(4800);
    CHECK(r4800 > 4660);
    CHECK(r4800 < 4950);
}