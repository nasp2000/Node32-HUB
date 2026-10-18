#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header (extract-header strategy): the pure decision
// logic in day_report_fallback.h has no Arduino deps, so the test verifies the
// exact code the firmware runs.
#include "day_report_fallback.h"

#include <string.h>

static dayrep::Entry makeEntry(int slot, int dayKey, long sched, int st, int att, long last, const char* line) {
    dayrep::Entry e;
    dayrep::entryInit(e);
    e.slot = slot;
    e.dayKey = dayKey;
    e.schedEpoch = sched;
    e.status = st;
    e.attempts = att;
    e.lastTryEpoch = last;
    strncpy(e.cmdLine, line, sizeof(e.cmdLine) - 1);
    return e;
}

TEST_CASE("isDayReportCommand accepts the 4 verbs, any case, with date") {
    char verb[12];
    CHECK(dayrep::isDayReportCommand("emailday", verb, sizeof(verb)));
    CHECK(strcmp(verb, "EMAILDAY") == 0);
    CHECK(dayrep::isDayReportCommand("EMAILDAY 29-12-2025", verb, sizeof(verb)));
    CHECK(dayrep::isDayReportCommand("  logday", verb, sizeof(verb)));
    CHECK(strcmp(verb, "LOGDAY") == 0);
    CHECK(dayrep::isDayReportCommand("SensorDay 01-03-2026", verb, sizeof(verb)));
    CHECK(dayrep::isDayReportCommand("shiftday 28-03-2026", verb, sizeof(verb)));
}

TEST_CASE("isDayReportCommand rejects non-day commands") {
    char verb[12];
    CHECK_FALSE(dayrep::isDayReportCommand("X10 off", verb, sizeof(verb)));
    CHECK_FALSE(dayrep::isDayReportCommand("emaildayx", verb, sizeof(verb)));
    CHECK_FALSE(dayrep::isDayReportCommand("", verb, sizeof(verb)));
    CHECK_FALSE(dayrep::isDayReportCommand(NULL, verb, sizeof(verb)));
    CHECK_FALSE(dayrep::isDayReportCommand("REBOOT", verb, sizeof(verb)));
}

TEST_CASE("hasDateParam detects explicit DD-MM-YYYY only") {
    CHECK(dayrep::hasDateParam("emailday 29-12-2025"));
    CHECK(dayrep::hasDateParam("LOGDAY 28-02-2026"));
    CHECK(dayrep::hasDateParam("sensorday 01-03-2026"));
    CHECK_FALSE(dayrep::hasDateParam("emailday"));
    CHECK_FALSE(dayrep::hasDateParam("shiftday "));
    CHECK_FALSE(dayrep::hasDateParam("emailday 29-13-2025"));  // month 13 invalid
    CHECK_FALSE(dayrep::hasDateParam("emailday yesterday"));
}

TEST_CASE("effectiveDayEpoch: 00:xx counts as previous day (1h tolerance)") {
    const long sched = 2000000000L;  // arbitrary valid epoch
    CHECK(dayrep::effectiveDayEpoch(sched, 0) == sched - 3600L);
    CHECK(dayrep::effectiveDayEpoch(sched, 1) == sched);
    CHECK(dayrep::effectiveDayEpoch(sched, 12) == sched);
    CHECK(dayrep::effectiveDayEpoch(sched, 23) == sched);
}

TEST_CASE("needsRetry fires only after +20min, max 3 attempts") {
    const long sched = 2000000000L;
    dayrep::Entry e = makeEntry(3, 20260328, sched, dayrep::ST_FAILED, 0, 0, "emailday 28-03-2026");
    CHECK_FALSE(dayrep::needsRetry(e, sched + 19 * 60));   // too early
    CHECK(dayrep::needsRetry(e, sched + 20 * 60));          // first fallback window
    CHECK(dayrep::needsRetry(e, sched + 120 * 60));
    e.attempts = 3;
    CHECK_FALSE(dayrep::needsRetry(e, sched + 120 * 60));   // attempts exhausted
}

TEST_CASE("isStale marks entries past the 24h window for pruning") {
    const long now = 2000000000L;
    dayrep::Entry fresh = makeEntry(3, 20260328, now - 3600, dayrep::ST_FAILED, 3, now - 3600, "emailday 28-03-2026");
    dayrep::Entry old = makeEntry(3, 20260327, now - 25 * 3600, dayrep::ST_FAILED, 3, now - 25 * 3600, "emailday 27-03-2026");
    CHECK_FALSE(dayrep::isStale(fresh, now));
    CHECK(dayrep::isStale(old, now));
    CHECK_FALSE(dayrep::isStale(fresh, 500L));  // clock unknown: keep, never prune blind
    dayrep::Entry empty;
    dayrep::entryInit(empty);
    CHECK(dayrep::isStale(empty, now));  // free slot counts as prunable/no-op
}

TEST_CASE("needsRetry never refires SENT, enforces spacing + window") {
    const long sched = 2000000000L;
    dayrep::Entry sent = makeEntry(3, 20260328, sched, dayrep::ST_SENT, 0, 0, "emailday 28-03-2026");
    CHECK_FALSE(dayrep::needsRetry(sent, sched + 60 * 60));  // sent stays sent (no duplicates)

    dayrep::Entry spaced = makeEntry(3, 20260328, sched, dayrep::ST_FAILED, 1, sched + 20 * 60, "logday 28-03-2026");
    CHECK_FALSE(dayrep::needsRetry(spaced, sched + 30 * 60));  // last try <20min ago
    CHECK(dayrep::needsRetry(spaced, sched + 41 * 60));        // spacing ok

    dayrep::Entry stale = makeEntry(3, 20260328, sched, dayrep::ST_FAILED, 0, 0, "emailday 28-03-2026");
    CHECK_FALSE(dayrep::needsRetry(stale, sched + 25 * 3600));  // older than 24h window
    CHECK_FALSE(dayrep::needsRetry(stale, 500L));               // clock invalid (no NTP)
}
