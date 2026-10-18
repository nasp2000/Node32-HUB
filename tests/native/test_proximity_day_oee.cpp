#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"

// Compile the REAL production day-OEE helpers from proximity_manager.h:
// proximityDayKey (shift-aligned day-rollover detector used by _proxDayTick) and
// proximityComputeOee (the exact formula the /api/proximity/oee handler uses
// for the day-so-far window: elapsed = seconds since the day boundary —
// 22:00 for 6/14/22 shifts, local midnight when no boundary is configured —
// planned stops overlapping [day boundary, now]). Stubs provide Arduino.h /
// ArduinoJson.h / feature_flags.h.
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

// ── Day key: tm_year*1000 + tm_yday ─────────────────────────────────────
// The shift-aligned day rollover detector compares this key; any calendar day
// maps to a unique key so a re-capture fires exactly once per day. Because
// _proxDayTick feeds it the ALIGNED date (proxShiftAlignedDate), the rollover
// actually fires at the shift day boundary (e.g. 22:00), not local midnight.
TEST_CASE("proximityDayKey: unique per calendar day, stable within a day") {
    int d1 = proximityDayKey(126, 100);
    int d2 = proximityDayKey(126, 100);
    CHECK(d1 == d2);
    // Next day -> different key (midnight rollover detected).
    CHECK(proximityDayKey(126, 101) != d1);
    // Year boundary: 31 Dec -> 1 Jan rolls the key.
    CHECK(proximityDayKey(126, 364) != proximityDayKey(127, 0));
    // Same yday in different years -> different key.
    CHECK(proximityDayKey(125, 100) != proximityDayKey(126, 100));
}

// ── Day-so-far OEE via proximityComputeOee ──────────────────────────────
// Handler inputs for the day window: runSec/cIn/good since the day-boundary
// baselines, elapsedSec = seconds since the day start (boundary hour),
// plannedStopSec overlapping [day boundary, now]. Mirrors the
// /api/proximity/oee day_* computation.
TEST_CASE("day OEE: mid-day window with a morning planned stop") {
    // 12:00 noon: elapsed = 12*3600 = 43200 s; machine ran 10h (36000 s);
    // 1h planned stop (06:00-07:00) inside the window; target 10 ppm.
    // A = 36000 / (43200-3600) = 90.91%; theoretical = 10 * (36000/60) = 6000.
    uint32_t runSec = 36000, elapsed = 43200, planned = 3600;
    uint32_t cIn = 6000, good = 5940;   // 60 scrap -> Q = 99%.
    ProximityOeeResult r = proximityComputeOee(runSec, elapsed, cIn, good, 10.0f, planned);
    CHECK(r.availability == doctest::Approx(90.91).epsilon(0.01));
    CHECK(r.performance == doctest::Approx(100.0));
    CHECK(r.quality == doctest::Approx(99.0));
    CHECK(r.oee == doctest::Approx(90.0).epsilon(0.5));
    CHECK(r.theoretical == 6000u);
}

TEST_CASE("day OEE: early morning, no production yet, no stops") {
    // 00:30, machine idle: A = 0/1800 = 0, P = 0 (no theoretical without run),
    // Q defaults to 100 with no parts, OEE = 0.
    ProximityOeeResult r = proximityComputeOee(0, 1800, 0, 0, 10.0f, 0);
    CHECK(r.availability == doctest::Approx(0.0));
    CHECK(r.performance == doctest::Approx(0.0));
    CHECK(r.quality == doctest::Approx(100.0));
    CHECK(r.oee == doctest::Approx(0.0));
    // Same instant but machine running at cadence: A=100, P=100, Q=100.
    ProximityOeeResult r2 = proximityComputeOee(1800, 1800, 300, 300, 10.0f, 0);
    CHECK(r2.availability == doctest::Approx(100.0));
    CHECK(r2.performance == doctest::Approx(100.0));
    CHECK(r2.oee == doctest::Approx(100.0));
}

// ── Shift-email stacked time bar (option 3) ─────────────────────────────
// Bar widths fill `width` blocks; legend percentages sum to 100; empty window
// yields an empty string (caller skips the block).
TEST_CASE("time bar: 62/15/23 split fills 40 blocks with matching legend") {
    String out = proximityBuildTimeBarAscii(17856, 4320, 28800,
        "Produ\xC3\xA7\xC3\xA3o", "Planeadas", "N\xC3\xa3o planeadas", 30);
    CHECK(out.length() > 0);
    // Bar line: 30 blocks x 3 bytes (UTF-8), '\n' at index 120.
    int nl = out.indexOf("\n");
    CHECK(nl == 90);
    CHECK(out.indexOf("62%") >= 0);
    CHECK(out.indexOf("15%") >= 0);
    CHECK(out.indexOf("23%") >= 0);
    CHECK(out.indexOf("Produ\xC3\xA7\xC3\xA3o") >= 0);
    CHECK(out.indexOf("Planeadas") >= 0);
    CHECK(out.indexOf("N\xC3\xa3o planeadas") >= 0);
    // Same window in English: labels follow the caller language.
    String en = proximityBuildTimeBarAscii(17856, 4320, 28800,
        "Production", "Planned", "Unplanned", 30);
    CHECK(en.indexOf("Production") >= 0);
    CHECK(en.indexOf("Planned") >= 0);
    CHECK(en.indexOf("Unplanned") >= 0);
    CHECK(en.indexOf("Produ\xC3\xA7") < 0);
}

TEST_CASE("time bar: empty window and overflow clamp") {
    String empty = proximityBuildTimeBarAscii(0, 0, 0, "P", "L", "U", 30);
    CHECK(empty.length() == 0);
    String empty2 = proximityBuildTimeBarAscii(100, 50, 0, "P", "L", "U", 30);
    CHECK(empty2.length() == 0);
    // Counters beyond the window clamp: full production bar, 100/0/0 legend.
    String full = proximityBuildTimeBarAscii(99999, 99999, 3600, "P", "L", "U", 30);
    int nl = full.indexOf("\n");
    CHECK(nl == 90);
    CHECK(full.indexOf("100%") >= 0);
    // Default width (omitted arg) matches explicit 30: single email line.
    String def = proximityBuildTimeBarAscii(17856, 4320, 28800, "P", "L", "U");
    String exp = proximityBuildTimeBarAscii(17856, 4320, 28800, "P", "L", "U", 30);
    CHECK(def.length() == exp.length());
    int dnl = def.indexOf("\n");
    CHECK(dnl == 90);
}

// ── Day-baseline persistence schema (/config/sensor_prox_daybase.json) ──
// Mirrors the production save/load contract: {dayKey, ch:[{in,run,down,good}]}
// atomic .tmp write; load applies only when the saved dayKey matches today;
// missing keys default to 0 (old files still load).
TEST_CASE("daybase schema: round-trip preserves baselines and day key") {
    JsonDocument doc;
    doc["dayKey"] = 46123;
    JsonArray ch = doc["ch"].to<JsonArray>();
    for (int i = 0; i < 8; i++) {
        JsonObject o = ch.createNestedObject();
        o["in"] = 1000u + (unsigned)i; o["run"] = 2000u + (unsigned)i;
        o["down"] = 300u + (unsigned)i; o["good"] = 900u + (unsigned)i;
        o["accIn"] = 400u + (unsigned)i; o["accRun"] = 500u + (unsigned)i;
        o["accDown"] = 60u + (unsigned)i; o["accGood"] = 350u + (unsigned)i;
    }
    String raw; serializeJson(doc, raw);
    CHECK(raw.length() > 0);
    JsonDocument back;
    CHECK(deserializeJson(back, raw) == DeserializationError::Ok);
    CHECK((int)back["dayKey"] == 46123);
    int n = 0;
    for (JsonVariant v : back["ch"].as<JsonArray>()) {
        if (n == 3) {
            CHECK((unsigned)v["in"] == 1003u);
            CHECK((unsigned)v["run"] == 2003u);
            CHECK((unsigned)v["down"] == 303u);
            CHECK((unsigned)v["good"] == 903u);
            CHECK((unsigned)v["accIn"] == 403u);
            CHECK((unsigned)v["accRun"] == 503u);
            CHECK((unsigned)v["accDown"] == 63u);
            CHECK((unsigned)v["accGood"] == 353u);
        }
        n++;
    }
    CHECK(n == 8);
}

TEST_CASE("daybase schema: day-key gate and old-file tolerance") {
    // Same-day key applies; another day starts fresh.
    int todayKey = proximityDayKey(126, 200);
    int savedSame = todayKey, savedOld = proximityDayKey(126, 199);
    CHECK(savedSame == todayKey);
    CHECK(savedOld != todayKey);
    // Old file without "ch" array parses but yields no channels.
    JsonDocument old;
    CHECK(deserializeJson(old, std::string("{\"dayKey\":1}")) == DeserializationError::Ok);
    CHECK(old["ch"].isNull());
    // Missing keys default to 0 (old firmware files without accums still load).
    JsonDocument part;
    CHECK(deserializeJson(part, std::string("{\"dayKey\":2,\"ch\":[{\"in\":5}]}")) == DeserializationError::Ok);
    for (JsonVariant v : part["ch"].as<JsonArray>()) {
        CHECK((unsigned)v["run"] == 0u);
        CHECK((unsigned)v["accIn"] == 0u);
        CHECK((unsigned)v["accRun"] == 0u);
    }
}

// ── Extended counters schema: run/down/good/setup survive, old files load ──
TEST_CASE("counters schema: running-state fields round-trip, old files load") {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    JsonObject o = arr.createNestedObject();
    o["in"] = 10u; o["out"] = 1u; o["ti"] = 100u; o["to"] = 50u;
    o["run"] = 3600u; o["down"] = 120u; o["good"] = 9u; o["setup"] = 2u;
    String raw; serializeJson(doc, raw);
    JsonDocument back;
    CHECK(deserializeJson(back, raw) == DeserializationError::Ok);
    bool seen = false;
    for (JsonVariant v : back.as<JsonArray>()) {
        CHECK((unsigned)v["run"] == 3600u);
        CHECK((unsigned)v["down"] == 120u);
        CHECK((unsigned)v["good"] == 9u);
        CHECK((unsigned)v["setup"] == 2u);
        seen = true;
    }
    CHECK(seen);
    // Old file (pre-extension) loads with new fields defaulting to 0.
    JsonDocument legacy;
    CHECK(deserializeJson(legacy, std::string("[{\"in\":7,\"out\":0,\"ti\":70,\"to\":0}]")) == DeserializationError::Ok);
    for (JsonVariant v : legacy.as<JsonArray>()) {
        CHECK((unsigned)v["in"] == 7u);
        CHECK((unsigned)v["run"] == 0u);
        seen = true;
    }
    CHECK(seen);
}
