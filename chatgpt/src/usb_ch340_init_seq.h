// usb_ch340_init_seq.h
// CH340/CH341 vendor-init step machine — ports the decisions of Linux kernel
// ch341.c v6.6 ch341_configure()/ch341_set_baudrate_lcr(). Pure logic, no
// Arduino or IDF dependencies, unit-tested in tests/native/test_usb_ch340_init_seq.cpp.
//
// Kernel init order (ch341_configure):
//   READ_VERSION (0x5F IN) -> SERIAL_INIT (0xA1) -> WRITE_REG(0x1312, divisor)
//     -> WRITE_REG(0x2518, LCR) ONLY when version >= 0x30 -> MODEM_CTRL (0xA4)
// The divisor value additionally gets BIT(7) only when version > 0x27.

#pragma once
#include <stdint.h>

enum Ch340InitStep {
    CH340_INIT_DONE          = 0,
    CH340_INIT_READ_VERSION  = 1,
    CH340_INIT_SERIAL_INIT   = 2,
    CH340_INIT_WRITE_DIVISOR = 3,
    CH340_INIT_WRITE_LCR2    = 4,
    CH340_INIT_MODEM_CTRL    = 5,
};

// Chips below version 0x30 (classic CH340G answers 0x27) read their line
// control from separate registers and REJECT the 0x2518 LCR2 write with a
// STALL — which permanently locks EP0. Kernel skips the write for them.
static inline bool ch340InitWritesLcr2(uint8_t version) {
    return version >= 0x30;
}

// Kernel sets BIT(7) on the divisor value only for chips newer than 0x27.
static inline uint16_t ch340InitDivisorValue(uint16_t divisor, uint8_t version) {
    return version > 0x27 ? (uint16_t)(divisor | 0x80) : divisor;
}

// Next init step after `done` completes, decided like ch341_configure().
// `version` is only meaningful once CH340_INIT_READ_VERSION has completed.
static inline Ch340InitStep ch340InitNextStep(Ch340InitStep done, uint8_t version) {
    switch (done) {
        case CH340_INIT_DONE:          return CH340_INIT_READ_VERSION;
        case CH340_INIT_READ_VERSION:  return CH340_INIT_SERIAL_INIT;
        case CH340_INIT_SERIAL_INIT:   return CH340_INIT_WRITE_DIVISOR;
        case CH340_INIT_WRITE_DIVISOR: return ch340InitWritesLcr2(version) ? CH340_INIT_WRITE_LCR2 : CH340_INIT_MODEM_CTRL;
        case CH340_INIT_WRITE_LCR2:    return CH340_INIT_MODEM_CTRL;
        case CH340_INIT_MODEM_CTRL:    return CH340_INIT_DONE;
        default:                       return CH340_INIT_DONE;
    }
}