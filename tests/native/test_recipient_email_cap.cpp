// Native test for the recipient email cap (BACKLOG RECIP-8).
//
// handleRecipientSlotPost() removed everything past 800 chars, but
// ConfigManager::updateRecipientSlotWithFreq() then truncates to 256 — the real
// storage limit — so the 800 cap was dead code. It is gone; this test drives
// the REAL ConfigManager and proves the effective limit is 256, plus a source
// check that the 800 removal is not reintroduced.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ConfigManager.h"

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

} // namespace

TEST_CASE("an email list longer than 256 is stored truncated to 256") {
    ConfigManager cm;
    REQUIRE_MESSAGE(cm.loadConfig(), "loadConfig on the host harness");
    const std::string raw(900, 'a');
    REQUIRE(cm.updateRecipientSlotWithFreq(1, "RULE", "I", String(raw.c_str()), 0, true));

    String name, op, emails;
    int freq = -1;
    REQUIRE(cm.getRecipientSlotWithFreq(1, name, op, emails, freq));
    CHECK(emails.length() == 256);          // the real ConfigManager limit
    CHECK(emails.length() != 800);          // the dead handler cap is not the limit
    for (unsigned i = 0; i < emails.length(); i++) CHECK(emails[i] == 'a');
    CHECK(name == "RULE");
}

TEST_CASE("the handler no longer removes past 800 chars") {
    // Source contract: the dead cap must not come back (ConfigManager owns the
    // 256 limit). RECIP-8.
    std::string handler;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) +
                             "/webpages/handlers_unified/recipients_handler.cpp", handler),
                    "cannot read recipients_handler.cpp");
    CHECK(handler.find("emails.remove(800)") == std::string::npos);
    CHECK(handler.find("length() > 800") == std::string::npos);
}
