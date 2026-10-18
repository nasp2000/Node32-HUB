#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Arduino.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

// Standalone reimpl of proximity_manager.cpp persistence, mirroring the EXACT
// production JSON schema (proximity_manager.cpp:609-655 and 932-1005):
//   - counters: /config/sensor_prox_counters.json  array of {in,out,ti,to}
//   - config:   /config/sensor_proximity.json      array of {n,m,gi,go,gi2,db,gl,pd,pu,mo,tp,om}
//   - atomic write: write .tmp -> bytes>0 -> remove original -> rename
//   - load: merge-not-wipe (missing key keeps current value), pulse_dur clamp.

#define PROXIMITY_MAX_CHANNELS 8
#define PROX_DO_MAX_PULSE_MS 10000UL
#define PROX_CFG_PATH "/config/sensor_proximity.json"
#define PROX_CNT_PATH "/config/sensor_prox_counters.json"
#define PROX_MODE_COUNTER 0

struct ProximityChannel {
    char        name[32];
    uint8_t     gpio_in;
    uint8_t     gpio_out;
    uint8_t     gpio_in2;
    uint8_t     mode;
    bool        monitor_only;
    uint32_t    debounce_ms;
    uint32_t    count_in;
    uint32_t    count_out;
    uint32_t    total_in;
    uint32_t    total_out;
    uint32_t    goal;
    uint32_t    last_pulse_ms;
    uint32_t    cycle_time_ms;
    float       rate_ppm;
    bool        state;
    uint32_t    pulse_delay_ms;
    uint32_t    pulse_dur_ms;
    bool        do_active;
    uint32_t    do_start_ms;
    uint8_t     direction;
    uint32_t    dir_edge_ms_in;
    uint32_t    running_seconds;
    uint32_t    downtime_seconds;
    uint32_t    good_parts;
    float       target_ppm;
    float       oee_manual;
};

// Global channel state, same shape as production's `static ProximityChannel _ch[]`.
static ProximityChannel _ch[PROXIMITY_MAX_CHANNELS];
static uint32_t         _rateBaseCount[PROXIMITY_MAX_CHANNELS];
LittleFSClass LittleFS;

static void _resetChannels() {
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        memset(&_ch[i], 0, sizeof(_ch[i]));
        snprintf(_ch[i].name, sizeof(_ch[i].name), "Channel %u", (unsigned)i);
        _ch[i].gpio_in  = 255;
        _ch[i].gpio_out = 255;
        _ch[i].gpio_in2 = 255;
        _ch[i].mode     = PROX_MODE_COUNTER;
        _ch[i].debounce_ms = 50;
        _ch[i].pulse_dur_ms = 500;
        _rateBaseCount[i] = 0;
    }
}

// Mirror of production saveProximityCounters() (line 609).
static void saveProximityCounters() {
    SpiRamJsonDocument doc(512);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["in"]   = _ch[i].count_in;
        obj["out"]  = _ch[i].count_out;
        obj["ti"]   = _ch[i].total_in;
        obj["to"]   = _ch[i].total_out;
    }
    String cntTmp = String(PROX_CNT_PATH) + ".tmp";
    File f = LittleFS.open(cntTmp, "w");
    if (!f) return;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    if (bytes > 0) {
        LittleFS.remove(PROX_CNT_PATH);
        LittleFS.rename(cntTmp, PROX_CNT_PATH);
    } else {
        LittleFS.remove(cntTmp);
    }
}

// Mirror of production loadProximityCounters() (line 633).
static void loadProximityCounters() {
    File f = LittleFS.open(PROX_CNT_PATH, "r");
    if (!f) return;
    String content = f.readString();
    f.close();
    SpiRamJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, content);
    if (err != DeserializationError::Ok) return;

    JsonArray arr = doc.as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        _ch[i].count_in  = obj["in"]  | 0U;
        _ch[i].count_out = obj["out"] | 0U;
        _ch[i].total_in  = obj["ti"]  | 0U;
        _ch[i].total_out = obj["to"]  | 0U;
        _rateBaseCount[i] = _ch[i].count_in;
        i++;
    }
}

// Mirror of production saveProximityConfig() (line 968).
static bool saveProximityConfig() {
    SpiRamJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["n"]  = _ch[i].name;
        obj["m"]  = (uint8_t)_ch[i].mode;
        obj["gi"] = _ch[i].gpio_in;
        obj["go"] = _ch[i].gpio_out;
        obj["gi2"]= _ch[i].gpio_in2;
        obj["db"] = _ch[i].debounce_ms;
        obj["gl"] = _ch[i].goal;
        obj["pd"] = _ch[i].pulse_delay_ms;
        obj["pu"] = _ch[i].pulse_dur_ms;
        obj["mo"] = _ch[i].monitor_only;
        obj["tp"] = _ch[i].target_ppm;
        obj["om"] = _ch[i].oee_manual;
    }
    String cfgTmp = String(PROX_CFG_PATH) + ".tmp";
    File f = LittleFS.open(cfgTmp, "w");
    if (!f) return false;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    if (bytes > 0) {
        LittleFS.remove(PROX_CFG_PATH);
        LittleFS.rename(cfgTmp, PROX_CFG_PATH);
    } else {
        LittleFS.remove(cfgTmp);
        return false;
    }
    return true;
}

// Mirror of production loadProximityConfig() (line 932). Merge-not-wipe.
static void loadProximityConfig() {
    File f = LittleFS.open(PROX_CFG_PATH, "r");
    if (!f) return;
    String content = f.readString();
    f.close();
    SpiRamJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, content);
    if (err != DeserializationError::Ok) return;

    JsonArray arr = doc.as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : arr) {
        if (i >= PROXIMITY_MAX_CHANNELS) break;
        JsonObject obj = v.as<JsonObject>();
        const char* n = obj["n"] | "";
        strncpy(_ch[i].name, n, sizeof(_ch[i].name) - 1);
        _ch[i].name[sizeof(_ch[i].name) - 1] = '\0';
        _ch[i].mode           = obj["m"]  | _ch[i].mode;
        _ch[i].gpio_in        = obj["gi"] | _ch[i].gpio_in;
        _ch[i].gpio_out       = obj["go"] | _ch[i].gpio_out;
        _ch[i].gpio_in2       = (uint8_t)(obj["gi2"] | (int)_ch[i].gpio_in2);
        _ch[i].debounce_ms    = obj["db"] | _ch[i].debounce_ms;
        _ch[i].goal           = obj["gl"] | _ch[i].goal;
        _ch[i].pulse_delay_ms = obj["pd"] | _ch[i].pulse_delay_ms;
        _ch[i].pulse_dur_ms   = obj["pu"] | _ch[i].pulse_dur_ms;
        if (_ch[i].pulse_dur_ms > PROX_DO_MAX_PULSE_MS) _ch[i].pulse_dur_ms = PROX_DO_MAX_PULSE_MS;
        _ch[i].monitor_only   = obj["mo"] | false;
        _ch[i].target_ppm     = obj["tp"] | 0.0f;
        _ch[i].oee_manual     = obj["om"] | 0.0f;
        i++;
    }
}

// --- JSON-schema invariants -------------------------------------------------

TEST_CASE("Counters file uses exact production JSON schema") {
    _resetChannels();
    LittleFS.files.clear();

    _ch[0].count_in  = 12345;
    _ch[0].count_out = 67;
    _ch[0].total_in  = 99999;
    _ch[0].total_out = 88;
    _ch[7].count_in  = 1;
    _ch[7].count_out = 2;
    _ch[7].total_in  = 3;
    _ch[7].total_out = 4;

    saveProximityCounters();

    CHECK(LittleFS.exists(PROX_CNT_PATH));
    CHECK_FALSE(LittleFS.exists(String(PROX_CNT_PATH) + ".tmp"));
    const std::string& raw = LittleFS.files[PROX_CNT_PATH];
    std::string hay = raw;
    CHECK(hay.find("\"in\": 12345") != std::string::npos);
    CHECK(hay.find("\"out\": 67") != std::string::npos);
    CHECK(hay.find("\"ti\": 99999") != std::string::npos);
    CHECK(hay.find("\"to\": 88") != std::string::npos);
    CHECK(hay.find("\"in\": 1") != std::string::npos);
    CHECK(hay.find("\"out\": 2") != std::string::npos);
    CHECK(hay.find("\"ti\": 3") != std::string::npos);
    CHECK(hay.find("\"to\": 4") != std::string::npos);
    CHECK(hay.find("\"name\"") == std::string::npos); // counters have no name key
}

TEST_CASE("Config file uses exact production JSON schema with all 12 keys") {
    _resetChannels();
    LittleFS.files.clear();

    _ch[0].name[0] = 'E';
    _ch[0].name[1] = 's';
    _ch[0].name[2] = 't';
    _ch[0].name[3] = '\0';
    _ch[0].mode = 2;
    _ch[0].gpio_in = 5;
    _ch[0].gpio_out = 255;
    _ch[0].gpio_in2 = 255;
    _ch[0].debounce_ms = 50;
    _ch[0].goal = 10000;
    _ch[0].pulse_delay_ms = 100;
    _ch[0].pulse_dur_ms = 500;
    _ch[0].monitor_only = true;
    _ch[0].target_ppm = 60.0f;
    _ch[0].oee_manual = 85.5f;

    CHECK(saveProximityConfig());

    CHECK(LittleFS.exists(PROX_CFG_PATH));
    CHECK_FALSE(LittleFS.exists(String(PROX_CFG_PATH) + ".tmp"));
    const std::string& raw = LittleFS.files[PROX_CFG_PATH];
    CHECK(raw.find("\"n\": \"Est\"") != std::string::npos);
    CHECK(raw.find("\"m\": 2") != std::string::npos);
    CHECK(raw.find("\"gi\": 5") != std::string::npos);
    CHECK(raw.find("\"go\": 255") != std::string::npos);
    CHECK(raw.find("\"gi2\": 255") != std::string::npos);
    CHECK(raw.find("\"db\": 50") != std::string::npos);
    CHECK(raw.find("\"gl\": 10000") != std::string::npos);
    CHECK(raw.find("\"pd\": 100") != std::string::npos);
    CHECK(raw.find("\"pu\": 500") != std::string::npos);
    CHECK(raw.find("\"mo\": true") != std::string::npos);
    CHECK(raw.find("\"tp\": 60") != std::string::npos);
    CHECK(raw.find("\"om\": 85.5") != std::string::npos);
}

TEST_CASE("Counters round-trip: all 8 channels preserved exactly") {
    _resetChannels();
    LittleFS.files.clear();
    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        _ch[i].count_in  = 0xFFFFFFFFu - i;
        _ch[i].count_out = 1000u + i;
        _ch[i].total_in  = 0x7FFFFFFFu;
        _ch[i].total_out = 0u;
    }
    saveProximityCounters();

    _resetChannels(); // "reboot": wipe RAM state
    loadProximityCounters();

    for (uint8_t i = 0; i < PROXIMITY_MAX_CHANNELS; i++) {
        CHECK(_ch[i].count_in  == 0xFFFFFFFFu - i);
        CHECK(_ch[i].count_out == 1000u + i);
        CHECK(_ch[i].total_in  == 0x7FFFFFFFu);
        CHECK(_ch[i].total_out == 0u);
        CHECK(_rateBaseCount[i] == _ch[i].count_in); // rate baseline sync
    }
}

TEST_CASE("Config round-trip: all 12 keys preserved across reboot") {
    _resetChannels();
    LittleFS.files.clear();
    _ch[3].mode = 1;
    _ch[3].gpio_in = 16;
    _ch[3].gpio_out = 17;
    _ch[3].gpio_in2 = 18;
    _ch[3].debounce_ms = 25;
    _ch[3].goal = 5000;
    _ch[3].pulse_delay_ms = 200;
    _ch[3].pulse_dur_ms = 1500;
    _ch[3].monitor_only = true;
    _ch[3].target_ppm = 42.25f;
    _ch[3].oee_manual = 0.0f;
    strcpy(_ch[3].name, "Linha A");

    CHECK(saveProximityConfig());

    _resetChannels();
    loadProximityConfig();

    CHECK(strcmp(_ch[3].name, "Linha A") == 0);
    CHECK(_ch[3].mode == 1);
    CHECK(_ch[3].gpio_in == 16);
    CHECK(_ch[3].gpio_out == 17);
    CHECK(_ch[3].gpio_in2 == 18);
    CHECK(_ch[3].debounce_ms == 25);
    CHECK(_ch[3].goal == 5000);
    CHECK(_ch[3].pulse_delay_ms == 200);
    CHECK(_ch[3].pulse_dur_ms == 1500);
    CHECK(_ch[3].monitor_only == true);
    CHECK(std::abs(_ch[3].target_ppm - 42.25f) < 0.01f);
    CHECK(std::abs(_ch[3].oee_manual - 0.0f) < 0.01f);
}

TEST_CASE("Load counters: missing file keeps defaults") {
    _resetChannels();
    LittleFS.files.clear();
    _ch[0].count_in = 77;
    loadProximityCounters(); // no file -> no-op
    CHECK(_ch[0].count_in == 77);
    CHECK(_ch[1].count_in == 0);
}

TEST_CASE("Load counters: corrupt JSON is rejected, state untouched") {
    _resetChannels();
    LittleFS.files.clear();
    _ch[0].count_in = 55;
    LittleFS.files[PROX_CNT_PATH] = "{ not valid json";
    loadProximityCounters();
    CHECK(_ch[0].count_in == 55); // load aborted, current value kept
}

TEST_CASE("Load counters: partial entries merge, missing keys default to 0") {
    _resetChannels();
    LittleFS.files.clear();
    // Only channel 1 present (index 0 empty object), only "in" key set.
    LittleFS.files[PROX_CNT_PATH] = "[{},{\"in\":42}]";
    loadProximityCounters();
    CHECK(_ch[1].count_in == 42);
    CHECK(_ch[1].count_out == 0);
    CHECK(_ch[1].total_in == 0);
    CHECK(_ch[1].total_out == 0);
    CHECK(_ch[0].count_in == 0); // absent channels stay default
    CHECK(_rateBaseCount[1] == 42);
}

TEST_CASE("Load config: merge-not-wipe (missing numeric key keeps current value)") {
    _resetChannels();
    LittleFS.files.clear();
    _ch[0].pulse_dur_ms = 9999;   // pre-existing runtime value
    _ch[0].gpio_in = 44;          // not in file -> must survive
    _ch[0].mode = 3;              // not in file -> must survive

    // File only overrides debounce, omits everything else.
    LittleFS.files[PROX_CFG_PATH] = "[{\"db\":77}]";

    loadProximityConfig();

    CHECK(_ch[0].debounce_ms == 77);
    CHECK(_ch[0].gpio_in == 44);      // untouched (merged, not wiped)
    CHECK(_ch[0].mode == 3);          // untouched
    CHECK(_ch[0].pulse_dur_ms == 9999); // untouched
    // Note: name fallback is "" in production, so a missing "n" wipes the label.
    CHECK(strcmp(_ch[0].name, "") == 0);
}

TEST_CASE("Load config: corrupt JSON is rejected, defaults kept") {
    _resetChannels();
    LittleFS.files.clear();
    LittleFS.files[PROX_CFG_PATH] = "this is not json";
    loadProximityConfig();
    CHECK(_ch[0].mode == PROX_MODE_COUNTER);
    CHECK(_ch[0].debounce_ms == 50);
    CHECK(strcmp(_ch[0].name, "Channel 0") == 0);
}

TEST_CASE("Load config: pulse_dur clamps to PROX_DO_MAX_PULSE_MS") {
    _resetChannels();
    LittleFS.files.clear();
    LittleFS.files[PROX_CFG_PATH] = "[{\"pu\":99999999}]";
    loadProximityConfig();
    CHECK(_ch[0].pulse_dur_ms == PROX_DO_MAX_PULSE_MS);

    LittleFS.files[PROX_CFG_PATH] = "[{\"pu\":5000}]";
    loadProximityConfig();
    CHECK(_ch[0].pulse_dur_ms == 5000); // under limit untouched
}

TEST_CASE("Atomic write: no .tmp leftover after successful save") {
    _resetChannels();
    LittleFS.files.clear();
    CHECK(saveProximityConfig());
    CHECK(LittleFS.exists(PROX_CFG_PATH));
    CHECK_FALSE(LittleFS.exists(String(PROX_CFG_PATH) + ".tmp"));
    CHECK(LittleFS.files.count(PROX_CFG_PATH) == 1);
}

TEST_CASE("Atomic write: failed open leaves nothing behind") {
    _resetChannels();
    LittleFS.files.clear();
    _ch[0].count_in = 5;
    saveProximityCounters();
    CHECK(LittleFS.exists(PROX_CNT_PATH));
    // original still readable
    loadProximityCounters();
    CHECK(_ch[0].count_in == 5);
}

// ============================================================================
// Shift config persistence — mirrors proximity_shift_config.cpp EXACTLY
// (schema /config/sensor_prox_shift.json):
//   save: {en, ar, ns, em, s:[{h,m},...]}
//   load: en/ar/em/s/ns with nSlots fallback
// ============================================================================

#define PROX_SHIFT_CFG_PATH "/config/sensor_prox_shift.json"

struct ProximityShiftSlot {
    uint8_t hour;
    uint8_t minute;
};

struct ProximityShiftConfig {
    bool                enabled;
    bool                auto_reset_on_shift;
    uint8_t             nSlots;
    ProximityShiftSlot  slots[3];
    char                shiftday_emails[128];
};

static ProximityShiftConfig _shiftCfg = {
    false, false, 3, {{6, 0}, {14, 0}, {22, 0}}
};

static void resetShiftCfg() {
    _shiftCfg.enabled = false;
    _shiftCfg.auto_reset_on_shift = false;
    _shiftCfg.nSlots = 3;
    _shiftCfg.slots[0] = {6, 0};
    _shiftCfg.slots[1] = {14, 0};
    _shiftCfg.slots[2] = {22, 0};
    _shiftCfg.shiftday_emails[0] = '\0';
}

// Mirror of production saveProximityShiftConfig() (proximity_shift_config.cpp:63).
// Uses doc.createNestedArray("s") in production; here doc["s"].to<JsonArray>()
// which the stub maps to the same JSON schema.
static bool saveProximityShiftConfig() {
    SpiRamJsonDocument doc(512);
    doc["en"] = _shiftCfg.enabled;
    doc["ar"] = _shiftCfg.auto_reset_on_shift;
    doc["ns"] = _shiftCfg.nSlots;
    doc["em"] = _shiftCfg.shiftday_emails;
    JsonArray arr = doc["s"].to<JsonArray>();
    for (uint8_t i = 0; i < _shiftCfg.nSlots; i++) {
        JsonObject o = arr.createNestedObject();
        o["h"] = _shiftCfg.slots[i].hour;
        o["m"] = _shiftCfg.slots[i].minute;
    }
    String shiftTmp = String(PROX_SHIFT_CFG_PATH) + ".tmp";
    File f = LittleFS.open(shiftTmp, "w");
    if (!f) return false;
    size_t bytes = serializeJsonPretty(doc, f);
    f.close();
    if (bytes > 0) {
        LittleFS.remove(PROX_SHIFT_CFG_PATH);
        LittleFS.rename(shiftTmp, PROX_SHIFT_CFG_PATH);
    } else {
        LittleFS.remove(shiftTmp);
        return false;
    }
    return true;
}

// Mirror of production loadProximityShiftConfig() (proximity_shift_config.cpp:28).
static void loadProximityShiftConfig() {
    File f = LittleFS.open(PROX_SHIFT_CFG_PATH, "r");
    if (!f) return;
    String content = f.readString();
    f.close();
    SpiRamJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, content);
    if (err != DeserializationError::Ok) return;

    _shiftCfg.enabled             = doc["en"] | false;
    _shiftCfg.auto_reset_on_shift = doc["ar"] | false;

    const char* em = doc["em"] | "";
    strncpy(_shiftCfg.shiftday_emails, em, sizeof(_shiftCfg.shiftday_emails) - 1);
    _shiftCfg.shiftday_emails[sizeof(_shiftCfg.shiftday_emails) - 1] = '\0';

    JsonArray arr = doc["s"].as<JsonArray>();
    _shiftCfg.nSlots = 0;
    for (JsonVariant v : arr) {
        if (_shiftCfg.nSlots >= 3) break;
        JsonObject o = v.as<JsonObject>();
        _shiftCfg.slots[_shiftCfg.nSlots].hour   = o["h"] | 6;
        _shiftCfg.slots[_shiftCfg.nSlots].minute = o["m"] | 0;
        _shiftCfg.nSlots++;
    }
    uint8_t savedNSlots = doc["ns"] | 0;
    if (_shiftCfg.nSlots == 0) {
        _shiftCfg.nSlots = (savedNSlots > 0 && savedNSlots <= 3) ? savedNSlots : 3;
    }
}

TEST_CASE("Shift config: save uses exact production JSON schema") {
    resetShiftCfg();
    LittleFS.files.clear();

    _shiftCfg.enabled = true;
    _shiftCfg.auto_reset_on_shift = true;
    _shiftCfg.nSlots = 2;
    _shiftCfg.slots[0] = {7, 30};
    _shiftCfg.slots[1] = {19, 0};
    strcpy(_shiftCfg.shiftday_emails, "shift@test.com");

    CHECK(saveProximityShiftConfig());
    CHECK(LittleFS.exists(PROX_SHIFT_CFG_PATH));
    CHECK_FALSE(LittleFS.exists(String(PROX_SHIFT_CFG_PATH) + ".tmp"));
    const std::string& raw = LittleFS.files[PROX_SHIFT_CFG_PATH];
    CHECK(raw.find("\"en\": true") != std::string::npos);
    CHECK(raw.find("\"ar\": true") != std::string::npos);
    CHECK(raw.find("\"ns\": 2") != std::string::npos);
    CHECK(raw.find("\"em\": \"shift@test.com\"") != std::string::npos);
    CHECK(raw.find("\"h\": 7") != std::string::npos);
    CHECK(raw.find("\"m\": 30") != std::string::npos);
    CHECK(raw.find("\"h\": 19") != std::string::npos);
    CHECK(raw.find("\"m\": 0") != std::string::npos);
}

TEST_CASE("Shift config: round-trip across reboot preserves all fields") {
    resetShiftCfg();
    LittleFS.files.clear();

    _shiftCfg.enabled = true;
    _shiftCfg.auto_reset_on_shift = false;
    _shiftCfg.nSlots = 3;
    _shiftCfg.slots[0] = {6, 0};
    _shiftCfg.slots[1] = {14, 15};
    _shiftCfg.slots[2] = {22, 45};
    strcpy(_shiftCfg.shiftday_emails, "a@b.com, c@d.com");

    CHECK(saveProximityShiftConfig());

    resetShiftCfg();  // "reboot": wipe RAM state
    loadProximityShiftConfig();

    CHECK(_shiftCfg.enabled == true);
    CHECK(_shiftCfg.auto_reset_on_shift == false);
    CHECK(_shiftCfg.nSlots == 3);
    CHECK(_shiftCfg.slots[0].hour == 6);
    CHECK(_shiftCfg.slots[0].minute == 0);
    CHECK(_shiftCfg.slots[1].hour == 14);
    CHECK(_shiftCfg.slots[1].minute == 15);
    CHECK(_shiftCfg.slots[2].hour == 22);
    CHECK(_shiftCfg.slots[2].minute == 45);
    CHECK(std::string(_shiftCfg.shiftday_emails) == "a@b.com, c@d.com");
}

TEST_CASE("Shift config: 2-slot config round-trips exactly (user edits slots)") {
    resetShiftCfg();
    LittleFS.files.clear();

    _shiftCfg.enabled = true;
    _shiftCfg.auto_reset_on_shift = true;
    _shiftCfg.nSlots = 2;
    _shiftCfg.slots[0] = {7, 30};
    _shiftCfg.slots[1] = {19, 0};

    CHECK(saveProximityShiftConfig());
    resetShiftCfg();
    loadProximityShiftConfig();

    CHECK(_shiftCfg.nSlots == 2);
    CHECK(_shiftCfg.slots[0].hour == 7);
    CHECK(_shiftCfg.slots[0].minute == 30);
    CHECK(_shiftCfg.slots[1].hour == 19);
    CHECK(_shiftCfg.slots[1].minute == 0);
    // Slots beyond nSlots must not resurrect from defaults
    CHECK(_shiftCfg.enabled == true);
}

TEST_CASE("Shift config: missing file keeps defaults") {
    resetShiftCfg();
    LittleFS.files.clear();
    _shiftCfg.enabled = true;
    loadProximityShiftConfig();  // no file -> no-op
    CHECK(_shiftCfg.enabled == true);
    CHECK(_shiftCfg.nSlots == 3);
}

TEST_CASE("Shift config: corrupt JSON rejected, state untouched") {
    resetShiftCfg();
    LittleFS.files.clear();
    _shiftCfg.enabled = true;
    _shiftCfg.nSlots = 1;
    LittleFS.files[PROX_SHIFT_CFG_PATH] = "{ not valid json";
    loadProximityShiftConfig();
    CHECK(_shiftCfg.enabled == true);
    CHECK(_shiftCfg.nSlots == 1);
}

TEST_CASE("Shift config: nSlots fallback when slots array empty") {
    resetShiftCfg();
    LittleFS.files.clear();
    LittleFS.files[PROX_SHIFT_CFG_PATH] = "{\"en\":true,\"ar\":false,\"ns\":2,\"s\":[],\"em\":\"\"}";
    loadProximityShiftConfig();
    CHECK(_shiftCfg.enabled == true);
    CHECK(_shiftCfg.nSlots == 2);  // ns fallback used
}

TEST_CASE("Shift config: atomic write leaves no .tmp after save") {
    resetShiftCfg();
    LittleFS.files.clear();
    CHECK(saveProximityShiftConfig());
    CHECK(LittleFS.exists(PROX_SHIFT_CFG_PATH));
    CHECK_FALSE(LittleFS.exists(String(PROX_SHIFT_CFG_PATH) + ".tmp"));
    CHECK(LittleFS.files.count(PROX_SHIFT_CFG_PATH) == 1);
}
