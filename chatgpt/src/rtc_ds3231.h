// rtc_ds3231.h
// Driver for DS3231 I2C real-time clock.
// Active only when BOARD_HAS_DS3231 == 1 (defined in board_pins.h for the
// Waveshare 8DI8DO POE board). Zero impact on N16R8 or any other board.
//
// Responsibilities:
//   - Apply DS3231 time to the ESP32 system clock at boot (before NTP sync).
//   - Write NTP-synced UTC time back to DS3231 via SNTP notification callback.
//
// Depends on: Wire (I2C), esp_sntp.h, littlefs_manager (none — no persistence needed).

#pragma once
#include "feature_flags.h"

#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231 == 1

#include <Arduino.h>

// Initialize Wire (safe even if already started by TCA9554), probe chip,
// register SNTP write-back callback, and apply RTC time to ESP32 system clock.
void rtcBegin();

// Return true if DS3231 was detected on the I2C bus at rtcBegin() time.
bool rtcPresent();

// Read current UTC time from DS3231 registers into dest.
// Returns false on I2C error.
bool rtcRead(struct tm* dest);

// Write UTC time to DS3231 registers.
// Returns false on I2C error.
bool rtcWrite(const struct tm* src);

// Apply DS3231 time to the ESP32 system clock via settimeofday().
// Skips if year < 2020 (chip was never set or lost power/battery).
// Returns true if time was applied.
bool rtcApplyToSystem();

#endif // BOARD_HAS_DS3231
