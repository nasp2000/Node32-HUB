// Native source-contract test for the /serial-monitor render + poll rework
// (BACKLOG MON-3).
//
// The page rebuilt all (up to 500) rows every 3 s, even with an idle log, and
// kept the fixed cadence. The new contract:
//   - the poll is self-scheduled (setTimeout) through nextPollDelay(): 3 s while
//     data advances, doubling to a 15 s cap while `total` (buffer_seq) stalls;
//   - a stalled poll does not touch the DOM at all;
//   - growth appends only the new rows, in one DocumentFragment, and drops the
//     rows the 500-message cap removed;
//   - a buffer reset or an active filter falls back to the full render.
//
// The runtime behaviour is exercised by tests/web/test_serial_monitor_render.js;
// this pins the source wiring so neither half can be removed silently.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string src() {
    std::string text, path = std::string(SRC_DIR_PATH) + "/web_monitor_pages.cpp";
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f, "cannot read " << path);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    return text;
}

size_t countOf(const std::string &hay, const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

} // namespace

TEST_CASE("the fixed 3 s interval is gone; the poll is self-scheduled") {
    const std::string page = src();
    CHECK(page.find("setInterval(load,3000)") == std::string::npos);
    CHECK(page.find("arInterval=setInterval") == std::string::npos);
    CHECK(page.find("function schedulePoll(") != std::string::npos);
    CHECK(page.find("_pollTimer=setTimeout(function(){_pollTimer=0;load();}") != std::string::npos);
    CHECK(page.find("function stopPoll(") != std::string::npos);
}

TEST_CASE("the poll delay backs off while the buffer does not advance") {
    const std::string page = src();
    CHECK(page.find("POLL_BASE_MS=3000") != std::string::npos);
    CHECK(page.find("POLL_MAX_MS=15000") != std::string::npos);
    CHECK(page.find("function nextPollDelay(cur,grew){if(grew)return POLL_BASE_MS;") != std::string::npos);
    CHECK(page.find("return n>POLL_MAX_MS?POLL_MAX_MS:n;") != std::string::npos);
    // Applied after every load, with the growth flag.
    CHECK(page.find("_pollDelay=nextPollDelay(_pollDelay,grew);") != std::string::npos);
}

TEST_CASE("growth appends the delta in one fragment and trims the cap") {
    const std::string page = src();
    CHECK(page.find("function appendNewRows(") != std::string::npos);
    // One fragment insertion instead of a full innerHTML rebuild.
    CHECK(page.find("document.createDocumentFragment();") != std::string::npos);
    CHECK(page.find("buf.appendChild(frag);") != std::string::npos);
    // Drop exactly the rows the 500-message slice removed.
    CHECK(page.find("buf.removeChild(buf.firstChild);") != std::string::npos);
    // The load path only appends when messages arrived...
    CHECK(page.find("}else if(nw.length){") != std::string::npos);
    CHECK(page.find("appendNewRows(nw.length,(prevLen+nw.length)-allMsgs.length);") != std::string::npos);
}

TEST_CASE("a reset or an active filter still takes the full-render path") {
    const std::string page = src();
    CHECK(page.find("if(resetCase||hasActiveFilter()){") != std::string::npos);
    CHECK(page.find("function hasActiveFilter(") != std::string::npos);
    // A stalled poll performs no render at all (only growth/reset trigger one).
    CHECK(countOf(page, "if(resetCase||nw.length)renderMsgs();") == 1);
}
