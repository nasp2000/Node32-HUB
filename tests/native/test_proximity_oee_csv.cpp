#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// Compile the REAL header: proximityIsoWeek / proximityWeeklyOeeFileName /
// proximityFormatOeeRow / proximityWeeklyHeaderHasExtras are the shipped pure
// helpers, so the CSV layout cannot drift from a local copy.
#define ENABLE_PROXIMITY 1
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

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
    CHECK(proximityIsoWeek(t, y) == 34);
}

TEST_CASE("weekly: ISO week 01 early January") {
    struct tm t = makeTm(2026, 1, 2, 9);  // 2026-01-02 Friday -> week 1
    int y = 0;
    CHECK(proximityIsoWeek(t, y) == 1);
}

TEST_CASE("weekly: ISO week wraps at year boundary") {
    // 2026-12-31 Thursday belongs to ISO week 53 of 2026 (and ISO year 2026).
    struct tm t = makeTm(2026, 12, 31, 10);
    int y = 0;
    int w = proximityIsoWeek(t, y);
    CHECK(w == 53);
    CHECK(y == 2026);
}

TEST_CASE("weekly: file name zero-padded") {
    char out[24];
    proximityWeeklyOeeFileName(34, out, sizeof(out));
    CHECK(strcmp(out, "wk34-oee.csv") == 0);
    proximityWeeklyOeeFileName(5, out, sizeof(out));
    CHECK(strcmp(out, "wk05-oee.csv") == 0);
    proximityWeeklyOeeFileName(53, out, sizeof(out));
    CHECK(strcmp(out, "wk53-oee.csv") == 0);
}

TEST_CASE("weekly: row format matches the 18-column schema") {
    struct tm t = makeTm(2026, 9, 1, 14);  // 2026-09-01 is a Tuesday (tm_wday=2)
    char row[200];
    proximityFormatOeeRow(row, sizeof(row), t, 1, 14, 14000, 98000, 4958, 0,
                 0.43f, 47.42f, 98.78f, 60.02f,
                 93042, 1120, 10800, 21780, 33700, 36.0f);
    CHECK(strcmp(row, "01-09-2026,1,14,14000,98000,4958,0,0.43,47.42,98.78,60.02,93042,1120,10800,21780,33700,36.00,2") == 0);
}

TEST_CASE("weekly: row with zero count and zero oee still renders all columns") {
    struct tm t = makeTm(2026, 9, 1, 7);  // Tuesday -> tm_wday=2
    char row[200];
    proximityFormatOeeRow(row, sizeof(row), t, 1, 7, 0, 0, 4958, 3,
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
// ===== 2026-10-31: appended columns (only on files born with them) =====
TEST_CASE("weekly: extras appended when a shift date is given (19..21 columns)") {
    struct tm t = makeTm(2026, 10, 30, 14);   // Friday -> tm_wday=5
    char row[240];
    proximityFormatOeeRow(row, sizeof(row), t, 2, 14, 17320, 91310, 1850, 100,
                          75.24f, 87.00f, 97.97f, 64.13f,
                          86947, 20315, 1800, 6685, 28800, 310.0f,
                          "30-10-2026", 288.7f, 0);
    CHECK(strcmp(row,
        "30-10-2026,2,14,17320,91310,1850,100,75.24,87.00,97.97,64.13,86947,20315,1800,6685,28800,310.00,5,"
        "30-10-2026,288.7,0") == 0);
}

TEST_CASE("weekly: legacy layout stays 18 columns when no shift date is passed") {
    struct tm t = makeTm(2026, 10, 30, 7);
    char row[240];
    proximityFormatOeeRow(row, sizeof(row), t, 2, 7, 12010, 12010, 1850, 100,
                          75.24f, 87.00f, 97.97f, 64.13f,
                          86947, 20315, 1800, 6685, 28800, 310.0f);
    CHECK(strlen(row) > 0);
    int commas = 0;
    for (const char* p = row; *p; p++) commas += (*p == ',');
    CHECK(commas == 17);              // 18 columns
    CHECK(strstr(row, "shift_date") == nullptr);
    // gap=1 renders too, on the extended layout.
    proximityFormatOeeRow(row, sizeof(row), t, 2, 7, 12010, 12010, 1850, 100,
                          75.24f, 87.00f, 97.97f, 64.13f,
                          86947, 20315, 1800, 6685, 28800, 310.0f,
                          "30-10-2026", 200.2f, 1);
    CHECK(strstr(row, ",30-10-2026,200.2,1") != nullptr);
}

TEST_CASE("weekly: header schema detection decides the layout of an existing file") {
    CHECK(proximityWeeklyHeaderHasExtras(
        "date,shift,hour,count_hour,total,scrap,setup,a,p,q,oee,good,run_sec,plan_sec,"
        "unplanned_sec,elapsed_sec,ppm,weekday,shift_date,ppm_meas,gap") == true);
    CHECK(proximityWeeklyHeaderHasExtras(
        "date,shift,hour,count_hour,total,scrap,setup,a,p,q,oee,good,run_sec,plan_sec,"
        "unplanned_sec,elapsed_sec,ppm,weekday") == false);
    CHECK(proximityWeeklyHeaderHasExtras("") == false);
    CHECK(proximityWeeklyHeaderHasExtras(nullptr) == false);
}
