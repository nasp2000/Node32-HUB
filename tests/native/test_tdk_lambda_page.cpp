// Native tests for the /tdk-lambda page asset (markup + wiring contract).
//
// The page is generated/edited as one PROGMEM blob, so a broken wrapper, a
// button without a tooltip or a JS id that does not exist in the markup only
// shows up in the browser. These checks run on the host with the asset header
// included directly (same approach as test_access_page_markup.cpp).
//
// Rules covered (AGENTS.md):
//  - every page asset opens the shared .page/.wrap shell (core.css turns <body>
//    into a flex row from 768 px up, so a missing wrapper squeezes the page);
//  - buttons use the core .btn class, no native alert(), d.success not d.ok;
//  - every control carries a tooltip (title + data-i18n-title).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>

#include "webpages/assets/page_tdk_lambda_asset.h"

#include <set>
#include <string>
#include <vector>

namespace {

std::string page() { return std::string(PAGE_TDK_LAMBDA); }

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

// Lines that contain `needle`.
std::vector<std::string> linesWith(const std::string& hay, const std::string& needle) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= hay.size()) {
        const size_t eol = hay.find('\n', pos);
        const std::string line = hay.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (line.find(needle) != std::string::npos) out.push_back(line);
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return out;
}

} // namespace

TEST_CASE("TDK page: shared page/wrap shell with balanced divs") {
    const std::string html = page();
    CHECK(html.find("<div class='page tdk-lambda'>") != std::string::npos);
    CHECK(html.find("<div class='wrap'>") != std::string::npos);
    CHECK(countOf(html, "<div") == countOf(html, "</div>"));
    // Home button must be the first child of .wrap (repo rule).
    const size_t wrap = html.find("<div class='wrap'>");
    const size_t firstChild = html.find("<div class=", wrap + 1);
    REQUIRE(firstChild != std::string::npos);
    CHECK(html.substr(firstChild, 32).find("'actions nav-inicio'") != std::string::npos);
    // Sections are .section-box + .section-title (IPG/Rambox construction).
    CHECK(countOf(html, "class='section-box'") == 7);
    CHECK(countOf(html, "class='section-title'") == 7);
    CHECK(countOf(html, "class='tdk-subtitle'") == 1);   // inside a section, not a section
}

TEST_CASE("TDK page: five tabs, one default, keys match the JS table") {
    const std::string html = page();
    CHECK(countOf(html, "id='tdkTabBar'") == 1);
    CHECK(countOf(html, "class='tdk-panel' data-tab=") == 5);
    CHECK(countOf(html, "data-tab-default='1'") == 1);

    const std::vector<std::string> keys = {"live", "console", "csv", "profile", "commands"};
    for (const std::string& k : keys) {
        INFO("tab " << k);
        CHECK(countOf(html, "data-tab='" + k + "'") == 1);       // one panel
        CHECK(countOf(html, "key:'" + k + "'") == 1);            // one TDK_TABS entry
        CHECK(countOf(html, "tipKey:'") >= 5);                   // tooltip per tab
    }
    // The catalogue "use" button loads the command and jumps to the Console.
    CHECK(html.find("function tdkShowTab(key)") != std::string::npos);
    CHECK(html.find("tdkShowTab('console')") != std::string::npos);
}

TEST_CASE("TDK page: every control has a tooltip") {
    const std::string html = page();
    // Buttons: the tooltip must be on the button itself.
    for (const std::string& line : linesWith(html, "<button")) {
        INFO(line);
        CHECK(line.find("title=") != std::string::npos);
        CHECK(line.find("data-i18n-title=") != std::string::npos);
        const bool coreBtn = (line.find("class='btn'") != std::string::npos ||
                              line.find("class=\"btn\"") != std::string::npos);
        CHECK(coreBtn);
    }
    // Inputs/selects: tooltip on the control or on the wrapping <label> (same line).
    for (const std::string& line : linesWith(html, "<input")) {
        INFO(line);
        CHECK(line.find("title=") != std::string::npos);
    }
    for (const std::string& line : linesWith(html, "<select")) {
        INFO(line);
        CHECK(line.find("title=") != std::string::npos);
    }
    // Value tiles and table headers explain themselves on hover. The last-line
    // tile carries an extra class ("tdk-val wide"), hence the prefix match.
    INFO("value tiles");
    CHECK(countOf(html, "<div class='tdk-val") == 12);   // 8 live + 4 CSV
    for (const std::string& line : linesWith(html, "<div class='tdk-val")) {
        INFO(line);
        CHECK(line.find("title=") != std::string::npos);
    }
    CHECK(countOf(html, "<th title=") == 10);   // catalogue 7 + CSV preview 3
    // No leftover custom button class: the core .btn look is the standard.
    CHECK(countOf(html, "tdk-btn") == 0);
}

TEST_CASE("TDK page: JS ids all exist in the markup") {
    const std::string html = page();
    std::set<std::string> ids;
    const std::string tag = "_tdkQ('";
    size_t pos = 0;
    while ((pos = html.find(tag, pos)) != std::string::npos) {
        const size_t start = pos + tag.size();
        const size_t end = html.find('\'', start);
        REQUIRE(end != std::string::npos);
        ids.insert(html.substr(start, end - start));
        pos = end;
    }
    CHECK(ids.size() > 30);                       // the scan really found the calls
    for (const std::string& id : ids) {
        INFO("id " << id);
        CHECK(html.find("id='" + id + "'") != std::string::npos);
    }
}

TEST_CASE("TDK page: no native alert, API answers read d.success") {
    const std::string html = page();
    // Custom dialogs only (auto.js customAlert/customConfirm). Note the
    // capital A: the native function is lowercase `alert(`, so this counts
    // exactly the forbidden ones.
    CHECK(countOf(html, "alert(") == 0);
    CHECK(countOf(html, "customAlert(") >= 2);
    CHECK(countOf(html, "_tdkFail(") >= 1);
    CHECK(html.find("d.success") != std::string::npos);
    CHECK(html.find("d.ok") == std::string::npos);
    // No arrow functions (old ESP32 browser builds).
    CHECK(html.find("=>") == std::string::npos);
}
