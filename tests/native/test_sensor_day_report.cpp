#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/sensor_day_report.h"

// Production header under test: ShiftEnd-style SensorDay body
// (resumo min/med/max + 12 linhas de 2 em 2 horas com colunas alinhadas e
// glyph sparkline por metrica, escala min-max do dia, humidade junto).

static int countOccurrences(const std::string& s, const std::string& sub) {
    int n = 0;
    size_t p = 0;
    while ((p = s.find(sub, p)) != std::string::npos) { n++; p += sub.size(); }
    return n;
}

TEST_CASE("sensorday: resumo min/med/max da temperatura") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 0, true, 18.0f, false, 0.0f);
    sensorDayAggrAdd(&a, 12, true, 22.0f, false, 0.0f);
    sensorDayAggrAdd(&a, 23, true, 26.0f, false, 0.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    CHECK(b.find("Registos: (SensorDay 2026-10-13)") != std::string::npos);
    // min 18.0, med (18+22+26)/3=22.0, max 26.0, N=3
    CHECK(b.find("Temp min 18.0C  med 22.0C  max 26.0C (N=3)") != std::string::npos);
    CHECK(b.find("Temp por hora:") != std::string::npos);
}

TEST_CASE("sensorday: 12 linhas (2 em 2 horas), vazias com --") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 6, true, 20.0f, false, 0.0f);
    sensorDayAggrAdd(&a, 7, true, 24.0f, false, 0.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    // 12 hour labels 00H,02H..22H (one row every 2 h — phone-width body)
    for (int h = 0; h < 24; h += 2) {
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%02dH", h);
        CHECK(b.find(lbl) != std::string::npos);
    }
    // Odd hours are not listed as rows...
    CHECK(b.find("07H") == std::string::npos);
    CHECK(b.find("23H") == std::string::npos);
    // ...but their samples still count in the summary (min 20 / med 22 / max 24).
    CHECK(b.find("Temp min 20.0C  med 22.0C  max 24.0C (N=2)") != std::string::npos);
    CHECK(b.find("06H  20.0C \xE2\x96\x81") != std::string::npos);
    CHECK(b.find("00H   --") != std::string::npos);
    // 12 rows, one with data -> 11 empty rows.
    CHECK(countOccurrences(b, "--") == 11);
}

TEST_CASE("sensorday: escala min-max com glyphs (min=nivel 0 max=nivel 7)") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 0, true, 10.0f, false, 0.0f);
    sensorDayAggrAdd(&a, 12, true, 30.0f, false, 0.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    CHECK(b.find("12H  30.0C \xE2\x96\x88") != std::string::npos);
    CHECK(b.find("00H  10.0C \xE2\x96\x81") != std::string::npos);
    // glyph direto: max do dia = nivel 7 (U+2588), min = nivel 0 (U+2581)
    CHECK(std::string(sensorDayLevelGlyph(30.0f, 10.0f, 30.0f)) == "\xE2\x96\x88");
    CHECK(std::string(sensorDayLevelGlyph(10.0f, 10.0f, 30.0f)) == "\xE2\x96\x81");
    // meio da escala = nivel 4 (U+2585)
    CHECK(std::string(sensorDayLevelGlyph(20.0f, 10.0f, 30.0f)) == "\xE2\x96\x85");
}

TEST_CASE("sensorday: dia flat rende glyph medio") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 3, true, 22.5f, false, 0.0f);
    sensorDayAggrAdd(&a, 4, true, 22.5f, false, 0.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    CHECK(b.find("Temp min 22.5C  med 22.5C  max 22.5C (N=2)") != std::string::npos);
    CHECK(std::string(sensorDayLevelGlyph(22.5f, 22.5f, 22.5f)) == "\xE2\x96\x85");
    // 03H is odd -> hidden as a row (its sample is inside the summary above);
    // the even 04H row renders the same 22.5C with the mid glyph.
    CHECK(b.find("03H 22.5C") == std::string::npos);
    CHECK(b.find("04H  22.5C \xE2\x96\x85") != std::string::npos);
}

TEST_CASE("sensorday: humidade junto quando simples") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 6, true, 21.0f, true, 40.0f);
    sensorDayAggrAdd(&a, 7, true, 23.0f, true, 60.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    CHECK(b.find("Hum min 40%  med 50%  max 60% (N=2)") != std::string::npos);
    CHECK(b.find("06H  21.0C \xE2\x96\x81   40% \xE2\x96\x81") != std::string::npos);
    // 07H is odd -> not a row (its 60% still shows in the Hum summary above).
    CHECK(b.find("07H") == std::string::npos);
}

TEST_CASE("sensorday: sem humidade omite linha e sufixo") {
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    sensorDayAggrAdd(&a, 6, true, 21.0f, false, 0.0f);
    std::string b = sensorDayBuildBody("2026-10-13", a);
    CHECK(b.find("Hum min") == std::string::npos);
    CHECK(b.find("06H  21.0C \xE2\x96\x85") != std::string::npos);
    CHECK(b.find("%") == std::string::npos);
}

TEST_CASE("sensorday: csv usa temp_avg com preferencia e humidade") {
    std::string csv =
        "timestamp,temp_aht20,temp_avg,humidity\n"
        "2026-10-13 00:10:00,19.0,20.0,40\n"
        "2026-10-13 12:10:00,23.0,24.0,60\n";
    std::string b = sensorDayBuildFromCsv(csv, "2026-10-13");
    // temp_avg (20/24), nao temp_aht20 (19/23)
    CHECK(b.find("Temp min 20.0C  med 22.0C  max 24.0C (N=2)") != std::string::npos);
    CHECK(b.find("00H  20.0C \xE2\x96\x81   40% \xE2\x96\x81") != std::string::npos);
    CHECK(b.find("12H  24.0C \xE2\x96\x88   60% \xE2\x96\x88") != std::string::npos);
}

TEST_CASE("sensorday: csv fallback temp_aht20 e sem humidade") {
    std::string csv =
        "timestamp,temp_aht20\n"
        "2026-10-13 06:00:00,21.5\n"
        "2026-10-13 07:00:00,22.5\n";
    std::string b = sensorDayBuildFromCsv(csv, "2026-10-13");
    CHECK(b.find("Temp min 21.5C  med 22.0C  max 22.5C (N=2)") != std::string::npos);
    CHECK(b.find("Hum") == std::string::npos);
}

TEST_CASE("sensorday: csv vazio/sem coluna temp devolve vazio") {
    CHECK(sensorDayBuildFromCsv("", "2026-10-13") == "");
    CHECK(sensorDayBuildFromCsv("timestamp,humidity\n2026-10-13 06:00:00,50\n",
                                "2026-10-13") == "");
    SensorDayAggr a;
    sensorDayAggrInit(&a);
    CHECK(sensorDayBuildBody("2026-10-13", a) == "");
}

TEST_CASE("sensorday: csv ignora linhas invalidas") {
    std::string csv =
        "timestamp,temp_avg,humidity\n"
        "\n"
        "garbage-line\n"
        "2026-10-13 06:00:00,20.0,50\n"
        "2026-10-13 99:99:99,99.0,99\n";
    std::string b = sensorDayBuildFromCsv(csv, "2026-10-13");
    CHECK(b.find("(N=1)") != std::string::npos);
    CHECK(b.find("06H  20.0C \xE2\x96\x85   50% \xE2\x96\x85") != std::string::npos);
}

TEST_CASE("sensorday: streaming parser equivale ao bulk e usa pouca RAM") {
    std::string csv =
        "timestamp,temp_avg,humidity\n"
        "2026-10-13 06:00:00,20.0,50\n"
        "2026-10-13 07:00:00,22.0,55\n";
    std::string bulk = sensorDayBuildFromCsv(csv, "2026-10-13");
    SensorDayParser p;
    sensorDayParserInit(&p);
    CHECK(sensorDayParserHasData(&p) == false);
    sensorDayParserFeed(&p, "timestamp,temp_avg,humidity");
    sensorDayParserFeed(&p, "2026-10-13 06:00:00,20.0,50");
    sensorDayParserFeed(&p, "2026-10-13 07:00:00,22.0,55");
    CHECK(sensorDayParserHasData(&p) == true);
    CHECK(sensorDayParserBody(&p, "2026-10-13") == bulk);
    // Linhas invalidas nao partem o parser nem criam dados.
    SensorDayParser q;
    sensorDayParserInit(&q);
    sensorDayParserFeed(&q, "");
    sensorDayParserFeed(&q, "garbage");
    sensorDayParserFeed(&q, "timestamp,humidity");
    sensorDayParserFeed(&q, "2026-10-13 06:00:00,50");
    CHECK(sensorDayParserHasData(&q) == false);
    CHECK(sensorDayParserBody(&q, "2026-10-13") == "");
}
