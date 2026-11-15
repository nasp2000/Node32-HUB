// Native tests for the date/time scheduled-command slot capacity (BACKLOG CMD-2).
//
// `/api/scheduled-datetime-slots` used to serialise only 8 slots while the
// per-slot GET/POST accepted 1..16, so slots 9..16 existed in storage, fired in
// the engine and could be saved individually, yet were invisible in the bulk
// listing. The fix makes SCHED_DT_SLOT_MAX (sched_dt_limits.h) the single
// source of truth: storage bounds, both bulk endpoints (WiFi + Ethernet), the
// status payload, the execution loop, the day-report journal and the page row
// cap all derive from it.
//
// These tests pin that contract: the constant, every boundary that must use it,
// and the page cap. Runtime behaviour of the page is covered by
// tests/web/test_commands_datetime.js.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Arduino.h"

#include "../../chatgpt/src/config_utils.h"   // clampSchedDtActive
#include "../../chatgpt/src/sched_dt_limits.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

// Compile-time: the documented capacity of the /commands-datetime page.
static_assert(SCHED_DT_SLOT_MAX == 16, "sched_dt capacity must match the page row cap");

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

std::string srcFile(const char *rel) {
    std::string text;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) + "/" + rel, text),
                    "cannot read " << rel);
    return text;
}

// Body of a function/loop: from `marker` up to `len` bytes. Bounded on purpose
// so a missing marker fails loudly instead of matching a neighbouring function.
std::string around(const std::string &text, const char *marker, size_t len = 1400) {
    size_t pos = text.find(marker);
    REQUIRE_MESSAGE(pos != std::string::npos, "marker not found: " << marker);
    return text.substr(pos, len);
}

// Parses `static const int kMaxEntries = N;`
int intAfter(const std::string &text, const char *marker) {
    size_t pos = text.find(marker);
    REQUIRE_MESSAGE(pos != std::string::npos, "marker not found: " << marker);
    return atoi(text.c_str() + pos + strlen(marker));
}

} // namespace

TEST_CASE("sched_dt capacity is one constant shared by the whole stack") {
    CHECK(SCHED_DT_SLOT_MAX == 16);
}

TEST_CASE("clampSchedDtActive keeps 1..SCHED_DT_SLOT_MAX") {
    CHECK(clampSchedDtActive(0) == 1);
    CHECK(clampSchedDtActive(1) == 1);
    CHECK(clampSchedDtActive(8) == 8);           // old cap is no longer a ceiling
    CHECK(clampSchedDtActive(15) == 15);
    CHECK(clampSchedDtActive(SCHED_DT_SLOT_MAX) == SCHED_DT_SLOT_MAX);
    CHECK(clampSchedDtActive(SCHED_DT_SLOT_MAX + 1) == SCHED_DT_SLOT_MAX);  // rejected
    CHECK(clampSchedDtActive(100) == SCHED_DT_SLOT_MAX);
    CHECK(clampSchedDtActive(-5) == 1);
}

TEST_CASE("bulk listing serialises every slot on WiFi and Ethernet") {
    std::string wifi = around(srcFile("webpages/handlers_unified/commands_handler.cpp"),
                              "void buildScheduledDatetimeSlotsJson");
    CHECK(wifi.find("i <= SCHED_DT_SLOT_MAX") != std::string::npos);
    CHECK(wifi.find("i <= 8") == std::string::npos);
    CHECK(wifi.find("i <= 16") == std::string::npos);  // no raw literal either

    std::string eth = around(srcFile("ethernet_bootstrap_handlers.h"),
                             "ethernetSendScheduledDatetimeSlots");
    CHECK(eth.find("i <= SCHED_DT_SLOT_MAX") != std::string::npos);
    CHECK(eth.find("i <= 8") == std::string::npos);
    CHECK(eth.find("i <= 16") == std::string::npos);
}

TEST_CASE("per-slot GET/POST accept exactly 1..SCHED_DT_SLOT_MAX") {
    std::string wifi = srcFile("webpages/handlers_unified/commands_handler.cpp");
    CHECK(wifi.find("slotIndex > SCHED_DT_SLOT_MAX") != std::string::npos);
    CHECK(wifi.find("slotIndex > 16") == std::string::npos);

    std::string eth = srcFile("ethernet_bootstrap_handlers.h");
    // Two sched_dt guards (single GET + POST); other 16 literals are unrelated.
    size_t n = 0, pos = 0;
    while ((pos = eth.find("i > SCHED_DT_SLOT_MAX", pos)) != std::string::npos) { n++; pos++; }
    CHECK(n == 2);
    CHECK(eth.find("i > 16) {") == std::string::npos);
}

TEST_CASE("status payload and engine iterate every slot") {
    std::string status = around(srcFile("system_status_payload.h"), "sched_dt_active", 900);
    CHECK(status.find("i <= SCHED_DT_SLOT_MAX") != std::string::npos);
    CHECK(status.find("i <= 8") == std::string::npos);

    std::string sched = srcFile("runtime_scheduler.cpp");
    CHECK(sched.find("for (int i = 1; i <= SCHED_DT_SLOT_MAX; ++i)") != std::string::npos);
    CHECK(sched.find("for (int i = 1; i <= 16; ++i)") == std::string::npos);
    CHECK(sched.find("i <= 10 && i <= 16") == std::string::npos);  // old 10-slot ceiling
    CHECK(sched.find("slotIdx > SCHED_DT_SLOT_MAX") != std::string::npos);
}

TEST_CASE("ConfigManager stores, clears and day-tracks every slot") {
    std::string cm = srcFile("ConfigManager.h");
    CHECK(cm.find("i > SCHED_DT_SLOT_MAX") != std::string::npos);        // get/set slot + last_run_day
    CHECK(cm.find("slotIndex > SCHED_DT_SLOT_MAX") != std::string::npos); // NVS mirrors
    CHECK(cm.find("i > 8") == std::string::npos);                        // old storage cap gone
    CHECK(cm.find("i > 16") == std::string::npos);
    CHECK(cm.find("slotIndex > 16") == std::string::npos);
}

TEST_CASE("day-report journal tracks one report per slot plus manual") {
    std::string jr = srcFile("day_report_fallback.h");
    int cap = intAfter(jr, "static const int kMaxEntries = ");
    CHECK(cap >= SCHED_DT_SLOT_MAX + 2);  // every slot + manual slot 0 + spare
    CHECK(jr.find("covers SCHED_DT_SLOT_MAX sched_dt slots") != std::string::npos);
}

TEST_CASE("page row cap matches the stored slot capacity") {
    std::string page = srcFile("webpages/assets/page_commands_datetime_asset.cpp");
    int max = intAfter(page, "var MAX=");
    CHECK(max == SCHED_DT_SLOT_MAX);
}
