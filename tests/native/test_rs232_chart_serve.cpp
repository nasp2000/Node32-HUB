// Native tests for the RS232 chart serve rule (chatgpt/src/rs232_chart_serve.h).
//
// BACKLOG SD-9: the RS232 chart API must serve persisted history (SD, or the
// LittleFS mirror at the identical path) PLUS the PSRAM live tail — one union,
// one decimation budget — exactly like Modbus and proximity. The union maths
// lives in chartMergePoints() (already covered by test_chart_ring_serve); this
// suite covers the RS232 half that used to be copy-pasted inline in the WiFi
// and Ethernet handlers: live point conversion, range filter, NaN padding,
// column derivation, keys, and the union end to end (file-only, ring-only,
// overlap with ring winning ties, and cap trimming from the oldest side).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>

#include "chart_ring_serve.h"
#include "rs232_chart_serve.h"

namespace {

constexpr uint8_t MAX_COLS = 8;
constexpr uint8_t LIVE_COLS = 4;

// Same shape as Rs232LivePoint (structures.h).
struct LivePoint {
    uint32_t timestamp;
    uint8_t valueCount;
    float values[LIVE_COLS];
};

LivePoint live(uint32_t ts, uint8_t n, float base) {
    LivePoint p{};
    p.timestamp = ts;
    p.valueCount = n;
    for (uint8_t i = 0; i < LIVE_COLS; i++) p.values[i] = (i < n) ? base + (float)i : NAN;
    return p;
}

ChartServePoint file(uint32_t ts, uint8_t n, float base) {
    ChartServePoint p{};
    p.timestamp = ts;
    p.valueCount = n;
    for (uint8_t i = 0; i < CHART_SERVE_MAX_COLS; i++) {
        p.values[i] = (i < n) ? base + (float)i : NAN;
    }
    return p;
}

} // namespace

TEST_CASE("a live point is copied with NaN padding beyond the live schema") {
    const LivePoint p = live(1000, 3, 10.0f);
    ChartServePoint out{};
    REQUIRE(rs232ChartCopyLivePoint(p, 0, 0, out, LIVE_COLS));
    CHECK(out.timestamp == 1000);
    CHECK(out.valueCount == 3);
    CHECK(out.values[0] == 10.0f);
    CHECK(out.values[2] == 12.0f);
    // Columns 4..(LIVE_COLS-1) exist but are NaN in this point...
    CHECK(std::isnan(out.values[3]));
    // ...and everything past the live schema is NaN too (not 0).
    CHECK(std::isnan(out.values[4]));
    CHECK(std::isnan(out.values[CHART_SERVE_MAX_COLS - 1]));
}

TEST_CASE("valueCount above the serve layout is capped") {
    LivePoint p = live(7, 4, 1.0f);
    p.valueCount = 99;
    ChartServePoint out{};
    REQUIRE(rs232ChartCopyLivePoint(p, 0, 0, out, LIVE_COLS));
    CHECK(out.valueCount == CHART_SERVE_MAX_COLS);
}

TEST_CASE("the range filter drops older live points and keeps the boundary") {
    ChartServePoint out{};
    CHECK_FALSE(rs232ChartCopyLivePoint(live(999, 2, 0), 1000, 0, out, LIVE_COLS));
    CHECK(rs232ChartCopyLivePoint(live(1000, 2, 0), 1000, 0, out, LIVE_COLS));   // inclusive
    CHECK(rs232ChartCopyLivePoint(live(1001, 2, 0), 1000, 0, out, LIVE_COLS));
    // fromTs 0 = unbounded (whole ring)
    CHECK(rs232ChartCopyLivePoint(live(5, 2, 0), 0, 0, out, LIVE_COLS));
}

TEST_CASE("columns come from the live schema, else from the widest record") {
    const ChartServePoint f = file(100, 3, 0);
    const ChartServePoint t[2] = {file(200, 2, 0), file(300, 5, 0)};
    // Live schema wins when it has columns.
    CHECK(rs232ChartResolveCols(4, &f, 1, t, 2, MAX_COLS) == 4);
    // Otherwise the widest of file and tail wins (tail can be wider).
    CHECK(rs232ChartResolveCols(0, &f, 1, t, 2, MAX_COLS) == 5);
    // Clamped to the source maximum.
    CHECK(rs232ChartResolveCols(0, &f, 1, t, 2, 4) == 4);
    CHECK(rs232ChartResolveCols(0, nullptr, 0, nullptr, 0, MAX_COLS) == 0);
}

TEST_CASE("keys are rs232_col1..N with 4 decimals") {
    char keys[MAX_COLS][16] = {};
    int8_t decs[MAX_COLS];
    rs232ChartFillKeys(3, keys, decs);
    CHECK(std::string(keys[0]) == "rs232_col1");
    CHECK(std::string(keys[1]) == "rs232_col2");
    CHECK(std::string(keys[2]) == "rs232_col3");
    CHECK(decs[0] == 4);
    CHECK(decs[2] == 4);
    CHECK(std::string(keys[3]) == "");   // untouched beyond nCols
}

TEST_CASE("union: file history plus live tail, ascending, ring wins ties") {
    const ChartServePoint filePts[3] = {file(100, 2, 1), file(200, 2, 2), file(300, 2, 3)};
    const LivePoint livePts[3] = {live(250, 2, 20), live(300, 2, 30), live(400, 2, 40)};

    ChartServePoint tail[3] = {};
    uint16_t nTail = 0;
    for (const LivePoint& p : livePts) {
        if (rs232ChartCopyLivePoint(p, 0, 0, tail[nTail], LIVE_COLS)) nTail++;
    }
    REQUIRE(nTail == 3);

    ChartServePoint merged[6] = {};
    const uint32_t n = chartMergePoints(filePts, 3, tail, nTail, merged, 6);
    REQUIRE(n == 5);
    CHECK(merged[0].timestamp == 100);
    CHECK(merged[1].timestamp == 200);
    CHECK(merged[2].timestamp == 250);
    CHECK(merged[3].timestamp == 300);
    CHECK(merged[3].values[0] == 30.0f);    // ring wins the tie with the file
    CHECK(merged[4].timestamp == 400);
}

TEST_CASE("union with an empty ring serves the file alone (and vice versa)") {
    const ChartServePoint filePts[2] = {file(100, 2, 1), file(200, 2, 2)};
    ChartServePoint merged[4] = {};
    CHECK(chartMergePoints(filePts, 2, nullptr, 0, merged, 4) == 2);
    CHECK(merged[0].timestamp == 100);
    CHECK(merged[1].timestamp == 200);

    const LivePoint p = live(500, 2, 5);
    ChartServePoint tail[1] = {};
    REQUIRE(rs232ChartCopyLivePoint(p, 0, 0, tail[0], LIVE_COLS));
    CHECK(chartMergePoints(nullptr, 0, tail, 1, merged, 4) == 1);
    CHECK(merged[0].timestamp == 500);
}

TEST_CASE("union keeps the newest points when it exceeds the cap") {
    const ChartServePoint filePts[3] = {file(100, 2, 1), file(200, 2, 2), file(300, 2, 3)};
    const LivePoint livePts[2] = {live(400, 2, 4), live(500, 2, 5)};
    ChartServePoint tail[2] = {};
    for (uint8_t i = 0; i < 2; i++) rs232ChartCopyLivePoint(livePts[i], 0, 0, tail[i], LIVE_COLS);

    ChartServePoint merged[3] = {};
    const uint32_t n = chartMergePoints(filePts, 3, tail, 2, merged, 3);
    REQUIRE(n == 3);
    CHECK(merged[0].timestamp == 300);      // oldest two dropped
    CHECK(merged[1].timestamp == 400);
    CHECK(merged[2].timestamp == 500);
}

TEST_CASE("a past-date window drops today's live tail instead of concatenating it") {
    // The ring always holds "now"; a request for a past day must bound the tail
    // to that day (untilTs = end of day, exclusive), or yesterday's file gets
    // today's samples appended.
    const ChartServePoint filePts[2] = {file(100, 2, 1), file(200, 2, 2)};
    const LivePoint today = live(900, 2, 9);

    ChartServePoint tail[1] = {};
    uint16_t nTail = 0;
    // fromTs = day start (50), untilTs = next midnight (800)
    if (rs232ChartCopyLivePoint(today, 50, 800, tail[nTail], LIVE_COLS)) nTail++;
    CHECK(nTail == 0);

    ChartServePoint merged[4] = {};
    const uint32_t n = chartMergePoints(filePts, 2, tail, nTail, merged, 4);
    CHECK(n == 2);
    CHECK(merged[1].timestamp == 200);

    // A point inside the window is still kept, boundary exclusive at the top.
    CHECK(rs232ChartCopyLivePoint(live(799, 2, 1), 50, 800, tail[0], LIVE_COLS));
    CHECK_FALSE(rs232ChartCopyLivePoint(live(800, 2, 1), 50, 800, tail[0], LIVE_COLS));
    // untilTs 0 = unbounded (today / live view).
    CHECK(rs232ChartCopyLivePoint(today, 0, 0, tail[0], LIVE_COLS));
}
