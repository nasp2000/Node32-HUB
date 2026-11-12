// Native tests for the alert e-mail validation (chatgpt/src/email_validation.h).
//
// Defect (BACKLOG ALERTS-2): the alerts page and both alert config handlers only
// checked `emails.indexOf('@') >= 0`, so "a@", "@b.com" and "a@b" were all
// accepted and then failed silently at send time.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "email_validation.h"

#include <string>

TEST_CASE("the old rule accepted these; the new one rejects them") {
    // What `indexOf('@') >= 0` let through.
    CHECK_FALSE(emailAddressLooksValid("a@"));
    CHECK_FALSE(emailAddressLooksValid("@b.com"));
    CHECK_FALSE(emailAddressLooksValid("a@b"));
    CHECK_FALSE(emailAddressLooksValid("a@.com"));
    CHECK_FALSE(emailAddressLooksValid("a@b."));
    CHECK_FALSE(emailAddressLooksValid("a@@b.com"));
    CHECK_FALSE(emailAddressLooksValid("a b@c.com"));
    CHECK_FALSE(emailAddressLooksValid("a@b..com"));
    CHECK_FALSE(emailAddressLooksValid("a@-b.com"));
    CHECK_FALSE(emailAddressLooksValid("a@b-.com"));
    CHECK_FALSE(emailAddressLooksValid("a@b.c"));      // 1-letter TLD
    CHECK_FALSE(emailAddressLooksValid("a@b.c0m"));    // digit in TLD
    CHECK_FALSE(emailAddressLooksValid(".a@b.com"));
    CHECK_FALSE(emailAddressLooksValid("a.@b.com"));
    CHECK_FALSE(emailAddressLooksValid("a..b@c.com"));
    CHECK_FALSE(emailAddressLooksValid(""));
    CHECK_FALSE(emailAddressLooksValid("plainaddress"));
}

TEST_CASE("ordinary addresses are accepted") {
    CHECK(emailAddressLooksValid("a@b.co"));
    CHECK(emailAddressLooksValid("user@example.com"));
    CHECK(emailAddressLooksValid("first.last@sub.example.co.uk"));
    CHECK(emailAddressLooksValid("user+tag@example.org"));
    CHECK(emailAddressLooksValid("USER_1@EXAMPLE.COM"));
    CHECK(emailAddressLooksValid("o'brien@example.ie"));
    CHECK(emailAddressLooksValid("a-b@my-host.example.com"));
    CHECK(emailAddressLooksValid("x@y.io"));
}

TEST_CASE("limits: local part, total length, label length") {
    std::string local64(64, 'a');
    CHECK(emailAddressLooksValid((local64 + "@example.com").c_str()));
    std::string local65(65, 'a');
    CHECK_FALSE(emailAddressLooksValid((local65 + "@example.com").c_str()));

    std::string tld63(63, 'a');
    CHECK(emailAddressLooksValid(("a@b." + tld63).c_str()));
    std::string tld64(64, 'a');
    CHECK_FALSE(emailAddressLooksValid(("a@b." + tld64).c_str()));

    std::string huge(300, 'a');
    CHECK_FALSE(emailAddressLooksValid((huge + "@example.com").c_str()));
}

TEST_CASE("alert fields hold a list: every item must be valid") {
    CHECK(emailListLooksValid("a@b.com"));
    CHECK(emailListLooksValid("a@b.com, c@d.org"));
    CHECK(emailListLooksValid("a@b.com;c@d.org"));
    CHECK(emailListLooksValid("  a@b.com ,  c@d.org  "));
    CHECK(emailListLooksValid("a@b.com,,c@d.org"));       // empty item ignored
    CHECK_FALSE(emailListLooksValid("a@b.com, a@"));      // one bad item rejects the list
    CHECK_FALSE(emailListLooksValid("a@"));
    CHECK_FALSE(emailListLooksValid(""));
    CHECK_FALSE(emailListLooksValid("   "));
    CHECK_FALSE(emailListLooksValid(",,"));
    CHECK_FALSE(emailListLooksValid("a@b.com, not-an-address"));
}

TEST_CASE("match the inline JS rule used by the alerts page") {
    // Same table as alertsEmailListValid() in page_alerts_asset.cpp: if this
    // fails, the client and the server disagree and users get a save that the
    // firmware rejects (or the opposite).
    const char* ok[] = {"a@b.co", "user.name+tag@example.com", "a@b.com, c@d.org"};
    const char* bad[] = {"a@", "@b.com", "a@b", "a@b.c", "a b@c.com", "a@b.com, x@", ""};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) CHECK(emailListLooksValid(ok[i]));
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK_FALSE(emailListLooksValid(bad[i]));
}
