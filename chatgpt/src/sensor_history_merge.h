#pragma once

#include <stdint.h>

// Pure source-selection / budget helpers for the sensor chart history stream.
// Kept header-only and dependency-free so the native tests compile it directly.
// Both handleSensorHistoryGet (source choice) and SensorConfigManager::
// streamMergedWindow (tail budget) use these as the single source of truth.

enum SensorHistorySource {
    kHistSourcePsram = 0,  // live ring buffer covers the whole window
    kHistSourceSd = 1,     // ring empty -> persisted .chart file only
    kHistSourceMerged = 2  // ring starts after fromTs -> SD head + ring tail
};

// The ring buffer owns the requested window when it holds at least one sample
// and its OLDEST sample is no newer than fromTs (the ring is contiguous after
// boot, so oldest <= fromTs means every sample from fromTs onward is live).
static inline SensorHistorySource sensorHistorySourceFor(uint32_t fromTs,
                                                         uint16_t ringCount,
                                                         uint32_t ringOldestTs) {
    if (fromTs == 0) return kHistSourcePsram;
    if (ringCount == 0) return kHistSourceSd;
    if (ringOldestTs <= fromTs) return kHistSourcePsram;
    return kHistSourceMerged;
}

// How many of the newest ring samples the merged window may keep after the
// backfilled SD head has consumed part of the maxPoints budget. UINT16_MAX
// marks "unbounded" (maxPoints==0 keeps everything the ring has).
static inline uint16_t sensorHistoryTailBudget(uint16_t maxPoints, uint16_t headCount) {
    if (maxPoints == 0) return UINT16_MAX;
    return (maxPoints > headCount) ? (uint16_t)(maxPoints - headCount) : 0;
}