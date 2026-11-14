// Native tests for "Save All" (BACKLOG RECIP-4).
//
// The old handler looped rows 1..N calling saveSlot() every 120 ms: each row
// raised its own alert (10+ popups), and a mid-loop failure left the table
// partially saved with the error buried among them.
//
// The new saveAll() posts one row at a time through postSlot() (no per-row UI),
// stops at the first failure and reports it once: the failing row, the reason
// and the partial-state note. A fully successful run is silent.
//
// The page ships as PROGMEM strings, so this checks the function contract; the
// runtime behaviour is exercised by tests/web/test_recipients.js.

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

std::string functionBody(const char *name, const char *nextName) {
    const std::string text = js();
    const size_t start = text.find(name);
    if (start == std::string::npos) return "";
    const size_t end = text.find(nextName, start);
    if (end == std::string::npos) return "";
    return text.substr(start, end - start);
}

std::string saveAllBody() { return functionBody("function saveAll()", "var saveAllBusy=false;"); }
std::string attachBody() { return functionBody("function attachSaveAll()", "function saveDontCopy()"); }

} // namespace

TEST_CASE("Save All uses one request at a time without per-row alerts") {
    const std::string attach = attachBody();
    const std::string all = saveAllBody();
    REQUIRE(!attach.empty());
    REQUIRE(!all.empty());
    // The click runs the aggregate path, not a saveSlot loop.
    CHECK(attach.find("saveAllBtn.addEventListener('click',saveAll)") != std::string::npos);
    CHECK(attach.find("saveSlot(") == std::string::npos);
    // And the aggregate path itself never calls saveSlot (the alert-per-row bug).
    CHECK(all.find("saveSlot(") == std::string::npos);
    CHECK(all.find("postSlot(") != std::string::npos);
    // Sequential: the next row waits 120 ms for the previous one.
    CHECK(all.find("setTimeout(next,120)") != std::string::npos);
}

TEST_CASE("a mid-loop failure stops the run and reports it once") {
    const std::string all = saveAllBody();
    REQUIRE(!all.empty());
    CHECK(all.find("if(!res.ok&&!res.skipped){ failedAt=at; failNet=!!res.net; finish(); return; }") != std::string::npos);
    CHECK(countOf(all, "customAlert(") == 1);
    CHECK(all.find("recipSaveAllFail") != std::string::npos);
    CHECK(all.find("recipSaveAllPartial") != std::string::npos);
    // The alert names the row that failed and the total.
    CHECK(all.find("failedAt+'/'+total") != std::string::npos);
    // Network vs server rejection are distinguishable.
    CHECK(all.find("failNet?rt('recipErrNetwork'") != std::string::npos);
}

TEST_CASE("a successful run stays silent and the button is released") {
    const std::string all = saveAllBody();
    REQUIRE(!all.empty());
    const size_t silent = all.find("if(!failedAt) return;");
    const size_t alert = all.find("customAlert(");
    REQUIRE(silent != std::string::npos);
    REQUIRE(alert != std::string::npos);
    CHECK(silent < alert);                     // success returns before any alert
    CHECK(all.find("showSavedCard") == std::string::npos);
    // The button cannot be clicked twice and is always re-enabled.
    CHECK(all.find("disableBtn(saveAllBtn)") != std::string::npos);
    CHECK(all.find("enableBtn(saveAllBtn)") != std::string::npos);
    CHECK(all.find("saveAllBusy=true") != std::string::npos);
    CHECK(all.find("saveAllBusy=false") != std::string::npos);
}
