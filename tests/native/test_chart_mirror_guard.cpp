// Native tests for the RS232 ".chart" LittleFS mirror (BACKLOG SD-8).
//
// Two halves:
//   1. the throttle that decides when the mirror may write (chart_mirror_guard.h)
//      — the defect was that the mirror was gated behind ENABLE_SDCARD, so it
//      never ran without a card; the cadence must follow the configured
//      flush interval, never a private fixed one;
//   2. the record bytes that actually land in the file (chart_record_write.h),
//      exercised against a real File object (in-memory LittleFS stub), plus the
//      short-write path that must not leave a half record.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "chart_mirror_guard.h"
#include "chart_record_write.h"

#include <LittleFS.h>

#include <cstring>
#include <string>

// The in-memory FS instance the stub declares (same pattern as test_littlefs_store).
LittleFSClass LittleFS;

namespace {

// Same field layout the firmware record uses (Rs232ChartRecord in
// chart_history.h: u32 ts, u8 count, u8 reserved[3], float values[]).
struct TestRecord {
    uint32_t timestamp;
    uint8_t valueCount;
    uint8_t reserved[3];
    float values[8];
};

// File that accepts the first `limit` bytes then fails, to prove a short write
// aborts instead of leaving a partial record.
class ShortFile : public Print {
public:
    size_t limit;
    std::string out;
    explicit ShortFile(size_t l) : limit(l) {}
    size_t write(uint8_t c) override {
        if (out.size() >= limit) return 0;
        out.push_back((char)c);
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
        size_t room = (out.size() >= limit) ? 0 : (limit - out.size());
        size_t k = n < room ? n : room;
        for (size_t i = 0; i < k; i++) out.push_back((char)b[i]);
        return k;   // arduino File::write returns the count, like this
    }
};

} // namespace

// ── throttle ────────────────────────────────────────────────────────────────

TEST_CASE("SD writes are never throttled by the mirror guard") {
    CHECK(chartMirrorDue(false, 1000, 900, 1800000UL));
    CHECK(chartMirrorDue(false, 0, 0, 1800000UL));
}

TEST_CASE("the first flash write after boot goes through") {
    CHECK(chartMirrorDue(true, 5, 0, 1800000UL));    // lastMs 0 = none yet
    CHECK(chartMirrorDue(true, 5, 0, 0));            // bad window does not block
}

TEST_CASE("the flash mirror follows the configured interval") {
    const unsigned long win = 1800000UL;   // 30m (default)
    CHECK_FALSE(chartMirrorDue(true, 1000 + 100, 1000, win));
    CHECK_FALSE(chartMirrorDue(true, 1000 + win - 1, 1000, win));
    CHECK(chartMirrorDue(true, 1000 + win, 1000, win));          // boundary
    CHECK(chartMirrorDue(true, 1000 + win + 5, 1000, win));

    // 10m and 60m settings behave the same way at their own boundary
    CHECK_FALSE(chartMirrorDue(true, 600000 - 1, 0 + 1, 600000UL));
    CHECK(chartMirrorDue(true, 1 + 600000UL, 1, 600000UL));
    CHECK_FALSE(chartMirrorDue(true, 1 + 3600000UL - 1, 1, 3600000UL));
    CHECK(chartMirrorDue(true, 1 + 3600000UL, 1, 3600000UL));
}

TEST_CASE("millis() rollover does not stall the mirror") {
    const unsigned long last = 0xFFFFFF00UL;
    const unsigned long now = 0x00000200UL;    // +0x300 = 768 ms later
    CHECK_FALSE(chartMirrorDue(true, now, last, 1800000UL));
    CHECK(chartMirrorDue(true, last + 1800000UL, last, 1800000UL));   // wraps, still >= window
}

// ── record bytes on a real File ─────────────────────────────────────────────

TEST_CASE("a chart record is written to the file with the documented layout") {
    File f = LittleFS.open("/test.chart", "w");
    REQUIRE(f);

    TestRecord rec = {};
    rec.timestamp = 0x01020304u;
    rec.valueCount = 3;
    rec.reserved[0] = 0;
    rec.reserved[1] = 0;
    rec.reserved[2] = 0;
    rec.values[0] = 1.5f;
    rec.values[1] = -2.25f;
    rec.values[2] = 42.0f;

    CHECK(chartWriteRecord(f, rec, 3));
    f.close();

    File r = LittleFS.open("/test.chart", "r");
    REQUIRE(r);
    const size_t expect = sizeof(uint32_t) + 1 + 3 + 3 * sizeof(float);
    CHECK(r.size() == expect);

    uint8_t buf[64] = {0};
    const size_t got = r.read(buf, sizeof(buf));
    r.close();
    CHECK(got == expect);

    uint32_t ts = 0;
    std::memcpy(&ts, buf, sizeof(ts));
    CHECK(ts == 0x01020304u);
    CHECK(buf[4] == 3);                       // valueCount
    CHECK(buf[5] == 0);   // reserved[0]
    CHECK(buf[6] == 0);   // reserved[1]
    CHECK(buf[7] == 0);   // reserved[2]

    float v0 = 0, v1 = 0, v2 = 0;
    std::memcpy(&v0, buf + 8, sizeof(float));
    std::memcpy(&v1, buf + 12, sizeof(float));
    std::memcpy(&v2, buf + 16, sizeof(float));
    CHECK(v0 == 1.5f);
    CHECK(v1 == -2.25f);
    CHECK(v2 == 42.0f);

    LittleFS.remove("/test.chart");
}

TEST_CASE("a short write aborts and reports failure (no half record claimed)") {
    // Fails inside the header: nothing accepted.
    ShortFile zero(0);
    TestRecord rec = {};
    rec.timestamp = 7;
    CHECK_FALSE(chartWriteRecord(zero, rec, 2));

    // Fails inside the float payload: the call reports false so the caller can
    // drop the record instead of treating a truncated write as success.
    ShortFile partial(sizeof(uint32_t) + 4 + 2 * sizeof(float) - 2);
    CHECK_FALSE(chartWriteRecord(partial, rec, 2));
}

TEST_CASE("columnCount 0 is rejected instead of writing a header-only record") {
    File f = LittleFS.open("/zero.chart", "w");
    REQUIRE(f);
    TestRecord rec = {};
    CHECK_FALSE(chartWriteRecord(f, rec, 0));
    f.close();
    File r = LittleFS.open("/zero.chart", "r");
    REQUIRE(r);
    CHECK(r.size() == 0);
    r.close();
    LittleFS.remove("/zero.chart");
}
