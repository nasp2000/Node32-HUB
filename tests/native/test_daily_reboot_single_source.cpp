// Native tests for the single daily-reboot persistence path (BACKLOG CMD-5).
//
// The two daily-reboot slots live in the `config` doc (core.json). Until CMD-5
// the firmware also mirrored them into the NVS key `cfg_drbt` and the getter
// read that copy FIRST, so both copies could drift: a stale NVS blob silently
// overrode the page, and a fresh core.json write never reached the engine.
//
// The fix keeps core.json as the only source and adds a one-shot upgrade read:
// `adoptLegacyDailyRebootSlots()` copies the legacy snapshot into the live doc
// (that is what the old NVS-first getter served) and `migrateLegacyDailyRebootNvs()`
// deletes the key only after core.json really holds the adopted values.
//
// Part A exercises the merge with the real JSON DOM; part B pins the plumbing
// (no NVS left in the runtime read/write paths, migration wired into loadConfig,
// response contract of /api/daily-reboot unchanged).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/daily_reboot_slots.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string readSrc(const char *rel) {
    std::string path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot read " << path);
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Bounded body: from `from` to the next `to` marker.
std::string slice(const std::string &text, const char *from, const char *to) {
    const size_t start = text.find(from);
    REQUIRE_MESSAGE(start != std::string::npos, "marker not found: " << from);
    const size_t end = text.find(to, start + 1);
    REQUIRE_MESSAGE(end != std::string::npos, "end marker not found: " << to);
    return text.substr(start, end - start);
}

constexpr const char *kLegacyBoth =
    "{\"s1\":{\"time\":\"03:30\",\"enabled\":true,\"last_run_day\":20260101},"
    " \"s2\":{\"time\":\"21:15\",\"enabled\":false,\"last_run_day\":0}}";

} // namespace

// ── Part A: the one-shot merge ───────────────────────────────────────────────

TEST_CASE("legacy snapshot is adopted into the live doc, slot by slot") {
    DynamicJsonDocument live(2048);
    live["daily_reboot"].to<JsonObject>();
    live["daily_reboot"]["s1"]["time"] = "00:00";
    live["daily_reboot"]["s1"]["enabled"] = true;

    const int adopted = adoptLegacyDailyRebootSlots(live["daily_reboot"], kLegacyBoth);

    CHECK(adopted == 2);
    CHECK(live["daily_reboot"]["s1"]["time"].as<String>() == String("03:30"));
    CHECK(live["daily_reboot"]["s1"]["enabled"].as<bool>() == true);
    CHECK(live["daily_reboot"]["s2"]["time"].as<String>() == String("21:15"));
    CHECK(live["daily_reboot"]["s2"]["enabled"].as<bool>() == false);
}

TEST_CASE("last_run_day already in core.json is never rolled back by the snapshot") {
    DynamicJsonDocument live(2048);
    live["daily_reboot"].to<JsonObject>();
    live["daily_reboot"]["s1"].to<JsonObject>();
    live["daily_reboot"]["s1"]["time"] = "00:00";
    live["daily_reboot"]["s1"]["enabled"] = true;
    live["daily_reboot"]["s1"]["last_run_day"] = 20261114; // fired today
    // s2 exists without last_run_day: the merge must fill it with 0.
    live["daily_reboot"]["s2"].to<JsonObject>();
    live["daily_reboot"]["s2"]["time"] = "";
    live["daily_reboot"]["s2"]["enabled"] = false;

    const int adopted = adoptLegacyDailyRebootSlots(live["daily_reboot"], kLegacyBoth);

    CHECK(adopted == 2);
    CHECK(live["daily_reboot"]["s1"]["last_run_day"].as<int>() == 20261114);
    CHECK(live["daily_reboot"]["s2"]["last_run_day"].as<int>() == 0);
}

TEST_CASE("an incomplete legacy slot is ignored instead of blanking the live one") {
    // s2 has no `enabled`: only s1 may be adopted.
    const char *legacy =
        "{\"s1\":{\"time\":\"05:45\",\"enabled\":true},"
        " \"s2\":{\"time\":\"23:59\"}}";
    DynamicJsonDocument live(2048);
    live["daily_reboot"].to<JsonObject>();
    live["daily_reboot"]["s1"].to<JsonObject>();
    live["daily_reboot"]["s1"]["time"] = "00:00";
    live["daily_reboot"]["s1"]["enabled"] = true;
    live["daily_reboot"]["s2"].to<JsonObject>();
    live["daily_reboot"]["s2"]["time"] = "12:00";
    live["daily_reboot"]["s2"]["enabled"] = true;

    const int adopted = adoptLegacyDailyRebootSlots(live["daily_reboot"], legacy);

    CHECK(adopted == 1);
    CHECK(live["daily_reboot"]["s1"]["time"].as<String>() == String("05:45"));
    CHECK(live["daily_reboot"]["s2"]["time"].as<String>() == String("12:00"));
    CHECK(live["daily_reboot"]["s2"]["enabled"].as<bool>() == true);
}

TEST_CASE("an unusable legacy blob adopts nothing and leaves the doc untouched") {
    DynamicJsonDocument live(2048);
    live["daily_reboot"].to<JsonObject>();
    live["daily_reboot"]["s1"].to<JsonObject>();
    live["daily_reboot"]["s1"]["time"] = "07:00";
    live["daily_reboot"]["s1"]["enabled"] = true;

    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], "") == 0);
    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], nullptr) == 0);
    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], "{not json") == 0);
    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], "[]") == 0);
    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], "{\"s9\":{\"time\":\"01:00\",\"enabled\":true}}") == 0);

    CHECK(live["daily_reboot"]["s1"]["time"].as<String>() == String("07:00"));
    CHECK(live["daily_reboot"]["s1"]["enabled"].as<bool>() == true);
}

TEST_CASE("merging twice is idempotent (boot loop cannot corrupt the slots)") {
    DynamicJsonDocument live(2048);
    live["daily_reboot"].to<JsonObject>();

    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], kLegacyBoth) == 2);
    String first;
    serializeJson(live, first);

    CHECK(adoptLegacyDailyRebootSlots(live["daily_reboot"], kLegacyBoth) == 2);
    String second;
    serializeJson(live, second);
    CHECK(first == second);
}

// ── Part B: one path only ────────────────────────────────────────────────────

TEST_CASE("ConfigManager reads and writes daily reboots without touching NVS") {
    const std::string header = readSrc("ConfigManager.h");

    const std::string getter = slice(header, "bool getDailyRebootSlot(", "// 🧠 Write daily reboot slot");
    const std::string setter = slice(header, "bool setDailyRebootSlot(", "// ✅ Getter for last_run_day");

    for (const std::string *body : {&getter, &setter}) {
        CHECK(body->find("cfg_drbt") == std::string::npos);
        CHECK(body->find("Preferences") == std::string::npos);
        CHECK(body->find("serializeJson") == std::string::npos);
    }
    CHECK(getter.find("config[\"daily_reboot\"]") != std::string::npos);
    CHECK(setter.find("isDirty = true") != std::string::npos);
}

TEST_CASE("the legacy key is adopted once at boot and dropped only after a save") {
    const std::string cpp = readSrc("ConfigManager.cpp");

    // Wired into the boot load, after the slots exist.
    const std::string load = slice(cpp, "bool ConfigManager::loadConfig(", "void ConfigManager::cleanDeprecatedFlowControl()");
    const size_t fields = load.find("ensureAllFieldsExist();");
    const size_t migrate = load.find("migrateLegacyDailyRebootNvs();");
    REQUIRE(fields != std::string::npos);
    REQUIRE(migrate != std::string::npos);
    CHECK(fields < migrate);

    const std::string fn = slice(cpp,
                                 "void ConfigManager::migrateLegacyDailyRebootNvs()",
                                 "uint32_t ConfigManager::computeHash32");
    CHECK(fn.find("adoptLegacyDailyRebootSlots(config[\"daily_reboot\"]") != std::string::npos);
    CHECK(fn.find("if (saveToLittleFS()) {") != std::string::npos);
    // The removal sits inside the save-success branch, never after a failed save.
    const size_t saveOk = fn.find("if (saveToLittleFS()) {");
    const size_t dropAdopted = fn.find("pr.remove(\"cfg_drbt\")", saveOk);
    REQUIRE(dropAdopted != std::string::npos);
    const size_t keptBranch = fn.find("KEPT (core.json not written");
    REQUIRE(keptBranch != std::string::npos);
    CHECK(dropAdopted < keptBranch);
}

TEST_CASE("NVS health no longer advertises cfg_drbt as a live config key") {
    const std::string cpp = readSrc("ConfigManager.cpp");
    const std::string health = slice(cpp, "void ConfigManager::checkNVSHealth()", "bool ConfigManager::verifySave()");
    CHECK(health.find("cfg_drbt") == std::string::npos);
    CHECK(health.find("cfg_sdcard") != std::string::npos);
    // Count follows the table instead of a hardcoded 12.
    CHECK(health.find("sizeof(keys) / sizeof(keys[0])") != std::string::npos);
}

TEST_CASE("/api/daily-reboot keeps its contract with core.json as the only read") {
    const std::string handler = readSrc("webpages/handlers_unified/commands_handler.cpp");

    const std::string get = slice(handler, "void handleDailyRebootGet(", "void handleDailyRebootPost(");
    const std::string post = slice(handler, "void handleDailyRebootPost(", "#if ENABLE_SCHEDULER == 1");

    for (const std::string *body : {&get, &post}) {
        CHECK(body->find("cfg_drbt") == std::string::npos);
        CHECK(body->find("Preferences") == std::string::npos);
        CHECK(body->find("nvs_dump") == std::string::npos);
        CHECK(body->find("nvs_len") == std::string::npos);
    }

    // Same JSON fields the page consumes (plus the pre-existing debug dump).
    for (const char *field : {"stored_time", "follows_shifts", "effective_time", "enabled", "daily_reboot"}) {
        CHECK(get.find(field) != std::string::npos);
    }
    CHECK(post.find("storedTime = timeValue") != std::string::npos);
    CHECK(post.find("storedEnabled = enabled") != std::string::npos);
    CHECK(post.find("setDailyRebootSlot(") != std::string::npos);
}
