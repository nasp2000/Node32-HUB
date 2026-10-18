#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compile the REAL production chart_ring_serve.h — the exact PSRAM-first
// serve math shipped to the board (shared by proximity/Modbus/RS232 .chart
// endpoints, all packs).
#include "chart_ring_serve.h"

TEST_CASE("servePlan: exact fit keeps every point") {
    uint32_t s = 0, c = 0;
    chartRingServePlan(288, 288, &s, &c);
    CHECK(s == 1);
    CHECK(c == 288);
}

TEST_CASE("servePlan: overflow strides oldest-first") {
    uint32_t s = 0, c = 0;
    chartRingServePlan(1440, 288, &s, &c);
    CHECK(s == 5);
    CHECK(c == 288);
    chartRingServePlan(1441, 288, &s, &c);
    CHECK(s == 6);
    CHECK(c == 241);
}

TEST_CASE("servePlan: empty ring and zero budget serve nothing") {
    uint32_t s = 0, c = 0;
    chartRingServePlan(0, 288, &s, &c);
    CHECK(c == 0);
    chartRingServePlan(100, 0, &s, &c);
    CHECK(c == 0);
    CHECK(s == 1);
}

TEST_CASE("servePlan: single point survives any budget") {
    uint32_t s = 0, c = 0;
    chartRingServePlan(1, 288, &s, &c);
    CHECK(s == 1);
    CHECK(c == 1);
}

TEST_CASE("servePick: stride 1 keeps all indices in order") {
    uint32_t idx[8];
    uint32_t n = chartRingServePick(5, 1, idx, 8);
    CHECK(n == 5);
    for (uint32_t i = 0; i < n; i++) CHECK(idx[i] == i);
}

TEST_CASE("servePick: stride skips oldest-first, tail survives cap") {
    uint32_t idx[4];
    // 10 points, stride 3 -> kept {0,3,6,9}; cap 4 fits all.
    uint32_t n = chartRingServePick(10, 3, idx, 4);
    CHECK(n == 4);
    CHECK(idx[0] == 0);
    CHECK(idx[3] == 9);
    // Same plan, cap 2 -> drops oldest {0,3}, keeps {6,9}.
    n = chartRingServePick(10, 3, idx, 2);
    CHECK(n == 2);
    CHECK(idx[0] == 6);
    CHECK(idx[1] == 9);
}

TEST_CASE("servePick: null cap and empty input are safe") {
    CHECK(chartRingServePick(5, 1, nullptr, 8) == 0);
    uint32_t idx[4];
    CHECK(chartRingServePick(5, 1, idx, 0) == 0);
    CHECK(chartRingServePick(0, 1, idx, 4) == 0);
    // stride 0 is defensive-only (callers pass >= 1): treated as 1.
    CHECK(chartRingServePick(3, 0, idx, 4) == 3);
}

TEST_CASE("coversRange: empty never covers, oldest must reach fromTs") {
    CHECK(chartRingCoversRange(1000, 0, 500, 2000) == false);
    CHECK(chartRingCoversRange(1000, 10, 500, 2000) == false); // oldest past fromTs
    CHECK(chartRingCoversRange(500, 10, 500, 2000) == true);
    CHECK(chartRingCoversRange(100, 10, 500, 2000) == true);
    CHECK(chartRingCoversRange(100, 10, 0, 2000) == true); // fromTs 0 = whole history
}

static ChartServePoint mkPt(uint32_t ts, float v0) {
    ChartServePoint p;
    p.timestamp = ts;
    p.valueCount = 1;
    for (int i = 0; i < CHART_SERVE_MAX_COLS; i++) p.values[i] = 0.0f;
    p.values[0] = v0;
    return p;
}

TEST_CASE("mergePoints: file history + ring tail stay ascending") {
    ChartServePoint f[3] = { mkPt(100, 1), mkPt(200, 2), mkPt(300, 3) };
    ChartServePoint r[2] = { mkPt(400, 4), mkPt(500, 5) };
    ChartServePoint out[8];
    uint32_t n = chartMergePoints(f, 3, r, 2, out, 8);
    CHECK(n == 5);
    CHECK(out[0].timestamp == 100);
    CHECK(out[4].timestamp == 500);
    CHECK(out[4].values[0] == 5.0f);
}

TEST_CASE("mergePoints: ring wins equal timestamps, interleaved order") {
    ChartServePoint f[3] = { mkPt(100, 1), mkPt(300, 3), mkPt(500, 5) };
    ChartServePoint r[2] = { mkPt(200, 20), mkPt(300, 30) };
    ChartServePoint out[8];
    uint32_t n = chartMergePoints(f, 3, r, 2, out, 8);
    CHECK(n == 4);
    CHECK(out[1].timestamp == 200);
    CHECK(out[2].timestamp == 300);
    CHECK(out[2].values[0] == 30.0f); // ring wins the tie
}

TEST_CASE("mergePoints: overflow drops oldest, newest minute survives") {
    ChartServePoint f[4] = { mkPt(100, 1), mkPt(200, 2), mkPt(300, 3), mkPt(400, 4) };
    ChartServePoint r[2] = { mkPt(500, 5), mkPt(600, 6) };
    ChartServePoint out[8];
    uint32_t n = chartMergePoints(f, 4, r, 2, out, 4);
    CHECK(n == 4);
    CHECK(out[0].timestamp == 300);
    CHECK(out[3].timestamp == 600); // most recent minute always served
}

TEST_CASE("mergePoints: empty sides and null guard") {
    ChartServePoint f[1] = { mkPt(100, 1) };
    ChartServePoint out[4];
    CHECK(chartMergePoints(f, 1, nullptr, 0, out, 4) == 1);
    CHECK(chartMergePoints(nullptr, 0, f, 1, out, 4) == 1);
    CHECK(chartMergePoints(f, 1, f, 1, nullptr, 4) == 0);
    CHECK(chartMergePoints(f, 1, f, 1, out, 0) == 0);
}
