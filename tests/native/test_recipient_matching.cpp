#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Arduino.h"
#include "../../chatgpt/src/recipient_matching_utils.h"

// ====== opStrToInt tests ======
TEST_CASE("opStrToInt: known operators") {
    CHECK(opStrToInt("-")  == 0);
    CHECK(opStrToInt("+")  == 1);
    CHECK(opStrToInt("D")  == 2);
    CHECK(opStrToInt("I")  == 3);
    CHECK(opStrToInt("G")  == 4);
    CHECK(opStrToInt("L")  == 5);
    CHECK(opStrToInt("R")  == 6);
    CHECK(opStrToInt("DF") == 7);
    CHECK(opStrToInt("V")  == 8);
    CHECK(opStrToInt("C")  == 9);
    CHECK(opStrToInt("P")  == 10);
    CHECK(opStrToInt("Cw") == 11);
    CHECK(opStrToInt("Hy") == 12);
}

TEST_CASE("opStrToInt: unknown/empty returns -1") {
    CHECK(opStrToInt("")   == -1);
    CHECK(opStrToInt("ZZ") == -1);
    CHECK(opStrToInt(" ")  == -1);
    CHECK(opStrToInt("X")  == -1);
}

TEST_CASE("opStrToInt: case sensitivity") {
    CHECK(opStrToInt("d") == -1);  // must be uppercase
    CHECK(opStrToInt("df") == -1);
    CHECK(opStrToInt("cw") == -1);
}

// ====== getNumberShape tests ======
TEST_CASE("getNumberShape: integer patterns") {
    CHECK(getNumberShape("0") == "d");
    CHECK(getNumberShape("12") == "dd");
    CHECK(getNumberShape("001") == "ddd");
    CHECK(getNumberShape("12345") == "ddddd");
}

TEST_CASE("getNumberShape: decimal patterns") {
    CHECK(getNumberShape("0.0") == "d.d");
    CHECK(getNumberShape("019.00") == "ddd.dd");
    CHECK(getNumberShape("3.14159") == "d.ddddd");
}

TEST_CASE("getNumberShape: stops at non-digit non-dot") {
    CHECK(getNumberShape("123abc") == "ddd");
    CHECK(getNumberShape("12 34") == "dd");
    CHECK(getNumberShape("-5") == "");
}

TEST_CASE("getNumberShape: empty string") {
    CHECK(getNumberShape("") == "");
}

// ====== tryMatchShape tests ======
TEST_CASE("tryMatchShape: basic integer match") {
    String outStr;
    double outVal;
    CHECK(tryMatchShape("value 123 end", 6, "ddd", outStr, outVal));
    CHECK(outStr == "123");
    CHECK(outVal == 123.0);
}

TEST_CASE("tryMatchShape: decimal match") {
    String outStr;
    double outVal;
    CHECK(tryMatchShape("temp 036.50 ok", 5, "ddd.dd", outStr, outVal));
    CHECK(outStr == "036.50");
    CHECK(outVal == 36.5);
}

TEST_CASE("tryMatchShape: rejected when part of longer number right") {
    String outStr;
    double outVal;
    CHECK(!tryMatchShape("12345", 0, "dd", outStr, outVal));
}

TEST_CASE("tryMatchShape: rejected when part of longer number left") {
    String outStr;
    double outVal;
    CHECK(!tryMatchShape("12345", 1, "ddd", outStr, outVal));
}

TEST_CASE("tryMatchShape: match at end of string (no right boundary issue)") {
    String outStr;
    double outVal;
    CHECK(tryMatchShape("12 345", 3, "ddd", outStr, outVal));
    CHECK(outStr == "345");
    CHECK(outVal == 345.0);
}

TEST_CASE("tryMatchShape: negative number with minus prefix") {
    String outStr;
    double outVal;
    CHECK(tryMatchShape("val -003.50 end", 5, "ddd.dd", outStr, outVal));
    CHECK(outStr == "003.50");
    CHECK(outVal == -3.5);
}

TEST_CASE("tryMatchShape: shape too long for line") {
    String outStr;
    double outVal;
    CHECK(!tryMatchShape("ab", 0, "ddd", outStr, outVal));
}

TEST_CASE("tryMatchShape: empty shape") {
    String outStr;
    double outVal;
    CHECK(!tryMatchShape("abc", 0, "", outStr, outVal));
}

TEST_CASE("tryMatchShape: dot mismatch") {
    String outStr;
    double outVal;
    CHECK(!tryMatchShape("12x34", 0, "dd.dd", outStr, outVal));
}

// ====== parseFormattedNumber tests ======
TEST_CASE("parseFormattedNumber: basic") {
    CHECK(parseFormattedNumber("0") == 0.0);
    CHECK(parseFormattedNumber("123") == 123.0);
    CHECK(parseFormattedNumber("-5") == -5.0);
    CHECK(parseFormattedNumber("3.14") == doctest::Approx(3.14));
}

TEST_CASE("parseFormattedNumber: with leading zeros") {
    CHECK(parseFormattedNumber("019.00") == 19.0);
    CHECK(parseFormattedNumber("001") == 1.0);
}

TEST_CASE("parseFormattedNumber: empty string") {
    CHECK(parseFormattedNumber("") == 0.0);
}

// ====== parseHysteresisThresholds tests ======
TEST_CASE("parseHysteresisThresholds: normal order") {
    double hi, lo;
    CHECK(parseHysteresisThresholds("30.0..20.0", hi, lo));
    CHECK(hi == doctest::Approx(30.0));
    CHECK(lo == doctest::Approx(20.0));
}

TEST_CASE("parseHysteresisThresholds: swapped order") {
    double hi, lo;
    CHECK(parseHysteresisThresholds("20.0..30.0", hi, lo));
    CHECK(hi == doctest::Approx(30.0));  // always hi >= lo
    CHECK(lo == doctest::Approx(20.0));
}

TEST_CASE("parseHysteresisThresholds: equal values") {
    double hi, lo;
    CHECK(parseHysteresisThresholds("25..25", hi, lo));
    CHECK(hi == doctest::Approx(25.0));
    CHECK(lo == doctest::Approx(25.0));
}

TEST_CASE("parseHysteresisThresholds: invalid format") {
    double hi, lo;
    CHECK(!parseHysteresisThresholds("", hi, lo));
    CHECK(!parseHysteresisThresholds("..", hi, lo));
    CHECK(!parseHysteresisThresholds("..10", hi, lo));  // sep at 0 -> false
    CHECK(!parseHysteresisThresholds("no-separator", hi, lo));
}

TEST_CASE("parseHysteresisThresholds: integers") {
    double hi, lo;
    CHECK(parseHysteresisThresholds("100..0", hi, lo));
    CHECK(hi == doctest::Approx(100.0));
    CHECK(lo == doctest::Approx(0.0));
}
