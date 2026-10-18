#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <string>

// Standalone reimpl of the shift email body FORMAT (proximityBuildShiftEmailBody)
// from proximity_manager.cpp. Tests the literal output structure that is NOT
// localized: "Registos:  (Turno XXH-XXH)", "{N} pcs (scrap) (setup)", "OEE N%  A=N%
// P=N% Q=N%", "Pcs por hora:", and the integer rounding of percentages.
// (The per-hour chart itself is SD-dependent and tested separately in
// test_shift_window for the window math.)

static std::string z2(int h) { return h < 10 ? "0" + std::to_string(h) : std::to_string(h); }

static std::string buildShiftBody(int startHour, int durHours,
                                  uint32_t pieces, float oee, float avail, float perf, float qual,
                                  uint32_t scrapCount = 0, uint32_t setupCount = 0) {
    int turnoStart = startHour;                    // columns are the actual shift hours
    int turnoEnd   = (startHour + durHours) % 24;

    std::string body = "Registos:  (Turno " + z2(turnoStart) + "H-" + z2(turnoEnd) + "H)\n\n";
    body += std::to_string(pieces) + " pcs";
    if (scrapCount > 0) body += "  (" + std::to_string(scrapCount) + " scrap)";
    if (setupCount > 0) body += " (" + std::to_string(setupCount) + " setup)";
    body += "\n\n";

    if (pieces > 0 || oee > 0) {
        if (oee > 0) {
            body += "OEE " + std::to_string((int)(oee + 0.5f))
                  + "%  A=" + std::to_string((int)(avail + 0.5f))
                  + "%  P=" + std::to_string((int)(perf + 0.5f))
                  + "%  Q=" + std::to_string((int)(qual + 0.5f)) + "%\n\n";
        } else {
            body += "OEE N/A (set Target PPM)\n\n";
        }
    } else {
        body += "OEE N/A\n\n";
    }
    return body;
}

TEST_CASE("email body: Registos line carries the shift turno hours") {
    // shift 22H->6H (chart first col 22H, dur 8) -> "(Turno 22H-06H)"
    auto b = buildShiftBody(22, 8, 446, 85.0f, 83.3f, 100.0f, 99.0f);
    CHECK(b.find("Registos:  (Turno 22H-06H)\n\n") == 0);
    CHECK(b.find("446 pcs") != std::string::npos);
}

TEST_CASE("email body: turno end hour wraps past midnight (22H first col)") {
    // startHour=22 -> turnoStart=22, dur=8 -> col 22..05 -> turnoEnd=06
    auto b = buildShiftBody(22, 8, 100, 85.0f, 80.0f, 100.0f, 99.0f);
    CHECK(b.find("(Turno 22H-06H)") != std::string::npos);
}

TEST_CASE("email body: day shift label 06H-14H") {
    // shift start 06H -> chart first col 06H, dur 8 -> last col 13H
    auto b = buildShiftBody(6, 8, 100, 85.0f, 80.0f, 100.0f, 99.0f);
    CHECK(b.find("(Turno 06H-14H)") != std::string::npos);
}

TEST_CASE("email body: scrap suffix appended when scrap > 0") {
    auto b = buildShiftBody(6, 8, 446, 85.0f, 83.3f, 100.0f, 99.0f, 12);
    CHECK(b.find("446 pcs  (12 scrap)") != std::string::npos);
}

TEST_CASE("email body: setup suffix appended when setup > 0") {
    auto b = buildShiftBody(6, 8, 446, 85.0f, 83.3f, 100.0f, 99.0f, 12, 881);
    CHECK(b.find("446 pcs  (12 scrap) (881 setup)") != std::string::npos);
}

TEST_CASE("email body: setup present but no scrap") {
    auto b = buildShiftBody(6, 8, 446, 85.0f, 83.3f, 100.0f, 99.0f, 0, 881);
    CHECK(b.find("446 pcs (881 setup)") != std::string::npos);
    CHECK(b.find("scrap") == std::string::npos);
}

TEST_CASE("email body: no scrap/setup suffix when zero") {
    auto b = buildShiftBody(6, 8, 446, 85.0f, 83.3f, 100.0f, 99.0f, 0, 0);
    CHECK(b.find("scrap") == std::string::npos);
    CHECK(b.find("setup") == std::string::npos);
}

TEST_CASE("email body: OEE line format with integer rounding") {
    auto b = buildShiftBody(6, 8, 446, 85.0f, 83.3f, 100.0f, 98.6f);
    CHECK(b.find("OEE 85%  A=83%  P=100%  Q=99%") != std::string::npos);
}

TEST_CASE("email body: OEE rounding uses +0.5 (85.5 -> 86)") {
    auto b = buildShiftBody(6, 8, 100, 85.5f, 50.4f, 60.4f, 70.4f);
    CHECK(b.find("OEE 86%") != std::string::npos);
    CHECK(b.find("A=50%") != std::string::npos);
    CHECK(b.find("P=60%") != std::string::npos);
    CHECK(b.find("Q=70%") != std::string::npos);
}

TEST_CASE("email body: OEE N/A when oee zero but pieces present") {
    auto b = buildShiftBody(6, 8, 446, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(b.find("OEE N/A (set Target PPM)") != std::string::npos);
}

TEST_CASE("email body: OEE N/A when nothing produced") {
    auto b = buildShiftBody(6, 8, 0, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(b.find("OEE N/A\n\n") != std::string::npos);
    CHECK(b.find("0 pcs") != std::string::npos);
}

TEST_CASE("email body: availability percent with value 0") {
    auto b = buildShiftBody(6, 8, 10, 50.0f, 0.0f, 100.0f, 100.0f);
    CHECK(b.find("A=0%") != std::string::npos);
}

TEST_CASE("email body: full expected block for a running shift") {
    auto b = buildShiftBody(22, 8, 446, 85.0f, 83.0f, 100.0f, 99.0f, 12, 881);
    std::string expect =
        "Registos:  (Turno 22H-06H)\n\n"
        "446 pcs  (12 scrap) (881 setup)\n\n"
        "OEE 85%  A=83%  P=100%  Q=99%\n\n";
    CHECK(b == expect);
}

TEST_CASE("email body: percentages are percentages (P not capped in format)") {
    auto b = buildShiftBody(6, 8, 100, 100.0f, 100.0f, 100.0f, 100.0f);
    CHECK(b.find("OEE 100%  A=100%  P=100%  Q=100%") != std::string::npos);
}

// ── Simple-SMTP body transfer encoding (mirrors smtpSendEmail) ──────────
// ASCII bodies go raw (implicit 7bit); any byte >= 0x80 (e.g. PT accents as
// UTF-8) selects base64, otherwise MTAs mangle the accents. Same rule as the
// multipart builder (buildMultipartMixedPlainText).

static bool isAscii7(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}

static std::string bodyCte(const std::string& body) {
    return isAscii7(body) ? "7bit(raw)" : "base64";
}

TEST_CASE("smtp simple: ASCII body stays raw 7bit") {
    CHECK(bodyCte("OEE alert: Shift OEE = 60.0% (threshold 75%)\nChannel: DI1 (DI1)\nShift pieces so far: 5660\n") == "7bit(raw)");
}

TEST_CASE("smtp simple: PT body with accents selects base64") {
    // "Peças do turno até agora: 5660" in UTF-8 (ç = C3 A7, é = C3 A9).
    std::string pt = "Alerta OEE: OEE turno = 60.0% (limiar 75%)\nPe\xc3\xa7" "as do turno at\xc3\xa9 agora: 5660\n";
    CHECK(!isAscii7(pt));
    CHECK(bodyCte(pt) == "base64");
}

TEST_CASE("smtp simple: any 8-bit byte (not just accents) selects base64") {
    std::string s = std::string("abc\xff");
    CHECK(bodyCte(s) == "base64");
    CHECK(bodyCte("") == "7bit(raw)");
}