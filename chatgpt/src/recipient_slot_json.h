// recipient_slot_json.h — one recipient slot in the API shape shared by the
// paginated list (GET /api/recipients) and the full list
// (GET /api/recipient-slots).
//
// RECIP-6: the paginated route omitted `active`, so a consumer could not tell a
// disabled rule from an enabled one, and the two routes returned different
// shapes for the same slot. Both transports (WiFi + Ethernet) and both routes
// now fill the slot through this single function.
//
// ArduinoJson only; no other dependencies, so it is host-testable
// (tests/native/test_recipient_slot_json.cpp).
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Field order matches the documented shape: name, op, emails, freq, active.
static inline void recipientSlotFillJson(JsonObject item, const String& name,
                                         const String& op, const String& emails,
                                         int freq, bool active) {
    item["name"] = name;
    item["op"] = op;
    item["emails"] = emails;
    item["freq"] = freq;
    item["active"] = active;
}

// Convenience for the paginated routes, which read straight from the stored
// slot object: applies the same defaults the slots route uses (op `I`,
// freq 0 = default, enabled true).
static inline void recipientSlotFillJsonFromStored(JsonObject item, JsonObject stored) {
    // Copy each value before the next lookup: some JSON implementations insert a
    // missing key on read, which can move the node the previous handle pointed
    // at. One handle at a time keeps this correct everywhere.
    JsonVariant v = stored["name"];
    String name = v.isNull() ? String("") : v.as<String>();
    v = stored["op"];
    String op = v.isNull() ? String("I") : v.as<String>();
    v = stored["emails"];
    String emails = v.isNull() ? String("") : v.as<String>();
    v = stored["freq"];
    int freq = v.isNull() ? 0 : v.as<int>();
    v = stored["enabled"];
    bool active = v.isNull() ? true : v.as<bool>();
    recipientSlotFillJson(item, name, op, emails, freq, active);
}
