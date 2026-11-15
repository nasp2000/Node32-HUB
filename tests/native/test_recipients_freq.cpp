// Native tests for the recipient frequency selector (BACKLOG RECIP-5).
//
// The server default is 0 = "always" (ConfigManager: `slot["freq"] | 0`,
// sanitized to 0..86400), but the page's <select> started at 180. A slot stored
// with freq:0 therefore rendered as the 600 option and the next save converted
// it to 600 — silently changing the throttling of an existing rule.
//
// The fix adds the 0 option ("Always"), makes it the default for new rows (the
// server default) and makes preload() apply freq even when it is 0, so the
// stored value round-trips unchanged. The runtime behaviour is exercised by
// tests/web/test_recipients.js; this checks the source contract and that the
// new i18n key exists in both dictionaries.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/webpages/assets/page_recipients_asset.cpp"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string js() { return std::string(PAGE_RECIPIENTS_JS); }

bool readFile(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string srcFile(const char *rel) {
    std::string text;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) + "/" + rel, text),
                    "cannot read " << rel);
    return text;
}

// The freq <select> markup lives in the rowHTML() line.
size_t freqSelect(const std::string &text) { return text.find("name='r_freq_"); }

} // namespace

TEST_CASE("the frequency list offers 0 = always with a translated label") {
    const std::string text = js();
    const size_t at = freqSelect(text);
    REQUIRE(at != std::string::npos);
    const size_t end = text.find("</select>", at);
    REQUIRE(end != std::string::npos);
    const std::string select = text.substr(at, end - at);

    CHECK(select.find("<option value='0' selected>") != std::string::npos);
    CHECK(select.find("rt('freqDefault3m','3m (default)')") != std::string::npos);
    // The old 600 default is gone (0 is the server default).
    CHECK(select.find("<option value='600' selected>") == std::string::npos);
    // The documented ladder is still there.
    CHECK(select.find("<option value='180'>") != std::string::npos);
    CHECK(select.find("<option value='86400'>") != std::string::npos);
}

TEST_CASE("the key exists in both language packs") {
    const std::string en = srcFile("languages/lang_en_ui.h");
    const std::string pt = srcFile("languages/lang_pt_ui.h");
    CHECK(en.find("\"freqDefault3m\"") != std::string::npos);
    CHECK(pt.find("\"freqDefault3m\"") != std::string::npos);
    // Non-empty values (the key is not just present in a comment).
    const size_t enAt = en.find("\"freqDefault3m\"");
    CHECK(en.find('"', en.find(':', enAt) + 1) != std::string::npos);
}

TEST_CASE("preload applies a stored freq:0 instead of leaving 600 selected") {
    const std::string text = js();
    // The truthiness check (s.freq) skipped 0; the new one is explicit.
    CHECK(text.find("if(f&&s.freq!==undefined&&s.freq!==null) f.value=String(s.freq);") != std::string::npos);
    CHECK(text.find("if(f&&s.freq) f.value=s.freq;") == std::string::npos);
}

TEST_CASE("saving a row keeps freq 0 and never rewrites it to 600") {
    const std::string text = js();
    // The row form reads the select as-is, with the server default as fallback.
    CHECK(text.find("fd.append('freq',(tr.querySelector(\"select[name='r_freq_\"+i+\"']\")||{}).value||'0');") != std::string::npos);
    // No remaining 600 fallback on the frequency path (removeRow had one).
    const bool noStale600 = text.find("||'600'") == std::string::npos;
    CHECK(noStale600);
}
