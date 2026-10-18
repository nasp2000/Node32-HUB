#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "email_parser.h"
#include <string>

// ============================================================
// Test cases for extractEventTimestampFromSourceLine
// ============================================================

TEST_CASE("extractEventTimestampFromSourceLine: HH:MM:SS format") {
    CHECK(extractEventTimestampFromSourceLine("12:34:56") == "12:34:56");
    CHECK(extractEventTimestampFromSourceLine("00:00:00") == "00:00:00");
    CHECK(extractEventTimestampFromSourceLine("23:59:59") == "23:59:59");
    CHECK(extractEventTimestampFromSourceLine("01:02:03 some text follows") == "01:02:03");
}

TEST_CASE("extractEventTimestampFromSourceLine: YYYY-MM-DD HH:MM:SS format") {
    CHECK(extractEventTimestampFromSourceLine("2026-07-08 14:30:00") == "2026-07-08 14:30:00");
    CHECK(extractEventTimestampFromSourceLine("2026-07-08 14:30:00 event trigger") == "2026-07-08 14:30:00");
    CHECK(extractEventTimestampFromSourceLine("1999-12-31 23:59:59") == "1999-12-31 23:59:59");
}

TEST_CASE("extractEventTimestampFromSourceLine: invalid formats") {
    CHECK(extractEventTimestampFromSourceLine("") == "");
    CHECK(extractEventTimestampFromSourceLine("no digits") == "");
    CHECK(extractEventTimestampFromSourceLine("12:34:5") == "");          // too short for HH:MM:SS
    CHECK(extractEventTimestampFromSourceLine("12:34:5x") == "");          // non-digit at end
    CHECK(extractEventTimestampFromSourceLine("x2:34:56") == "");          // non-digit at start
    CHECK(extractEventTimestampFromSourceLine("12x34:56") == "");          // wrong separator
    CHECK(extractEventTimestampFromSourceLine("1234:56") == "");           // missing colon at pos 2
    CHECK(extractEventTimestampFromSourceLine("2026-07-08") == "");        // date-only, no time
    CHECK(extractEventTimestampFromSourceLine("2026/07/08 14:30:00") == ""); // wrong date separator
    CHECK(extractEventTimestampFromSourceLine("2026-07-08x14:30:00") == ""); // wrong separator at pos 10
    CHECK(extractEventTimestampFromSourceLine("a") == "");
    CHECK(extractEventTimestampFromSourceLine("12:34:5678") == "12:34:56"); // extra chars after 8-char prefix
}

TEST_CASE("extractEventTimestampFromSourceLine: boundary conditions") {
    CHECK(extractEventTimestampFromSourceLine("99:99:99") == "99:99:99");
    CHECK(extractEventTimestampFromSourceLine("0000-00-00 00:00:00") == "0000-00-00 00:00:00");
    CHECK(extractEventTimestampFromSourceLine("12:34:5") == "");
    CHECK(extractEventTimestampFromSourceLine("0000-00-00 00:00:0") == "");
}

// ============================================================
// Test cases for applyHighlightToSourceLine
// ============================================================

TEST_CASE("applyHighlightToSourceLine: basic highlighting") {
    CHECK(applyHighlightToSourceLine("value is 42", "42") == "value is ***42***");
    CHECK(applyHighlightToSourceLine("temperature 25.5C", "25.5C") == "temperature ***25.5C***");
}

TEST_CASE("applyHighlightToSourceLine: no match") {
    CHECK(applyHighlightToSourceLine("no digits here", "42") == "no digits here");
}

TEST_CASE("applyHighlightToSourceLine: empty val or line") {
    CHECK(applyHighlightToSourceLine("some line", "") == "some line");
    CHECK(applyHighlightToSourceLine("", "val") == "");
    CHECK(applyHighlightToSourceLine("", "") == "");
}

TEST_CASE("applyHighlightToSourceLine: first occurrence only") {
    CHECK(applyHighlightToSourceLine("42 and 42 again", "42") == "***42*** and 42 again");
    CHECK(applyHighlightToSourceLine("111", "11") == "***11***1");
}

TEST_CASE("applyHighlightToSourceLine: at start and end") {
    CHECK(applyHighlightToSourceLine("start", "start") == "***start***");
    CHECK(applyHighlightToSourceLine("end", "end") == "***end***");
}
