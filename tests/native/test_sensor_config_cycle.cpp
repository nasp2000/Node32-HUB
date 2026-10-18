#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "MockConfigStore.h"

#include <string>
#include <sstream>
#include <iomanip>

// Standalone round-trip test for the sensor config persistence pattern used by
// MQ135ConfigManager (and the other sensor managers: HX711, INA226, Sound,
// MAX31865, Proximity). Mirrors:
//   - toJson(): manual JSON build with controlled float precision
//   - updateFromJson(): only-present-keys merge (ArduinoJson containsKey + as<>)
//   - storage via MockConfigStore (stands in for LittleFS)
// The pattern is identical across sensors; testing one validates all.
namespace {

struct GasConfig {
    bool enabled = false;
    float offset = 0.0f;
    bool inject = false;
};

struct Mq135Config {
    bool sensor_enabled = true;
    int  adc_pin = 5;
    float r0 = 180.0f;
    float rl = 20.0f;
    int  read_interval_sec = 30;
    bool use_env_compensation = false;
    bool inject_to_buffer = false;
    bool output_pretty = true;
    bool output_csv = true;
    GasConfig gas_co2;
    GasConfig gas_co;
    GasConfig gas_aqi;
};

// Mirrors MQ135ConfigManager::toJson() — manual build, String(v,d) formatting.
std::string toJson(const Mq135Config& c) {
    std::ostringstream j;
    j << "{";
    auto bv = [&](const char* k, bool v) { j << "\"" << k << "\":" << (v ? "true" : "false"); };
    auto nv = [&](const char* k, float v, int d) { j << "\"" << k << "\":" << std::fixed << std::setprecision(d) << v; };
    auto iv = [&](const char* k, int v) { j << "\"" << k << "\":" << v; };
    auto sep = [&]() { j << ","; };

    bv("sensor_enabled", c.sensor_enabled); sep();
    iv("adc_pin", c.adc_pin); sep();
    nv("r0", c.r0, 4); sep();
    nv("rl", c.rl, 0); sep();
    iv("read_interval_sec", c.read_interval_sec); sep();
    bv("use_env_compensation", c.use_env_compensation); sep();
    bv("inject_to_buffer", c.inject_to_buffer); sep();
    bv("output_pretty", c.output_pretty); sep();
    bv("output_csv", c.output_csv); sep();

    auto addGas = [&](const char* prefix, const GasConfig& g, bool comma) {
        j << "\"" << prefix << "_en\":" << (g.enabled ? "true" : "false") << ",";
        j << "\"" << prefix << "_offset\":" << std::fixed << std::setprecision(2) << g.offset << ",";
        j << "\"" << prefix << "_inject\":" << (g.inject ? "true" : "false");
        if (comma) j << ",";
    };
    addGas("co2", c.gas_co2, true);
    addGas("co", c.gas_co, true);
    addGas("aqi", c.gas_aqi, false);

    j << "}";
    return j.str();
}

// Minimal ArduinoJson-style accessor: returns the value for a key as a string,
// or empty if absent. Handles the JSON produced by toJson() (no nesting, no
// arrays, no escapes). Mirrors containsKey() + as<T>() semantics.
bool jsonGet(const std::string& json, const std::string& key, std::string& out) {
    std::string pat = "\"" + key + "\":";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p += pat.size();
    size_t end = json.find_first_of(",}", p);
    if (end == std::string::npos) end = json.size();
    out = json.substr(p, end - p);
    // strip surrounding quotes for strings
    if (out.size() >= 2 && out.front() == '"' && out.back() == '"') {
        out = out.substr(1, out.size() - 2);
    }
    return true;
}

// Mirrors MQ135ConfigManager::updateFromJson() — merges only keys present.
bool updateFromJson(Mq135Config& c, const std::string& json) {
    std::string v;
    if (jsonGet(json, "sensor_enabled", v))       c.sensor_enabled = (v == "true");
    if (jsonGet(json, "adc_pin", v))              c.adc_pin = std::stoi(v);
    if (jsonGet(json, "r0", v))                   c.r0 = std::stof(v);
    if (jsonGet(json, "rl", v))                   c.rl = std::stof(v);
    if (jsonGet(json, "read_interval_sec", v))    c.read_interval_sec = std::max(10, std::stoi(v));
    if (jsonGet(json, "use_env_compensation", v)) c.use_env_compensation = (v == "true");
    if (jsonGet(json, "inject_to_buffer", v))     c.inject_to_buffer = (v == "true");
    if (jsonGet(json, "output_pretty", v))        c.output_pretty = (v == "true");
    if (jsonGet(json, "output_csv", v))           c.output_csv = (v == "true");

    auto gas = [&](const char* prefix, GasConfig& g) {
        std::string en = std::string(prefix) + "_en";
        std::string off = std::string(prefix) + "_offset";
        std::string inj = std::string(prefix) + "_inject";
        std::string t;
        if (jsonGet(json, en, t))  g.enabled = (t == "true");
        if (jsonGet(json, off, t)) g.offset = std::stof(t);
        if (jsonGet(json, inj, t)) g.inject = (t == "true");
    };
    gas("co2", c.gas_co2);
    gas("co", c.gas_co);
    gas("aqi", c.gas_aqi);
    return true;
}

// Full save->load round trip through the mock store.
bool saveLoadRoundTrip(MockConfigStore& store, const std::string& key,
                       const Mq135Config& in, Mq135Config& out) {
    std::string json = toJson(in);
    store.setRaw(key, json);
    out = Mq135Config();   // start from defaults
    std::string loaded;
    if (!store.load(key, loaded)) return false;
    updateFromJson(out, loaded);
    return true;
}

} // namespace

TEST_CASE("save then load round-trip preserves all values") {
    MockConfigStore store;
    Mq135Config in;
    in.sensor_enabled = true;
    in.adc_pin = 34;
    in.r0 = 150.5f;
    in.rl = 20.0f;
    in.read_interval_sec = 60;
    in.use_env_compensation = true;
    in.inject_to_buffer = true;
    in.output_pretty = false;
    in.output_csv = true;
    in.gas_co2.enabled = true; in.gas_co2.offset = 12.34f; in.gas_co2.inject = true;
    in.gas_aqi.enabled = true; in.gas_aqi.offset = -5.5f;

    Mq135Config out;
    REQUIRE(saveLoadRoundTrip(store, "mq135", in, out));
    CHECK(out.sensor_enabled == in.sensor_enabled);
    CHECK(out.adc_pin == in.adc_pin);
    CHECK(std::abs(out.r0 - in.r0) < 0.0001f);
    CHECK(out.read_interval_sec == in.read_interval_sec);
    CHECK(out.use_env_compensation == in.use_env_compensation);
    CHECK(out.inject_to_buffer == in.inject_to_buffer);
    CHECK(out.output_pretty == in.output_pretty);
    CHECK(out.output_csv == in.output_csv);
    CHECK(out.gas_co2.enabled == in.gas_co2.enabled);
    CHECK(std::abs(out.gas_co2.offset - in.gas_co2.offset) < 0.001f);
    CHECK(out.gas_co2.inject == in.gas_co2.inject);
    CHECK(out.gas_aqi.enabled == in.gas_aqi.enabled);
    CHECK(std::abs(out.gas_aqi.offset - in.gas_aqi.offset) < 0.001f);
}

TEST_CASE("load without prior save falls back to defaults") {
    MockConfigStore store;
    Mq135Config out;
    std::string loaded;
    CHECK_FALSE(store.load("mq135", loaded));   // nothing saved
    updateFromJson(out, "{}");                  // empty JSON = all defaults kept
    CHECK(out.sensor_enabled == true);
    CHECK(out.adc_pin == 5);
    CHECK(std::abs(out.r0 - 180.0f) < 0.0001f);
    CHECK(out.read_interval_sec == 30);
    CHECK(out.gas_co2.enabled == false);
}

TEST_CASE("partial JSON merges only the present fields") {
    MockConfigStore store;
    Mq135Config out;
    out.adc_pin = 99;                 // pre-existing value (like a running device)
    out.gas_co2.enabled = true;
    std::string partial = "{\"sensor_enabled\":false,\"r0\":200.0}";
    updateFromJson(out, partial);
    CHECK(out.sensor_enabled == false);          // updated
    CHECK(std::abs(out.r0 - 200.0f) < 0.0001f);  // updated
    CHECK(out.adc_pin == 99);                    // untouched
    CHECK(out.gas_co2.enabled == true);          // untouched
    CHECK(out.read_interval_sec == 30);          // default untouched
}

TEST_CASE("read_interval_sec is clamped to minimum 10") {
    MockConfigStore store;
    Mq135Config out;
    updateFromJson(out, "{\"read_interval_sec\":1}");
    CHECK(out.read_interval_sec == 10);
    updateFromJson(out, "{\"read_interval_sec\":0}");
    CHECK(out.read_interval_sec == 10);
    updateFromJson(out, "{\"read_interval_sec\":-5}");
    CHECK(out.read_interval_sec == 10);
    updateFromJson(out, "{\"read_interval_sec\":42}");
    CHECK(out.read_interval_sec == 42);
}

TEST_CASE("boundary values survive the round trip") {
    MockConfigStore store;
    Mq135Config in;
    in.adc_pin = 0;
    in.read_interval_sec = 2147483647;   // INT_MAX
    in.r0 = 0.0001f;
    in.rl = -1.0f;
    in.use_env_compensation = false;
    in.gas_co2.offset = 999.99f;

    Mq135Config out;
    REQUIRE(saveLoadRoundTrip(store, "mq135", in, out));
    CHECK(out.adc_pin == 0);
    CHECK(out.read_interval_sec == 2147483647);
    CHECK(std::abs(out.r0 - 0.0001f) < 0.0001f);
    CHECK(std::abs(out.rl - -1.0f) < 0.0001f);
    CHECK(std::abs(out.gas_co2.offset - 999.99f) < 0.01f);
}

TEST_CASE("float precision is preserved per-field (r0=4dp, gas offsets=2dp)") {
    // Mirrors production: r0 uses String(v,4), gas offsets use String(v,2).
    MockConfigStore store;
    Mq135Config in;
    in.r0 = 123.456f;          // 4-decimal field
    in.gas_co.offset = 0.125f; // 2-decimal field -> rounds to 0.12 in JSON
    in.gas_aqi.offset = 3.99f; // exact at 2 decimals

    Mq135Config out;
    REQUIRE(saveLoadRoundTrip(store, "mq135", in, out));
    CHECK(std::abs(out.r0 - 123.456f) < 0.0005f);
    CHECK(std::abs(out.gas_co.offset - 0.12f) < 0.001f);   // 0.125 stored as 0.12
    CHECK(std::abs(out.gas_aqi.offset - 3.99f) < 0.001f);
}

TEST_CASE("multiple sensors use independent storage keys") {
    MockConfigStore store;
    Mq135Config mq135;
    mq135.adc_pin = 34;
    Mq135Config hx711;   // same struct shape, different logical sensor
    hx711.adc_pin = 35;

    store.setRaw("mq135", toJson(mq135));
    store.setRaw("hx711", toJson(hx711));

    Mq135Config outM, outH;
    std::string loaded;
    REQUIRE(store.load("mq135", loaded)); updateFromJson(outM, loaded);
    REQUIRE(store.load("hx711", loaded)); updateFromJson(outH, loaded);
    CHECK(outM.adc_pin == 34);
    CHECK(outH.adc_pin == 35);
    CHECK(store.size() == 2);
}

TEST_CASE("store remove clears the sensor config") {
    MockConfigStore store;
    store.setRaw("mq135", toJson(Mq135Config()));
    CHECK(store.exists("mq135"));
    CHECK(store.remove("mq135"));
    CHECK_FALSE(store.exists("mq135"));
    std::string gone;
    CHECK_FALSE(store.load("mq135", gone));
}
