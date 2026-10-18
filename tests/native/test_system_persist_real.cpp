// TASK 2 — system-settings persistence round-trip through the REAL production
// ConfigManager.cpp (docs/_OPENCE_system_inact_persist.md, option B).
//
// setMdnsEnabled/setSystemName/setSystemLanguage only call
// markDirtyAndScheduleSave() (a no-op) and must be persisted by
// saveSection("system"), which currently writes ONLY printer/system_name/
// system_language — mdns_enabled and the config["system"] fields are dropped.
// Expected end state: saveSection("system") writes mdns_enabled + full system
// section, load restores them from system.json.
//
// Red before the fix (fails on getMdnsEnabled() + file-content only, and the
// system_name/language asserts may already pass since saveSection('system') did
// write those two) → green after.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ConfigManager.h"
#include <LittleFS.h>

extern void nativeResetConfigStorage();

TEST_CASE("system: mdns_enabled persists via saveSection('system')") {
    nativeResetConfigStorage();
    ConfigManager cm;
    CHECK(cm.loadConfig());
    cm.setMdnsEnabled(false);
    cm.setSystemName("NASP-HUB");
    cm.setSystemLanguage("pt");
    cm.setSerialMonitorEnabled(false); // config["system"] subtree field
    CHECK(cm.saveSection("system"));

    // Raw file must carry mdns_enabled (top level) and the system section.
    // Before the fix only {printer, system_name, system_language} are written,
    // so these check the missing-field bug directly.
    CHECK(LittleFS.exists("/config/system.json"));
    String raw;
    File f = LittleFS.open("/config/system.json", "r");
    REQUIRE(f);
    raw = f.readString();
    f.close();
    DynamicJsonDocument doc(8192);
    CHECK(deserializeJson(doc, raw) == DeserializationError::Ok);
    CHECK(doc["mdns_enabled"] == false);
    CHECK(doc["system_name"].as<const char*>() != nullptr);
    CHECK(String(doc["system_name"].as<const char*>()) == "NASP-HUB");
    CHECK(String(doc["system_language"].as<const char*>()) == "pt");
    CHECK(doc["system"]["serial_monitor_enabled"] == false);

    // Fresh ConfigManager must restore the runtime values on boot.
    ConfigManager cm2;
    CHECK(cm2.loadConfig());
    CHECK_MESSAGE(cm2.getMdnsEnabled() == false,
                  "mdns_enabled lost across reboot");
    CHECK(cm2.getSystemName() == "NASP-HUB");
    CHECK(cm2.getSystemLanguage() == "pt");
    CHECK_MESSAGE(cm2.getSerialMonitorEnabled() == false,
                  "system.serial_monitor_enabled lost across reboot");
}

TEST_CASE("system: mdns_enabled default true is kept when not persisted (variation 2)") {
    nativeResetConfigStorage();
    ConfigManager cm;
    CHECK(cm.loadConfig());
    CHECK(cm.getMdnsEnabled() == true); // default on empty storage
    cm.setMdnsEnabled(true);
    cm.setSystemName("DEFAULT-NAME"); // explicit save with mdns still ON
    CHECK(cm.saveSection("system"));

    ConfigManager cm2;
    CHECK(cm2.loadConfig());
    CHECK(cm2.getMdnsEnabled() == true);
    CHECK(cm2.getSystemName() == "DEFAULT-NAME");
}