// Native tests for "Save All" on both scheduled-command pages (BACKLOG CMD-4).
//
// Both pages looped rows 1..N calling saveSlot()/ss() every 160/180 ms: every
// row raised its own alert and a mid-loop failure left the table partially
// saved with the error buried among them (same defect the recipients page had,
// RECIP-4).
//
// The new aggregate path posts one row at a time through cmdPostSlot() /
// sdtPostSlot() (no per-row UI), stops at the first failure and reports it
// once: the failing row, the reason and the partial-state note. A fully
// successful run is silent.
//
// The pages ship as PROGMEM strings, so this checks the function contract; the
// runtime behaviour is exercised by tests/web/test_commands.js and
// tests/web/test_commands_datetime.js.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/webpages/assets/page_commands_asset.cpp"
#include "../../chatgpt/src/webpages/assets/page_commands_datetime_asset.cpp"

#include <string>

namespace {

size_t countOf(const std::string &hay, const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

std::string slice(const std::string &text, const char *from, const char *to) {
    const size_t start = text.find(from);
    if (start == std::string::npos) return "";
    const size_t end = text.find(to, start);
    if (end == std::string::npos) return "";
    return text.substr(start, end - start);
}

std::string intervalJs() { return std::string(PAGE_COMMANDS_JS); }
std::string datetimeJs() { return std::string(PAGE_COMMANDS_DATETIME_JS); }

std::string intervalSaveAll() { return slice(intervalJs(), "function cmdSaveAll(){", "function attachSaveAll(){"); }
std::string intervalAttach() { return slice(intervalJs(), "function attachSaveAll(){", "init();"); }
std::string datetimeSaveAll() { return slice(datetimeJs(), "function sdtSaveAll(){", "function attachSaveAll(){"); }
std::string datetimeAttach() { return slice(datetimeJs(), "function attachSaveAll(){", "init();"); }

// Shared contract for one page's aggregate Save All.
void checkAggregate(const std::string &all, const std::string &attach, const char *postFn,
                    const char *handler, const char *busyFlag, const char *button, const char *delay) {
    REQUIRE_MESSAGE(!all.empty(), "Save All body not found");
    REQUIRE_MESSAGE(!attach.empty(), "attachSaveAll body not found");

    // The click runs the aggregate path, never a per-row save loop.
    CHECK(attach.find("addEventListener('click'") != std::string::npos);
    CHECK(countOf(attach, handler) == 1);  // the click runs the aggregate path, nothing else
    CHECK(attach.find("saveSlot(") == std::string::npos);
    CHECK(attach.find("ss(i,") == std::string::npos);
    CHECK(all.find("saveSlot(") == std::string::npos);
    CHECK(all.find(postFn) != std::string::npos);

    // Sequential, one request at a time.
    CHECK(all.find(std::string("setTimeout(next,") + delay) != std::string::npos);

    // Stops at the first failure and reports it exactly once.
    CHECK(all.find("if(!res.ok&&!res.skipped){ failedAt=at; failNet=!!res.net; finish(); return; }") != std::string::npos);
    CHECK(countOf(all, "customAlert(") == 1);
    CHECK(all.find("cmdsSaveAllFail") != std::string::npos);
    CHECK(all.find("cmdsSaveAllPartial") != std::string::npos);
    CHECK(all.find("failedAt+'/'+total") != std::string::npos);
    CHECK(all.find("failNet?t('cmdsErrNetwork'") != std::string::npos);

    // Success is silent: the early return happens before any alert.
    const size_t silent = all.find("if(!failedAt) return;");
    const size_t alert = all.find("customAlert(");
    REQUIRE(silent != std::string::npos);
    REQUIRE(alert != std::string::npos);
    CHECK(silent < alert);
    CHECK(all.find("showSavedCard") == std::string::npos);

    // Re-entrancy guard and the button is always released.
    CHECK(all.find(std::string("disableBtn(") + button + ")") != std::string::npos);
    CHECK(all.find(std::string("enableBtn(") + button + ")") != std::string::npos);
    CHECK(all.find(std::string(busyFlag) + "=true") != std::string::npos);
    CHECK(all.find(std::string(busyFlag) + "=false") != std::string::npos);
}

} // namespace

TEST_CASE("interval page Save All is aggregate, sequential and stops at the first failure") {
    checkAggregate(intervalSaveAll(), intervalAttach(), "cmdPostSlot(", "cmdSaveAll", "cmdSaveAllBusy", "saveAllBtn", "160");
}

TEST_CASE("date/time page Save All is aggregate, sequential and stops at the first failure") {
    checkAggregate(datetimeSaveAll(), datetimeAttach(), "sdtPostSlot(", "sdtSaveAll", "sdtSaveAllBusy", "sv", "180");
}

TEST_CASE("a per-row save still reports its own result") {
    // Individual rows keep their immediate feedback (success, server error,
    // network error) — only the bulk action became aggregate.
    const std::string interval = slice(intervalJs(), "function saveSlot(i,btn){", "// Save All:");
    REQUIRE(!interval.empty());
    CHECK(interval.find("cmdsSaved") != std::string::npos);
    CHECK(interval.find("cmdsError") != std::string::npos);
    CHECK(interval.find("cmdsNetError") != std::string::npos);

    const std::string dt = slice(datetimeJs(), "function ss(i,btn){", "// Save All:");
    REQUIRE(!dt.empty());
    CHECK(dt.find("cmdsSaved") != std::string::npos);
    CHECK(dt.find("cmdsError") != std::string::npos);
    CHECK(dt.find("cmdsNetError") != std::string::npos);
}

TEST_CASE("interval page: three default rows, the button grows, saved slots are shown") {
    const std::string js = intervalJs();
    // Three empty rows like /commands-by-datetime and /recipients.
    const bool three = js.find("for(var i=0;i<3;i++) addRow();") != std::string::npos;
    const bool ten   = js.find("for(var i=0;i<10;i++) addRow();") != std::string::npos;
    CHECK(three);
    CHECK_FALSE(ten);
    // The Add Command button exists and respects MAX.
    const bool addBtn = js.find("getElementById('add_cmd')") != std::string::npos;
    const bool maxMsg = js.find("cmdsMaxRows") != std::string::npos;
    CHECK(addBtn);
    CHECK(maxMsg);
    // A saved command in a slot above the default rows still renders.
    const std::string app = slice(js, "function applySlot(j){", "function loadAll(){");
    REQUIRE_MESSAGE(!app.empty(), "applySlot body not found");
    const bool grows = app.find("while(currentCount < i)") != std::string::npos;
    CHECK(grows);
}

TEST_CASE("interval page: the information box spans the column and is left-aligned") {
    const std::string html = std::string(PAGE_COMMANDS_JS) + std::string(PAGE_COMMANDS_HTML);
    const bool wide  = html.find(".cmd-info{width:100%!important") != std::string::npos;
    const bool left  = html.find(".cmd-info,.cmd-info *{text-align:left!important}") != std::string::npos;
    CHECK(wide);
    CHECK(left);
}
