// rtc_ds3231.cpp
// DS3231 I2C real-time clock driver.
// See rtc_ds3231.h for the public API.

#include "rtc_ds3231.h"

#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231 == 1

#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "touch7_i2c.h" // bus-free guard (GT911 can hold SDA pre-reset)
#endif

// DS3231 7-bit I2C address (A0=A1=A2=GND).
#define DS3231_ADDR  0x68

// I2C bus alias: Touch-LCD-7 runs all sensors on Wire1 (port 1) because the
// display stack owns I2C0 (touch + CH422G). Everywhere else keeps Wire.
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#define RTC_WIRE Wire1
#else
#define RTC_WIRE Wire
#endif

// I2C pins — share the same bus as TCA9554 and environmental sensors.
// Resolved from LCD_SDA/SCL which are set per-board in board_pins.h.
#ifndef DS3231_SDA_PIN
  #ifdef LCD_SDA
    #define DS3231_SDA_PIN  LCD_SDA
  #else
    #define DS3231_SDA_PIN  42
  #endif
#endif
#ifndef DS3231_SCL_PIN
  #ifdef LCD_SCL
    #define DS3231_SCL_PIN  LCD_SCL
  #else
    #define DS3231_SCL_PIN  41
  #endif
#endif

static bool s_present = false;

// BCD conversion helpers.
static inline uint8_t bcdToDec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static inline uint8_t decToBcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool rtcPresent() { return s_present; }

bool rtcRead(struct tm* dest) {
    RTC_WIRE.beginTransmission(DS3231_ADDR);
    RTC_WIRE.write(0x00); // point to register 0 (seconds)
    if (RTC_WIRE.endTransmission(false) != 0) return false;
    if (RTC_WIRE.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) < 7) return false;

    uint8_t sec  = bcdToDec(RTC_WIRE.read() & 0x7F);
    uint8_t min  = bcdToDec(RTC_WIRE.read() & 0x7F);
    uint8_t hour = bcdToDec(RTC_WIRE.read() & 0x3F); // 24h mode: bit6=0 forced
    RTC_WIRE.read();                                  // day-of-week (unused)
    uint8_t mday = bcdToDec(RTC_WIRE.read() & 0x3F);
    uint8_t mon  = bcdToDec(RTC_WIRE.read() & 0x1F);
    uint8_t yr   = bcdToDec(RTC_WIRE.read());         // 00-99 -> 2000-2099

    memset(dest, 0, sizeof(struct tm));
    dest->tm_sec  = sec;
    dest->tm_min  = min;
    dest->tm_hour = hour;
    dest->tm_mday = mday;
    dest->tm_mon  = mon - 1;    // struct tm: months 0-11
    dest->tm_year = yr + 100;   // struct tm: years since 1900
    return true;
}

bool rtcWrite(const struct tm* src) {
    RTC_WIRE.beginTransmission(DS3231_ADDR);
    RTC_WIRE.write(0x00);
    RTC_WIRE.write(decToBcd((uint8_t)src->tm_sec));
    RTC_WIRE.write(decToBcd((uint8_t)src->tm_min));
    RTC_WIRE.write(decToBcd((uint8_t)src->tm_hour));          // 24h, bit6=0
    RTC_WIRE.write(decToBcd((uint8_t)(src->tm_wday + 1)));    // DS3231: 1-7
    RTC_WIRE.write(decToBcd((uint8_t)src->tm_mday));
    RTC_WIRE.write(decToBcd((uint8_t)(src->tm_mon + 1)));     // DS3231: 1-12
    RTC_WIRE.write(decToBcd((uint8_t)(src->tm_year - 100)));  // DS3231: 00-99
    return RTC_WIRE.endTransmission() == 0;
}

bool rtcApplyToSystem() {
    struct tm t;
    if (!rtcRead(&t)) return false;
    if (t.tm_year < 120) {
        // Year < 2020: chip was never programmed or lost its battery.
        Serial.printf("[RTC] DS3231 skipped: year %d < 2020 (RTC not set)\n", t.tm_year + 1900);
        return false;
    }
    struct timeval tv;
    tv.tv_sec  = mktime(&t);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    Serial.printf("[RTC] DS3231: system time set to %04d-%02d-%02d %02d:%02d:%02d UTC\n",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}

// ---------------------------------------------------------------------------
// SNTP notification callback — runs in lwIP task context.
// Writes NTP-synced UTC time back to DS3231 to keep it accurate.
// Must be fast and allocation-free; no ESP logging stack here.
// ---------------------------------------------------------------------------
static void _rtcSntpCallback(struct timeval* tv) {
    struct tm utc;
    gmtime_r(&tv->tv_sec, &utc);
    if (rtcWrite(&utc)) {
        Serial.printf("[RTC] DS3231 updated from NTP: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
            utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
            utc.tm_hour, utc.tm_min, utc.tm_sec);
    } else {
        Serial.println("[RTC] DS3231 NTP write-back FAILED (I2C error)");
    }
}

// ---------------------------------------------------------------------------

void rtcBegin() {
    // Wire may already be initialized by tca9554Begin(); calling begin() with
    // the same pins again is idempotent on ESP32 Arduino. On Touch-LCD-7 the
    // bus is Wire1 (started by Touch7Display::begin); re-begin is idempotent.
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // T7-24c: local I2C unsupported on TOUCH7 (shared-bus deadlock — see
    // sensor_aht20_bmp280.cpp). Never transact; report absent.
    Serial.println(F("[RTC] local I2C disabled on TOUCH7 (shared bus)"));
    s_present = false;
    return;
#endif
    RTC_WIRE.begin(DS3231_SDA_PIN, DS3231_SCL_PIN);

    // Probe: a zero-byte write with ACK means the chip is present.
    RTC_WIRE.beginTransmission(DS3231_ADDR);
    s_present = (RTC_WIRE.endTransmission() == 0);

    if (!s_present) {
        Serial.printf("[RTC] DS3231 NOT found (SDA=%d SCL=%d addr=0x%02X)\n",
            DS3231_SDA_PIN, DS3231_SCL_PIN, DS3231_ADDR);
        return;
    }
    Serial.printf("[RTC] DS3231 detected (SDA=%d SCL=%d)\n", DS3231_SDA_PIN, DS3231_SCL_PIN);

    // Register SNTP write-back: first NTP sync and every subsequent sync will
    // update the DS3231 to keep it accurate over time.
    sntp_set_time_sync_notification_cb(_rtcSntpCallback);

    // Apply DS3231 time to the system clock now, before NTP is available.
    rtcApplyToSystem();
}

#endif // BOARD_HAS_DS3231
