// proximity_shift_config.cpp
// Persistent storage and in-memory state for shift management configuration.
// Shift + shiftday emails are stored INSIDE the unified proximity config file
// (/config/sensor_proximity.json, section "shift"), shared by all proximity
// page tabs (status/config/shifts/shiftday) so they stay in sync.

#include "proximity_manager.h"

#include "feature_flags.h"
#include "../psram_helpers.h"
#if ENABLE_PROXIMITY == 1

#include <ArduinoJson.h>
#include "../arduinojson_fix.h"
#include <LittleFS.h>
#include "feature_flags.h"
#include "../psram_helpers.h"
#include "../littlefs_manager.h"
#include "../commands.h"

// Unified config file: channels + shift + shiftday emails in ONE file.
#define PROX_UNIFIED_CFG_PATH "/config/sensor_proximity.json"

// In-memory shift config. Defaults: disabled, no auto-reset, 3 daily slots.
static ProximityShiftConfig _shiftCfg = {
    false, false, 3, {{6, 0}, {14, 0}, {22, 0}}
};

// --- Persistence (unified file) ---

// Applies the "shift" object (en/ar/ns/s/em) to _shiftCfg from a parsed doc.
static void applyShiftFromObj(JsonObjectConst shiftObj) {
    if (shiftObj.isNull()) return;
    _shiftCfg.enabled             = shiftObj["en"] | false;
    _shiftCfg.auto_reset_on_shift = shiftObj["ar"] | false;
    const char* em = shiftObj["em"] | "";
    strncpy(_shiftCfg.shiftday_emails, em, sizeof(_shiftCfg.shiftday_emails) - 1);
    _shiftCfg.shiftday_emails[sizeof(_shiftCfg.shiftday_emails) - 1] = '\0';
    JsonArrayConst arr = shiftObj["s"].as<JsonArrayConst>();
    _shiftCfg.nSlots = 0;
    for (JsonVariantConst v : arr) {
        if (_shiftCfg.nSlots >= 3) break;
        JsonObjectConst o = v.as<JsonObjectConst>();
        _shiftCfg.slots[_shiftCfg.nSlots].hour   = o["h"] | 6;
        _shiftCfg.slots[_shiftCfg.nSlots].minute = o["m"] | 0;
        _shiftCfg.nSlots++;
    }
    uint8_t savedNSlots = shiftObj["ns"] | 0;
    if (_shiftCfg.nSlots == 0) {
        _shiftCfg.nSlots = (savedNSlots > 0 && savedNSlots <= 3) ? savedNSlots : 3;
    }
    proximitySortShiftSlots(_shiftCfg);   // heal any unsorted order persisted by older firmware
}

// Public: apply the "shift" section of a unified config doc.
bool proximityShiftFromJson(JsonVariantConst doc) {
    JsonObjectConst shiftObj = doc["shift"].as<JsonObjectConst>();
    if (shiftObj.isNull()) return false;
    applyShiftFromObj(shiftObj);
    return true;
}

// Public: inject current shift state as the "shift" member of `out`.
void proximityShiftToJson(JsonObject& out) {
    JsonObject shiftObj = out.createNestedObject("shift");
    shiftObj["en"] = _shiftCfg.enabled;
    shiftObj["ar"] = _shiftCfg.auto_reset_on_shift;
    shiftObj["ns"] = _shiftCfg.nSlots;
    shiftObj["em"] = _shiftCfg.shiftday_emails;
    for (uint8_t i = 0; i < _shiftCfg.nSlots; i++) {
        // ArduinoJson 6.21: JsonArray::createNestedObject() returns void —
        // assignment from it never compiled. Nested values are added by
        // member-assign through the parent object proxy each iteration.
        shiftObj["s"][i]["h"] = _shiftCfg.slots[i].hour;
        shiftObj["s"][i]["m"] = _shiftCfg.slots[i].minute;
    }
}

void loadProximityShiftConfig() {
    if (!littlefsBegin()) {
        sendVirtualMessage(F("[prox:shift] load: littlefsBegin failed"), COMMAND_RESPONSE, true);
        return;
    }
    File f = LittleFS.open(PROX_UNIFIED_CFG_PATH, "r");
    if (!f) {
        sendVirtualMessage(F("[prox:shift] load: no unified config, using defaults"), COMMAND_RESPONSE, true);
        return;
    }

    SpiRamJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        sendVirtualMessage(F("[prox:shift] load: JSON parse failed, keeping defaults"), COMMAND_RESPONSE, true);
        return;
    }

    // If the file is still the legacy channel-array format, there is no shift.
    if (doc.is<JsonObject>()) proximityShiftFromJson(doc);
    if (doc.is<JsonObject>()) proximityStopsFromJson(doc);

    sendVirtualMessage(String(F("[prox:shift] loaded: en=")) + String((int)_shiftCfg.enabled) +
        F(" nSlots=") + String((int)_shiftCfg.nSlots) +
        F(" h0=") + String((int)_shiftCfg.slots[0].hour) +
        F(" m0=") + String((int)_shiftCfg.slots[0].minute), COMMAND_RESPONSE, true);
}

bool saveProximityShiftConfig() {
    sendVirtualMessage(String(F("[prox:shift] save: en=")) + String((int)_shiftCfg.enabled) +
        F(" nSlots=") + String((int)_shiftCfg.nSlots) +
        F(" h0=") + String((int)_shiftCfg.slots[0].hour) +
        F(" m0=") + String((int)_shiftCfg.slots[0].minute), COMMAND_RESPONSE, true);
    if (!littlefsBegin()) {
        sendVirtualMessage(F("[prox:shift] save: littlefsBegin FAILED"), COMMAND_RESPONSE, true);
        return false;
    }
    if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");

    // Round-trip: read the unified file, update only the "shift" section,
    // then write it back — channels + shift stay in sync in ONE file.
    SpiRamJsonDocument doc(4096);
    File rf = LittleFS.open(PROX_UNIFIED_CFG_PATH, "r");
    if (rf) {
        deserializeJson(doc, rf);
        rf.close();
    }
    if (doc.is<JsonArray>()) {
        // Legacy channel-array format: convert to the unified object.
        JsonArray legacy = doc.as<JsonArray>();
        JsonArray channels = doc.to<JsonObject>().createNestedArray("channels");
        for (JsonVariantConst v : legacy) channels.add(v);
    }

    JsonObject shiftObj = doc["shift"].to<JsonObject>();
    shiftObj["en"] = _shiftCfg.enabled;
    shiftObj["ar"] = _shiftCfg.auto_reset_on_shift;
    shiftObj["ns"] = _shiftCfg.nSlots;
    shiftObj["em"] = _shiftCfg.shiftday_emails;
    shiftObj.createNestedArray("s");
    for (uint8_t i = 0; i < _shiftCfg.nSlots; i++) {
        // ArduinoJson 6.21: JsonArray::createNestedObject() returns void —
        // assignment from it never compiled. Nested values are added by
        // member-assign through the parent object proxy each iteration.
        shiftObj["s"][i]["h"] = _shiftCfg.slots[i].hour;
        shiftObj["s"][i]["m"] = _shiftCfg.slots[i].minute;
    }
    { JsonObject rootRef = doc.to<JsonObject>(); proximityStopsToJson(rootRef); }

    String shiftTmp = String(PROX_UNIFIED_CFG_PATH) + ".tmp";
    File f = LittleFS.open(shiftTmp, "w");
    if (!f) {
        sendVirtualMessage(F("[prox:shift] save: open tmp file FAILED"), COMMAND_RESPONSE, true);
        return false;
    }
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes > 0) {
        LittleFS.remove(PROX_UNIFIED_CFG_PATH);
        LittleFS.rename(shiftTmp, PROX_UNIFIED_CFG_PATH);
        sendVirtualMessage(String(F("[prox:shift] save: OK (")) + String((int)bytes) + F(" bytes, unified)"), COMMAND_RESPONSE, true);
    } else {
        sendVirtualMessage(F("[prox:shift] save: serialize wrote 0 bytes"), COMMAND_RESPONSE, true);
        LittleFS.remove(shiftTmp);
        return false;
    }
    return true;
}

// --- Accessors ---

ProximityShiftConfig proximityGetShiftConfig() {
    return _shiftCfg;
}

// --- Planned stops (ISO planned busy time) ---

// In-memory list. Defaults: empty (no exclusion → legacy elapsed-based A).
static ProximityPlannedStop _stops[PROX_MAX_PLANNED_STOPS];
static uint8_t _nStops = 0;

static bool stopsEntryValid(const ProximityPlannedStop& s) {
    if (s.sh > 23 || s.eh > 23 || s.sm > 59 || s.em > 59) return false;
    if (s.sh == s.eh && s.sm == s.em) return false;   // zero-length
    return true;
}

uint8_t proximityGetPlannedStops(ProximityPlannedStop* out, uint8_t maxOut) {
    if (out == nullptr || maxOut == 0) return 0;
    uint8_t n = (_nStops < maxOut) ? _nStops : maxOut;
    for (uint8_t i = 0; i < n; i++) out[i] = _stops[i];
    return n;
}

bool proximitySetPlannedStops(const ProximityPlannedStop* stops, uint8_t nStops) {
    if (nStops > PROX_MAX_PLANNED_STOPS) return false;
    ProximityPlannedStop tmp[PROX_MAX_PLANNED_STOPS];
    for (uint8_t i = 0; i < nStops; i++) {
        if (stops == nullptr || !stopsEntryValid(stops[i])) return false;
        tmp[i] = stops[i];
    }
    if (proxPlannedStopsOverlap(tmp, nStops)) return false;
    for (uint8_t i = 0; i < nStops; i++) _stops[i] = tmp[i];
    _nStops = nStops;
    sendVirtualMessage(String(F("[prox:stops] set: n=")) + String((int)_nStops), COMMAND_RESPONSE, true);
    return true;
}

uint32_t proximityPlannedStopSec(time_t resetWall, time_t nowWall) {
    if (nowWall < 1000000000L || resetWall >= nowWall || _nStops == 0) return 0;
    return proxPlannedStopOverlapSec(resetWall, nowWall, _stops, _nStops);
}

// Apply the "stops" array of a parsed doc to memory. Lenient (load path):
// keeps valid non-overlapping entries, drops the rest, never fails boot.
static void applyStopsFromObj(JsonObjectConst stopsObj) {
    _nStops = 0;
    if (stopsObj.isNull()) return;
    JsonArrayConst arr = stopsObj["list"].as<JsonArrayConst>();
    if (arr.isNull()) return;
    ProximityPlannedStop tmp[PROX_MAX_PLANNED_STOPS];
    uint8_t n = 0;
    for (JsonVariantConst v : arr) {
        if (n >= PROX_MAX_PLANNED_STOPS) break;
        JsonObjectConst o = v.as<JsonObjectConst>();
        tmp[n].sh = (uint8_t)(o["sh"] | 0);
        tmp[n].sm = (uint8_t)(o["sm"] | 0);
        tmp[n].eh = (uint8_t)(o["eh"] | 0);
        tmp[n].em = (uint8_t)(o["em"] | 0);
        if (!stopsEntryValid(tmp[n])) continue;
        n++;
    }
    if (n > 0 && proxPlannedStopsOverlap(tmp, n)) {
        sendVirtualMessage(F("[prox:stops] load: overlapping list dropped, keeping empty"), COMMAND_RESPONSE, true);
        return;
    }
    for (uint8_t i = 0; i < n; i++) _stops[i] = tmp[i];
    _nStops = n;
}

// Public: apply the "stops" section of a unified config doc (load path).
bool proximityStopsFromJson(JsonVariantConst doc) {
    JsonObjectConst stopsObj = doc["stops"].as<JsonObjectConst>();
    if (stopsObj.isNull()) return false;
    applyStopsFromObj(stopsObj);
    return true;
}

// Public: inject current stops as the "stops" member of `out` (save path).
void proximityStopsToJson(JsonObject& out) {
    JsonObject stopsObj = out.createNestedObject("stops");
    stopsObj.createNestedArray("list");
    for (uint8_t i = 0; i < _nStops; i++) {
        stopsObj["list"][i]["sh"] = _stops[i].sh;
        stopsObj["list"][i]["sm"] = _stops[i].sm;
        stopsObj["list"][i]["eh"] = _stops[i].eh;
        stopsObj["list"][i]["em"] = _stops[i].em;
    }
}

// Day-boundary hour derived from the shift config.
// A shift starting at 00:00 means the calendar midnight is already a valid
// shift boundary, so we keep the file rolling over at 00:00. Otherwise the
// boundary is the last (latest) shift start hour of the day (e.g. 15h for
// 03-15/15-03 shifts, 22h for 22-06/06-14/14-22 shifts).
uint8_t proximityShiftDayBoundaryHour() {
    const ProximityShiftConfig &sc = _shiftCfg;
    if (!sc.enabled || sc.nSlots == 0) return 0;
    uint8_t last = 0;
    for (uint8_t i = 0; i < sc.nSlots; i++) {
        if (sc.slots[i].hour == 0) return 0;   // 00:00 shift → keep midnight rollover
        if (sc.slots[i].hour > last) last = sc.slots[i].hour;
    }
    return last;
}

// Compute the day that `now` belongs to once the boundary hour is applied.
// The day is labeled with the date it ENDS into: it runs from the boundary hour
// on day D-1 to the SAME boundary hour on day D, so times at/after today's
// boundary already belong to the NEXT day's file (R2 — the CSV for day D is
// created at the boundary of day D-1). Semantics live in the pure
// proxShiftAlignedDate() helper (proximity_manager.h).
uint8_t proximityShiftDayAlignedDate(time_t now, struct tm* out) {
    return proxShiftAlignedDate(now, proximityShiftDayBoundaryHour(), out);
}

// proximityResetShiftDetection() is defined in proximity_manager.cpp and resets
// _currentShiftIdx so the next tick forces a re-baseline.
void proximitySetShiftConfig(const ProximityShiftConfig& cfg) {
    _shiftCfg = cfg;
    proximitySortShiftSlots(_shiftCfg);   // contract: slots sorted ascending (web/CLI never sort)
    sendVirtualMessage(String(F("[prox:shift] setConfig: en=")) + String((int)_shiftCfg.enabled) +
        F(" nSlots=") + String((int)_shiftCfg.nSlots) +
        F(" h0=") + String((int)_shiftCfg.slots[0].hour), COMMAND_RESPONSE, true);
    proximityResetShiftDetection();
}

#endif // ENABLE_PROXIMITY == 1

