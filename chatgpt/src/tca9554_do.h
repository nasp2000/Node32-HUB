// tca9554_do.h
// Driver for TCA9554 I2C digital output expander (8 open-drain outputs).
// Active only when BOARD_HAS_TCA9554_DO == 1 (defined in board_pins.h for the
// Waveshare 8DI8DO POE board). Zero impact on N16R8 or any other board.

#pragma once
#include "feature_flags.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include <Arduino.h>

// Number of DO channels exposed by the TCA9554.
#define TCA9554_DO_CHANNELS  8

// I2C pins — shared with the rest of the I2C bus on the 8DI8DO board.
#ifndef TCA9554_SDA_PIN
#define TCA9554_SDA_PIN  42
#endif
#ifndef TCA9554_SCL_PIN
#define TCA9554_SCL_PIN  41
#endif

// I2C address default (A0=A1=A2=GND -> 0x20). Override in board_pins.h if needed.
#ifndef TCA9554_I2C_ADDR
#define TCA9554_I2C_ADDR  0x20
#endif

// Initialize Wire, configure all 8 pins as outputs, and restore last saved state.
// Safe to call even if Wire was already initialized by screen or sensor code.
void tca9554Begin();

// Set a single output (ch: 0-7, state: true = HIGH / relay energised).
// Returns true on I2C ACK.
bool tca9554SetOutput(uint8_t ch, bool state);

// Set all 8 outputs from a bitmask (bit 0 = CH0 ... bit 7 = CH7).
// Returns true on I2C ACK.
bool tca9554SetAll(uint8_t mask);

// Return cached output bitmask (no I2C read needed).
uint8_t tca9554GetMask();

// Set the per-channel inversion mask. When bit N is set, the physical output of
// channel N is the logical state inverted (for active-low loads). The current
// mask is re-applied immediately. Software-only inversion: the chip's polarity
// register stays 0x00, so the cached mask always holds the LOGICAL state.
void tca9554SetInvert(uint8_t invertMask);

// Return the current inversion mask.
uint8_t tca9554GetInvert();

// Persist current output state to LittleFS /config/tca9554_do.json.
void tca9554SaveState();

// Load last saved state from LittleFS and apply it to the chip.
void tca9554LoadState();

#endif // BOARD_HAS_TCA9554_DO
