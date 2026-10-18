#pragma once

// touch7_i2c.h — shared-bus guard for TouchMeteo-7 (I2C pins 8/9 carry BOTH
// the panel's legacy-port-0 traffic (touch GT911 + CH422G) and Arduino
// Wire1 (port 1) sensors). Before the panel resets the touch, the GT911 can
// hold SDA low; ANY Wire1 transaction then blocks inside the IDF driver and
// the boot hangs in sensor begin() until the 120 s task watchdog reboots
// the board (proven 16-09-2026: "AHT begin..." then silence, TWDT at ~138s).
//
// Rule: on TOUCH7, never transact before checking the bus is free, and retry
// hardware begins later (post-panel, bus guaranteed clean).

#include "feature_flags.h"

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1

#include <Arduino.h>

// True when the bus idles free (SDA AND SCL HIGH twice in a row). Pure GPIO
// reads, never starts a transaction, so it cannot hang. SDA = GPIO8,
// SCL = GPIO9 on this board.
// T7-24: BOTH lines are checked. The old SDA-only check passed on a
// clock-held bus (GT911 stretching/holding SCL pre-panel while SDA floats
// HIGH via pullups) and the first Wire1 transaction then blocked inside the
// IDF driver until the 120 s task watchdog (proven 17-09-2026: T7-24 build
// hung deterministically at AHT begin while 1133 — whose early Wire1.begin
// accidentally detached the pins before probing — booted fine).
static inline bool touch7I2CBusFree() {
    pinMode(8, INPUT);
    pinMode(9, INPUT);
    delay(2);
    if (digitalRead(8) == LOW || digitalRead(9) == LOW) return false;
    delay(2);
    return digitalRead(8) != LOW && digitalRead(9) != LOW;
}

// Bounded presence probe (single cheap transaction). Call ONLY after
// touch7I2CBusFree() returned true.
static inline bool touch7I2CProbe(TwoWire& w, uint8_t addr) {
    w.beginTransmission(addr);
    return w.endTransmission() == 0;
}

// Claim Wire1 (port 1) on the shared pins 8/9 @100kHz. Call ONLY post-panel,
// after touch7I2CBusFree(), from the sensor manager that is about to
// transact — never at boot/display-begin. T7-24: claiming the second I2C
// controller pre-panel leaves two masters muxed to one bus while GT911 +
// CH422G run on port 0, so Wire1 stays off the bus until a real sensor
// needs it (display stack then owns port 0 uncontended).
static inline void touch7Wire1Begin(TwoWire& w) {
    w.begin(8, 9);
    w.setClock(100000);
}

// True once the RGB panel is streaming (VSYNC ISR live on every frame).
// Non-sensor modules can cheaply test "panel up" via bus-free (flash-free);
// sensor managers use this instead of a direct display-core dependency.
extern bool touch7RgbStreaming();

#endif // TARGET_BOARD_S3_TOUCH7
