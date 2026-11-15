// Native tests for the shared recipient-slot JSON shape (BACKLOG RECIP-6).
//
// GET /api/recipients returned {name, op, emails, freq} while
// GET /api/recipient-slots returned the same four plus `active`, so a consumer
// of the paginated route could not tell a disabled rule from an enabled one.
// Both routes (and both transports) now fill a slot through
// recipientSlotFillJson()/recipientSlotFillJsonFromStored(), so the shapes
// cannot drift again.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "recipient_slot_json.h"

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

std::string srcFile(const char *rel) {
    std::string text;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) + "/" + rel, text),
                    "cannot read " << rel);
    return text;
}

size_t countOf(const std::string &hay, const std::string &needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

} // namespace

TEST_CASE("a slot always carries the five documented fields") {
    DynamicJsonDocument doc(512);
    JsonObject item = doc.to<JsonObject>();
    recipientSlotFillJson(item, "FAULT", "I", "a@b.c", 600, true);

    CHECK(!item["name"].isNull());
    CHECK(!item["op"].isNull());
    CHECK(!item["emails"].isNull());
    CHECK(!item["freq"].isNull());
    CHECK(!item["active"].isNull());            // RECIP-6: the missing field
    CHECK(item["name"].as<String>() == "FAULT");
    CHECK(item["op"].as<String>() == "I");
    CHECK(item["emails"].as<String>() == "a@b.c");
    CHECK(item["freq"].as<int>() == 600);
    CHECK(item["active"].as<bool>() == true);
}

TEST_CASE("`active` is emitted, including the disabled state") {
    DynamicJsonDocument doc(512);
    JsonObject item = doc.to<JsonObject>();
    recipientSlotFillJson(item, "X", "D", "", 0, false);
    CHECK(!item["active"].isNull());
    CHECK(item["active"].as<bool>() == false);
    CHECK(item["freq"].as<int>() == 0);
}

TEST_CASE("the stored-slot helper applies the slots-route defaults") {
    // Separate documents: the stored slot and the emitted item are distinct
    // objects in the firmware too (config array vs response array).
    DynamicJsonDocument sdoc(512);
    JsonObject stored = sdoc.to<JsonObject>();
    stored["name"] = "K";
    // op, emails, freq and enabled are absent -> defaults like /api/recipient-slots.
    DynamicJsonDocument idoc(512);
    JsonObject item = idoc.to<JsonObject>();
    recipientSlotFillJsonFromStored(item, stored);
    CHECK(item["name"].as<String>() == "K");
    CHECK(item["op"].as<String>() == "I");
    CHECK(item["emails"].as<String>() == "");
    CHECK(item["freq"].as<int>() == 0);
    CHECK(item["active"].as<bool>() == true);      // default enabled
}

TEST_CASE("a stored slot with enabled:false reports active:false") {
    DynamicJsonDocument sdoc(512);
    JsonObject stored = sdoc.to<JsonObject>();
    stored["name"] = "K";
    stored["enabled"] = false;
    stored["freq"] = 180;
    DynamicJsonDocument idoc(512);
    JsonObject item = idoc.to<JsonObject>();
    recipientSlotFillJsonFromStored(item, stored);
    CHECK(item["active"].as<bool>() == false);
    CHECK(item["freq"].as<int>() == 180);
}

TEST_CASE("both routes and both transports use the shared helper") {
    const std::string wifi = srcFile("webpages/handlers/recipients_handler.cpp");
    const std::string eth = srcFile("ethernet_bootstrap_handlers.h");
    // One call per route handler (paginated + slots) in each transport.
    CHECK(countOf(wifi, "recipientSlotFillJsonFromStored(") == 1);   // paginated
    CHECK(countOf(wifi, "recipientSlotFillJson(") == 1);             // slots
    CHECK(countOf(eth, "recipientSlotFillJsonFromStored(") == 1);    // paginated
    CHECK(countOf(eth, "recipientSlotFillJson(") == 1);              // slots
    // No direct `active` assignment is left outside the shared helper.
    CHECK(wifi.find("[\"active\"]") == std::string::npos);
    CHECK(eth.find("[\"active\"]") == std::string::npos);
}
