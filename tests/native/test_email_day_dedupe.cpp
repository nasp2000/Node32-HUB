#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compile the REAL production dedupe helper — the exact guard shipped in
// startEmailDayAsync (single funnel for EMAILDAY/LOGDAY/SENSORDAY/SHIFTDAY).
#include "Arduino.h"
#include "email_day_dedupe.h"

TEST_CASE("first send always allowed, records state") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 1000) == true);
    CHECK(st.armed == true);
    CHECK(st.folder == "/Equip15");
}

TEST_CASE("identical send inside the window is refused") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 1000) == true);
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 61000) == false);   // +60s
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 121000) == false);  // +120s
}

TEST_CASE("identical send past the window is allowed") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 1000) == true);
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 181001) == true); // +180s past window
}

TEST_CASE("different folder or subject is a different send") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 1000) == true);
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject B", 2000) == true);
    CHECK(emailDayResendAllowed(st, "/logs/log_14-10-2026", "Subject B", 3000) == true);
}

TEST_CASE("refused send does not move the window (third send still gated on first)") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 1000) == true);
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 60000) == false);
    // Window still anchored at t=1000: t=181001 is past it.
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 181001) == true);
}

TEST_CASE("millis wrap stays safe") {
    EmailDayDedupeState st;
    st.armed = false;
    st.ms = 0;
    unsigned long near_wrap = 4294967295UL - 60000UL;
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", near_wrap) == true);
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 30000UL) == false); // +90s across wrap
    CHECK(emailDayResendAllowed(st, "/Equip15", "Subject A", 300000UL) == true); // past window
}
