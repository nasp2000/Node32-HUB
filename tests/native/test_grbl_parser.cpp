#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "grbl_line_parser.h"
#include <string>
#include <string.h>

// ====== stripLine tests ======
TEST_CASE("stripLine: basic line") {
    char dst[64];
    size_t len = stripLine("G1 X10 Y20", dst, sizeof(dst));
    CHECK(std::string(dst) == "G1 X10 Y20");
    CHECK(len == 10);
}

TEST_CASE("stripLine: strips inline comment after ;") {
    char dst[64];
    size_t len = stripLine("G1 X10 ; move to X", dst, sizeof(dst));
    CHECK(std::string(dst) == "G1 X10");
    CHECK(len == 6);
}

TEST_CASE("stripLine: uppercases") {
    char dst[64];
    stripLine("g1 x10 y20", dst, sizeof(dst));
    CHECK(std::string(dst) == "G1 X10 Y20");
}

TEST_CASE("stripLine: strips trailing spaces") {
    char dst[64];
    size_t len = stripLine("G1 X10   ", dst, sizeof(dst));
    CHECK(std::string(dst) == "G1 X10");
    CHECK(len == 6);
}

TEST_CASE("stripLine: strips \\r and \\n") {
    char dst[64];
    size_t len = stripLine("G1\r\n", dst, sizeof(dst));
    CHECK(std::string(dst) == "G1");
    CHECK(len == 2);
}

TEST_CASE("stripLine: empty line becomes empty") {
    char dst[64] = "dirty";
    size_t len = stripLine("", dst, sizeof(dst));
    CHECK(std::string(dst) == "");
    CHECK(len == 0);
}

TEST_CASE("stripLine: comment-only line") {
    char dst[64];
    size_t len = stripLine("; this is a comment", dst, sizeof(dst));
    CHECK(std::string(dst) == "");
    CHECK(len == 0);
}

TEST_CASE("stripLine: respects dstSize") {
    char dst[4];
    size_t len = stripLine("G1 X10", dst, sizeof(dst));
    // dstSize=4 → max 3 chars + null → "G1 " → trim trailing space → "G1"
    CHECK(std::string(dst) == "G1");
    CHECK(len == 2);
}

// ====== isCriticalPersistentCommand tests ======
TEST_CASE("isCriticalPersistentCommand: $-commands with digits=") {
    CHECK(isCriticalPersistentCommand("$10=250", 7));
    CHECK(isCriticalPersistentCommand("$132=0", 6));
}

TEST_CASE("isCriticalPersistentCommand: special $-commands") {
    CHECK(isCriticalPersistentCommand("$$", 2));
    CHECK(isCriticalPersistentCommand("$#", 2));
    CHECK(isCriticalPersistentCommand("$I", 2));
    CHECK(isCriticalPersistentCommand("$N", 2));
}

TEST_CASE("isCriticalPersistentCommand: $RST=") {
    CHECK(isCriticalPersistentCommand("$RST=*", 6));
}

TEST_CASE("isCriticalPersistentCommand: G10/G28/G30") {
    CHECK(isCriticalPersistentCommand("G10 L2 P1 X0", 12));
    CHECK(isCriticalPersistentCommand("G28", 3));
    CHECK(isCriticalPersistentCommand("G30", 3));
}

TEST_CASE("isCriticalPersistentCommand: G54..G59") {
    CHECK(isCriticalPersistentCommand("G54", 3));
    CHECK(isCriticalPersistentCommand("G55", 3));
    CHECK(isCriticalPersistentCommand("G59", 3));
    CHECK(!isCriticalPersistentCommand("G53", 3)); // G53 is not a coordinate system
}

TEST_CASE("isCriticalPersistentCommand: normal G-code is not critical") {
    CHECK(!isCriticalPersistentCommand("G0 X0", 5));
    CHECK(!isCriticalPersistentCommand("G1 X10", 6));
    CHECK(!isCriticalPersistentCommand("M3 S1000", 8));
    CHECK(!isCriticalPersistentCommand("M5", 2));
}

TEST_CASE("isCriticalPersistentCommand: null/empty") {
    CHECK(!isCriticalPersistentCommand(nullptr, 0));
    CHECK(!isCriticalPersistentCommand("", 0));
}

TEST_CASE("isCriticalPersistentCommand: $-command without = is not critical") {
    CHECK(!isCriticalPersistentCommand("$", 1));
    CHECK(!isCriticalPersistentCommand("$X", 2)); // $X is alarm unlock
}

// ====== isRepeatedRealtimeStatusQuery tests ======
TEST_CASE("isRepeatedRealtimeStatusQuery: single ?") {
    CHECK(isRepeatedRealtimeStatusQuery("?", 1));
}

TEST_CASE("isRepeatedRealtimeStatusQuery: multiple ?") {
    CHECK(isRepeatedRealtimeStatusQuery("???", 3));
    CHECK(isRepeatedRealtimeStatusQuery("?????", 5));
}

TEST_CASE("isRepeatedRealtimeStatusQuery: ? mixed with other chars") {
    CHECK(!isRepeatedRealtimeStatusQuery("?x", 2));
    CHECK(!isRepeatedRealtimeStatusQuery("x?", 2));
}

TEST_CASE("isRepeatedRealtimeStatusQuery: null/empty") {
    CHECK(!isRepeatedRealtimeStatusQuery(nullptr, 0));
    CHECK(!isRepeatedRealtimeStatusQuery("", 0));
}

// ====== preprocessGcodeInPsram tests ======
TEST_CASE("preprocessGcodeInPsram: passthrough simple") {
    char buf[] = "G1 X10 Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10 Y20\n");
}

TEST_CASE("preprocessGcodeInPsram: strips inline comments (parens)") {
    char buf[] = "G1 X10 (move here) Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10 Y20\n");
}

TEST_CASE("preprocessGcodeInPsram: strips ; comments") {
    char buf[] = "G1 X10 ; inline comment\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10\n");
}

TEST_CASE("preprocessGcodeInPsram: removes blank/comment-only lines") {
    char buf[] = "G1 X10\n; comment\n\nG1 Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode starts at 0(=G0). First G1 is new mode, second G1 is redundant
    CHECK(std::string(buf, len) == "G1 X10\nY20\n");
}

TEST_CASE("preprocessGcodeInPsram: uppercases") {
    char buf[] = "g1 x10 y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10 Y20\n");
}

TEST_CASE("preprocessGcodeInPsram: collapses multiple spaces") {
    char buf[] = "G1  X10   Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10 Y20\n");
}

TEST_CASE("preprocessGcodeInPsram: strips \\r") {
    char buf[] = "G1 X10\r\nG1 Y20\r\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode starts at 0(=G0). First G1 is new mode, second G1 is redundant
    CHECK(std::string(buf, len) == "G1 X10\nY20\n");
}

TEST_CASE("preprocessGcodeInPsram: suppresses redundant G0 (modal)") {
    char buf[] = "G0 X0\nG0 X10\nG0 Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode starts at 0 (=G0), so ALL G0 commands are redundant
    CHECK(std::string(buf, len) == "X0\nX10\nY20\n");
}

TEST_CASE("preprocessGcodeInPsram: suppresses redundant G90") {
    char buf[] = "G90 G1 X10\nG90 G1 Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode=0, isAbsolute=true initially
    // Line1: G90 redundant(absolute), G1 new(motionMode→1) → "G1 X10"
    // Line2: G90 redundant(absolute), G1 redundant(mode=1) → "Y20"
    CHECK(std::string(buf, len) == "G1 X10\nY20\n");
}

TEST_CASE("preprocessGcodeInPsram: tracks G90->G91 transition") {
    char buf[] = "G90 G1 X10\nG91 G1 X10\nG90 G1 X10\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode=0, isAbsolute=true
    // Line1: G90 redundant, G1 new(mode=1) → "G1 X10"
    // Line2: G91 changes absolute→false, G1 redundant(mode=1) → "G91 X10"
    // Line3: G90 changes absolute→true, G1 redundant(mode=1) → "G90 X10"
    CHECK(std::string(buf, len) == "G1 X10\nG91 X10\nG90 X10\n");
}

TEST_CASE("preprocessGcodeInPsram: mixed G0/G1 modal tracking") {
    char buf[] = "G0 X0\nG1 X10\nG1 Y20\nG0 Z5\nG0 Z10\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    // motionMode=0 → G0 redundant (X0), G1 new (X10), G1 redundant (Y20),
    // G0 new (Z5, mode→0), G0 redundant (Z10)
    CHECK(std::string(buf, len) == "X0\nG1 X10\nY20\nG0 Z5\nZ10\n");
}

TEST_CASE("preprocessGcodeInPsram: trims leading/trailing whitespace") {
    char buf[] = "  G1 X10  \n\tG1 Y20\t\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10\nY20\n");
}

TEST_CASE("preprocessGcodeInPsram: nested parens") {
    char buf[] = "G1 X10 (outer (inner) still) Y20\n";
    size_t len = preprocessGcodeInPsram(buf, sizeof(buf) - 1);
    CHECK(std::string(buf, len) == "G1 X10 Y20\n");
}
