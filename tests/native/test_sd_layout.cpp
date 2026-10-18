#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define ENABLE_PROXIMITY 1
#include "doctest.h"

// Compile the REAL production sd_layout.h (central SD path convention shared
// by all packs) so the tests exercise the exact strings shipped to the board.
#include "Arduino.h"
#include "ArduinoJson.h"
#include "sd_layout.h"
// T8 — compile the REAL production proximity_manager.h helpers for
// turno_data.txt (same path/sink decision code shipped to the board).
#include "sensors/proximity_manager.h"

TEST_CASE("sysDir builds the system folder") {
    CHECK(sd_layout::sysDir("Equip15") == "/Equip15");
    CHECK(sd_layout::sysDir("SD_LOG") == "/SD_LOG");
}

TEST_CASE("dayFile builds flat per-day paths") {
    CHECK(sd_layout::dayFile("Equip15", "PROXIMITY", "18-09-2026", "csv") == "/Equip15/PROXIMITY_18-09-2026.csv");
    CHECK(sd_layout::dayFile("SD_LOG", "RS232", "01-01-2025", "txt") == "/SD_LOG/RS232_01-01-2025.txt");
}

TEST_CASE("dayDir builds dated subfolders for rotated sources") {
    CHECK(sd_layout::dayDir("Equip15", "RS232", "18-09-2026") == "/Equip15/RS232_18-09-2026");
}

TEST_CASE("isAggregate matches weekly files only") {
    CHECK(sd_layout::isAggregate("wk37-oee.csv") == true);
    CHECK(sd_layout::isAggregate("WK37-oee.csv") == true);
    CHECK(sd_layout::isAggregate("PROXIMITY_18-09-2026.csv") == false);
    CHECK(sd_layout::isAggregate("wk37-oee.txt") == false);
    CHECK(sd_layout::isAggregate("short") == false);
    CHECK(sd_layout::isAggregate("") == false);
}

TEST_CASE("isDayFile accepts dated day files and returns the date") {
    String date;
    CHECK(sd_layout::isDayFile("PROXIMITY_18-09-2026.csv", &date) == true);
    CHECK(date == "18-09-2026");
    CHECK(sd_layout::isDayFile("RS232_01-12-2025.txt", nullptr) == true);
    CHECK(sd_layout::isDayFile("Equip15_PROXIMITY_18-09-2026.csv", &date) == true);
    CHECK(date == "18-09-2026");
}

TEST_CASE("isDayFile rejects non-day names") {
    CHECK(sd_layout::isDayFile("wk37-oee.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("PROXIMITY_.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("PROXIMITY_99-99-2026.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("PROXIMITY_18-13-2026.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("PROXIMITY_18-09-1999.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("PROXIMITY_18-09-2026", nullptr) == false);
    CHECK(sd_layout::isDayFile("nodate.csv", nullptr) == false);
    CHECK(sd_layout::isDayFile("", nullptr) == false);
    CHECK(sd_layout::isDayFile("a.csv", nullptr) == false);
}

// cleanupDeleteAllowed: the core gate for auto-cleanup.
// keepDays == 0 is UI "∞" (no limit) = NEVER delete.
TEST_CASE("cleanupDeleteAllowed: keepDays=0 never deletes") {
    CHECK(sd_layout::cleanupDeleteAllowed(0, 0) == false);
    CHECK(sd_layout::cleanupDeleteAllowed(365, 0) == false);
    CHECK(sd_layout::cleanupDeleteAllowed(99999, 0) == false);
}

TEST_CASE("cleanupDeleteAllowed: keepDays=N deletes when age >= N") {
    CHECK(sd_layout::cleanupDeleteAllowed(0, 30) == false);
    CHECK(sd_layout::cleanupDeleteAllowed(29, 30) == false);
    CHECK(sd_layout::cleanupDeleteAllowed(30, 30) == true);
    CHECK(sd_layout::cleanupDeleteAllowed(365, 30) == true);
}

TEST_CASE("cleanupDeleteAllowed: negative age is defensive") {
    CHECK(sd_layout::cleanupDeleteAllowed(-1, 30) == false);
    CHECK(sd_layout::cleanupDeleteAllowed(-100, 10) == false);
}

// clockReady: pre-NTP gate shared by all SD writers.
TEST_CASE("clockReady: boundary at 2020-09-13") {
    CHECK(sd_layout::clockReady(0) == false);           // boot-time RTC
    CHECK(sd_layout::clockReady(1599999999L) == false); // one second before boundary
    CHECK(sd_layout::clockReady(1600000000L) == true);  // exactly the boundary
    CHECK(sd_layout::clockReady(1768000000L) == true);  // "now"
}

// parseDateFromName: multi-format date extraction for auto-cleanup.
TEST_CASE("parseDateFromName: ISO YYYY-MM-DD (camera/sensor_history/PSRAM)") {
    CHECK(sd_layout::parseDateFromName("2026-09-20") == 20260920);
    CHECK(sd_layout::parseDateFromName("2026-09-20.csv") == 20260920);
    CHECK(sd_layout::parseDateFromName("2026-01-09") == 20260109);
    CHECK(sd_layout::parseDateFromName("backup-2026-09-20.zip") == 20260920);
}

TEST_CASE("parseDateFromName: compact YYYYMMDD (GRBL job logs, legacy camera)") {
    CHECK(sd_layout::parseDateFromName("20260920_120000_RS232.log") == 20260920);
    CHECK(sd_layout::parseDateFromName("20260920") == 20260920);
    CHECK(sd_layout::parseDateFromName("CAM20261101") == 20261101);
}

TEST_CASE("parseDateFromName: datastore DD-MM-YYYY") {
    CHECK(sd_layout::parseDateFromName("PROXIMITY_18-09-2026.csv") == 20260918);
    CHECK(sd_layout::parseDateFromName("log_15-09-2026") == 20260915);
    CHECK(sd_layout::parseDateFromName("15-09-2026.txt") == 20260915);
    CHECK(sd_layout::parseDateFromName("RS232_01-12-2025") == 20251201);
}

TEST_CASE("parseDateFromName: rejects non-dates and staged/protected names") {
    CHECK(sd_layout::parseDateFromName("PENDING") == 0);
    CHECK(sd_layout::parseDateFromName("/CAM/MOTION") == 0);
    CHECK(sd_layout::parseDateFromName("/CAM/PHOTOS") == 0);
    CHECK(sd_layout::parseDateFromName("wk37-oee.csv") == 0);
    CHECK(sd_layout::parseDateFromName("nodate.txt") == 0);
    CHECK(sd_layout::parseDateFromName("") == 0);
    CHECK(sd_layout::parseDateFromName("short") == 0);
}

TEST_CASE("parseDateFromName: dated chart names still parse (same as isDayFile under /sys)") {
    CHECK(sd_layout::parseDateFromName("WF_18-09-2026.chart") == 20260918);
}

TEST_CASE("parseDateFromName: invalid calendar dates are rejected") {
    CHECK(sd_layout::parseDateFromName("2026-13-20") == 0); // month 13
    CHECK(sd_layout::parseDateFromName("20261320") == 0);
    CHECK(sd_layout::parseDateFromName("99-99-2026") == 0);
    CHECK(sd_layout::parseDateFromName("1999-09-20") == 0); // year < 2000
}

TEST_CASE("dateYmdParts decomposes canonical YYYYMMDD") {    int y = 0, m = 0, d = 0;
    sd_layout::dateYmdParts(20260920, y, m, d);
    CHECK(y == 2026);
    CHECK(m == 9);
    CHECK(d == 20);
    sd_layout::dateYmdParts(20251201, y, m, d);
    CHECK(y == 2025);
    CHECK(m == 12);
    CHECK(d == 1);
}

// T8 — turno_data.txt: path + sink decision (REAL production helpers from
// sensors/proximity_manager.h — the exact code shipped to the board).
TEST_CASE("shiftDataTxtPath builds /{sys}/turno_data.txt") {
    CHECK(proximityShiftDataTxtPath("Equip15") == "/Equip15/turno_data.txt");
    CHECK(proximityShiftDataTxtPath("SD_LOG") == "/SD_LOG/turno_data.txt");
}

TEST_CASE("shiftDataTxtPath defaults empty sys to SD_LOG") {
    CHECK(proximityShiftDataTxtPath("") == "/SD_LOG/turno_data.txt");
}

TEST_CASE("shiftDataTxtPath matches sd_layout sysDir convention") {
    // Parity: same folder convention as the day CSVs, fixed filename.
    CHECK(proximityShiftDataTxtPath("Equip15") == sd_layout::sysDir("Equip15") + "/turno_data.txt");
    CHECK(proximityShiftDataTxtPath("SD_LOG") == sd_layout::sysDir("SD_LOG") + "/turno_data.txt");
}

TEST_CASE("shiftDataTxtUseSD: SD only when mode is SD and mounted") {
    CHECK(proximityShiftDataTxtUseSD(true, true) == true);    // SD mounted -> SD
    CHECK(proximityShiftDataTxtUseSD(true, false) == false);  // SD mode, card out -> LittleFS
    CHECK(proximityShiftDataTxtUseSD(false, true) == false);  // PSRAM/NONE mode -> LittleFS
    CHECK(proximityShiftDataTxtUseSD(false, false) == false); // no storage -> LittleFS attempt
}
