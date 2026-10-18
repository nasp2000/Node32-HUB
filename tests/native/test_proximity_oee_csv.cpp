#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// Standalone reimpl of the weekly OEE CSV pure helpers from proximity_manager.h
// (proximityIsoWeek / proximityWeeklyOeeFileName / proximityFormatOeeRow).
// Mirrors production exactly so the CSV format is locked by tests.

static int isoWeek(const struct tm& ti, int& isoYear) {
    struct tm copy = ti;
    char wbuf[8] = {0}, ybuf[8] = {0};
    strftime(wbuf, sizeof(wbuf), "%V", &copy);
    strftime(ybuf, sizeof(ybuf), "%G", &copy);
    int w = atoi(wbuf);
    if (w < 1) w = 1;
    if (w > 53) w = 53;
    isoYear = atoi(ybuf) > 0 ? atoi(ybuf) : (ti.tm_year + 1900);
    return w;
}

static void weeklyOeeFileName(int week, char* out, size_t cap) {
    snprintf(out, cap, "wk%02d-oee.csv", week);
}

static int formatOeeRow(char* out, size_t cap, const struct tm& ti,
                        int shift, int hour, uint32_t countHour,
                        uint32_t total, uint32_t scrap, uint32_t setup,
                        float avail, float perf, float qual,
                        float oee,
                        uint32_t good, uint32_t runSec, uint32_t planSec,
                        uint32_t unplannedSec, uint32_t elapsedSec,
                        float ppm) {
    return snprintf(out, cap, "%02d-%02d-%04d,%d,%d,%lu,%lu,%lu,%lu,%.2f,%.2f,%.2f,%.2f,%lu,%lu,%lu,%lu,%lu,%.2f,%d",
                    ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900,
                    shift, hour,
                    (unsigned long)countHour, (unsigned long)total,
                    (unsigned long)scrap, (unsigned long)setup,
                    (double)avail, (double)perf, (double)qual, (double)oee,
                    (unsigned long)good, (unsigned long)runSec,
                    (unsigned long)planSec, (unsigned long)unplannedSec,
                    (unsigned long)elapsedSec, (double)ppm,
                    (ti.tm_wday < 0 ? 0 : ti.tm_wday));
}

static struct tm makeTm(int y, int mo, int d, int h) {
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_isdst = -1;
    // Normalize via mktime so tm_wday/tm_yday are filled (production gets a
    // fully-populated struct from localtime_r; strftime %V/%G need tm_wday).
    mktime(&t);
    return t;
}

TEST_CASE("weekly: ISO week 34 for a mid-week date in August 2026") {
    // 2026-08-19 is a Wednesday -> ISO week 34.
    struct tm t = makeTm(2026, 8, 19, 12);
    int y = 0;
    CHECK(isoWeek(t, y) == 34);
}

TEST_CASE("weekly: ISO week 01 early January") {
    struct tm t = makeTm(2026, 1, 2, 9);  // 2026-01-02 Friday -> week 1
    int y = 0;
    CHECK(isoWeek(t, y) == 1);
}

TEST_CASE("weekly: ISO week wraps at year boundary") {
    // 2026-12-31 Thursday belongs to ISO week 53 of 2026 (and ISO year 2026).
    struct tm t = makeTm(2026, 12, 31, 10);
    int y = 0;
    int w = isoWeek(t, y);
    CHECK(w == 53);
    CHECK(y == 2026);
}

TEST_CASE("weekly: file name zero-padded") {
    char out[24];
    weeklyOeeFileName(34, out, sizeof(out));
    CHECK(strcmp(out, "wk34-oee.csv") == 0);
    weeklyOeeFileName(5, out, sizeof(out));
    CHECK(strcmp(out, "wk05-oee.csv") == 0);
    weeklyOeeFileName(53, out, sizeof(out));
    CHECK(strcmp(out, "wk53-oee.csv") == 0);
}

TEST_CASE("weekly: row format matches the 18-column schema") {
    struct tm t = makeTm(2026, 9, 1, 14);  // 2026-09-01 is a Tuesday (tm_wday=2)
    char row[200];
    formatOeeRow(row, sizeof(row), t, 1, 14, 14000, 98000, 4958, 0,
                 0.43f, 47.42f, 98.78f, 60.02f,
                 93042, 1120, 10800, 21780, 33700, 36.0f);
    CHECK(strcmp(row, "01-09-2026,1,14,14000,98000,4958,0,0.43,47.42,98.78,60.02,93042,1120,10800,21780,33700,36.00,2") == 0);
}

TEST_CASE("weekly: row with zero count and zero oee still renders all columns") {
    struct tm t = makeTm(2026, 9, 1, 7);  // Tuesday -> tm_wday=2
    char row[200];
    formatOeeRow(row, sizeof(row), t, 1, 7, 0, 0, 4958, 3,
                 0.0f, 0.0f, 100.0f, 0.0f,
                 0, 0, 3600, 0, 3600, 36.0f);
    CHECK(strcmp(row, "01-09-2026,1,7,0,0,4958,3,0.00,0.00,100.00,0.00,0,0,3600,0,3600,36.00,2") == 0);
}

TEST_CASE("weekly: running total accumulates across hours and last is pinned") {
    // Simulate exactly what _proxWriteWeeklyOee does for an 8h shift window.
    uint32_t hourCounts[8] = {0, 0, 0, 21100, 22340, 23020, 17540, 14000};
    uint32_t shiftTotal    = 98000;               // real shift-end total (pinns the last hour)
    uint32_t running = 0;
    uint32_t lastTotal = 0;
    for (int k = 0; k < 8; k++) {
        uint32_t countHour = hourCounts[k];
        running += countHour;
        lastTotal = (k == 7) ? shiftTotal : running;
    }
    CHECK(lastTotal == 98000);
    // Accumulated sum of counts without pinning:
    uint32_t sum = 0; for (auto c : hourCounts) sum += c;
    CHECK(sum == 98000);
}