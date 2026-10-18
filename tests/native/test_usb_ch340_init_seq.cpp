// test_usb_ch340_init_seq.cpp
// Unit tests for the CH340 vendor-init step machine (usb_ch340_init_seq.h).
// Locks the Linux kernel ch341.c configure order and the version gates that
// fix the P4_CNC CH340G board: LCR2 (0x2518) is only issued for version >= 0x30
// and the divisor value only gets BIT(7) for version > 0x27.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "usb_ch340_init_seq.h"
#include <vector>

// Walk the real production step machine with a mock transport: when the
// READ_VERSION step is reached the mock chip answers with `mockVersion`
// (0x27 for a classic CH340G, 0x30 for a modern CH341). Records every step.
static std::vector<Ch340InitStep> runMockInit(uint8_t mockVersion) {
    std::vector<Ch340InitStep> out;
    uint8_t ver = 0;
    Ch340InitStep step = ch340InitNextStep(CH340_INIT_DONE, ver);
    while (step != CH340_INIT_DONE) {
        out.push_back(step);
        if (step == CH340_INIT_READ_VERSION) ver = mockVersion;
        step = ch340InitNextStep(step, ver);
    }
    return out;
}

TEST_CASE("CH340G (version 0x27) init skips the LCR2 write") {
    // Classic CH340G answers 0x27 to the 0x5F version read: kernel order without
    // the 0x2518 write (which this chip REJECTS with a STALL that locks EP0)
    // and without BIT(7) on the divisor.
    const std::vector<Ch340InitStep> seq = runMockInit(0x27);
    const std::vector<Ch340InitStep> expect = {
        CH340_INIT_READ_VERSION,
        CH340_INIT_SERIAL_INIT,
        CH340_INIT_WRITE_DIVISOR,
        CH340_INIT_MODEM_CTRL,
    };
    CHECK(seq == expect);

    CHECK(ch340InitWritesLcr2(0x27) == false);
    CHECK(ch340InitDivisorValue(0xCC03, 0x27) == 0xCC03); // 115200 encoding unchanged
}

TEST_CASE("CH341 (version 0x30) keeps LCR2 + divisor BIT(7)") {
    // Modern CH341: separate-register path is gone, so the kernel writes LCR2
    // and sets BIT(7) on the divisor value.
    const std::vector<Ch340InitStep> seq = runMockInit(0x30);
    const std::vector<Ch340InitStep> expect = {
        CH340_INIT_READ_VERSION,
        CH340_INIT_SERIAL_INIT,
        CH340_INIT_WRITE_DIVISOR,
        CH340_INIT_WRITE_LCR2,
        CH340_INIT_MODEM_CTRL,
    };
    CHECK(seq == expect);

    CHECK(ch340InitWritesLcr2(0x30) == true);
    CHECK(ch340InitDivisorValue(0xCC03, 0x30) == 0xCC83); // BIT7 set
}

TEST_CASE("gates follow the kernel thresholds exactly") {
    // LCR2 begins exactly at version 0x30.
    CHECK(ch340InitWritesLcr2(0x2F) == false);
    CHECK(ch340InitWritesLcr2(0x30) == true);
    CHECK(ch340InitWritesLcr2(0x31) == true);
    CHECK(ch340InitWritesLcr2(0x27) == false);

    // BIT(7) kicks in strictly above 0x27 and never double-sets.
    CHECK(ch340InitDivisorValue(0xB202, 0x27) == 0xB202);
    CHECK(ch340InitDivisorValue(0xB202, 0x28) == 0xB282);
    CHECK(ch340InitDivisorValue(0xCC83, 0x30) == 0xCC83);
}

TEST_CASE("init sequence always terminates for any chip version") {
    // Version read failures fall back to 0x30; every possible byte value still
    // produces a terminating step walk (no infinite loop, DONE is always hit).
    for (uint32_t v = 0; v <= 0xFF; v++) {
        uint8_t fed = 0;
        Ch340InitStep step = ch340InitNextStep(CH340_INIT_DONE, fed);
        int guard = 0;
        while (step != CH340_INIT_DONE && guard < 64) {
            if (step == CH340_INIT_READ_VERSION) fed = (uint8_t)v;
            step = ch340InitNextStep(step, fed);
            guard++;
        }
        CHECK(step == CH340_INIT_DONE);
    }
}