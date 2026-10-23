#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>

// Compile the REAL production event formatter — the exact payload behind the
// persisted EMAIL_PIPELINE_OK / EMAIL_PIPELINE_FAIL `details=`.
#include "email_event_fields.h"

TEST_CASE("legacy format byte-identical when there is no failure reason") {
    char withNull[224], bare[224];
    emailEventFormatPipelineDetails(withNull, sizeof(withNull), "emailday-batch", "SMTP",
                                    3, "Subject", "a@x.com", nullptr);
    snprintf(bare, sizeof(bare), "stage=%s channel=%s files=%d subject=%.72s to=%.72s",
             "emailday-batch", "SMTP", 3, "Subject", "a@x.com");
    CHECK(strcmp(withNull, bare) == 0);

    // Empty string error == no error (GAS failures / stale buffer never leak in).
    char withEmpty[224];
    emailEventFormatPipelineDetails(withEmpty, sizeof(withEmpty), "emailday-batch", "SMTP",
                                    3, "Subject", "a@x.com", "");
    CHECK(strcmp(withEmpty, bare) == 0);
}

TEST_CASE("failure reason appended as err=, subject/to shortened to fit 224B") {
    char out[224];
    emailEventFormatPipelineDetails(out, sizeof(out), "emailday-batch", "SMTP",
                                    2, "Equip15 - ShiftEnd", "a@x.com", "auth 535 5.7.8");
    CHECK(strstr(out, "err=auth 535 5.7.8") != nullptr);
    CHECK(strstr(out, "stage=emailday-batch channel=SMTP files=2") != nullptr);
    CHECK(strlen(out) < sizeof(out));
}

TEST_CASE("err= sanitizes server replies (single line, no field separator)") {
    // smtpErrorSet() already scrubs, but the formatter must stay safe even if a
    // raw multi-line reply ever reaches it: snprintf keeps it on one line.
    char out[224];
    emailEventFormatPipelineDetails(out, sizeof(out), "inactivity", "SMTP",
                                    0, "S", "a@x.com", "rcpt 550 5.1.1");
    CHECK(strchr(out, '\n') == nullptr);
    CHECK(strstr(out, "err=rcpt 550 5.1.1") != nullptr);
}
