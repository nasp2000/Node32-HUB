#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/sensor_rtc_tail.h"

// Pure RTC-tail math used by SensorConfigManager::updateHistory (the "no-wear
// RTC RAM mirror" recovery layer). Dependency-free header compiled as-is so the
// exact production formulas (newest-K selection over the wrapped ring + header
// validity) are verified here.

TEST_CASE("tail len: clamps to max and to the ring count") {
    CHECK(sensorTailLen(0, SENSOR_RTC_TAIL_SAMPLES) == 0);
    CHECK(sensorTailLen(5, SENSOR_RTC_TAIL_SAMPLES) == 5);
    CHECK(sensorTailLen(2016, SENSOR_RTC_TAIL_SAMPLES) == SENSOR_RTC_TAIL_SAMPLES);
    CHECK(sensorTailLen(300, SENSOR_RTC_TAIL_SAMPLES) == SENSOR_RTC_TAIL_SAMPLES);
}

TEST_CASE("tail start: not-full ring starts at count - n (linear layout)") {
    // 5 samples, capacity 2016, head 5: the newest 3 are logical 2..4 -> pos 2
    CHECK(sensorTailStartPos(5, 5, 2016, 3) == 2);
    // n == count -> whole ring, oldest at 0
    CHECK(sensorTailStartPos(5, 5, 2016, 5) == 0);
    // empty ring -> no tail
    CHECK(sensorTailStartPos(5, 0, 2016, 0) == 0);
    // small non-full ring, n=2
    CHECK(sensorTailStartPos(4, 4, 64, 2) == 2);
}

TEST_CASE("tail start: full ring starts at the oldest wrapped slot") {
    // capacity 4, head 2, count 4: physical buffer holds b2,b3,b0,b1 old->new
    CHECK(sensorTailStartPos(2, 4, 4, 4) == 2);   // whole ring
    CHECK(sensorTailStartPos(2, 4, 4, 1) == 1);   // newest only  -> pos (2+4-1)%4 = 1
    CHECK(sensorTailStartPos(2, 4, 4, 3) == 3);   // newest 3     -> pos (2+4-3)%4 = 3
    CHECK(sensorTailStartPos(0, 4, 4, 4) == 0);   // head at 0, full
    // n larger than the ring count is clamped to the count first
    CHECK(sensorTailStartPos(2, 4, 4, 200) == 2);
}

TEST_CASE("tail start: edge guards") {
    CHECK(sensorTailStartPos(0, 0, 0, 0) == 0);   // capacity 0
    CHECK(sensorTailStartPos(3, 2, 8, 12) == 0);  // n clamped to 2, not full -> 2-2
    CHECK(sensorTailStartPos(7, 7, 8, 1) == 6);   // not full (7<8), newest only
}

TEST_CASE("rtc tail header validity rejects garbage and bad versions") {
    CHECK(sensorRtcTailValid(SENSOR_RTC_MAGIC, SENSOR_RTC_VERSION, 1));
    CHECK(sensorRtcTailValid(SENSOR_RTC_MAGIC, SENSOR_RTC_VERSION, SENSOR_RTC_TAIL_SAMPLES));
    CHECK_FALSE(sensorRtcTailValid(0, SENSOR_RTC_VERSION, 5));                    // power-on garbage magic
    CHECK_FALSE(sensorRtcTailValid(SENSOR_RTC_MAGIC, 0, 5));                      // old version
    CHECK_FALSE(sensorRtcTailValid(SENSOR_RTC_MAGIC, SENSOR_RTC_VERSION, 0));     // empty mirror
    CHECK_FALSE(sensorRtcTailValid(SENSOR_RTC_MAGIC, SENSOR_RTC_VERSION, SENSOR_RTC_TAIL_SAMPLES + 1)); // over run
}