// Native tests for the alert "max per day" range (chatgpt/src/alerts_limits.h).
//
// ALERTS-4: the page offered up to 60/day while the API accepted and the docs
// documented 1..100, so an API-set value (e.g. 80) could not be seen or edited
// in the UI and the next save snapped it to 60.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "alerts_limits.h"

TEST_CASE("the select reaches the documented API limit") {
    CHECK(alertsMaxPerDayOptionCount() >= 10);
    // ascending, and the last option is the documented cap
    for (int i = 1; i < alertsMaxPerDayOptionCount(); i++) {
        CHECK(alertsMaxPerDayOptionAt(i) > alertsMaxPerDayOptionAt(i - 1));
    }
    CHECK(alertsMaxPerDayOptionAt(alertsMaxPerDayOptionCount() - 1) == ALERTS_MAX_PER_DAY_LIMIT);
    CHECK(ALERTS_MAX_PER_DAY_LIMIT == 100);
}

TEST_CASE("every value the page offered before is still offered") {
    const int previous[] = {3, 6, 9, 12, 15, 20, 25, 30, 45, 60};
    for (size_t i = 0; i < sizeof(previous) / sizeof(previous[0]); i++) {
        CHECK(alertsMaxPerDayIsOffered(previous[i]));
    }
    CHECK(alertsMaxPerDayIsOffered(75));
    CHECK(alertsMaxPerDayIsOffered(90));
    CHECK(alertsMaxPerDayIsOffered(100));
}

TEST_CASE("a stored value outside the list still gets an option") {
    // The defect: API-set 80 had no option, so the UI showed 60 and saving
    // overwrote the value. The renderer now appends it.
    CHECK(alertsMaxPerDayExtraOption(80) == 80);
    CHECK(alertsMaxPerDayExtraOption(1) == 1);
    CHECK(alertsMaxPerDayExtraOption(99) == 99);
    // Values already in the list are not duplicated.
    CHECK(alertsMaxPerDayExtraOption(60) == 0);
    CHECK(alertsMaxPerDayExtraOption(100) == 0);
    // Out of range (legacy junk): no extra option, the handler rejects it.
    CHECK(alertsMaxPerDayExtraOption(0) == 0);
    CHECK(alertsMaxPerDayExtraOption(-5) == 0);
    CHECK(alertsMaxPerDayExtraOption(101) == 0);
}

TEST_CASE("range check matches the config handlers (1..100)") {
    CHECK_FALSE(alertsMaxPerDayInRange(0));
    CHECK(alertsMaxPerDayInRange(1));
    CHECK(alertsMaxPerDayInRange(60));
    CHECK(alertsMaxPerDayInRange(100));
    CHECK_FALSE(alertsMaxPerDayInRange(101));
    CHECK_FALSE(alertsMaxPerDayInRange(200));
}

TEST_CASE("handler validation reports why a value is refused") {
    // The handlers call alertsValidateMaxPerDay(); the message is built from the
    // same constants, so the bounds cannot drift from the UI again.
    CHECK(alertsValidateMaxPerDay(-1) == ALERTS_MAX_PER_DAY_TOO_LOW);
    CHECK(alertsValidateMaxPerDay(0) == ALERTS_MAX_PER_DAY_TOO_LOW);
    CHECK(alertsValidateMaxPerDay(1) == ALERTS_MAX_PER_DAY_OK);
    CHECK(alertsValidateMaxPerDay(60) == ALERTS_MAX_PER_DAY_OK);
    CHECK(alertsValidateMaxPerDay(80) == ALERTS_MAX_PER_DAY_OK);   // API-set value the UI must show
    CHECK(alertsValidateMaxPerDay(100) == ALERTS_MAX_PER_DAY_OK);
    CHECK(alertsValidateMaxPerDay(101) == ALERTS_MAX_PER_DAY_TOO_HIGH);
    CHECK(alertsValidateMaxPerDay(1000) == ALERTS_MAX_PER_DAY_TOO_HIGH);
}

TEST_CASE("option index guards") {
    CHECK(alertsMaxPerDayOptionAt(-1) == 0);
    CHECK(alertsMaxPerDayOptionAt(alertsMaxPerDayOptionCount()) == 0);
    CHECK(alertsMaxPerDayOptionAt(0) >= ALERTS_MAX_PER_DAY_MIN);
}
