#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>
#include <ctime>

// Compile the REAL production store — the exact struct/JSON used to persist the
// FTP page's "last sends" across reboots (ftp_client.cpp owns the LittleFS I/O).
#include "Arduino.h"
#include "ArduinoJson.h"
#include "ftp_status_store.h"

TEST_CASE("empty store: zeroed, serialises and parses back") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    CHECK(s.count == 0);
    CHECK(s.okCount == 0);
    CHECK(s.failCount == 0);
    CHECK(s.lastTs == 0);
    CHECK(s.lastResult[0] == '\0');

    String js = ftpStatusStoreToJson(s);
    CHECK(js.indexOf("\"ok_count\":0") >= 0);
    CHECK(js.indexOf("\"recent\":[]") >= 0);

    FtpStatusStore back;
    CHECK(ftpStatusStoreFromJson(js, back) == true);
    CHECK(back.count == 0);
    CHECK(back.okCount == 0);
}

TEST_CASE("add sends newest-first, totals and last result updated") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    ftpStatusStoreAddSend(s, "a.csv", 100, true, 1000, "OK a.csv (100B)");
    ftpStatusStoreAddSend(s, "b.csv", 200, false, 2000, "STOR failed: 550");
    CHECK(s.count == 2);
    CHECK(s.okCount == 1);
    CHECK(s.failCount == 1);
    CHECK(strcmp(s.recent[0].name, "b.csv") == 0);   // newest first
    CHECK(s.recent[0].ok == false);
    CHECK(s.recent[0].size == 200);
    CHECK(strcmp(s.recent[1].name, "a.csv") == 0);
    CHECK(s.lastTs == 2000);
    CHECK(strcmp(s.lastResult, "STOR failed: 550") == 0);
}

TEST_CASE("list caps at FTP_STATUS_RECENT_MAX, oldest dropped") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    for (int i = 0; i < FTP_STATUS_RECENT_MAX + 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%d.txt", i);
        ftpStatusStoreAddSend(s, name, (uint32_t)i, true, (uint32_t)(100 + i), "OK");
    }
    CHECK(s.count == FTP_STATUS_RECENT_MAX);
    // Newest is f10, oldest kept is f3 (f0..f2 fell off).
    char expect[32];
    snprintf(expect, sizeof(expect), "f%d.txt", FTP_STATUS_RECENT_MAX + 2);
    CHECK(strcmp(s.recent[0].name, expect) == 0);
    CHECK(strcmp(s.recent[FTP_STATUS_RECENT_MAX - 1].name, "f3.txt") == 0);
    CHECK(s.okCount == (uint32_t)(FTP_STATUS_RECENT_MAX + 3));  // totals keep counting
}

TEST_CASE("JSON round-trip keeps names with spaces/quotes and all fields") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    ftpStatusStoreAddSend(s, "log \"weird\".txt", 12345, true, 1792700000, "OK path|with\\slash");
    ftpStatusStoreAddSend(s, "plain.csv", 7, false, 1792700100, "550 nope");

    String js = ftpStatusStoreToJson(s);
    FtpStatusStore back;
    CHECK(ftpStatusStoreFromJson(js, back) == true);
    CHECK(back.count == 2);
    CHECK(back.okCount == 1);
    CHECK(back.failCount == 1);
    CHECK(back.lastTs == 1792700100);
    CHECK(strcmp(back.recent[0].name, "plain.csv") == 0);
    CHECK(strcmp(back.recent[1].name, "log \"weird\".txt") == 0);
    CHECK(back.recent[1].size == 12345);
    CHECK(back.recent[1].ok == true);
    CHECK(back.recent[1].ts == 1792700000);
    CHECK(strcmp(back.lastResult, "550 nope") == 0);
}

TEST_CASE("name is truncated to the field, control chars dropped") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    char longName[200];
    memset(longName, 'x', sizeof(longName));
    longName[sizeof(longName) - 1] = '\0';
    ftpStatusStoreAddSend(s, longName, 1, true, 10, "OK");
    CHECK(strlen(s.recent[0].name) == FTP_STATUS_NAME_MAX - 1);

    ftpStatusStoreInit(s);
    ftpStatusStoreAddSend(s, "a\r\nb.txt", 1, true, 10, "res\tult");
    CHECK(strcmp(s.recent[0].name, "ab.txt") == 0);
    CHECK(strcmp(s.lastResult, "result") == 0);
}

TEST_CASE("corrupt JSON is rejected and leaves the store unchanged") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    ftpStatusStoreAddSend(s, "keep.csv", 9, true, 42, "OK");
    FtpStatusStore copy = s;
    CHECK(ftpStatusStoreFromJson(String("{not json"), s) == false);
    CHECK(s.count == copy.count);
    CHECK(strcmp(s.recent[0].name, "keep.csv") == 0);
    CHECK(ftpStatusStoreFromJson(String(""), s) == false);
}

TEST_CASE("tolerant parse: missing keys default, junk entries skipped") {
    FtpStatusStore s;
    ftpStatusStoreInit(s);
    CHECK(ftpStatusStoreFromJson(String("{\"ok_count\":4}"), s) == true);
    CHECK(s.okCount == 4);
    CHECK(s.failCount == 0);
    CHECK(s.count == 0);

    CHECK(ftpStatusStoreFromJson(
        String("{\"recent\":[{\"file\":\"\"},{\"file\":\"ok.txt\",\"size\":3,\"ok\":true,\"ts\":5}]}"), s) == true);
    CHECK(s.count == 1);
    CHECK(strcmp(s.recent[0].name, "ok.txt") == 0);
}

TEST_CASE("timestamp formatting: unset clock renders '-', epoch renders local time") {
    char buf[32];
    ftpStatusFormatTs(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "-") == 0);

    const uint32_t ts = 1792700000;
    ftpStatusFormatTs(ts, buf, sizeof(buf));
    time_t t = (time_t)ts;
    struct tm lt;
    localtime_r(&t, &lt);
    char expect[64];
    snprintf(expect, sizeof(expect), "%02d-%02d-%04d %02d:%02d:%02d",
             lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900, lt.tm_hour, lt.tm_min, lt.tm_sec);
    CHECK(strcmp(buf, expect) == 0);
    CHECK(strlen(buf) == 19);
}
