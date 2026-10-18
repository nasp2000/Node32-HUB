#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/email_body_archive.h"

// Production header under test: T24 — arquivo .txt do corpo de TODOS os
// emails (/{sys}/<tipo>_<YYYY-MM-DD>_<HH-MM>.txt, padrao T8b turno_data).
// Puro (sem deps Arduino): tipo por subject + path + sanitize.

TEST_CASE("archive: tipo a partir do subject (familia EmailDay)") {
    CHECK(emailArchiveTipoFromSubject("Node32 - EmailDay") == "EMAILDAY");
    CHECK(emailArchiveTipoFromSubject("Servo - emailday 13-10-2026") == "EMAILDAY");
    CHECK(emailArchiveTipoFromSubject("X - LogDay 13-10-2026") == "LOGDAY");
    CHECK(emailArchiveTipoFromSubject("X - SensorDay 2026-10-13") == "SENSORDAY");
    CHECK(emailArchiveTipoFromSubject("X - ShiftDay 13-10-2026") == "SHIFTDAY");
}

TEST_CASE("archive: ShiftEnd inclui FimTurno") {
    CHECK(emailArchiveTipoFromSubject("N16R8 - ShiftEnd - 13-10-2026") == "SHIFTEND");
    CHECK(emailArchiveTipoFromSubject("N16R8 - FimTurno - 13-10-2026") == "SHIFTEND");
    CHECK(emailArchiveTipoFromSubject("Alerta de Inatividade") == "INACTIVITY");
    CHECK(emailArchiveTipoFromSubject("Inactivity Alert") == "INACTIVITY");
}

TEST_CASE("archive: restantes tipos e fallback") {
    CHECK(emailArchiveTipoFromSubject("Sensor Alert - Temperature") == "ALARM");
    CHECK(emailArchiveTipoFromSubject("Test email") == "TEST");
    CHECK(emailArchiveTipoFromSubject("Unikon chegada") == "UNIKON");
    CHECK(emailArchiveTipoFromSubject("Motion detetado") == "MOTION");
    CHECK(emailArchiveTipoFromSubject("qualquer coisa") == "EMAIL");
    CHECK(emailArchiveTipoFromSubjectOr("qualquer coisa", "UNIKON") == "UNIKON");
    CHECK(emailArchiveTipoFromSubjectOr("", "") == "EMAIL");
}

TEST_CASE("archive: path segue /{sys}/<tipo>_<data>_<hora>.txt") {
    CHECK(emailArchivePath("N16R8", "EMAILDAY", "2026-10-13", "17-20")
          == "/N16R8/EMAILDAY_2026-10-13_17-20.txt");
    CHECK(emailArchivePath("SD_LOG", "SENSORDAY", "2026-10-13", "08-00")
          == "/SD_LOG/SENSORDAY_2026-10-13_08-00.txt");
    // sys vazio -> SD_LOG; tipo normalizado para maiusculas
    CHECK(emailArchivePath("", "sensorday", "2026-10-13", "08-00")
          == "/SD_LOG/SENSORDAY_2026-10-13_08-00.txt");
    // labels vazias -> nodate/notime (fallback sem NTP)
    CHECK(emailArchivePath("SYS", "STREAM", "", "")
          == "/SYS/STREAM_nodate_notime.txt");
}

TEST_CASE("archive: sanitize nao quebra o path") {
    CHECK(emailArchiveSanitizeToken("N16 R8.2", 32) == "N16_R8_2");
    CHECK(emailArchiveSanitizeToken("café/<>", 32) == "caf");
    CHECK(emailArchiveSanitizeToken("EMAILDAY", 16) == "EMAILDAY");
    // maxLen corta sem rebentar o snprintf do path
    CHECK(emailArchivePath("SYS", "EMAILDAY", "2026-10-13", "17-20").size() < 128);
    CHECK(emailArchiveSanitizeToken("123456789", 4) == "1234");
}
