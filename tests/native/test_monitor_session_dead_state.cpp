// Native test for the removed monitor session state (BACKLOG MON-2).
//
// Investigation result: monitorOpenClients was only incremented/decremented and
// never read; autoEmailMonitorOverrideActive was written as false on both open
// and close, and its only reader (manage_email_impl.h) explicitly ignored it.
// The open/close endpoints therefore acknowledged nothing that gated a path, so
// the counters, the flag and the helper module were removed and the handlers
// now return a plain acknowledgement.
//
// This test pins the removal and the remaining contract:
//   - the dead module is gone and no TU includes it;
//   - the handlers answer {"status":"ok"} and carry no session/override state;
//   - the monitor page still announces open/close but never reads `override`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

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

std::string src(const char *rel) {
    std::string text;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) + "/" + rel, text),
                    "cannot read " << rel);
    return text;
}

} // namespace

TEST_CASE("the dead session-state module is gone") {
    std::string ignored;
    CHECK_FALSE(readFile(std::string(SRC_DIR_PATH) + "/monitor_session_state.h", ignored));
    CHECK_FALSE(readFile(std::string(SRC_DIR_PATH) + "/monitor_session_state.cpp", ignored));
    // No translation unit references its include any more.
    const std::string eth = src("ethernet.cpp");
    CHECK(eth.find("monitor_session_state.h") == std::string::npos);
}

TEST_CASE("the monitor handlers only acknowledge the open/close") {
    const std::string h = src("webpages/handlers_unified/monitor_handler.cpp");
    // Both endpoints still exist...
    CHECK(h.find("void handleMonitorOpenPost(") != std::string::npos);
    CHECK(h.find("void handleMonitorClosePost(") != std::string::npos);
    // ...and answer with a plain ack.
    CHECK(h.find("{\\\"status\\\":\\\"ok\\\"}") != std::string::npos);
    // No counter, no flag, no override field.
    CHECK(h.find("monitorOpenIncrement") == std::string::npos);
    CHECK(h.find("monitorOpenDecrement") == std::string::npos);
    CHECK(h.find("monitorOpenClients") == std::string::npos);
    CHECK(h.find("autoEmailMonitorOverrideActive") == std::string::npos);
    CHECK(h.find("\"override\"") == std::string::npos);   // no response field
}

TEST_CASE("the monitor page announces open/close but never reads an override") {
    const std::string page = src("web_monitor_pages.cpp");
    CHECK(page.find("/api/monitor-open") != std::string::npos);
    CHECK(page.find("/api/monitor-close") != std::string::npos);
    // The old response field was never consumed by the page; nothing replaced it.
    CHECK(page.find("override") == std::string::npos);
}
