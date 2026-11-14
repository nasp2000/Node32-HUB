// Native tests for the alert slot save rule (chatgpt/src/alerts_slot_merge.h).
//
// ALERTS-6: the alerts page posts every row, disabled ones included, with
// whatever the form shows (the defaults for a row that was never edited). The
// handlers wrote those values back, so saving with a slot disabled wiped its
// tuned timeout/max/e-mails. A disabled row now keeps what is stored; only the
// `enabled` flag follows the request.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>

#include "alerts_slot_merge.h"

#include <string>

namespace {
bool sameText(const String& a, const String& b) {
    return std::string(a.c_str()) == std::string(b.c_str());
}
} // namespace

TEST_CASE("a disabled slot keeps its stored numbers, not the form defaults") {
    // Form posts the defaults (20 min / 12 per day) for a row that was never
    // touched; the stored configuration of that disabled slot was 45 / 30.
    const int storedTimeout = 45, storedMax = 30;
    CHECK(alertsMergeInt(false, 20, storedTimeout) == 45);
    CHECK(alertsMergeInt(false, 6, storedMax) == 30);
    // ... on every save, so repeated saves cannot erode it
    int timeout = 20;
    for (int save = 0; save < 5; save++) {
        timeout = alertsMergeInt(false, 20, timeout);
    }
    CHECK(timeout == 20);   // first save: stored was 20 → stays 20
    int t2 = storedTimeout;
    for (int save = 0; save < 5; save++) {
        t2 = alertsMergeInt(false, 20, t2);
    }
    CHECK(t2 == 45);
}

TEST_CASE("an enabled slot takes what the form sent") {
    CHECK(alertsMergeInt(true, 20, 45) == 20);
    CHECK(alertsMergeInt(true, 100, 3) == 100);
    CHECK(sameText(alertsMergeText(true, String("a@b.com"), String("old@x.io")), String("a@b.com")));
}

TEST_CASE("a disabled slot keeps its stored e-mail list") {
    CHECK(sameText(alertsMergeText(false, String(""), String("ops@example.com, boss@example.com")),
                   String("ops@example.com, boss@example.com")));
    // and an empty stored list stays empty (not turned into the form default)
    CHECK(sameText(alertsMergeText(false, String("x@y.z"), String("")), String("")));
}

TEST_CASE("the OEE tab fields follow the same rule") {
    // metric / threshold / persist / max_per_day are merged with the same call
    CHECK(alertsMergeInt(false, 1, 2) == 2);    // metric
    CHECK(alertsMergeInt(false, 60, 85) == 85); // threshold
    CHECK(alertsMergeInt(false, 15, 45) == 45); // persist min
    CHECK(alertsMergeInt(false, 6, 24) == 24);  // max per day
    CHECK(alertsMergeInt(true, 85, 60) == 85);  // enabled: form wins
}

TEST_CASE("enabling a slot never resurrects the old numbers") {
    // A user turns the row on and edits it: the form values must win outright.
    CHECK(alertsMergeInt(true, 5, 1440) == 5);
    CHECK(alertsMergeInt(true, 1, 100) == 1);
    CHECK(sameText(alertsMergeText(true, String("new@ex.com"), String("old@ex.com")), String("new@ex.com")));
}
