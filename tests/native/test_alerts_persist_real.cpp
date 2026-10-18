// TASK 1 — alerts persistence round-trip through the REAL production
// ConfigManager.cpp compiled natively (docs/_OPENCE_waveshare_alerts_persist.md
// + _OPENCE_system_inact_persist.md).
//
// Scenario: setAlertSlot(0, true, 7, 3, "owner@example.com") →
// saveSection("alerts") → /config/alerts.json must contain enabled=true →
// a SECOND ConfigManager instance (fresh RAM) must re-enable slot 0 on boot.
//
// If this passes, the loss is elsewhere (web handler); if it fails, the
// save/load bug lives in ConfigManager.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ConfigManager.h"
#include <LittleFS.h>

extern void nativeResetConfigStorage();

static ConfigManager* makeInstance(bool& ok) {
    ConfigManager* cm = new ConfigManager();
    ok = cm->loadConfig();
    return cm;
}

TEST_CASE("alerts: enabled slot 0 survives saveSection -> reload") {
    nativeResetConfigStorage();
    bool ok = false;
    ConfigManager* cm = makeInstance(ok);
    REQUIRE_MESSAGE(ok, "loadConfig on empty storage");
    cm->setAlertSlot(0, true, 7, 3, "owner@example.com");
    CHECK(cm->saveSection("alerts"));
    CHECK(LittleFS.exists("/config/alerts.json"));
    delete cm;

    // 1) Raw file inspection — must contain the enabled slot, not defaults.
    {
        String raw;
        File f = LittleFS.open("/config/alerts.json", "r");
        REQUIRE(f);
        raw = f.readString();
        f.close();
        CHECK(raw.length() > 0);
        DynamicJsonDocument doc(16384);
        CHECK(deserializeJson(doc, raw) == DeserializationError::Ok);
        CHECK(doc["slots"].is<JsonArray>() == true);
        CHECK(doc["slots"][0]["enabled"] == true);
        CHECK(doc["slots"][0]["timeout_minutes"] == 7);
        CHECK(doc["slots"][0]["max_per_day"] == 3);
        CHECK(doc["slots"][0]["emails"].as<const char*>() != nullptr);
        CHECK(String(doc["slots"][0]["emails"].as<const char*>()).indexOf("owner@example.com") >= 0);
    }

    // 2) Fresh ConfigManager (same storage) must restore the slot on boot.
    ConfigManager* cm2 = makeInstance(ok);
    REQUIRE_MESSAGE(ok, "loadConfig on storage with alerts.json");
    CHECK_MESSAGE(cm2->getAlertSlotEnabled(0) == true,
                  "slot 0 lost enabled across reboot");
    CHECK(cm2->getAlertSlotTimeout(0) == 7);
    CHECK(cm2->getAlertSlotMaxPerDay(0) == 3);
    CHECK(cm2->getAlertSlotEmails(0).indexOf("owner@example.com") >= 0);
    CHECK(cm2->getInactivityAlertEnabled() == true);
    delete cm2;
}

TEST_CASE("alerts: multi-slot round trip + idempotent re-save (variation 2)") {
    nativeResetConfigStorage();
    bool ok = false;
    ConfigManager* cm = makeInstance(ok);
    REQUIRE(ok);

    // Two slots configured at once; slot 1 explicitly disabled.
    cm->setAlertSlot(0, true, 7, 3, "owner@example.com");
    cm->setAlertSlot(1, false, 10, 1, "recto@shifft.dev");
    cm->setAlertSlot(3, true, 45, 5, "owner@example.com,alerts@cnc.local");
    CHECK(cm->saveSection("alerts"));
    // Second (idempotent) save must not corrupt the section.
    cm->setAlertSlot(0, true, 7, 3, "owner@example.com");
    CHECK(cm->saveSection("alerts"));
    delete cm;

    ConfigManager* cm2 = makeInstance(ok);
    REQUIRE(ok);
    CHECK(cm2->getAlertSlotEnabled(0) == true);
    CHECK(cm2->getAlertSlotEnabled(1) == false);
    CHECK(cm2->getAlertSlotEnabled(3) == true);
    CHECK(cm2->getAlertSlotTimeout(3) == 45);
    CHECK(cm2->getAlertSlotMaxPerDay(3) == 5);
    CHECK(cm2->getAlertSlotEmails(3).indexOf("alerts@cnc.local") >= 0);
    CHECK(cm2->getAlertSlotEmails(3).indexOf("owner@example.com") >= 0);
    delete cm2;
}

// OEE tab never persists (.29 board, 2026-10-12): live /config/alerts.json
// carries "oee_slots":[] — the key EXISTS as an empty array, so the old
// _getOeeAlertSlots() skipped the default-fill, every setOeeAlertSlot silently
// no-op'd and every getter returned hardcoded defaults (POST validated +
// answered ok, GET/page rendered blanks). The getter must self-heal.
TEST_CASE("oee alerts: empty oee_slots array self-heals to 3 defaults and persists") {
    nativeResetConfigStorage();
    // Reproduce the live-board state: inactivity slots intact, oee_slots empty.
    {
        File f = LittleFS.open("/config/alerts.json", "w");
        REQUIRE(f);
        f.print("{\"slots\":[{\"enabled\":false,\"timeout_minutes\":20,\"max_per_day\":12,\"emails\":\"\"}],\"oee_slots\":[]}");
        f.close();
    }
    bool ok = false;
    ConfigManager* cm = makeInstance(ok);
    REQUIRE_MESSAGE(ok, "loadConfig on storage with empty oee_slots");
    // Healed defaults (slot 1 = day OEE, slot 2 = availability < 80%).
    CHECK(cm->getOeeAlertMetric(1) == 1);
    CHECK(cm->getOeeAlertThreshold(2) == 80);
    cm->setOeeAlertSlot(0, true, 0, 77, 15, 6, "test@example.com");
    CHECK(cm->getOeeAlertEnabled(0) == true);
    CHECK(cm->getOeeAlertThreshold(0) == 77);
    CHECK(cm->saveSection("alerts"));
    delete cm;

    // Fresh instance (same storage) must restore the OEE slot across reboot.
    ConfigManager* cm2 = makeInstance(ok);
    REQUIRE(ok);
    CHECK_MESSAGE(cm2->getOeeAlertEnabled(0) == true,
                  "oee slot 0 lost enabled across reboot");
    CHECK(cm2->getOeeAlertThreshold(0) == 77);
    CHECK(cm2->getOeeAlertEmails(0).indexOf("test@example.com") >= 0);
    CHECK(cm2->getOeeAlertMetric(1) == 1);
    delete cm2;
}