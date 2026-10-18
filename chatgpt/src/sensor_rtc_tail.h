#pragma once

#include <stdint.h>

// RTC-tail helpers for the sensor history recovery layers:
//   1. RTC_RAM mirror (no-wear, survives esp_restart/OTA/WDT/deep sleep) — the
//      newest ring samples mirrored every reading and re-seeded at boot.
//   2. SD daily .chart head — power-cut recovery up to the last flush (the
//      merge window already backfills it on the charts page).
// Dependency-free by design so the exact production math is compiled and
// verified by the native suite (see tests/native/test_sensor_rtc_tail.cpp).

// Newest ring samples kept in the RTC mirror. 96 samples ≈ 96 min at the
// default 1 min sample cadence and fits the 8 KB S3 RTC_SLOW_MEM with margin.
#define SENSOR_RTC_TAIL_SAMPLES 96

// Magic guards the RTC area against garbage on a power-on (RTC_NOINIT_ATTR is
// not cleared). Written last, after the samples, in the mirror loop.
#define SENSOR_RTC_MAGIC    0x53454254UL   // "SEBT"
#define SENSOR_RTC_VERSION  1U

// How many of the NEWEST samples to keep (clamped to maxSamples).
static inline uint16_t sensorTailLen(uint16_t ringCount, uint16_t maxSamples) {
    return (ringCount < maxSamples) ? ringCount : maxSamples;
}

// Wrapped buffer index of the OLDEST sample within the newest-n span (n must
// already be clamped with sensorTailLen). Ring addressing matches
// SensorHistory::getPoint: when not full the points start at 0; when full they
// start at ringHead (oldest). Callers walk (start + i) % capacity for i in [0,n).
static inline uint16_t sensorTailStartPos(uint16_t ringHead, uint16_t ringCount,
                                          uint16_t capacity, uint16_t n) {
    if (capacity == 0) return 0;
    if (n > ringCount) n = ringCount;
    if (n == 0) return 0;
    return (ringCount < capacity) ? (uint16_t)(ringCount - n)
                                  : (uint16_t)((ringHead + ringCount - n) % capacity);
}

// True when the RTC mirror header is coherent (magic/version/count sane) and
// can seed the ring.
static inline bool sensorRtcTailValid(uint32_t magic, uint32_t version, uint16_t count) {
    return magic == SENSOR_RTC_MAGIC
        && version == SENSOR_RTC_VERSION
        && count > 0 && count <= SENSOR_RTC_TAIL_SAMPLES;
}