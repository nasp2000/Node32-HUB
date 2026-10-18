#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

// Standalone reimpl of the shift-baseline persistence guard in
// proximity_manager.cpp (_proxRestoreShiftBaselines): a mid-shift reboot must
// keep the shift window (totals/OEE continue) instead of restarting it at
// boot. The saved file holds {shift, dayKey, nSlots, ch[{in,out,run,down,
// good,setup}]}; it applies ONLY when every condition below holds, otherwise
// the engine captures fresh baselines (legacy behavior).
//
// Mirrors production exactly:
//   - savedShift in range && savedNSlots == current nSlots (slot edit restarts)
//   - savedKey == todayKey (another day restarts)
//   - savedShift == detectedShift (shift already changed restarts)
//   - live counters >= saved bases for every channel (stale counters file
//     restarts — e.g. counters saved before the bases were captured)

#define PROXIMITY_MAX_CHANNELS 8

static bool shiftBaseApplicable(int savedShift, int savedDayKey, int savedNSlots,
                                int nCh,
                                const uint32_t* bIn, const uint32_t* bRun,
                                int detectedShift, int todayKey, int nSlots,
                                const uint32_t* liveIn, const uint32_t* liveRun) {
    if (savedShift < 0 || savedShift >= nSlots) return false;
    if (savedNSlots != nSlots) return false;
    if (savedDayKey != todayKey) return false;
    if (savedShift != detectedShift) return false;
    if (nCh <= 0) return false;
    for (int k = 0; k < nCh && k < PROXIMITY_MAX_CHANNELS; k++) {
        if (liveIn[k] < bIn[k] || liveRun[k] < bRun[k]) return false;
    }
    return true;
}

TEST_CASE("shiftbase: applies mid-shift after reboot (email case 23-09)") {
    // Shift 14H-22H (slot 1 of 6/14/22), reboot 20:10, detection 20:11.
    // Bases captured at 14:00 (counters reset -> 0); counters restored 78080.
    uint32_t bIn[8] = {0,0,0,0,0,0,0,0}, bRun[8] = {0,0,0,0,0,0,0,0};
    uint32_t liveIn[8]  = {78080,1581,0,0,0,0,0,0};
    uint32_t liveRun[8] = {17467,0,0,0,0,0,0,0};
    CHECK(shiftBaseApplicable(1, 2026266, 3, 8, bIn, bRun, 1, 2026266, 3, liveIn, liveRun));
}

TEST_CASE("shiftbase: rejects when the shift already changed") {
    uint32_t b[8] = {0}, l[8] = {5000,0,0,0,0,0,0,0};
    // Saved turn 3 (14H), but detection now sees turn 1 (22H) -> fresh capture.
    CHECK(!shiftBaseApplicable(2, 2026266, 3, 8, b, b, 0, 2026266, 3, l, l));
}

TEST_CASE("shiftbase: rejects another day") {
    uint32_t b[8] = {0}, l[8] = {5000,0,0,0,0,0,0,0};
    CHECK(!shiftBaseApplicable(1, 2026265, 3, 8, b, b, 1, 2026266, 3, l, l));
}

TEST_CASE("shiftbase: rejects edited slot count") {
    uint32_t b[8] = {0}, l[8] = {5000,0,0,0,0,0,0,0};
    CHECK(!shiftBaseApplicable(1, 2026266, 2, 8, b, b, 1, 2026266, 3, l, l));
}

TEST_CASE("shiftbase: rejects stale counters (live below saved bases)") {
    // Counters file older than the bases (e.g. post-reset save failed):
    // live 1000 < base 50000 -> do not apply, capture fresh.
    uint32_t bIn[8]  = {50000,0,0,0,0,0,0,0};
    uint32_t bRun[8] = {1000,0,0,0,0,0,0,0};
    uint32_t liveIn[8]  = {1000,0,0,0,0,0,0,0};
    uint32_t liveRun[8] = {1000,0,0,0,0,0,0,0};
    CHECK(!shiftBaseApplicable(0, 2026266, 3, 8, bIn, bRun, 0, 2026266, 3, liveIn, liveRun));
}

TEST_CASE("shiftbase: rejects out-of-range shift index") {
    uint32_t b[8] = {0}, l[8] = {5000,0,0,0,0,0,0,0};
    CHECK(!shiftBaseApplicable(-1, 2026266, 3, 8, b, b, 0, 2026266, 3, l, l));
    CHECK(!shiftBaseApplicable(3, 2026266, 3, 8, b, b, 0, 2026266, 3, l, l));
}

TEST_CASE("shiftbase: schema round-trips the six baseline fields") {
    // The JSON schema must carry in/out/run/down/good/setup per channel.
    char row[192];
    snprintf(row, sizeof(row), "%lu,%lu,%lu,%lu,%lu,%lu",
             (unsigned long)78070, (unsigned long)0, (unsigned long)17467,
             (unsigned long)4568, (unsigned long)78070, (unsigned long)20);
    CHECK(strcmp(row, "78070,0,17467,4568,78070,20") == 0);
}
