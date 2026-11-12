// Native tests for the alerts slot markup (chatgpt/src/alerts_max_per_day_options.h).
//
// ALERTS-4/5 live in the HTML: a stored max/day outside the option list must be
// appended and selected (otherwise the page shows a neighbour and the next save
// overwrites it), and each inactivity slot needs its live-state placeholder.
// These cases render into a String-backed Print and assert the shipped markup.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>

#include "alerts_max_per_day_options.h"

#include <string>

namespace {

// Print sink that keeps everything, so we can assert on the exact HTML.
class StringPrint : public Print {
public:
    String out;
    size_t write(uint8_t c) override { out += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t n) override {
        for (size_t i = 0; i < n; i++) out += (char)buf[i];
        return n;
    }
    std::string str() const { return std::string(out.c_str()); }
};

std::string render(int selected) {
    StringPrint p;
    alertsMaxPerDayOptions(p, selected);
    return p.str();
}

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

} // namespace

TEST_CASE("option list covers the documented range") {
    const std::string html = render(12);
    CHECK(countOf(html, "<option value='") == (size_t)alertsMaxPerDayOptionCount());
    CHECK(html.find("<option value='100'") != std::string::npos);
    CHECK(html.find("<option value='75'") != std::string::npos);
    CHECK(html.find("<option value='90'") != std::string::npos);
    CHECK(html.find("alerts/day</option>") != std::string::npos);
    // exactly one selected option, and it is the stored value
    CHECK(countOf(html, "selected") == 1);
    CHECK(html.find("<option value='12' selected>12 alerts/day</option>") != std::string::npos);
}

TEST_CASE("a value outside the list is appended, in order, and selected") {
    // The defect: 80 (API-set) had no option, the UI showed 60 and saving
    // overwrote 80 with 60.
    const std::string html = render(80);
    CHECK(html.find("<option value='60' selected>") == std::string::npos);
    CHECK(html.find("<option value='75' selected>") == std::string::npos);
    CHECK(html.find("<option value='90' selected>") == std::string::npos);
    CHECK(html.find("<option value='80' selected>80 alerts/day</option>") != std::string::npos);
    CHECK(countOf(html, "selected") == 1);
    // ascending: 75 < 80 < 90
    const size_t p75 = html.find("<option value='75'");
    const size_t p80 = html.find("<option value='80'");
    const size_t p90 = html.find("<option value='90'");
    CHECK(p75 != std::string::npos);
    CHECK(p80 != std::string::npos);
    CHECK(p90 != std::string::npos);
    CHECK(p75 < p80);
    CHECK(p80 < p90);
}

TEST_CASE("values already in the list are not duplicated") {
    for (int v : {3, 60, 100}) {
        const std::string html = render(v);
        const std::string needle = "<option value='" + std::to_string(v) + "'";
        CHECK(countOf(html, needle) == 1);
    }
}

TEST_CASE("live-state placeholder carries the slot id") {
    StringPrint p;
    alertsLiveStateDiv(p, 3);
    const std::string html = p.str();
    CHECK(html.find("<div id='alert_live_3'") != std::string::npos);
    CHECK(html.find(">--</div>") != std::string::npos);
    // one id per slot: 0..5 must all differ
    StringPrint q;
    alertsLiveStateDiv(q, 0);
    CHECK(q.str().find("alert_live_0") != std::string::npos);
    CHECK(q.str().find("alert_live_1") == std::string::npos);
}

TEST_CASE("out-of-range stored values add no option") {
    // Legacy junk must not become a selectable option; the handler rejects it.
    CHECK(render(0).find("<option value='0'") == std::string::npos);
    CHECK(render(101).find("<option value='101'") == std::string::npos);
    CHECK(render(-5).find("<option value='-5'") == std::string::npos);
}
