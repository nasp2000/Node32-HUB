// TASK 2 — NTP timezone/server persistence through the REAL production
// NtpManager.cpp (compiled as ntp_manager_linux.cpp, see the Makefile: the
// POSIX-clashing global `timezone` is renamed to ntpTimeZoneGlobal so the host
// can link it next to glibc). Mirrors the config_handler change that now calls
// ntpManager.save() after a timezone/server edit on the system page.
//
// setServer/setTimezone only mark the manager dirty (auto-save is disabled),
// so before the fix a reboot reverted the timezone to the default. The fix is
// the explicit save() in handleConfigPost + this round-trip proving the file
// is written and re-read by a fresh instance.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "NtpManager.h"
#include <LittleFS.h>

extern void nativeResetNtpStorage();

TEST_CASE("ntp: timezone/server round-trip via ntpManager.save()") {
    nativeResetNtpStorage();
    NtpManager nm;
    nm.setServer("pt.pool.ntp.org");
    nm.setTimezone("CET-1CEST,M3.5.0/1,M10.5.0");
    CHECK(nm.getServer() == "pt.pool.ntp.org");
    CHECK(nm.getTimezone() == "CET-1CEST,M3.5.0/1,M10.5.0");
    nm.save();

    CHECK(LittleFS.exists("/config/ntp.json"));
    String raw;
    File f = LittleFS.open("/config/ntp.json", "r");
    REQUIRE(f);
    raw = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    CHECK(deserializeJson(doc, raw) == DeserializationError::Ok);
    CHECK(String(doc["server"].as<const char*>()) == "pt.pool.ntp.org");
    CHECK(String(doc["timezone"].as<const char*>()) == "CET-1CEST,M3.5.0/1,M10.5.0");

    // Simulated reboot: a fresh instance must restore the persisted values.
    NtpManager nm2;
    nm2.begin("pool.ntp.org", "WET0WEST,M3.5.0/1,M10.5.0", 60);
    CHECK(nm2.getServer() == "pt.pool.ntp.org");
    CHECK(nm2.getTimezone() == "CET-1CEST,M3.5.0/1,M10.5.0");
    CHECK(nm2.getSyncIntervalMinutes() == 60);
}

TEST_CASE("ntp: begin() persists fallback on first boot (variation 2)") {
    nativeResetNtpStorage();
    NtpManager nm;
    nm.begin("fallback.ntp.example", "UTC0", 30);
    CHECK(nm.getServer() == "fallback.ntp.example");
    CHECK(nm.getTimezone() == "UTC0");
    CHECK(LittleFS.exists("/config/ntp.json"));
    String raw;
    File f = LittleFS.open("/config/ntp.json", "r");
    REQUIRE(f);
    raw = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    CHECK(deserializeJson(doc, raw) == DeserializationError::Ok);
    CHECK(String(doc["server"].as<const char*>()) == "fallback.ntp.example");
    CHECK(String(doc["timezone"].as<const char*>()) == "UTC0");
}