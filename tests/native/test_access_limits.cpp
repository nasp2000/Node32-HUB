// Native tests for the Access numeric-field limits (BACKLOG ACC-5).
//
// The page was narrower than the API without saying so: idle timeout was a
// <select> of 5/10/15/20/30/60 while the server accepted 1..120, and the
// absolute timeout a <select> of 15/20/30/60/120 while the server accepted
// 5..240. A script could store a value the browser could not display, and the
// browser could not offer every value the server accepts.
//
// Decision: widen the UI to the API limits (the API range is the documented
// contract and already persisted values must stay valid). chatgpt/src/
// access_limits.h is the single source of truth used by the handler; these
// cases compare the page markup against it, so the two cannot drift again.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// No <Arduino.h>: the asset only needs PROGMEM (stubs/pgmspace.h), and the
// Arduino stub would redefine the same pgm_read_* macros with warnings.
#include "access_limits.h"
#include "webpages/assets/page_access_asset.h"

#include <string>

namespace {

std::string page() { return std::string(PAGE_ACCESS); }

size_t countOf(const std::string &hay, const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

// The whole opening tag that carries name='<key>'.
std::string tagOf(const std::string &html, const std::string &key) {
    const size_t at = html.find("name='" + key + "'");
    if (at == std::string::npos) return "";
    const size_t open = html.rfind('<', at);
    const size_t close = html.find('>', at);
    if (open == std::string::npos || close == std::string::npos) return "";
    return html.substr(open, close - open + 1);
}

std::string attrOf(const std::string &tag, const std::string &name) {
    const size_t at = tag.find(name + "='");
    if (at == std::string::npos) return "";
    const size_t start = at + name.size() + 2;
    const size_t end = tag.find('\'', start);
    if (end == std::string::npos) return "";
    return tag.substr(start, end - start);
}

} // namespace

TEST_CASE("the shared table pins the chosen limits") {
    REQUIRE(kAccessIntFieldCount == 3);
    // Documented contract, reported verbatim by the 400 payload.
    CHECK(std::string(kAccessIntFields[0].key) == "web_max_sessions");
    CHECK(kAccessIntFields[0].minVal == 1);
    CHECK(kAccessIntFields[0].maxVal == 5);
    CHECK(std::string(kAccessIntFields[1].key) == "web_idle_timeout_min");
    CHECK(kAccessIntFields[1].minVal == 1);
    CHECK(kAccessIntFields[1].maxVal == 120);
    CHECK(std::string(kAccessIntFields[2].key) == "web_abs_timeout_min");
    CHECK(kAccessIntFields[2].minVal == 5);
    CHECK(kAccessIntFields[2].maxVal == 240);
}

TEST_CASE("every page control uses the same min/max as the API table") {
    const std::string html = page();
    for (int i = 0; i < kAccessIntFieldCount; i++) {
        const std::string key(kAccessIntFields[i].key);
        CAPTURE(key);
        CHECK(countOf(html, "name='" + key + "'") == 1);
        const std::string tag = tagOf(html, key);
        REQUIRE(!tag.empty());
        CHECK(attrOf(tag, "type") == "number");
        CHECK(attrOf(tag, "min") == std::to_string(kAccessIntFields[i].minVal));
        CHECK(attrOf(tag, "max") == std::to_string(kAccessIntFields[i].maxVal));
        CHECK(attrOf(tag, "value") == std::to_string(kAccessIntFields[i].defaultValue));
    }
}

TEST_CASE("the timeouts are free numeric inputs, not narrow option lists") {
    const std::string html = page();
    // The ACC-5 defect: a <select> capped the choice at the listed values.
    CHECK(countOf(html, "<select name='web_idle_timeout_min'") == 0);
    CHECK(countOf(html, "<select name='web_abs_timeout_min'") == 0);
    CHECK(tagOf(html, "web_idle_timeout_min").find("<select") == std::string::npos);
    CHECK(tagOf(html, "web_abs_timeout_min").find("<select") == std::string::npos);
}

TEST_CASE("values the API accepts are representable in the control") {
    const std::string html = page();
    // A value stored by a script (e.g. idle 90, abs 200) must be displayable;
    // a number input takes anything in range, an option list silently blanked.
    for (const char *key : { "web_idle_timeout_min", "web_abs_timeout_min" }) {
        CAPTURE(key);
        const std::string tag = tagOf(html, key);
        REQUIRE(!tag.empty());
        CHECK(tag.find("<input") != std::string::npos);
        CHECK(tag.find("step='1'") != std::string::npos);
    }
    // Max sessions keeps its number input and 1..5 window.
    const std::string sessions = tagOf(html, "web_max_sessions");
    REQUIRE(!sessions.empty());
    CHECK(sessions.find("min='1'") != std::string::npos);
    CHECK(sessions.find("max='5'") != std::string::npos);
}
