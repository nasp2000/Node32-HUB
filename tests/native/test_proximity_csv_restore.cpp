#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"

// Compile the REAL production CSV-restore helpers from proximity_manager.h
// (proxCsvRowIsAbsolute + proxCsvParseAbsRow). These turn the daily
// PROXIMITY_<date>.csv "tick"/"shift_boundary" ABSOLUTE snapshots into the
// power-cut counter restore point used by _proxRestoreFromSdCsvTail.
// Stubs provide Arduino.h / ArduinoJson.h / feature_flags.h.
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sensors/proximity_manager.h"

TEST_CASE("absolute row detection: only tick and shift_boundary qualify") {
    CHECK(proxCsvRowIsAbsolute("tick"));
    CHECK(proxCsvRowIsAbsolute("shift_boundary"));
    CHECK_FALSE(proxCsvRowIsAbsolute("shift_end"));  // deltas, not absolute
    CHECK_FALSE(proxCsvRowIsAbsolute("boot"));       // post-restore snapshot
    CHECK_FALSE(proxCsvRowIsAbsolute("type"));       // header row
    CHECK_FALSE(proxCsvRowIsAbsolute(nullptr));
    CHECK_FALSE(proxCsvRowIsAbsolute(""));
}

TEST_CASE("absolute parse: 2-channel tick row") {
    char line[] = "1727000000,tick,DI1,100,50,24.5,3601,120,99,7,3,1.0,10,5,7";
    uint32_t in[8], out[8], run[8], down[8], good[8];
    CHECK(proxCsvParseAbsRow(line, 8, in, out, run, down, good));
    CHECK(in[0] == 100);
    CHECK(out[0] == 50);
    CHECK(run[0] == 3601);
    CHECK(down[0] == 120);
    CHECK(good[0] == 99);
    CHECK(in[1] == 7);
    CHECK(out[1] == 3);
    CHECK(run[1] == 10);
    CHECK(down[1] == 5);
    CHECK(good[1] == 7);
}

TEST_CASE("absolute parse: shift_boundary row is also a restore point") {
    char line[] = "1727100000,shift_boundary,DI2,200,99,30.0,7200,240,198,14,6,2.0,20,9,14";
    uint32_t in[8], out[8], run[8], down[8], good[8];
    CHECK(proxCsvParseAbsRow(line, 8, in, out, run, down, good));
    CHECK(in[0] == 200);
    CHECK(out[0] == 99);
    CHECK(good[1] == 14);
}

TEST_CASE("absolute parse: rejects non-absolute and malformed rows") {
    uint32_t in[8], out[8], run[8], down[8], good[8];

    char shiftEnd[] = "1727200000,shift_end,23:00,500,200,40.0,1800,60,480,40,10,3.0,100,30,40";
    CHECK_FALSE(proxCsvParseAbsRow(shiftEnd, 8, in, out, run, down, good));

    char boot[] = "1727250000,boot,3,0,0,0.0,0,0,0,0,0,0.0,0,0,0";
    CHECK_FALSE(proxCsvParseAbsRow(boot, 8, in, out, run, down, good));

    char header[] = "timestamp,type,shift,ch0_in,ch0_out,ch0_ppm,ch0_run,ch0_down,ch0_good";
    CHECK_FALSE(proxCsvParseAbsRow(header, 8, in, out, run, down, good));

    char truncated[] = "1727300000,tick,DI1,100,50";
    CHECK_FALSE(proxCsvParseAbsRow(truncated, 8, in, out, run, down, good));

    char empty[] = "";
    CHECK_FALSE(proxCsvParseAbsRow(empty, 8, in, out, run, down, good));

    CHECK_FALSE(proxCsvParseAbsRow(nullptr, 8, in, out, run, down, good));
}

TEST_CASE("absolute parse: adapts to fewer channels (older files)") {
    char old6ch[] = "1727400000,tick,DI1,300,150,60.0,18000,600,297,30,15,6.0,1800,60,29,45,20,9.0,2700,90,44,60,30,12.0,3600,120,58,75,35,15.0,4500,150,73,90,40,18.0,5400,180,88";
    uint32_t in[8], out[8], run[8], down[8], good[8];
    // 3 header + 6*6 channel tokens -> only 6 channels parsed; remaining fields unchanged
    CHECK(proxCsvParseAbsRow(old6ch, 8, in, out, run, down, good));
    CHECK(in[0] == 300);
    CHECK(in[5] == 90);
    CHECK(run[2] == 2700);
    CHECK(run[3] == 3600);
    CHECK(good[5] == 88);
}