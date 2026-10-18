#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "grbl_resume_scan.h"
#include <string>
#include <string.h>

// ====== fold: units / distance / WCS / plane ======
TEST_CASE("fold: G21 G90 G54 G17") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("G21 G90 G54 G17", &m);
    CHECK(m.hasUnits);       CHECK(!m.inches);
    CHECK(m.hasDistance);    CHECK(!m.incremental);
    CHECK(m.hasWcs);         CHECK(m.wcs == 0);
    CHECK(m.hasPlane);       CHECK(m.plane == 17);
    CHECK(!m.hasFeed);       CHECK(!m.hasSpindle);
}

TEST_CASE("fold: G20 G91 G55 and comments, lowercase") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("g20 g91 (inch mode) ; trailing", &m);
    grblResumeFoldRawLine("G55 X0", &m);
    CHECK(m.hasUnits);       CHECK(m.inches);
    CHECK(m.hasDistance);    CHECK(m.incremental);
    CHECK(m.hasWcs);         CHECK(m.wcs == 1);
}

// ====== fold: feed / spindle / coolant ======
TEST_CASE("fold: F S M3 M8 with decimals and later override") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("S12000 M3", &m);
    grblResumeFoldRawLine("F1500.0", &m);
    grblResumeFoldRawLine("M8", &m);
    grblResumeFoldRawLine("F2000 ; speed up", &m);
    CHECK(m.hasFeed);        CHECK(m.feed == 2000);
    CHECK(m.hasSpindle);     CHECK(m.spindle == 12000);
    CHECK(m.hasSpindleOn);   CHECK(m.spindleDir == 3);
    CHECK(m.hasCoolant);     CHECK(m.coolant == 8);
}

TEST_CASE("fold: M5/M9 off-state wins over earlier on-state") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("M4 S8000", &m);
    grblResumeFoldRawLine("M5", &m);
    grblResumeFoldRawLine("M9", &m);
    CHECK(m.hasSpindleOn);   CHECK(m.spindleDir == 5);
    CHECK(m.hasCoolant);     CHECK(m.coolant == 9);
    CHECK(m.hasSpindle);     CHECK(m.spindle == 8000);
}

TEST_CASE("fold: motion words do not disturb modals") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("G1 X10.5 Y-20 Z0.5 I2 J3 F1200", &m);
    grblResumeFoldRawLine("G2 X0 Y0 R5", &m);
    grblResumeFoldRawLine("G0 Z5", &m);
    CHECK(!m.hasUnits);      CHECK(!m.hasDistance);  CHECK(!m.hasWcs);
    CHECK(m.hasFeed);        CHECK(m.feed == 1200);
}

// ====== preamble emission ======
TEST_CASE("preamble: full snapshot exact output") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("G21 G90 G54 G17", &m);
    grblResumeFoldRawLine("F1500", &m);
    grblResumeFoldRawLine("S12000 M3", &m);
    grblResumeFoldRawLine("M8", &m);
    char out[128];
    size_t n = grblResumeEmitPreamble(&m, out, sizeof(out));
    CHECK(std::string(out) == "G21\nG90\nG54\nG17\nF1500\nS12000 M3\nG4 P2\nM8");
    CHECK(n == strlen(out));
}

TEST_CASE("preamble: empty snapshot emits nothing") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    char out[64];
    memset(out, 'X', sizeof(out));
    CHECK(grblResumeEmitPreamble(&m, out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');
}

TEST_CASE("preamble: M5 off emits M5, no dwell, G59 plane G18") {
    GrblResumeModals m;
    grblResumeModalsInit(&m);
    grblResumeFoldRawLine("G59 G18", &m);
    grblResumeFoldRawLine("M5", &m);
    char out[64];
    grblResumeEmitPreamble(&m, out, sizeof(out));
    CHECK(std::string(out) == "G59\nG18\nM5");
}

// ====== line counting (skip arithmetic) ======
TEST_CASE("countLines: blanks, CRLF and unterminated tail") {
    CHECK(grblResumeCountLines("G1\nG2\nG3\n", 9) == 3);
    CHECK(grblResumeCountLines("G1\r\n\r\nG2\r\n", 10) == 3);
    CHECK(grblResumeCountLines("G1\nG2", 5) == 2);
    CHECK(grblResumeCountLines("", 0) == 0);
    CHECK(grblResumeCountLines(nullptr, 0) == 0);
}

TEST_CASE("countLines: resume offset lands on first unacked line") {
    // 5 physical lines, 3 acked -> skip 3, resume at index 3 ("G1 X30").
    const char* job = "G21\nG90\nG1 X10\nG1 X20\nG1 X30\n";
    CHECK(grblResumeCountLines(job, strlen(job)) == 5);
    // first 3 lines consume "G21\nG90\nG1 X10\n" == 15 bytes
    CHECK(strncmp(job + 15, "G1 X20", 6) == 0);
}

// ====== sendable skip offset ======
TEST_CASE("skipOffset: skips blanks and comments, lands after Nth sendable") {
    // sendable: G21(0) G90(1) G1 X10(2) G1 X30(3); blanks/comments skipped.
    const char* job = "G21\n\n; hello\nG90\nG1 X10\n   \nG1 X30\n";
    size_t off = grblResumeSkipOffsetSendable(job, strlen(job), 3);
    CHECK(strncmp(job + off, "   \nG1 X30\n", 11) == 0);
    // skip past everything -> len
    CHECK(grblResumeSkipOffsetSendable(job, strlen(job), 99) == strlen(job));
    // n == 0 -> 0, null-safe
    CHECK(grblResumeSkipOffsetSendable(job, strlen(job), 0) == 0);
    CHECK(grblResumeSkipOffsetSendable(nullptr, 0, 3) == 0);
}

TEST_CASE("skipOffset: CRLF and exact-boundary resume") {
    const char* job = "G21\r\nG1 X10\r\nG1 X20\r\n";
    // skip 2 sendable -> offset at start of "G1 X20"
    size_t off = grblResumeSkipOffsetSendable(job, strlen(job), 2);
    CHECK(strncmp(job + off, "G1 X20", 6) == 0);
    // skip 1 -> start of "G1 X10"
    CHECK(strncmp(job + grblResumeSkipOffsetSendable(job, strlen(job), 1), "G1 X10", 6) == 0);
}
