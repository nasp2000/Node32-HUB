// Native tests for the TDK-Lambda SCPI reply parsers (tdk_lambda_scpi.h).
//
// The parsers are the only part of the module that can be wrong silently: a
// DVC? reply with the wrong number of fields must NOT be accepted as a sample
// (it would plot garbage), and a command classifier mistake would attribute a
// reply to the wrong query. Both are pure functions, tested here on the host.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/tdk_lambda_scpi.h"

TEST_CASE("tdkParseSixFloats accepts a DVC? reply") {
    float f[6] = {0};
    CHECK(tdkParseSixFloats("599.99,600.00,000.50,001.30,660.0,0.000", f));
    CHECK(f[0] == doctest::Approx(599.99f));
    CHECK(f[1] == doctest::Approx(600.00f));
    CHECK(f[2] == doctest::Approx(0.50f));
    CHECK(f[3] == doctest::Approx(1.30f));
    CHECK(f[4] == doctest::Approx(660.0f));
    CHECK(f[5] == doctest::Approx(0.0f));
}

TEST_CASE("tdkParseSixFloats tolerates spaces and signs") {
    float f[6] = {0};
    CHECK(tdkParseSixFloats("  0.5 , 2 , 3.25 , -4 , 5 , 6  ", f));
    CHECK(f[0] == doctest::Approx(0.5f));
    CHECK(f[3] == doctest::Approx(-4.0f));
    CHECK(f[5] == doctest::Approx(6.0f));
}

TEST_CASE("tdkParseSixFloats rejects other replies") {
    float f[6] = {0};
    CHECK_FALSE(tdkParseSixFloats("OK", f));                 // accepted command
    CHECK_FALSE(tdkParseSixFloats("CV", f));                 // MODE? reply
    CHECK_FALSE(tdkParseSixFloats("C01", f));                // error code
    CHECK_FALSE(tdkParseSixFloats("1,2,3,4,5", f));          // 5 fields (truncated)
    CHECK_FALSE(tdkParseSixFloats("MV(45.2),PV(45)", f));    // STT? shape
}

TEST_CASE("tdkParseFloat accepts a single numeric reply") {
    float v = -1.0f;
    CHECK(tdkParseFloat("599.99", &v));
    CHECK(v == doctest::Approx(599.99f));
    CHECK(tdkParseFloat("000.50", &v));
    CHECK(v == doctest::Approx(0.5f));
    CHECK_FALSE(tdkParseFloat("OK", &v));
    CHECK_FALSE(tdkParseFloat("", &v));
}

TEST_CASE("tdkUpperTrim normalises the console input") {
    char out[16];
    tdkUpperTrim(" dvc? \r\n", out, sizeof(out));
    CHECK(std::string(out) == "DVC?");
    tdkUpperTrim("mode?", out, sizeof(out));
    CHECK(std::string(out) == "MODE?");
    tdkUpperTrim("", out, sizeof(out));
    CHECK(std::string(out) == "");
}

TEST_CASE("tdkClassify maps queries to the reply parser") {
    CHECK(tdkClassify("DVC?")  == TDK_P_DVC);
    CHECK(tdkClassify("MV?")   == TDK_P_MV);
    CHECK(tdkClassify("MC?")   == TDK_P_MC);
    CHECK(tdkClassify("MODE?") == TDK_P_MODE);
    CHECK(tdkClassify("OUT?")  == TDK_P_OUT);
    CHECK(tdkClassify("STT?")  == TDK_P_STT);
    CHECK(tdkClassify("STAT?") == TDK_P_OTHER);
    CHECK(tdkClassify("PV 100") == TDK_P_OTHER);
}

TEST_CASE("tdkSuggestMap: first exchange adopts the documented layout") {
    const float dvc[6] = {599.99f, 600.00f, 0.50f, 1.30f, 660.0f, 0.0f};
    uint8_t map[6] = {9, 9, 9, 9, 9, 9};
    float sv = 0, si = 0;
    char why[48] = {0};
    CHECK(tdkSuggestMap(dvc, 6, 600.0f, 1.3f, map, &sv, &si, why, sizeof(why)) == 1);
    CHECK(map[0] == 0); CHECK(map[1] == 1); CHECK(map[2] == 2);
    CHECK(map[3] == 3); CHECK(map[4] == 4); CHECK(map[5] == 5);
    CHECK(sv == doctest::Approx(1.0f));
    CHECK(si == doctest::Approx(1.0f));
}

TEST_CASE("tdkSuggestMap: macro at 0 V / 0 A is ambiguous -> no change") {
    const float idle[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t map[6] = {9, 9, 9, 9, 9, 9};
    float sv = 1, si = 1;
    CHECK(tdkSuggestMap(idle, 6, 600.0f, 1.3f, map, &sv, &si, nullptr, 0) == 0);
    CHECK(map[0] == 9);
}

TEST_CASE("tdkSuggestMap: a reply in mV/mA suggests the 0.001 scale") {
    // Same reading expressed in millivolts / milliamps.
    const float mv[6] = {599990.0f, 600000.0f, 500.0f, 1300.0f, 660000.0f, 0.0f};
    uint8_t map[6];
    float sv = 1, si = 1;
    CHECK(tdkSuggestMap(mv, 6, 600.0f, 1.3f, map, &sv, &si, nullptr, 0) == 1);
    CHECK(sv == doctest::Approx(0.001f));
    CHECK(map[0] == 0); CHECK(map[2] == 2);
}

TEST_CASE("tdkSuggestMap: swapped pairs are re-ordered") {
    // Firmware that sends (Iset, Imeas, Vset, Vmeas, OVP, UVL).
    const float swapped[6] = {1.30f, 0.50f, 600.0f, 599.99f, 660.0f, 0.0f};
    uint8_t map[6];
    float sv = 1, si = 1;
    CHECK(tdkSuggestMap(swapped, 6, 600.0f, 1.3f, map, &sv, &si, nullptr, 0) == 1);
    CHECK(map[0] == 3);   // Vmeas
    CHECK(map[1] == 2);   // Vset
    CHECK(map[2] == 1);   // Imeas
    CHECK(map[3] == 0);   // Iset
}

TEST_CASE("tdkSuggestMap: non-numeric or short replies are refused") {
    uint8_t map[6];
    float sv = 1, si = 1;
    const float two[6] = {599.99f, 600.0f, 0, 0, 0, 0};
    CHECK(tdkSuggestMap(two, 2, 600.0f, 1.3f, map, &sv, &si, nullptr, 0) == 0);
}
