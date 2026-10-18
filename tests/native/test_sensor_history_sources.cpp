#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/sensor_history_merge.h"

// Pure source-selection / tail-budget logic used by handleSensorHistoryGet and
// SensorConfigManager::streamMergedWindow (the "show everything from boot"
// backfill for the sensor charts page). The header is dependency-free on
// purpose so the exact production logic is compiled and verified here.

TEST_CASE("source selection: live window start uses the ring") {
    // fromTs == 0 means "latest N points" — always served from PSRAM.
    CHECK(sensorHistorySourceFor(0, 0, 0) == kHistSourcePsram);
    CHECK(sensorHistorySourceFor(0, 5, 1700000000u) == kHistSourcePsram);
}

TEST_CASE("source selection: empty ring after reboot falls back to SD") {
    // No post-boot sample yet: persisted .chart only (no live tail to merge).
    CHECK(sensorHistorySourceFor(1700000000u, 0, 0) == kHistSourceSd);
    CHECK(sensorHistorySourceFor(1690000000u, 0, 42) == kHistSourceSd);
}

TEST_CASE("source selection: ring covers fromTs serves live only") {
    // Oldest retained sample <= fromTs => every sample from fromTs onward is live.
    CHECK(sensorHistorySourceFor(1700000000u, 1, 1700000000u) == kHistSourcePsram);
    CHECK(sensorHistorySourceFor(1700000000u, 100, 1699000000u) == kHistSourcePsram);
    // Ring wrapping while still covering the window keeps the live source.
    CHECK(sensorHistorySourceFor(1699900000u, 2016, 1699800000u) == kHistSourcePsram);
}

TEST_CASE("source selection: ring newer than fromTs requires merge") {
    // Rebooted mid-window: the ring starts after fromTs, so the SD head has to
    // be backfilled (this is the twin of the pre-fix proximity dashboard bug).
    CHECK(sensorHistorySourceFor(1700000000u, 1, 1700060000u) == kHistSourceMerged);
    CHECK(sensorHistorySourceFor(1690000000u, 50, 1700000000u) == kHistSourceMerged);
}

TEST_CASE("tail budget: unbounded when maxPoints==0") {
    CHECK(sensorHistoryTailBudget(0, 0) == UINT16_MAX);
    CHECK(sensorHistoryTailBudget(0, 288) == UINT16_MAX);
}

TEST_CASE("tail budget: head alone consumes the whole budget") {
    CHECK(sensorHistoryTailBudget(288, 288) == 0);
    CHECK(sensorHistoryTailBudget(72, 100) == 0);   // head capped, nothing left
}

TEST_CASE("tail budget: leftover after the head is reserved for the tail") {
    CHECK(sensorHistoryTailBudget(288, 120) == 168);
    CHECK(sensorHistoryTailBudget(72, 6) == 66);
}

TEST_CASE("head + tail never exceeds maxPoints") {
    // The .chart reader caps the head at maxPoints before the budget runs, so
    // the caller-planned head is always <= maxPoints. Simulate that cap, then
    // check the invariant budget paths keep head + tail within bounds.
    for (uint16_t maxPoints : {uint16_t(72), uint16_t(144), uint16_t(288)}) {
        for (uint16_t rawHead = 0; rawHead <= maxPoints + 64; rawHead += 7) {
            uint16_t headCap = (rawHead < maxPoints) ? rawHead : maxPoints;
            uint16_t tail = sensorHistoryTailBudget(maxPoints, headCap);
            CHECK((uint16_t)(headCap + tail) <= maxPoints);
        }
    }
}