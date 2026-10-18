// test_touch7_logic.cpp — TouchMeteo-7 pure-logic tests (host doctest).
// Covers touch7_peers_logic.h: clamp, poll cadence, backoff, condition
// groups, theme/clock ids, lux curve, I2C map, WAV header parsing.
// Standalone: no Arduino headers required.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header directly (framework-independent, stdint only).
#include "../../chatgpt/src/touch7_peers_logic.h"

TEST_CASE("clamp int") {
    CHECK(touch7ClampInt(5, 0, 10) == 5);
    CHECK(touch7ClampInt(-3, 0, 10) == 0);
    CHECK(touch7ClampInt(99, 0, 10) == 10);
    CHECK(touch7ClampInt(0, 0, 2) == 0);
    CHECK(touch7ClampInt(2, 0, 2) == 2);
}

TEST_CASE("peer poll cadence by count") {
    CHECK(touch7PeerPollIntervalSec(0) == 10);
    CHECK(touch7PeerPollIntervalSec(1) == 10);
    CHECK(touch7PeerPollIntervalSec(2) == 10);
    CHECK(touch7PeerPollIntervalSec(3) == 15);
    CHECK(touch7PeerPollIntervalSec(4) == 20);
    CHECK(touch7PeerPollIntervalSec(9) == 20);
}

TEST_CASE("peer backoff grows and caps") {
    CHECK(touch7PeerBackoffSec(1, 0) == 10);
    CHECK(touch7PeerBackoffSec(2, 0) == 20);
    CHECK(touch7PeerBackoffSec(3, 0) == 40);
    CHECK(touch7PeerBackoffSec(4, 0) == 80);
    CHECK(touch7PeerBackoffSec(9, 0) == 120);
    CHECK(touch7PeerBackoffSec(9, 1) == 123); // slot jitter
    CHECK(touch7PeerBackoffSec(0, 0) == 10);
}

TEST_CASE("condition groups follow Open-Meteo codes") {
    CHECK(touch7CondGroup(0) == TOUCH7_COND_CLEAR);
    CHECK(touch7CondGroup(1) == TOUCH7_COND_PARTLY);
    CHECK(touch7CondGroup(2) == TOUCH7_COND_PARTLY);
    CHECK(touch7CondGroup(3) == TOUCH7_COND_CLOUDY);
    CHECK(touch7CondGroup(45) == TOUCH7_COND_FOG);
    CHECK(touch7CondGroup(48) == TOUCH7_COND_FOG);
    CHECK(touch7CondGroup(51) == TOUCH7_COND_RAIN);
    CHECK(touch7CondGroup(61) == TOUCH7_COND_RAIN);
    CHECK(touch7CondGroup(80) == TOUCH7_COND_RAIN);
    CHECK(touch7CondGroup(71) == TOUCH7_COND_SNOW);
    CHECK(touch7CondGroup(77) == TOUCH7_COND_SNOW);
    CHECK(touch7CondGroup(85) == TOUCH7_COND_SNOW);
    CHECK(touch7CondGroup(95) == TOUCH7_COND_STORM);
    CHECK(touch7CondGroup(99) == TOUCH7_COND_STORM);
    CHECK(touch7CondGroup(1234) == TOUCH7_COND_CLOUDY); // unknown -> cloudy
}

TEST_CASE("theme and clock ids") {
    CHECK(touch7ThemeValid(0));   // Claro
    CHECK(touch7ThemeValid(2));   // Escuro
    CHECK(touch7ThemeValid(3));   // Sky
    CHECK(touch7ThemeValid(5));   // Bright (last)
    CHECK(!touch7ThemeValid(6));  // one past the end
    CHECK(!touch7ThemeValid(-1));
    CHECK(touch7ClockValid(0));
    CHECK(touch7ClockValid(4));
    CHECK(!touch7ClockValid(5));
}

TEST_CASE("deferred config save uses hidden-screen window and safe fallback") {
    CHECK(!touch7DeferredSaveDue(2999, 0, 0, true));
    CHECK(touch7DeferredSaveDue(3000, 0, 0, true));
    CHECK(!touch7DeferredSaveDue(59999, 0, 0, false));
    CHECK(touch7DeferredSaveDue(60000, 0, 0, false));
}

TEST_CASE("deferred config save retries are throttled and millis wrap is safe") {
    CHECK(!touch7DeferredSaveDue(10000, 0, 9000, true));
    CHECK(touch7DeferredSaveDue(39000, 0, 9000, true));
    const uint32_t dirtyBeforeWrap = UINT32_MAX - 1000U;
    CHECK(!touch7DeferredSaveDue(1998U, dirtyBeforeWrap, 0, true));
    CHECK(touch7DeferredSaveDue(1999U, dirtyBeforeWrap, 0, true));
}

TEST_CASE("remote pointer watchdog releases stale presses, including at millis wrap") {
    CHECK(!touch7RemotePointerExpired(500U, 0U));
    CHECK(touch7RemotePointerExpired(501U, 0U));
    const uint32_t pressedBeforeWrap = UINT32_MAX - 100U;
    CHECK(!touch7RemotePointerExpired(399U, pressedBeforeWrap));
    CHECK(touch7RemotePointerExpired(400U, pressedBeforeWrap));
}

TEST_CASE("lux to backlight curve") {
    CHECK(touch7LuxToBacklight(-5.0f) == 20);
    CHECK(touch7LuxToBacklight(0.0f) == 20);
    CHECK(touch7LuxToBacklight(10.0f) == 35);
    CHECK(touch7LuxToBacklight(100.0f) == 55);
    CHECK(touch7LuxToBacklight(400.0f) == 75);
    CHECK(touch7LuxToBacklight(2000.0f) == 100);
}

TEST_CASE("shared I2C map") {
    CHECK(touch7I2CAddrExpected(0x38));  // AHT20
    CHECK(touch7I2CAddrExpected(0x76));  // BMP280
    CHECK(touch7I2CAddrExpected(0x77));  // BMP280 alt
    CHECK(touch7I2CAddrExpected(0x23));  // BH1750
    CHECK(touch7I2CAddrExpected(0x68));  // DS3231
    CHECK(touch7I2CAddrExpected(0x40));  // INA226
    CHECK(touch7I2CAddrExpected(0x5D));  // GT911 touch
    CHECK(touch7I2CAddrExpected(0x20));  // CH422G
    CHECK(!touch7I2CAddrExpected(0x3C)); // no OLED on this board
    CHECK(!touch7I2CAddrExpected(0x50));
}

TEST_CASE("threshold alarms bitmask") {
    CHECK(touch7AlarmCheck(28.0f, 50.0f, 27.5f, 18.0f, 70.0f) == TOUCH7_ALARM_THOT);
    CHECK(touch7AlarmCheck(17.0f, 50.0f, 27.5f, 18.0f, 70.0f) == TOUCH7_ALARM_TCOLD);
    CHECK(touch7AlarmCheck(22.0f, 80.0f, 27.5f, 18.0f, 70.0f) == TOUCH7_ALARM_THUM);
    CHECK(touch7AlarmCheck(22.0f, 50.0f, 27.5f, 18.0f, 70.0f) == TOUCH7_ALARM_NONE);
    // Combined bits.
    uint8_t m = touch7AlarmCheck(30.0f, 90.0f, 27.5f, 18.0f, 70.0f);
    CHECK((m & TOUCH7_ALARM_THOT) != 0);
    CHECK((m & TOUCH7_ALARM_THUM) != 0);
    CHECK((m & TOUCH7_ALARM_TCOLD) == 0);
}

TEST_CASE("threshold alarms degenerate config disables") {
    // t_max <= 0 disables the heat alarm.
    CHECK(touch7AlarmCheck(99.0f, 50.0f, 0.0f, 18.0f, 70.0f) == TOUCH7_ALARM_NONE);
    // t_min >= t_max disables the cold alarm (but heat still fires).
    CHECK(touch7AlarmCheck(10.0f, 50.0f, 27.5f, 30.0f, 70.0f) == TOUCH7_ALARM_NONE);
    CHECK(touch7AlarmCheck(99.0f, 50.0f, 27.5f, 30.0f, 70.0f) == TOUCH7_ALARM_THOT);
    // h_max <= 0 disables the humidity alarm.
    CHECK(touch7AlarmCheck(22.0f, 99.0f, 27.5f, 18.0f, 0.0f) == TOUCH7_ALARM_NONE);
}

TEST_CASE("remote tap clamp 800x480") {
    int ox = -1, oy = -1;
    CHECK(touch7ClampTap(102, 446, &ox, &oy));
    CHECK(ox == 102);
    CHECK(oy == 446);
    CHECK(touch7ClampTap(0, 0, &ox, &oy));
    CHECK(touch7ClampTap(799, 479, &ox, &oy));
    CHECK(!touch7ClampTap(800, 100, &ox, &oy));
    CHECK(!touch7ClampTap(100, 480, &ox, &oy));
    CHECK(!touch7ClampTap(-1, 0, &ox, &oy));
    CHECK(!touch7ClampTap(0, -5, 0, 0));
}

TEST_CASE("wav header: valid 16-bit mono 16k") {
    uint8_t h[44] = {0};
    h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F';
    h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
    h[20]=1;                       // PCM
    h[22]=1;                       // mono
    h[24]=0x80; h[25]=0x3E;        // 16000 Hz
    h[34]=16;                      // 16-bit
    h[40]=0x00; h[41]=0x7D;        // 32000 data bytes
    Touch7WavInfo r = touch7ParseWavHeader(h, 44);
    CHECK(r.ok);
    CHECK(r.channels == 1);
    CHECK(r.sampleRate == 16000u);
    CHECK(r.bits == 16);
    CHECK(r.dataBytes == 32000u);
}

TEST_CASE("wav header: 8-bit stereo accepted, garbage rejected") {
    uint8_t h[44] = {0};
    h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F';
    h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
    h[20]=1; h[22]=2; h[24]=0x44; h[25]=0xAC; // 44.1k stereo? rate bytes
    h[25]=0xAC; h[26]=0x00; h[27]=0x00;
    h[34]=8; h[40]=0x10;
    Touch7WavInfo r = touch7ParseWavHeader(h, 44);
    CHECK(r.ok);
    CHECK(r.channels == 2);

    uint8_t bad[44] = {0};
    Touch7WavInfo b = touch7ParseWavHeader(bad, 44);
    CHECK(!b.ok);
    Touch7WavInfo t = touch7ParseWavHeader(h, 20); // truncated
    CHECK(!t.ok);
    Touch7WavInfo n = touch7ParseWavHeader(0, 44); // null
    CHECK(!n.ok);
}
