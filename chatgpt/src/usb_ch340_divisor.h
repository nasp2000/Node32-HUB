// usb_ch340_divisor.h
// CH340/CH341 baud-rate divisor encoding — ported 1:1 from Linux kernel
// drivers/usb/serial/ch341.c v6.6 (ch341_get_divisor). Pure logic, no Arduino
// or IDF dependencies, unit-tested in tests/native/test_usb_ch340_divisor.cpp.
//
// Line speed:  baud = 48000000 / (2^(12 - 3*ps - fact) * div), where
//              0 <= ps <= 3, 0 <= fact <= 1, 2 <= div <= 256 (fact=0) / 9..256 (fact=1)
// Encoding:    return value = (0x100 - div) << 8 | fact << 2 | ps
//
// Steps mirrored from the kernel: clamp speed to the supported [46, 3000000]
// range; pick the highest prescaler whose minimum rate the baud exceeds; start
// with the fastest base clock (fact=1) and halve it (fact=0, div/=2, clk_div*=2)
// if the divisor falls outside [9,255]; round the divisor to the nearest rate;
// finally prefer the lower base clock again when the divisor is even (divisor
// halved, fact=0) so the receiver is more tolerant of timing errors.
//
// Reference encodings (CH340G @ 48 MHz) — same as the WCH drivers program:
//   115200 → ps=3, fact=0, div=52  → 0xCC03
//   9600   → ps=2, fact=0, div=78  → 0xB202

#pragma once
#include <stdint.h>

static inline uint16_t ch340CalcDivisor(unsigned long baud) {
    static const uint32_t minRate[4] = { 46, 366, 2930, 23438 };

    if (baud < 46)        baud = 46;
    if (baud > 3000000UL) baud = 3000000UL;

    uint32_t fact = 1;
    int ps;
    for (ps = 3; ps >= 0; ps--) {
        if (baud > minRate[ps]) break;
    }
    if (ps < 0) ps = 0;

    uint64_t clkDiv = (uint64_t)1u << (12 - 3 * ps - (int)fact);
    uint64_t div = 48000000ULL / (clkDiv * baud);

    if (div < 9 || div > 255) {
        div /= 2;
        clkDiv *= 2;
        fact = 0;
    }
    if (div < 2)   div = 2;
    if (div > 256) div = 256;

    if (16ULL * 48000000ULL / (clkDiv * div) - 16ULL * baud >=
        16ULL * baud - 16ULL * 48000000ULL / (clkDiv * (div + 1))) {
        div++;
    }

    if (fact == 1 && (div & 1u) == 0) {
        div /= 2;
        fact = 0;
    }

    return (uint16_t)(((0x100 - (uint32_t)div) << 8) | (fact << 2) | (uint32_t)ps);
}