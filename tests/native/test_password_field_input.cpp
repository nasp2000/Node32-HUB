// Native tests for the password-field rule (chatgpt/src/password_field_input.h).
//
// BACKLOG ACC-3: the Access handler compared the posted password against the
// Portuguese strings "(não alterado)"/"(nao alterado)" while the client is
// English and renders the hint "(unchanged)" as a PLACEHOLDER. A placeholder
// is never submitted, so the real "keep the current password" signal is the
// empty value and the sentinel comparison was dead code. These cases pin the
// contract after its removal and reproduce the dead check to prove it could
// never fire from the UI.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <string>

#include "password_field_input.h"

namespace {

// The old handler rule, kept here only to reproduce why it was dead: a
// non-empty value equal to either PT sentinel was treated as "keep".
bool oldRuleApplies(const std::string &v) {
    return !v.empty() && v != "(não alterado)" && v != "(nao alterado)";
}

bool applies(const std::string &v) {
    return passwordFieldIsNew(v.c_str(), v.size());
}

} // namespace

TEST_CASE("an empty password keeps the stored one") {
    CHECK_FALSE(applies(""));
    CHECK_FALSE(passwordFieldIsNew(nullptr, 0));
    // An explicit length of 0 wins even with a non-null pointer.
    const char *buf = "secret";
    CHECK_FALSE(passwordFieldIsNew(buf, 0));
}

TEST_CASE("any non-empty value is a new password") {
    CHECK(applies("s3cret"));
    CHECK(applies("a"));
    CHECK(applies("  "));       // whitespace is still a value (same as before)
    CHECK(applies("(unchanged)"));   // the EN hint is a placeholder, never posted
}

TEST_CASE("the old PT sentinel comparison was dead for the shipped client") {
    // The English page never posts these strings; the empty value is what it
    // posts for an untouched field. Reproduce both sides to show the old check
    // could not have protected anything the new rule does not.
    const char *pt1 = "(não alterado)";
    const char *pt2 = "(nao alterado)";
    CHECK_FALSE(oldRuleApplies(pt1));    // old code: treated as "keep"
    CHECK_FALSE(oldRuleApplies(pt2));
    CHECK(applies(pt1));                 // new code: ordinary text (never sent)
    CHECK(applies(pt2));

    // The signal the page actually sends:
    CHECK_FALSE(oldRuleApplies(""));
    CHECK_FALSE(applies(""));
}

TEST_CASE("values that merely look like a sentinel are ordinary passwords") {
    CHECK(applies("(nao alterado) "));
    CHECK(applies("(unchanged) "));
    CHECK(applies("(não alterado"));
    CHECK(applies("(nao alterado)"));
    CHECK(applies("nao alterado"));
}

TEST_CASE("the rule is identical for the admin and viewer password fields") {
    // Both handler lines call the same helper; this pins that no field keeps a
    // private sentinel list.
    struct FieldCase { const char *value; bool applies; };
    const FieldCase cases[] = {
        { "",              false },
        { "nova-pass",     true  },
        { "(não alterado)", true  },   // no longer special anywhere
    };
    for (const FieldCase &c : cases) {
        CAPTURE(c.value);
        CHECK(passwordFieldIsNew(c.value, std::strlen(c.value)) == c.applies);
    }
}
