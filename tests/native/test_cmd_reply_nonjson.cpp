// Command-reply handling contract (BACKLOG MON-4).
//
// Both command senders assumed a JSON reply:
//   /monitor        send()    : r.ok ? r.json() : null
//   /serial-monitor sendCmd() : r.json().catch(...)
// A 503 answered with a text/HTML error page therefore surfaced as a generic
// "network error"/"Failed" and the real status was lost.
//
// The fix moved the body reading into shared helpers in auto.js
// (window._readApiReply / window._apiReplyText) and both pages use them. This
// pins the wiring so neither the helper nor its use can be dropped silently;
// the runtime behaviour is covered by tests/web/test_serial_monitor.js and
// tests/web/test_monitor_send_reply.js.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string read(const std::string &rel) {
    std::string text, path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f, "cannot read " << path);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    return text;
}

} // namespace

TEST_CASE("auto.js exposes the shared reply reader") {
    const std::string js = read("webpages/auto_js.h");
    CHECK(js.find("function readApiReply(r)") != std::string::npos);
    CHECK(js.find("function apiReplyText(rep)") != std::string::npos);
    CHECK(js.find("window._readApiReply=readApiReply;") != std::string::npos);
    CHECK(js.find("window._apiReplyText=apiReplyText;") != std::string::npos);
    // Reads the body as text first, parses only if possible -> no throw on HTML.
    CHECK(js.find("typeof r.text==='function'") != std::string::npos);
    CHECK(js.find("var p=JSON.parse(txt);") != std::string::npos);
    // The summary strips tags (script/style included) and caps the excerpt.
    CHECK(js.find("replace(/<(script|style)[\\s\\S]*?<\\/\\1>/gi,' ')") != std::string::npos);
    CHECK(js.find("plain.length>120") != std::string::npos);
}

TEST_CASE("both command senders use the shared reader") {
    const std::string page = read("web_monitor_pages.cpp");

    // No `r.json()` left on the command paths (the old assumption).
    CHECK(page.find("return r.ok ? r.json() : Promise.resolve(null)") == std::string::npos);
    CHECK(page.find("r.json().catch(function(){return {success:false,response:'HTTP '+r.status};})") == std::string::npos);

    // /monitor send(): reads the reply, keeps the JSON happy path, falls back to
    // the HTTP status + excerpt only when there is no JSON.
    CHECK(page.find(".then(function(r){ return _readApiReply(r); })") != std::string::npos);
    CHECK(page.find("var why = j ? (j.response || j.message || j.error || _t('monFailed','Failed'))") != std::string::npos);
    CHECK(page.find(": _apiReplyText(rep);") != std::string::npos);

    // /serial-monitor sendCmd(): same reader, non-JSON lines are typed ERROR.
    CHECK(page.find("}).then(function(r){return _readApiReply(r);}).then(function(rep){var j=rep.json;") != std::string::npos);
    CHECK(page.find("type:j?'COMMAND_RESPONSE':'ERROR'") != std::string::npos);
    CHECK(page.find("resp=j?((j.response)?j.response:(j.success?'Sent':'Failed')):_apiReplyText(rep)") != std::string::npos);
}
