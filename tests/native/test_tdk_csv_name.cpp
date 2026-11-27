// The FTP client flushes the PSU CSV before streaming it, and EMAILDAY does the
// same for its attachments. Both decisions rest on recognising the file name
// (with or without a folder, any case) — that predicate is real production code
// from ftp_client.h, not a copy, so this TU includes the header directly.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/ftp_client.h"

TEST_CASE("ftpIsTdkCsvName recognises the PSU daily CSV only") {
    CHECK(ftpIsTdkCsvName("TDK_27-11-2026.csv"));
    CHECK(ftpIsTdkCsvName("PSU/TDK_27-11-2026.csv"));
    CHECK(ftpIsTdkCsvName("SD:/logs/TDK_01-01-2027.CSV"));
    CHECK(ftpIsTdkCsvName("tdk_27-11-2026.csv"));
    CHECK(ftpIsTdkCsvName("LFS:/TDK_1-1-2026.csv"));
}

TEST_CASE("ftpIsTdkCsvName rejects everything else") {
    CHECK_FALSE(ftpIsTdkCsvName("TDK_27-11-2026.chart"));
    CHECK_FALSE(ftpIsTdkCsvName("TDK_27-11-2026.csv.bak"));
    CHECK_FALSE(ftpIsTdkCsvName("other.csv"));
    CHECK_FALSE(ftpIsTdkCsvName("TDK_.csv"));       // too short
    CHECK_FALSE(ftpIsTdkCsvName("TDK"));
    CHECK_FALSE(ftpIsTdkCsvName(""));
    CHECK_FALSE(ftpIsTdkCsvName(nullptr));
}
