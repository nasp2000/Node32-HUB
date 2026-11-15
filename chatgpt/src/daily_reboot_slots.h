#pragma once
// Daily-reboot slot helpers (BACKLOG CMD-5).
//
// The two daily-reboot slots live in the `config` doc → /config/core.json and
// nowhere else. Until CMD-5 the firmware also mirrored them into the NVS key
// `cfg_drbt` and read that copy FIRST, so two files could drift apart (a core
// save or a config restore stopped affecting the engine, and a stale NVS blob
// silently overrode the page).
//
// adoptLegacyDailyRebootSlots() is the one-shot upgrade read: it takes a
// `cfg_drbt` snapshot and copies its slots into the live `daily_reboot` object,
// which is what the old NVS-first getter effectively served. Only `time` and
// `enabled` are adopted — they are the slot identity, while `last_run_day` has
// its own crash-safe anchor (`drb_lr*` in NVS + the RTC mirror) and must not be
// rolled back by an old snapshot.
//
// Pure JSON in / count out, so the merge is testable on the host
// (tests/native/test_daily_reboot_single_source.cpp).

#include <Arduino.h>
#include <ArduinoJson.h>

#include <stdio.h>

// Copy the slots of a legacy `cfg_drbt` snapshot into `dailyReboot`.
// Returns how many slots were adopted; 0 when the blob is empty, corrupt or
// carries no complete slot (both `time` and `enabled` are required).
inline int adoptLegacyDailyRebootSlots(JsonVariant dailyReboot, const char *legacyJson) {
    if (legacyJson == nullptr || legacyJson[0] == '\0') return 0;
    if (!dailyReboot.is<JsonObject>()) return 0;

    DynamicJsonDocument legacyDoc(512);
    if (deserializeJson(legacyDoc, String(legacyJson))) return 0; // non-zero = parse error
    if (!legacyDoc.is<JsonObject>()) return 0;

    JsonObject dst = dailyReboot.as<JsonObject>();
    int adopted = 0;
    for (int i = 1; i <= 2; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "s%d", i);
        JsonVariant legacySlot = legacyDoc[key];
        if (!legacySlot.is<JsonObject>()) continue;
        JsonObject src = legacySlot.as<JsonObject>();
        JsonVariant srcTime = src["time"];
        JsonVariant srcEnabled = src["enabled"];
        if (srcTime.isNull() || srcEnabled.isNull()) continue; // incomplete: keep core.json

        JsonVariant dstSlot = dst[key];
        if (!dstSlot.is<JsonObject>()) dstSlot.to<JsonObject>(); // create/replace: never re-to<>() an existing slot
        JsonObject slot = dstSlot.as<JsonObject>();
        slot["time"] = String(srcTime.as<const char *>());
        slot["enabled"] = srcEnabled.as<bool>();
        if (slot["last_run_day"].isNull()) slot["last_run_day"] = 0;
        adopted++;
    }
    return adopted;
}
