// Native tests for the /sdcard page body (chatgpt/src/webpages/renderers/sdcard_page_body.h).
//
// SD-2: PAGE_SDCARD_PART2 carried a second, dead initSdcardPage(); auto.js loads
// after the inline script and defines the same global, and the renderer only
// calls it after the auto.js footer — so the inline copy never ran and it used
// helpers that do not exist in that scope (bindClick/apiGet/apiPost), posting
// mount/unmount to the wrong endpoint.
//
// SD-3: SDCARD_COMMANDS_SECTION (EMAILDAY/LOGDAY/SENSORDAY help) was defined in
// the asset and never printed.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>

#include "webpages/renderers/sdcard_page_body.h"

#include <string>

namespace {

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

std::string body() {
    StringPrint p;
    streamSdcardPageBody(p);
    return p.str();
}

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

} // namespace

TEST_CASE("SD-3: the commands card is rendered, once, before the Save button") {
    const std::string html = body();
    CHECK(html.find("data-i18n='sdCommandsTitle'") != std::string::npos);
    CHECK(html.find("EMAILDAY") != std::string::npos);
    CHECK(html.find("LOGDAY") != std::string::npos);
    CHECK(html.find("SENSORDAY") != std::string::npos);
    // the examples the card documents
    CHECK(html.find("emailday 29-12-2025") != std::string::npos);
    CHECK(html.find("logday 28-02-2026") != std::string::npos);
    CHECK(html.find("sensorday 01-03-2026") != std::string::npos);

    CHECK(countOf(html, "data-i18n='sdCommandsTitle'") == 1);   // exactly one card

    // order: settings -> commands -> save
    const size_t settings = html.find("data-i18n='sdSavingTitle'");
    const size_t commands = html.find("data-i18n='sdCommandsTitle'");
    const size_t save = html.find("id='sd_save'");
    CHECK(settings != std::string::npos);
    CHECK(commands != std::string::npos);
    CHECK(save != std::string::npos);
    CHECK(settings < commands);
    CHECK(commands < save);
}

TEST_CASE("SD-2: the dead inline init is gone and no script ships in the body") {
    const std::string html = body();
    CHECK(html.find("function initSdcardPage") == std::string::npos);
    CHECK(html.find("function t(k,fb)") == std::string::npos);
    CHECK(html.find("bindClick(") == std::string::npos);
    CHECK(html.find("apiGet(") == std::string::npos);
    CHECK(html.find("apiPost(") == std::string::npos);
    CHECK(html.find("jget(") == std::string::npos);
    CHECK(html.find("jpost(") == std::string::npos);
    // no inline script at all: the initialiser is the renderer's footer call
    CHECK(html.find("<script") == std::string::npos);
    CHECK(html.find("</script>") == std::string::npos);
}

TEST_CASE("the markup auto.js's initSdcardPage() needs is intact") {
    const std::string html = body();
    const char* ids[] = {
        "sd_info", "sd_mount", "sd_unmount", "sd_refresh", "sd_save", "sd_config_msg",
        "sd_write_pause_ms", "sd_active", "sd_format", "sd_email",
        "sd_cleanup_threshold", "sd_keep_days",
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        CHECK(html.find(std::string("id='") + ids[i] + "'") != std::string::npos);
    }
    // the selects keep the values the page maps to API fields
    CHECK(html.find("<option value='1800000' selected>30m</option>") != std::string::npos);
    CHECK(html.find("<option value='0' selected>&#8734;</option>") != std::string::npos);
}

TEST_CASE("the body is well formed at the seams") {
    const std::string html = body();
    // copied prefix/suffix keep the wrapper open across the three assets
    CHECK(html.find("<div class='page sdcard'>") != std::string::npos);
    CHECK(html.find("</div>") != std::string::npos);
    // the commands card is a complete block (opens and closes with the section div)
    const size_t commands = html.find("data-i18n='sdCommandsTitle'");
    const size_t save = html.find("id='sd_save'");
    CHECK(html.find("<!-- 📦 Section 3: EMAILDAY Commands -->") != std::string::npos);
    CHECK(commands < save);
    // nothing from the mobile wizard or another page leaked in
    CHECK(html.find("id='gm'") == std::string::npos);
}
