#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Standalone reimpl of proximityBuildHourlyChart's CSV row parsing + per-hour
// accumulation, used to verify Bug 1 fix (last hour filled) AND Bug 2 fix
// (delta attributed to the PREVIOUS sample's hour, not the current tick's hour,
// so the first/last shift hour are no longer left at 0):
//   "o sdcard só e escrito de 30 em 30 minutos" -> a shift ending on the hour
//   leaves the final hour with NO tick (always 0). Fix: a final "tick" row is
//   written immediately at the shift boundary (real sensor snapshot), so the
//   last hour column is filled. The parser counts ONLY "tick" rows (real sensor
//   counts), never "shift_end" (which holds a per-shift delta, not an absolute).
//
// Bug 2: each delta spans prevTick..thisTick, so it belongs to the hour the
// production actually happened in (prevHour), not the current tick's hour.

struct ChartState {
    long prevIn;
    int  prevHour;
    int  prevMin;
    long hourVal[24];
};

// Mirrors proximityBuildHourlyChart's inner loop for a single line.
// Window membership (hour columns [startHour, startHour+durHours)) defaults to
// the full day for the early window-agnostic tests; the reset rule below is
// identical in both: a negative delta means the counter was reset since the
// previous row, so ch0 (production since the reset) is attributed to the
// previous row's hour when charted, else to the current row's hour.
// Positive deltas are gap-aware: they are credited to every bucket the sample
// pair covers, proportional to the minutes spent there (a lost tick must not
// dump hours of production into one column).
static bool hourInWindow(int h, int startHour, int durHours) {
    if (h < 0 || h > 23 || durHours <= 0) return false;
    for (int k = 0; k < durHours && k < 24; k++) {
        if ((startHour + k) % 24 == h) return true;
    }
    return false;
}
static void parseLine(ChartState& st, const char* line, int startHour = 0, int durHours = 24) {
    char* p = (char*)line;
    while (*p == ' ' || *p == '\r') p++;
    if (*p) {
        char* c1 = strchr(p, ',');
        if (c1 && c1 - p >= 14) {
            char* c2 = strchr(c1 + 1, ',');
            if (c2) {
                char* c3 = strchr(c2 + 1, ',');
                if (c3) {
                    size_t typeLen = (size_t)(c2 - (c1 + 1));
                    bool isTick = (typeLen == 4) && strncmp(c1 + 1, "tick", 4) == 0;
                    bool isBoundary = (typeLen == 14) && strncmp(c1 + 1, "shift_boundary", 14) == 0;
                    if (isTick || isBoundary) {
                        long ch0 = strtol(c3 + 1, nullptr, 10);
                        int hour = (p[11] - '0') * 10 + (p[12] - '0');
                        int minute = (c1 - p >= 16) ? ((p[14] - '0') * 10 + (p[15] - '0')) : 0;
                        if (minute < 0 || minute > 59) minute = 0;
                        if (hour >= 0 && hour <= 23 && st.prevIn >= 0 && st.prevHour >= 0) {
                            long delta = ch0 - st.prevIn;
                            int attrHour = st.prevHour;
                            if (delta < 0) {
                                delta = ch0;
                                attrHour = hourInWindow(st.prevHour, startHour, durHours)
                                    ? st.prevHour : hour;
                                if (st.hourVal[attrHour] < 0) st.hourVal[attrHour] = 0;
                                st.hourVal[attrHour] += delta;
                            } else {
                                int prevMin = (st.prevMin < 0) ? 0 : st.prevMin;
                                int spanMin = (hour * 60 + minute) - (st.prevHour * 60 + prevMin);
                                if (spanMin < 0) spanMin += 1440;   // crossed midnight
                                if (spanMin == 0) spanMin = 1;      // sub-minute sample pair
                                int firstBucketMin = 60 - prevMin;
                                if (firstBucketMin <= 0 || firstBucketMin > 60) firstBucketMin = 60;
                                int consumed = 0;
                                long credited = 0;
                                for (int k = 0; k < 24 && consumed < spanMin; k++) {
                                    int hh = (st.prevHour + k) % 24;
                                    int bucketMin = (k == 0) ? firstBucketMin : 60;
                                    int take = spanMin - consumed;
                                    if (take > bucketMin) take = bucketMin;
                                    long share = (long)((double)delta * (double)take / (double)spanMin + 0.5);
                                    if (share > delta - credited) share = delta - credited;
                                    if (share < 0) share = 0;
                                    if (hourInWindow(hh, startHour, durHours)) {
                                        if (st.hourVal[hh] < 0) st.hourVal[hh] = 0;
                                        st.hourVal[hh] += share;
                                    }
                                    credited += share;
                                    consumed += take;
                                }
                                if (credited < delta && hourInWindow(hour, startHour, durHours)) {
                                    if (st.hourVal[hour] < 0) st.hourVal[hour] = 0;
                                    st.hourVal[hour] += (delta - credited);
                                }
                            }
                        }
                        st.prevIn = ch0;
                        if (hour >= 0 && hour <= 23) st.prevHour = hour;
                        st.prevMin = minute;
                    }
                }
            }
        }
    }
}

TEST_CASE("Bug1: last tick before shift-end fills final hour (no shift_end needed)") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;  // unset
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // Shift 06:00->14:00, ticks every 30 min. Last tick at 13:42 = 9844.
    // Final tick written at shift boundary 14:00:05 = 9950.
    parseLine(st, "2026-08-21 13:42:32,tick,,9844,0,24.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-21 14:00:05,tick,,9950,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");

    // Bug 2: delta of 9950-9844 = 106 belongs to the PREVIOUS hour (13), not 14.
    CHECK(st.hourVal[13] == 106);
    CHECK(st.hourVal[14] == -1);  // 14:00 tick has no prior tick in window -> no delta here
}

TEST_CASE("Bug1: shift_end row is IGNORED by the chart parser") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = 9844;
    st.prevHour = 13;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // shift_end holds a per-shift delta (not absolute), must not be counted.
    parseLine(st, "2026-08-21 14:00:05,shift_end,1,9844,0,24.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    CHECK(st.hourVal[14] == -1);  // unchanged (row ignored)
    CHECK(st.prevIn == 9844);     // unchanged
    CHECK(st.prevHour == 13);     // unchanged
}

TEST_CASE("extended tick row (run/down/good cols) parses like the legacy row") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;  // unset
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // Same values as Bug1 but with the 6-column layout (run/down/good appended
    // per channel): ch0_in stays at the same fixed position, buckets identical.
    parseLine(st, "2026-08-21 13:42:32,tick,,9844,0,24.0,5000,100,9000,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0");
    parseLine(st, "2026-08-21 14:00:05,tick,,9950,0,12.0,5100,120,9100,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0");
    CHECK(st.hourVal[13] == 106);
    CHECK(st.hourVal[14] == -1);
}

TEST_CASE("boot row is ignored by the chart parser") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = 9844;
    st.prevHour = 13;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // Boot marker (live snapshot + reset-reason code in the shift slot) must
    // not pollute hourly buckets or baselines.
    parseLine(st, "2026-08-21 13:42:32,boot,3,9844,0,24.0,5000,100,9000,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0");
    CHECK(st.hourVal[13] == -1);  // unchanged
    CHECK(st.prevIn == 9844);     // unchanged
    CHECK(st.prevHour == 13);     // unchanged
}

TEST_CASE("shift_boundary row is processed like tick by the chart parser") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = 9844;
    st.prevHour = 13;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // shift_boundary holds absolute counts (like tick), used at shift transition.
    parseLine(st, "2026-08-21 14:00:05,shift_boundary,,9970,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    // Delta 9970-9844=126 attributed to prevHour=13 (the last hour of the ended shift).
    CHECK(st.hourVal[13] == 126);
    CHECK(st.prevIn == 9970);     // updated to boundary ch0
    CHECK(st.prevHour == 14);     // updated to boundary hour
}

TEST_CASE("Bug1: real production tick accumulates per hour") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    parseLine(st, "2026-08-21 15:12:33,tick,,1756,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-21 16:12:33,tick,,3489,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    // delta 3489-1756 = 1733 covers 15:12:33 -> 16:12:33, i.e. 48 min in hour 15
    // and 12 min in hour 16 (gap-aware attribution: proportional to minutes).
    CHECK(st.hourVal[15] + st.hourVal[16] == 1733);
    CHECK(st.hourVal[15] > 1300);   // ~48/60 of the window
    CHECK(st.hourVal[16] > 0);      // ~12/60 of the window
}

// Bug 2 regression: night shift 22:00->08:00. First (22h) and last (07h) hours
// must NOT be left at 0 due to the 1-hour attribution shift.
TEST_CASE("Bug2: night shift 22h-08h first/last hour not zero") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // Ticks every 30 min from 22:00 to 08:00 (next day). ch0_in rising.
    // 22:00 baseline captured as first delta source.
    parseLine(st, "2026-08-23 22:00:00,tick,,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-23 22:30:00,tick,,1000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-23 23:00:00,tick,,2000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-23 23:30:00,tick,,3000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-24 07:30:00,tick,,9000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");
    parseLine(st, "2026-08-24 08:00:00,tick,,9500,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0");

    // Delta 0->1000 (tick 22:30) + 1000->2000 (tick 23:00) both land on hour 22
    // (prevHour 22) -> hourVal[22] accumulates to 2000.
    CHECK(st.hourVal[22] == 2000);
    // The 23:30->07:30 gap (6000 over 480 min) is spread across every bucket it
    // covers (~750 each) instead of dumping it all into hour 23; the 23:00->23:30
    // tick (1000) still belongs to hour 23 alone.
    CHECK(st.hourVal[23] == 1375);        // 1000 + 30/480 of the gap
    CHECK(st.hourVal[0] > 0);             // overnight hours are filled, not 0
    CHECK(st.hourVal[3] > 0);
    // Total preserved (2000 + 1000 + spread 6000 + 500).
    long sum = 0;
    for (int h = 0; h < 24; h++) if (st.hourVal[h] > 0) sum += st.hourVal[h];
    CHECK(sum == 9500);
    // The first shift hour (22h) is NON-ZERO (was 0 before Bug 2 fix).
    CHECK(st.hourVal[22] > 0);
    // The last shift hour (07h) also gets its delta -> NON-ZERO.
    CHECK(st.hourVal[7] > 0);
}

// Reset rule: the first partial hour after a counter reset is counted.
// Shift auto-reset at the 14:00 boundary zeroes counters; the 13:59 boundary
// row holds the old absolute (110180), the 14:30 tick the new one (7640).
// Window 14..21 (actual shift hours): prevHour=13 is not charted, so the
// 7640 lands on the current row's hour (14).
TEST_CASE("reset: first partial hour after shift auto-reset is counted") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    parseLine(st, "2026-09-23 13:59:00,shift_boundary,,110180,0,360.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0", 14, 8);
    parseLine(st, "2026-09-23 14:30:00,tick,,7640,0,120.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0", 14, 8);
    CHECK(st.hourVal[14] == 7640);
    parseLine(st, "2026-09-23 15:00:00,tick,,15910,0,360.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0", 14, 8);
    // Normal delta 15910-7640=8270 belongs to prevHour=14 (charted).
    CHECK(st.hourVal[14] == 7640 + 8270);
}

// Reset rule: mid-shift manual reset attributes to the previous hour when charted.
TEST_CASE("reset: mid-shift manual reset attributes to previous charted hour") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = 50000;
    st.prevHour = 14;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    parseLine(st, "2026-09-23 15:10:00,tick,,1200,0,240.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0", 14, 8);
    CHECK(st.hourVal[14] == 1200);
    CHECK(st.hourVal[15] == -1);
}

// Reset rule: reboot without counter restore restarts accumulation.
TEST_CASE("reset: reboot without restore counts post-boot production") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = 78070;
    st.prevHour = 20;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    parseLine(st, "2026-09-23 20:30:00,tick,,500,0,240.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0", 14, 8);
    CHECK(st.hourVal[20] == 500);
}

// Full replication of proximityBuildHourlyChart's loop + the corrected end-hour
// fix, used to verify the day-shift-end boundary case (UID 18: shift ending
// exactly on the hour with the previous tick at :30 of the penultimate hour,
// so the final interval must be re-credited to the last shift hour).
struct ChartFull {
    long hourVal[24];
    long prevIn, prevPrevIn, lastCh0;
    int  prevHour, prevPrevHour, lastHour;
    long boundaryDelta;
    int  boundaryFromHour;
};
static void buildChartFixed(ChartFull& st, int startHour, int durHours,
                            const char* const* lines, int n) {
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;
    st.prevIn = st.prevPrevIn = st.lastCh0 = -1;
    st.prevHour = st.prevPrevHour = st.lastHour = -1;
    st.boundaryDelta = -1;
    st.boundaryFromHour = -1;
    int lastShiftHour = (startHour + durHours - 1) % 24;
    for (int i = 0; i < n; i++) {
        const char* line = lines[i];
        char* p = (char*)line;
        while (*p == ' ' || *p == '\r') p++;
        if (*p) {
            char* c1 = strchr(p, ',');
            if (c1 && c1 - p >= 14) {
                char* c2 = strchr(c1 + 1, ',');
                if (c2) {
                    char* c3 = strchr(c2 + 1, ',');
                    if (c3) {
                        size_t typeLen = (size_t)(c2 - (c1 + 1));
                        bool isTick = (typeLen == 4) && strncmp(c1 + 1, "tick", 4) == 0;
                        bool isBoundary = (typeLen == 14) && strncmp(c1 + 1, "shift_boundary", 14) == 0;
                        if (isTick || isBoundary) {
                            long ch0 = strtol(c3 + 1, nullptr, 10);
                            int hour = (p[11] - '0') * 10 + (p[12] - '0');
                            if (hour >= 0 && hour <= 23 && st.prevIn >= 0 && st.prevHour >= 0) {
                                long delta = ch0 - st.prevIn;
                                int attrHour = st.prevHour;
                                if (delta < 0) {
                                    // Counter reset (mirror of production C2 rule).
                                    delta = ch0;
                                    attrHour = hourInWindow(st.prevHour, startHour, durHours)
                                        ? st.prevHour : hour;
                                } else if (hour == lastShiftHour && st.prevHour != lastShiftHour && st.prevHour >= 0) {
                                    st.boundaryDelta = delta;
                                    st.boundaryFromHour = st.prevHour;
                                }
                                if (st.hourVal[attrHour] < 0) st.hourVal[attrHour] = 0;
                                st.hourVal[attrHour] += delta;
                            }
                            st.prevPrevIn = st.prevIn;
                            st.prevPrevHour = st.prevHour;
                            st.prevIn = ch0;
                            st.prevHour = (hour >= 0 && hour <= 23) ? hour : st.prevHour;
                            st.lastCh0 = ch0;
                            st.lastHour = (hour >= 0 && hour <= 23) ? hour : st.lastHour;
                        }
                    }
                }
            }
        }
    }
    if (st.lastHour >= 0 && st.prevPrevHour >= 0) {
        if (st.boundaryDelta >= 0 && st.boundaryFromHour >= 0
                && st.boundaryFromHour != lastShiftHour) {
            int hourDiff = (lastShiftHour - st.boundaryFromHour + 24) % 24;
            if (hourDiff == 1) {
                if (st.hourVal[st.boundaryFromHour] >= st.boundaryDelta) st.hourVal[st.boundaryFromHour] -= st.boundaryDelta;
                else st.hourVal[st.boundaryFromHour] = 0;
                if (st.hourVal[lastShiftHour] < 0) st.hourVal[lastShiftHour] = 0;
                st.hourVal[lastShiftHour] += st.boundaryDelta;
            }
        }
        if (st.prevPrevIn >= 0 && st.lastCh0 >= 0) {
            long delta = st.lastCh0 - st.prevPrevIn;
            if (delta < 0) delta = 0;
            if (st.prevPrevHour != lastShiftHour && delta > 0) {
                bool alreadyMoved = (st.boundaryDelta >= 0
                                     && st.prevPrevHour == st.boundaryFromHour
                                     && delta == st.boundaryDelta);
                if (!alreadyMoved) {
                    if (st.hourVal[st.prevPrevHour] >= delta) st.hourVal[st.prevPrevHour] -= delta;
                    else st.hourVal[st.prevPrevHour] = 0;
                    if (st.hourVal[lastShiftHour] < 0) st.hourVal[lastShiftHour] = 0;
                    st.hourVal[lastShiftHour] += delta;
                }
            }
        }
    }
}

TEST_CASE("Bug2+: day shift ending on the hour fills last hour (UID 18: 15h->22h)") {
    // 30-min ticks from 15:00 to 22:00; shift-end flush tick exactly at 22:00.
    // Cumulative ch0_in rising by 17500 each half-hour (total 245000).
    const char* lines[] = {
        "2026-08-25 15:00:00,tick,,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 15:30:00,tick,,17500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 16:00:00,tick,,35000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 16:30:00,tick,,52500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 17:00:00,tick,,70000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 17:30:00,tick,,87500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 18:00:00,tick,,105000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 18:30:00,tick,,122500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 19:00:00,tick,,140000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 19:30:00,tick,,157500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 20:00:00,tick,,175000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 20:30:00,tick,,192500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 21:00:00,tick,,210000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 21:30:00,tick,,227500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:00,tick,,245000,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0"
    };
    ChartFull st;
    // Parser-level window 15..22 (explicit args, independent of the production
    // shift-window layout): last shift hour = 22.
    buildChartFixed(st, 15, 8, lines, 15);
    // Before the fix the final interval (21:30->22:00) landed on hour 21,
    // leaving hour 22 at 0. After the fix it is re-credited to hour 22.
    CHECK(st.hourVal[22] > 0);
    // The penultimate hour keeps its own production (not the final interval).
    CHECK(st.hourVal[21] > 0);
    // The first shift hour is also non-zero.
    CHECK(st.hourVal[15] > 0);
    // Sum of shift hours must equal the total reported production (245000).
    long sum = 0;
    for (int k = 0; k < 8; k++) { int h = (15 + k) % 24; if (st.hourVal[h] > 0) sum += st.hourVal[h]; }
    CHECK(sum == 245000);
}

// Regression: double-tick at the shift-end hour (periodic aligned + flush tick).
// Both ticks at 22:00 — the delta from 21:30->22:00 must land in hour 22, not 21.
TEST_CASE("Bug2+: double tick at shift-end hour (periodic + flush at 22:00)") {
    const char* lines[] = {
        "2026-08-25 21:00:00,tick,,210000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 21:30:00,tick,,227500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:00,tick,,245000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:15,tick,,245000,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0"
    };
    ChartFull st;
    buildChartFixed(st, 15, 8, lines, 4);
    // Delta 227500->245000 = 17500 must land in hour 22 (last shift hour), not 21.
    CHECK(st.hourVal[22] == 17500);
    CHECK(st.hourVal[21] > 0);
    CHECK(st.hourVal[22] > 0);
}

// Triple tick at shift-end hour — all deltas within the last hour must stay there.
// Mirrors proximityBuildHourlyChart's bar render (lighter production visual, same
// as the shift email's stacked time bar): U+2593 DARK SHADE (filled) +
// U+2592 MEDIUM SHADE (planned-stop share) + U+2591 LIGHT SHADE (empty track).
// Returns the bracket-wrapped bar for one hour.
static const char* BLOCK = "\xE2\x96\x93";
static const char* PLAN = "\xE2\x96\x92";
static const char* SHADE = "\xE2\x96\x91";

static std::string utf8(const char* seg, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += seg;
    return out;
}

// Mirrors proximityBuildHourlyChart's render loop WITH the planned-stop overlay:
// planned share first (absolute time fraction, rounded), production fills what
// is left. planSec=0 reproduces the pre-overlay output exactly (identity).
static std::string renderBar(long v, long maxV, long planSec = 0) {
    const int barLen = 10;
    int plan = (int)(((long)barLen * planSec + 1800L) / 3600L);
    if (plan > barLen) plan = barLen;
    int avail = barLen - plan;
    int fill = (maxV > 0 && avail > 0) ? (int)((long)avail * v / maxV) : 0;
    if (v > 0 && fill < 1 && avail > 0) fill = 1;
    std::string out = "[";
    for (int b = 0; b < barLen; b++) {
        const char* seg = (b < fill) ? BLOCK : ((b < fill + plan) ? PLAN : SHADE);
        out += seg;
    }
    out += "]";
    return out;
}

TEST_CASE("hourly bar render: full production uses FULL BLOCK only") {
    CHECK(renderBar(100, 100) == "[" + utf8(BLOCK, 10) + "]");
}

TEST_CASE("hourly bar render: zero production uses LIGHT SHADE track") {
    CHECK(renderBar(0, 100) == "[" + utf8(SHADE, 10) + "]");
}

TEST_CASE("hourly bar render: half value fills half the block track") {
    CHECK(renderBar(50, 100) == "[" + utf8(BLOCK, 5) + utf8(SHADE, 5) + "]");
}

TEST_CASE("hourly bar render: tiny non-zero value still draws one block") {
    // v=1 -> fill=0 -> bumped to 1 -> [▓░░░░░░░░░]
    CHECK(renderBar(1, 1000) == "[" + utf8(BLOCK, 1) + utf8(SHADE, 9) + "]");
}

TEST_CASE("hourly bar render: 30-min planned stop takes half the bar (max hour)") {
    // 17H-like case at max cadence: plan=(10*1800+1800)/3600=5, avail=5,
    // fill=5*17710/17710=5 -> [5xBLOCK + 5xPLAN], no SHADE left.
    CHECK(renderBar(17710, 17710, 1800) == "[" + utf8(BLOCK, 5) + utf8(PLAN, 5) + "]");
}

TEST_CASE("hourly bar render: sub-max hour keeps production share + stop share") {
    // User's 17H case: v=17340 maxV=17710 planSec=1800 -> plan=5, avail=5,
    // fill=5*17340/17710=4 -> [4xBLOCK + 5xPLAN + 1xSHADE].
    CHECK(renderBar(17340, 17710, 1800) == "[" + utf8(BLOCK, 4) + utf8(PLAN, 5) + utf8(SHADE, 1) + "]");
}

TEST_CASE("hourly bar render: full-hour stop shows only PLAN even with pieces") {
    // plan=(10*3600+1800)/3600=10 -> avail=0 -> fill=0 regardless of v.
    CHECK(renderBar(5000, 17710, 3600) == "[" + utf8(PLAN, 10) + "]");
}

TEST_CASE("hourly bar render: 15-min stop rounds to 3 segments") {
    // plan=(10*900+1800)/3600=10800/3600=3, avail=7, fill=7*17710/17710=7.
    CHECK(renderBar(17710, 17710, 900) == "[" + utf8(BLOCK, 7) + utf8(PLAN, 3) + "]");
}

TEST_CASE("hourly bar render: zero production with stop shows stop only") {
    // v=0 -> fill stays 0 (min-1 rule needs v>0); plan=5 -> [5xPLAN + 5xSHADE].
    CHECK(renderBar(0, 17710, 1800) == "[" + utf8(PLAN, 5) + utf8(SHADE, 5) + "]");
}

TEST_CASE("hourly row: pcs suffix present and bars start at the same column") {
    // Mirrors proximityBuildHourlyChart's row format: "%02dH %*d pcs" + "  [".
    // numW = digits of the shift max (here 16240 -> 5); the count is
    // right-aligned so every row's bar opens at the same column.
    char r1[64], r2[64], r3[64];
    snprintf(r1, sizeof(r1), "%02dH %c%*d pcs  [", 7, ' ', 5, 690);
    snprintf(r2, sizeof(r2), "%02dH %c%*d pcs  [", 8, ' ', 5, 15680);
    snprintf(r3, sizeof(r3), "%02dH %c%*d pcs  [", 14, '~', 5, 16240);
    std::string s1(r1), s2(r2), s3(r3);
    CHECK(s1.find(" pcs") != std::string::npos);
    CHECK(s1.find("690 pcs") != std::string::npos);
    CHECK(s2.find("15680 pcs") != std::string::npos);
    size_t b1 = s1.find('['), b2 = s2.find('['), b3 = s3.find('[');
    CHECK(b1 == b2);
    CHECK(b2 == b3);
    CHECK(s1 == "07H    690 pcs  [");
    CHECK(s2 == "08H  15680 pcs  [");
    // An interpolated hour carries the "~" mark in the same 1-char column.
    CHECK(s3 == "14H ~16240 pcs  [");
    CHECK(s3.find("~") != std::string::npos);
}

TEST_CASE("shift email bars: production uses DARK SHADE, never FULL BLOCK") {
    // Owner pick: lighter black in the shift-end email. Production = U+2593,
    // planned = U+2592, empty = U+2591; U+2588 must not appear.
    CHECK(std::string(BLOCK) == "\xE2\x96\x93");
    CHECK(std::string(PLAN) == "\xE2\x96\x92");
    CHECK(std::string(SHADE) == "\xE2\x96\x91");
    std::string bar = renderBar(15680, 16240);
    CHECK(bar.find("\xE2\x96\x88") == std::string::npos);
    CHECK(bar.find(BLOCK) != std::string::npos);
}

// ── Lost-tick gaps (owner report 2026-10-20) ─────────────────────────────
// Today's emails/chart showed empty hours and ONE huge hour: a 30-min tick
// stopped being written (reboot / SD busy / clock jump), so the next delta
// covered several hours and the whole span landed in a single column.
// The delta is now spread over the buckets it actually covers.
TEST_CASE("gap: 6h without ticks spreads the delta instead of one huge hour") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // Real 20-10 case, shift 06H-14H: last tick 08:00:02 = 1368048, next row
    // 13:59:00 = 1433688 (delta 65640 over 5h59m, no ticks in between).
    parseLine(st, "2026-10-20 08:00:02,tick,,1368048,0,360.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 6, 8);
    parseLine(st, "2026-10-20 13:59:00,shift_boundary,,1433688,0,240.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 6, 8);
    // 08:00 -> 13:59 covers buckets 8,9,10,11,12,13 (6 columns, ~60 min each).
    for (int h = 8; h <= 13; h++) {
        CHECK(st.hourVal[h] > 9000);
        CHECK(st.hourVal[h] < 13000);
    }
    // No single column absorbed the whole span.
    for (int h = 0; h < 24; h++) CHECK(st.hourVal[h] < 65640);
    // Sum preserved (no pieces lost or duplicated).
    long sum = 0;
    for (int h = 0; h < 24; h++) if (st.hourVal[h] > 0) sum += st.hourVal[h];
    CHECK(sum == 65640);
}

TEST_CASE("gap: partial-hour buckets get the proportional share") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    // 01:00:01 (1286658) -> 05:59:00 (1340538): delta 53880 over 4h59m.
    parseLine(st, "2026-10-20 01:00:01,tick,,1286658,0,120.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 0, 24);
    parseLine(st, "2026-10-20 05:59:00,shift_boundary,,1340538,0,120.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 0, 24);
    long sum = 0;
    for (int h = 0; h < 24; h++) if (st.hourVal[h] > 0) sum += st.hourVal[h];
    CHECK(sum == 53880);
    // 01H..04H are nearly full buckets, 05H gets the last 59 min.
    CHECK(st.hourVal[1] > st.hourVal[5]);
    CHECK(st.hourVal[5] > 9000);
    CHECK(st.hourVal[2] > 9000);
}

TEST_CASE("gap: normal 30-min tick still lands in a single hour (no regression)") {
    ChartState st;
    memset(&st, 0, sizeof(st));
    st.prevIn = -1;
    st.prevHour = -1;
    for (int h = 0; h < 24; h++) st.hourVal[h] = -1;

    parseLine(st, "2026-10-20 09:00:01,tick,,1000,0,300.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 6, 8);
    parseLine(st, "2026-10-20 09:30:01,tick,,3000,0,300.0,0,0,0.0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0,0,0,0.0,0,0,0", 6, 8);
    CHECK(st.hourVal[9] == 2000);
    CHECK(st.hourVal[10] == -1);
}

TEST_CASE("Bug2+: triple tick at shift-end hour stays in last hour") {
    const char* lines[] = {
        "2026-08-25 21:00:00,tick,,210000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 21:30:00,tick,,227500,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:00,tick,,240000,0,36.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:08,tick,,243000,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0",
        "2026-08-25 22:00:15,tick,,245000,0,12.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0,0,0,0.0"
    };
    ChartFull st;
    buildChartFixed(st, 15, 8, lines, 5);
    // Boundary delta: 227500->240000=12500 moves from hour 21 to hour 22.
    // Deltas within hour 22: 240000->243000=3000 + 243000->245000=2000 = 5000.
    // Total hour 22 = 12500 + 5000 = 17500.
    CHECK(st.hourVal[22] == 17500);
    // Hour 21 keeps its own production (21:00->21:30 delta = 17500).
    CHECK(st.hourVal[21] > 0);
}
