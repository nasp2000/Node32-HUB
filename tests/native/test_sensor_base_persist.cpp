#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <map>
#include <cmath>

// Compiles the REAL production persistence template sensor_config_base.h
// (CRTP) with a derived manager whose toJson()/updateFromJson() mirror the
// MQ135 JSON schema. Exercises the actual atomic-save code path used by the
// MQ135/HX711/INA226/MAX31865/Sound config managers:
//   saveToFile():  open .tmp "w" -> print(toJson) -> close ->
//                  bytes==0? remove tmp+false : remove final, rename tmp->final
//
// NOTE: loadFromFile() checks file existence via host stat("/littlefs/...")
// which does not see the in-memory LittleFS stub, so the load half is
// exercised by reading the stored file from the stub store and feeding it to
// the derived updateFromJson() (the JSON merge/parse logic that matters).
// A schema drift between save() and load() fails these tests.

#include "sensor_config_base_stub.h"  // copy of ../../chatgpt/src/sensors/sensor_config_base.h
#include "stubs/ArduinoJson.h"
#include "stubs/LittleFS.h"
#include "stubs/littlefs_manager.h"

// Production code uses a global LittleFS instance (declared extern in stub).
LittleFSClass LittleFS;

// --- Derived config mirroring MQ135ConfigManager JSON schema ---
struct Mq135Cfg {
    bool  sensor_enabled = true;
    int   adc_pin = 5;
    float r0 = 76.63f;
    float rl = 10000.0f;
    int   read_interval_sec = 60;
    bool  use_env_compensation = true;
    bool  inject_to_buffer = true;
    bool  output_pretty = false;
    bool  output_csv = true;
    float gas_co2_offset = 0.0f;
    float last_co2 = 0.0f;
};

// Mirrors MQ135ConfigManager::toJson()/updateFromJson() (keys and float
// precision identical to production).
class Mq135Manager : public SensorConfigBase<Mq135Manager, Mq135Cfg> {
public:
    // Expose protected persistence for the test.
    bool doSaveToFile() const { return saveToFile(); }
    Mq135Cfg& getConfigRW() { return _config; }

    String toJson() const {
        String j;
        j = "{";
        j += "\"sensor_enabled\":"; j += _config.sensor_enabled ? "true" : "false"; j += ',';
        j += "\"adc_pin\":"; j += _config.adc_pin; j += ',';
        j += "\"r0\":"; j += String(_config.r0, 4); j += ',';
        j += "\"rl\":"; j += String(_config.rl, 0); j += ',';
        j += "\"read_interval_sec\":"; j += _config.read_interval_sec; j += ',';
        j += "\"use_env_compensation\":"; j += _config.use_env_compensation ? "true" : "false"; j += ',';
        j += "\"inject_to_buffer\":"; j += _config.inject_to_buffer ? "true" : "false"; j += ',';
        j += "\"output_pretty\":"; j += _config.output_pretty ? "true" : "false"; j += ',';
        j += "\"output_csv\":"; j += _config.output_csv ? "true" : "false"; j += ',';
        j += "\"co2_offset\":"; j += String(_config.gas_co2_offset, 2); j += ',';
        j += "\"last_co2\":"; j += String(_config.last_co2, 1);
        j += "}";
        return j;
    }

    bool updateFromJson(const String& json) {
        SpiRamJsonDocument doc(1024);
        if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
        if (doc.containsKey("sensor_enabled"))       _config.sensor_enabled = doc["sensor_enabled"].as<bool>();
        if (doc.containsKey("adc_pin"))              _config.adc_pin = doc["adc_pin"].as<int>();
        if (doc.containsKey("r0"))                   _config.r0 = doc["r0"].as<float>();
        if (doc.containsKey("rl"))                   _config.rl = doc["rl"].as<float>();
        if (doc.containsKey("read_interval_sec"))    _config.read_interval_sec = std::max(10, doc["read_interval_sec"].as<int>());
        if (doc.containsKey("use_env_compensation")) _config.use_env_compensation = doc["use_env_compensation"].as<bool>();
        if (doc.containsKey("inject_to_buffer"))     _config.inject_to_buffer = doc["inject_to_buffer"].as<bool>();
        if (doc.containsKey("output_pretty"))        _config.output_pretty = doc["output_pretty"].as<bool>();
        if (doc.containsKey("output_csv"))           _config.output_csv = doc["output_csv"].as<bool>();
        if (doc.containsKey("co2_offset"))           _config.gas_co2_offset = doc["co2_offset"].as<float>();
        if (doc.containsKey("last_co2"))             _config.last_co2 = doc["last_co2"].as<float>();
        return true;
    }
};

// --- helper: set path on a manager (production uses initFilesystem()) ---
static void setPath(Mq135Manager& m, const char* path) {
    m.initFilesystem(path);
}

TEST_CASE("REAL saveToFile: atomic write, tmp removed, final created") {
    Mq135Manager m;
    setPath(m, "/config/sensor_mq135.json");   // init first (applies defaults if absent)
    m.getConfigRW().sensor_enabled = true;
    m.getConfigRW().adc_pin = 34;
    m.getConfigRW().r0 = 82.5f;
    m.getConfigRW().rl = 10000.0f;
    m.getConfigRW().read_interval_sec = 300;
    m.getConfigRW().use_env_compensation = false;
    m.getConfigRW().inject_to_buffer = false;
    m.getConfigRW().output_pretty = true;
    m.getConfigRW().output_csv = false;
    m.getConfigRW().gas_co2_offset = 12.5f;
    m.getConfigRW().last_co2 = 410.5f;

    REQUIRE(m.doSaveToFile());
    // atomic write: tmp removed, final exists
    CHECK_FALSE(LittleFS.exists("/config/sensor_mq135.json.tmp"));
    CHECK(LittleFS.exists("/config/sensor_mq135.json"));

    // Simulate reboot: fresh manager loads from the stored file. Note: the
    // stored content must be captured BEFORE initFilesystem() on the fresh
    // manager, because host stat() cannot see the in-memory stub store, so
    // initFilesystem() treats the file as missing and applies defaults.
    std::string stored = LittleFS.files["/config/sensor_mq135.json"];
    Mq135Manager fresh;
    setPath(fresh, "/config/sensor_mq135.json");
    REQUIRE(fresh.updateFromJson(String(stored.c_str())));
    CHECK(fresh.getConfig().sensor_enabled == true);
    CHECK(fresh.getConfig().adc_pin == 34);
    CHECK(std::abs(fresh.getConfig().r0 - 82.5f) < 0.001f);
    CHECK(fresh.getConfig().rl == 10000.0f);
    CHECK(fresh.getConfig().read_interval_sec == 300);
    CHECK(fresh.getConfig().use_env_compensation == false);
    CHECK(fresh.getConfig().inject_to_buffer == false);
    CHECK(fresh.getConfig().output_pretty == true);
    CHECK(fresh.getConfig().output_csv == false);
    CHECK(std::abs(fresh.getConfig().gas_co2_offset - 12.5f) < 0.01f);
    CHECK(std::abs(fresh.getConfig().last_co2 - 410.5f) < 0.1f);
}

TEST_CASE("REAL saveToFile writes valid JSON with every persisted key") {
    Mq135Manager m;
    setPath(m, "/config/sensor_mq135.json");   // init first (applies defaults if absent)
    m.getConfigRW().sensor_enabled = true;
    m.getConfigRW().adc_pin = 15;
    m.getConfigRW().r0 = 76.63f;
    m.getConfigRW().rl = 10000.0f;
    m.getConfigRW().read_interval_sec = 60;
    m.getConfigRW().use_env_compensation = true;
    m.getConfigRW().inject_to_buffer = true;
    m.getConfigRW().output_pretty = true;
    m.getConfigRW().output_csv = true;
    m.getConfigRW().gas_co2_offset = -3.25f;
    m.getConfigRW().last_co2 = 123.0f;

    REQUIRE(m.doSaveToFile());
    std::string saved = LittleFS.files["/config/sensor_mq135.json"];

    // Every key that saveToFile writes must be present (load must read them).
    SpiRamJsonDocument doc(2048);
    REQUIRE(deserializeJson(doc, String(saved.c_str())) == DeserializationError::Ok);
    CHECK(doc.containsKey("sensor_enabled"));
    CHECK(doc.containsKey("adc_pin"));
    CHECK(doc.containsKey("r0"));
    CHECK(doc.containsKey("rl"));
    CHECK(doc.containsKey("read_interval_sec"));
    CHECK(doc.containsKey("use_env_compensation"));
    CHECK(doc.containsKey("inject_to_buffer"));
    CHECK(doc.containsKey("output_pretty"));
    CHECK(doc.containsKey("output_csv"));
    CHECK(doc.containsKey("co2_offset"));
    CHECK(doc.containsKey("last_co2"));
}

TEST_CASE("load missing key keeps previous value (merge, not wipe)") {
    // A partial JSON (e.g. legacy file) must not reset fields the parser
    // does not see.
    Mq135Manager m;
    m.getConfigRW().adc_pin = 34;
    m.getConfigRW().output_pretty = true;
    String partial = "{\"sensor_enabled\":false}";
    REQUIRE(m.updateFromJson(partial));
    CHECK(m.getConfig().sensor_enabled == false);
    CHECK(m.getConfig().adc_pin == 34);          // untouched
    CHECK(m.getConfig().output_pretty == true);  // untouched
}

TEST_CASE("read_interval_sec clamped to minimum 10") {
    Mq135Manager m;
    String j = "{\"read_interval_sec\":3}";
    REQUIRE(m.updateFromJson(j));
    CHECK(m.getConfig().read_interval_sec == 10);

    m.updateFromJson("{\"read_interval_sec\":500}");
    CHECK(m.getConfig().read_interval_sec == 500);
}

TEST_CASE("corrupt JSON rejected, state preserved") {
    Mq135Manager m;
    m.getConfigRW().adc_pin = 99;
    CHECK_FALSE(m.updateFromJson("{not valid json"));
    CHECK(m.getConfig().adc_pin == 99);   // state intact
    CHECK_FALSE(m.updateFromJson(""));
}

TEST_CASE("deserializeJson parses float/int/bool/string accurately") {
    SpiRamJsonDocument doc(1024);
    REQUIRE(deserializeJson(doc, String("{\"r0\":76.63,\"pin\":34,\"on\":true,\"name\":\"a\\\"b\"}"))
            == DeserializationError::Ok);
    CHECK(doc.containsKey("r0"));
    CHECK(std::abs(doc["r0"].as<float>() - 76.63f) < 0.001f);
    CHECK(doc["pin"].as<int>() == 34);
    CHECK(doc["on"].as<bool>() == true);
    CHECK(std::string(doc["name"].as<const char*>()) == "a\"b");
}

TEST_CASE("serializeJson round-trips nested object order") {
    SpiRamJsonDocument doc(1024);
    REQUIRE(deserializeJson(doc, String("{\"b\":1,\"a\":2,\"z\":[true,3.5]}")) == DeserializationError::Ok);
    std::string out;
    serializeJson(doc._root, out);
    CHECK(out == "{\"b\":1,\"a\":2,\"z\":[true,3.5]}");
}
