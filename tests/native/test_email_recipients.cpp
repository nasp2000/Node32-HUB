#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compile the REAL production recipient normalizer — the exact helper the SMTP
// transport (RCPT TO + To:) and the emailday-batch envelope use, so the
// persisted `to=` matches what was actually tried (no dupes, no stray spaces).
#include "Arduino.h"
#include "email_recipients.h"

TEST_CASE("brief case: primary + extras with dupe and stray space") {
    // .29 log showed: "nasp2000@gmail.com, nasp2000@gmail.com,joana2escura@gmail.com"
    String out = emailNormalizeRecipientList("nasp2000@gmail.com, nasp2000@gmail.com,joana2escura@gmail.com");
    CHECK(out == "nasp2000@gmail.com,joana2escura@gmail.com");
}

TEST_CASE("case-insensitive dedupe, first spelling wins, order stable") {
    String out = emailNormalizeRecipientList("b@y.com, A@X.com, a@x.com ,b@y.com");
    CHECK(out == "b@y.com,A@X.com");
}

TEST_CASE("semicolons, angle brackets, empties and junk dropped") {
    String out = emailNormalizeRecipientList(" <a@x.com> ; ;b@y.com, ,no-at-here,");
    CHECK(out == "a@x.com,b@y.com");
}

TEST_CASE("single address unchanged; empty in gives empty out") {
    CHECK(emailNormalizeRecipientList("solo@x.com") == "solo@x.com");
    CHECK(emailNormalizeRecipientList("") == "");
    CHECK(emailNormalizeRecipientList(" , ; ,") == "");
}
