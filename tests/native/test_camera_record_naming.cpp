#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_record_naming.h is a pure decision helper — zero ESP32 deps.
#include "../../chatgpt/src/camera_record_naming.h"

// Fixed epochs (UTC):
//   1768000000 -> 2026-01-09 23:06:40  (2026-01-09_230640)
//   1700000000 -> 2023-11-14 22:13:20  (2023-11-14_221320)
static const time_t T_SYNCED = 1768000000;
static const time_t T_SYNCED2 = 1700000000;

static void useUTC() {
#ifdef _WIN32
    _putenv_s("TZ", "UTC0");
    _tzset();
#else
    setenv("TZ", "UTC0", 1);
    tzset();
#endif
}

TEST_CASE("clock readiness: boundary at 2020-09-13, factory/pre-NTP epoch is NOT ready") {
    CHECK_FALSE(cameraClockReady(0));            // boot-time RTC = 1970
    CHECK_FALSE(cameraClockReady(1599999999L));  // one second before boundary
    CHECK(cameraClockReady(1600000000L));        // exactly the boundary
    CHECK(cameraClockReady(T_SYNCED));
}

TEST_CASE("base name pre-NTP: NOSYNC_<bootSec>, never a bogus 1970 date") {
    char out[40];
    cameraRecordBaseName(0, 1234U, out);
    CHECK(std::string(out) == "NOSYNC_1234");
    CHECK(std::strstr(out, "1970") == nullptr);
    cameraRecordBaseName(1599999999L, 7U, out);
    CHECK(std::string(out) == "NOSYNC_7");
    CHECK(std::strstr(out, "1970") == nullptr);
}

TEST_CASE("base name synced: YYYY-MM-DD_HHMMSS (UTC-dated, so TFTP/SD names are date-sorted)") {
    useUTC();
    char out[40];
    cameraRecordBaseName(T_SYNCED, 0U, out);
    CHECK(std::string(out) == "2026-01-09_230640");
    cameraRecordBaseName(T_SYNCED2, 0U, out);
    CHECK(std::string(out) == "2023-11-14_221320");
}

TEST_CASE("day/time parts: empty before sync, dated after (UTC)") {
    useUTC();
    char day[16], tim[16];
    // clock not ready -> empty strings (callers must gate on cameraClockReady)
    cameraDayTimeParts(0, day, tim);
    CHECK(day[0] == '\0');
    CHECK(tim[0] == '\0');
    cameraDayTimeParts(T_SYNCED, day, tim);
    CHECK(std::string(day) == "2026-01-09");
    CHECK(std::string(tim) == "230640");
}

TEST_CASE("folder pick: PENDING staging until clock ready, dated folder after") {
    char out[40];
    // AVI recording pre-sync -> staged (never /CAM/1970-01-01)
    cameraRecordPickFolder(0, "/CAM/1970-01-01", out);
    CHECK(std::string(out) == CAM_REC_STAGE_FOLDER);
    CHECK(std::string(out) == "/CAM/PENDING");
    // Synced -> the caller's real folder is used verbatim
    cameraRecordPickFolder(T_SYNCED, "/CAM/2026-01-09", out);
    CHECK(std::string(out) == "/CAM/2026-01-09");
    // Motion/timelapse snap shots keep their dedicated folders only when synced
    cameraRecordPickFolder(0, "/CAM/MOTION", out);
    CHECK(std::string(out) == "/CAM/PENDING");
    cameraRecordPickFolder(T_SYNCED, "/CAM/TIMELAPSE", out);
    CHECK(std::string(out) == "/CAM/TIMELAPSE");
}

TEST_CASE("res label: from live geometry, never from the legacy enum") {
    // P4 stores the legacy boot enum (framesize=6/QVGA) even at 720p, so the
    // old enum-derived label put QVGA in the name of every P4 recording.
    CHECK(std::string(cameraRecordResLabel(1280, 720)) == "HD");
    CHECK(std::string(cameraRecordResLabel(1920, 1080)) == "FHD");
    CHECK(std::string(cameraRecordResLabel(2592, 1944)) == "5MP");
    CHECK(std::string(cameraRecordResLabel(1280, 960)) == "SXGA");
    CHECK(std::string(cameraRecordResLabel(800, 640)) == "SVGA");
    CHECK(std::string(cameraRecordResLabel(800, 1280)) == "SVGA");
    CHECK(std::string(cameraRecordResLabel(800, 800)) == "SVGA");
    CHECK(std::string(cameraRecordResLabel(640, 480)) == "VGA");
    CHECK(std::string(cameraRecordResLabel(1234, 567)) == "VID");  // unknown
}

TEST_CASE("migrated prefix: dated 'YYYY-MM-DD_HHMMSS_' once synced (used for staged renames)") {
    useUTC();
    char out[40];
    cameraRecordMigratedPrefix(T_SYNCED, out);
    CHECK(std::string(out) == "2026-01-09_230640_");
    // Pre-sync prefix is empty (production callers guard with cameraClockReady)
    cameraRecordMigratedPrefix(0, out);
    CHECK(std::string(out) == "__");
}