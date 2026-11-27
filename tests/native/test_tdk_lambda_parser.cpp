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

TEST_CASE("tdkApplyMap: identity map keeps the documented order") {
    const float dvc[6] = {599.99f, 600.00f, 0.50f, 1.30f, 660.0f, 0.0f};
    const uint8_t map[6] = {0, 1, 2, 3, 4, 5};
    float vM, vS, iM, iS, ovp, uvl;
    tdkApplyMap(dvc, 6, map, 1.0f, 1.0f, 0.0f, 0.0f, &vM, &vS, &iM, &iS, &ovp, &uvl);
    CHECK(vM == doctest::Approx(599.99f));
    CHECK(vS == doctest::Approx(600.00f));
    CHECK(iM == doctest::Approx(0.50f));
    CHECK(iS == doctest::Approx(1.30f));
    CHECK(ovp == doctest::Approx(660.0f));
    CHECK(uvl == doctest::Approx(0.0f));
}

TEST_CASE("tdkApplyMap: swapped map + scale + offsets") {
    // (Iset, Imeas, Vset, Vmeas) and a mV/mA reply with a +2 V calibration offset.
    const float dvc[6] = {1300.0f, 500.0f, 600000.0f, 599990.0f, 660000.0f, 0.0f};
    const uint8_t map[6] = {3, 2, 1, 0, 4, 5};
    float vM, vS, iM, iS, ovp, uvl;
    tdkApplyMap(dvc, 6, map, 0.001f, 0.001f, 2.0f, 0.0f, &vM, &vS, &iM, &iS, &ovp, &uvl);
    CHECK(vM == doctest::Approx(601.99f));   // 599990 * 0.001 + 2
    CHECK(vS == doctest::Approx(602.00f));   // 600000 * 0.001 + 2
    CHECK(iM == doctest::Approx(0.50f));     // 500 * 0.001
    CHECK(iS == doctest::Approx(1.30f));     // 1300 * 0.001
}

TEST_CASE("tdkApplyMap: missing fields fall back to zero, never out of range") {
    const float shortReply[2] = {123.0f, 456.0f};
    const uint8_t map[6] = {0, 9, 1, 9, 9, 9};   // indexes beyond n
    float vM, vS, iM, iS, ovp, uvl;
    tdkApplyMap(shortReply, 2, map, 1.0f, 1.0f, 0.0f, 0.0f, &vM, &vS, &iM, &iS, &ovp, &uvl);
    CHECK(vM == doctest::Approx(123.0f));
    CHECK(vS == doctest::Approx(0.0f));
    CHECK(iM == doctest::Approx(456.0f));
    CHECK(ovp == doctest::Approx(0.0f));
}

TEST_CASE("tdkFormatCsvRow: a single sample is n=1 with min=max=avg") {
    char row[400];
    TdkAgg agg;                            // never fed: the formatter must cope
    const size_t n = tdkFormatCsvRow(row, sizeof(row), "2026-11-26 10:00:00", true,
                                     599.99f, 600.0f, 0.5f, 1.3f, 660.0f, 0.0f, "CV", true, &agg);
    CHECK(n > 0);
    const std::string s(row);
    CHECK(s.find("vout, 599.99") != std::string::npos);
    CHECK(s.find("vmin, 599.99") != std::string::npos);
    CHECK(s.find("vmax, 599.99") != std::string::npos);
    CHECK(s.find("iout, 0.500") != std::string::npos);
    CHECK(s.find("n, 1") != std::string::npos);
    CHECK(s.find("mode, CV") != std::string::npos);
    CHECK(s.find("out, ON") != std::string::npos);
}

TEST_CASE("tdkFormatCsvRow: the aggregate carries min/max/wh and the average") {
    char row[400];
    TdkAgg agg;
    agg.add(600.0f, 1.30f, 1000);
    agg.add(599.0f, 1.35f, 11000);   // 10 s at 600 V / 1.30 A
    agg.add(601.0f, 1.25f, 21000);   // 10 s at 599 V / 1.35 A
    agg.close(31000);                // 10 s at 601 V / 1.25 A
    CHECK(agg.n == 3);
    CHECK(agg.vMin == 599.0f);
    CHECK(agg.vMax == 601.0f);
    CHECK(agg.iMin == 1.25f);
    CHECK(agg.iMax == 1.35f);
    // energy: 3 intervals of 10 s with the value held from the previous sample
    const double wh = (600.0 * 1.30 + 599.0 * 1.35 + 601.0 * 1.25) * (10.0 / 3600.0);
    CHECK(agg.wh > wh - 1e-6);
    CHECK(agg.wh < wh + 1e-6);

    const size_t n = tdkFormatCsvRow(row, sizeof(row), "2026-11-26 10:01:00", false,
                                     600.0f, 600.0f, 1.30f, 1.35f, 660.0f, 0.0f, "CC", false, &agg);
    CHECK(n > 0);
    const std::string s(row);
    CHECK(s.find(",3,599.00,601.00,1.250,1.350,") != std::string::npos);  // samples + min/max
    CHECK(s.find(",CC,0,") != std::string::npos);                          // mode + output off
    CHECK(s.find("600.00") != std::string::npos);                          // average voltage
}

TEST_CASE("tdkFormatCsvRow: bad arguments and a null mode are safe") {
    char row[400];
    TdkAgg agg;
    CHECK(tdkFormatCsvRow(nullptr, sizeof(row), "t", true, 0, 0, 0, 0, 0, 0, "CV", true, &agg) == 0);
    CHECK(tdkFormatCsvRow(row, 0, "t", true, 0, 0, 0, 0, 0, 0, "CV", true, &agg) == 0);
    CHECK(tdkFormatCsvRow(row, sizeof(row), "t", true, 0, 0, 0, 0, 0, 0, nullptr, false, nullptr) > 0);
}

TEST_CASE("TdkSession integrates energy and time in CC / limit / off") {
    TdkSession s;
    s.add(600.0f, 1.0f, "CV", true, false, 60000);   // 1 min at 600 W
    s.add(600.0f, 1.0f, "CC", true, true, 30000);    // 30 s in CC at the limit
    s.add(0.0f, 0.0f, "OFF", false, false, 30000);   // 30 s with the output off
    CHECK(s.transitions == 1);                        // CV -> CC
    CHECK(s.ccMs == 30000u);
    CHECK(s.limMs == 30000u);
    CHECK(s.offMs == 30000u);
    const double wh = 600.0 * 1.0 * (60.0 / 3600.0) + 600.0 * 1.0 * (30.0 / 3600.0);
    CHECK(s.wh > wh - 1e-6);
    CHECK(s.wh < wh + 1e-6);

    s.add(0.0f, 0.0f, "CV", true, false, 1000);       // OFF -> CV is not a CV/CC transition
    CHECK(s.transitions == 1);
    s.add(0.0f, 0.0f, "CC", true, false, 1000);
    CHECK(s.transitions == 2);
}

TEST_CASE("tdkOutFromMode: MODE? decides whether the output is enabled") {
    CHECK(tdkOutFromMode("CV", false) == true);
    CHECK(tdkOutFromMode("CC", false) == true);
    CHECK(tdkOutFromMode("OFF", true) == false);
    CHECK(tdkOutFromMode("CV", true) == true);
    // Anything that is not a mode keeps what we knew (STAT? replies, blanks).
    CHECK(tdkOutFromMode("", true) == true);
    CHECK(tdkOutFromMode("--", false) == false);
    CHECK(tdkOutFromMode("0,0,0", true) == true);
    CHECK(tdkOutFromMode(nullptr, true) == true);
    CHECK(tdkOutFromMode("O", true) == true);      // too short to be OFF
    CHECK(tdkOutFromMode("C", false) == false);
}

TEST_CASE("tdkIsQueryCmd: only trailing-? commands may ride the poll profile") {
    CHECK(tdkIsQueryCmd("DVC?"));
    CHECK(tdkIsQueryCmd("dvc?"));
    CHECK(tdkIsQueryCmd("  MODE?  "));
    CHECK(tdkIsQueryCmd("PV?"));
    CHECK_FALSE(tdkIsQueryCmd("PV 600"));
    CHECK_FALSE(tdkIsQueryCmd("OUT 1"));
    CHECK_FALSE(tdkIsQueryCmd("SAV"));
    CHECK_FALSE(tdkIsQueryCmd(""));
    CHECK_FALSE(tdkIsQueryCmd("   "));
    CHECK_FALSE(tdkIsQueryCmd(nullptr));
}

TEST_CASE("tdkReplyTimeoutMs: 3..15 s, whatever the poll interval") {
    CHECK(tdkReplyTimeoutMs(60000) == 15000);   // 1 min poll: give up after 15 s
    CHECK(tdkReplyTimeoutMs(300000) == 15000);  // 5 min poll: same ceiling
    CHECK(tdkReplyTimeoutMs(10000) == 10000);   // follows a short poll
    CHECK(tdkReplyTimeoutMs(5000) == 5000);
    CHECK(tdkReplyTimeoutMs(1000) == 3000);     // floor
    CHECK(tdkReplyTimeoutMs(0) == 15000);       // unknown -> 60 s -> ceiling
}
