// Native tests for the sender-copy toggle save path (BACKLOG RECIP-3).
//
// saveDontCopy() POSTed /api/config and swallowed every failure with
// `.catch(function(){})`: a rejected save left the checkbox showing a state the
// device never stored, with no message. The page now parses the response
// (r.ok + success), restores the checkbox on failure and shows a customAlert;
// a successful save stays silent because the checkbox already reflects it.
//
// The page ships as PROGMEM strings, so the test compiles the asset and checks
// the function body contract; the behaviour itself is exercised by
// tests/web/test_recipients.js (real fetch stubs, visible failure / silent
// success).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/webpages/assets/page_recipients_asset.cpp"

#include <string>

namespace {

std::string js() { return std::string(PAGE_RECIPIENTS_JS); }

size_t countOf(const std::string &hay, const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

// Body of saveDontCopy(): from its declaration to the next function.
std::string body() {
    const std::string text = js();
    const size_t start = text.find("function saveDontCopy()");
    if (start == std::string::npos) return "";
    const size_t end = text.find("function preload()", start);
    if (end == std::string::npos) return "";
    return text.substr(start, end - start);
}

} // namespace

TEST_CASE("the save no longer swallows failures silently") {
    const std::string fn = body();
    REQUIRE(!fn.empty());
    // The old fire-and-forget shape is gone.
    CHECK(fn.find(".catch(function(){});") == std::string::npos);
    // The response is inspected instead of ignored.
    CHECK(fn.find("r.text()") != std::string::npos);
    CHECK(fn.find("o.ok") != std::string::npos);
    CHECK(fn.find("j.success===false") != std::string::npos);
    // And a rejected request still runs the failure path.
    CHECK(fn.find(".catch(function(){ fail(true); })") != std::string::npos);
}

TEST_CASE("a failed save is visible and reverts the checkbox") {
    const std::string fn = body();
    REQUIRE(!fn.empty());
    CHECK(fn.find("cb.checked=prev") != std::string::npos);
    // Exactly one alert, and it is the failure one.
    CHECK(countOf(fn, "customAlert(") == 1);
    CHECK(fn.find("recipDontCopySaveFail") != std::string::npos);
    CHECK(fn.find("Error saving the sender-copy option") != std::string::npos);
    // A network error adds its own reason instead of a bare failure.
    CHECK(fn.find("recipErrNetwork") != std::string::npos);
}

TEST_CASE("a successful save stays silent") {
    const std::string fn = body();
    REQUIRE(!fn.empty());
    // The failure branch is the only place that alerts; the success comment
    // documents the intentional silence.
    const size_t alert = fn.find("customAlert(");
    const size_t success = fn.find("Success stays silent");
    REQUIRE(alert != std::string::npos);
    REQUIRE(success != std::string::npos);
    CHECK(alert < success);                       // alert lives inside fail()
    // No success keys (saved/success) are raised from this toggle.
    CHECK(fn.find("recipSaved") == std::string::npos);
    CHECK(fn.find("showSavedCard") == std::string::npos);
}

TEST_CASE("the toggle still saves the same field and stays wired") {
    const std::string text = js();
    const std::string fn = body();
    REQUIRE(!fn.empty());
    CHECK(fn.find("'section','recipients'") != std::string::npos);
    CHECK(fn.find("'email_dont_copy_sender'") != std::string::npos);
    CHECK(fn.find("/api/config") != std::string::npos);
    // The checkbox change event keeps calling it.
    CHECK(text.find("cb.addEventListener('change',saveDontCopy)") != std::string::npos);
}
