#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the config slot registry logic in
// ConfigManager::saveToLittleFS() (pages.reg) and loadFromLittleFS().
// The registry is a JSON array of {stem, max_bytes} written to /config/pages.reg.
// Tests verify: adding/removing slots, corrupt JSON fallback to hardcoded
// defaults, unknown slots ignored, and max_bytes enforcement.

namespace {

struct PageSlot {
    std::string stem;
    size_t maxBytes;
};

// Hardcoded defaults - mirrors the addPage(...) calls in saveToLittleFS()
const std::vector<PageSlot>& defaultSlots() {
    static const std::vector<PageSlot> slots = {
        {"core", 10240}, {"rs232", 8192}, {"recipients", 4096},
        {"schedule", 4096}, {"modbus", 4096}, {"access", 8192},
        {"system", 4096}, {"usb_server", 4096}, {"smtp_email", 4096},
        {"alerts", 4096}, {"telnet", 4096}, {"ethernet", 4096},
        {"sdcard", 4096}, {"debug", 4096}, {"access_point", 4096},
        {"wifi_hybrid", 4096}, {"do", 4096},
        {"sensor_config", 4096}, {"sensor_mq135", 4096}, {"sensor_hx711", 4096},
        {"sensor_ina226", 4096}, {"sensor_sound", 4096}, {"sensor_max31865", 4096},
        {"sensor_proximity", 4096}, {"sensor_prox_counters", 4096},
    };
    return slots;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string serializeRegistry(const std::vector<PageSlot>& slots) {
    std::string out = "{\"pages\":[";
    for (size_t i = 0; i < slots.size(); ++i) {
        if (i) out += ",";
        out += "{\"stem\":\"" + jsonEscape(slots[i].stem) +
               "\",\"max_bytes\":" + std::to_string(slots[i].maxBytes) + "}";
    }
    out += "]}";
    return out;
}

// Minimal JSON array-of-objects parser tolerant of the exact schema.
// Returns false on any malformed input (caller falls back to defaults).
bool parseRegistry(const std::string& json, std::vector<PageSlot>& out) {
    out.clear();
    size_t i = 0;
    auto skipWs = [&]() { while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i; };
    skipWs();
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    // find "pages":
    std::string key;
    skipWs();
    if (i >= json.size() || json[i] != '"') return false;
    ++i;
    while (i < json.size() && json[i] != '"') { key += json[i]; ++i; }
    if (i >= json.size()) return false;
    ++i; // closing quote
    skipWs();
    if (i >= json.size() || json[i] != ':') return false;
    ++i;
    skipWs();
    if (i >= json.size() || json[i] != '[') return false;
    ++i;
    skipWs();
    if (i < json.size() && json[i] == ']') return true; // empty pages array is valid
    while (i < json.size()) {
        skipWs();
        if (i >= json.size() || json[i] != '{') return false;
        ++i;
        PageSlot slot;
        bool sawStem = false, sawBytes = false;
        while (true) {
            skipWs();
            if (i >= json.size()) return false;
            if (json[i] == '}') { ++i; break; }
            if (json[i] != '"') return false;
            ++i;
            std::string k;
            while (i < json.size() && json[i] != '"') { k += json[i]; ++i; }
            if (i >= json.size()) return false;
            ++i;
            skipWs();
            if (i >= json.size() || json[i] != ':') return false;
            ++i;
            skipWs();
            if (k == "stem") {
                if (i >= json.size() || json[i] != '"') return false;
                ++i;
                while (i < json.size() && json[i] != '"') { slot.stem += json[i]; ++i; }
                if (i >= json.size()) return false;
                ++i;
                sawStem = true;
            } else if (k == "max_bytes") {
                size_t start = i;
                while (i < json.size() && json[i] >= '0' && json[i] <= '9') ++i;
                if (i == start) return false;
                slot.maxBytes = (size_t)std::stoul(json.substr(start, i - start));
                sawBytes = true;
            } else {
                return false; // unknown key corrupts the registry -> default
            }
            skipWs();
            if (i < json.size() && json[i] == ',') { ++i; continue; }
        }
        if (!sawStem || !sawBytes) return false;
        out.push_back(slot);
        skipWs();
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == ']') { ++i; break; }
        return false;
    }
    skipWs();
    if (i >= json.size() || json[i] != '}') return false;
    return true;
}

// Mirrors loadFromLittleFS(): registry missing/corrupt -> hardcoded defaults.
std::vector<PageSlot> loadRegistry(const std::string& json, bool fileExists) {
    if (!fileExists) return defaultSlots();
    std::vector<PageSlot> parsed;
    if (!parseRegistry(json, parsed)) return defaultSlots();
    return parsed;
}

// Mirrors writeJsonAtomic(): payload exceeding max_bytes is skipped.
bool writeSlot(const std::vector<PageSlot>& slots, const std::string& stem,
               const std::string& payload, std::string& error) {
    for (const PageSlot& s : slots) {
        if (s.stem == stem) {
            if (payload.size() > s.maxBytes) {
                error = "too large: " + std::to_string(payload.size()) + " > " + std::to_string(s.maxBytes);
                return false;
            }
            error.clear();
            return true;
        }
    }
    error = "unknown stem";
    return false;
}

} // namespace

TEST_CASE("serialize+parse round trip preserves all slots") {
    std::vector<PageSlot> parsed;
    REQUIRE(parseRegistry(serializeRegistry(defaultSlots()), parsed));
    REQUIRE(parsed.size() == defaultSlots().size());
    for (size_t i = 0; i < parsed.size(); ++i) {
        CHECK(parsed[i].stem == defaultSlots()[i].stem);
        CHECK(parsed[i].maxBytes == defaultSlots()[i].maxBytes);
    }
}

TEST_CASE("registry missing -> fallback to hardcoded defaults") {
    std::vector<PageSlot> slots = loadRegistry("", /*fileExists*/ false);
    CHECK(slots.size() == defaultSlots().size());
    CHECK(slots[0].stem == "core");
    CHECK(slots[0].maxBytes == 10240);
}

TEST_CASE("corrupt registry JSON -> fallback to defaults") {
    std::vector<PageSlot> slots = loadRegistry("{\"pages\":[", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("empty registry JSON is corrupt -> defaults") {
    std::vector<PageSlot> slots = loadRegistry("", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("unknown key in registry entry -> corrupt -> defaults") {
    std::vector<PageSlot> slots = loadRegistry("{\"pages\":[{\"stem\":\"core\",\"bogus\":1}]}", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("missing max_bytes field -> corrupt -> defaults") {
    std::vector<PageSlot> slots = loadRegistry("{\"pages\":[{\"stem\":\"core\"}]}", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("missing pages key -> corrupt -> defaults") {
    std::vector<PageSlot> slots = loadRegistry("{\"other\":1}", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("truncated registry mid-entry -> corrupt -> defaults") {
    std::vector<PageSlot> slots = loadRegistry("{\"pages\":[{\"stem\":\"core\",\"max_bytes\":1024}", /*fileExists*/ true);
    CHECK(slots.size() == defaultSlots().size());
}

TEST_CASE("valid registry with one slot is accepted") {
    std::string reg = "{\"pages\":[{\"stem\":\"core\",\"max_bytes\":10240}]}";
    std::vector<PageSlot> slots = loadRegistry(reg, /*fileExists*/ true);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].stem == "core");
    CHECK(slots[0].maxBytes == 10240);
}

TEST_CASE("adding a new stem is reflected in registry after round trip") {
    std::vector<PageSlot> slots = defaultSlots();
    slots.push_back({"sensor_new", 1024});
    std::vector<PageSlot> parsed;
    REQUIRE(parseRegistry(serializeRegistry(slots), parsed));
    REQUIRE(parsed.size() == defaultSlots().size() + 1);
    CHECK(parsed.back().stem == "sensor_new");
    CHECK(parsed.back().maxBytes == 1024);
}

TEST_CASE("payload under max_bytes is accepted") {
    std::string err;
    CHECK(writeSlot(defaultSlots(), "core", std::string(1024, 'x'), err));
    CHECK(err.empty());
}

TEST_CASE("payload over max_bytes is rejected") {
    std::string err;
    CHECK_FALSE(writeSlot(defaultSlots(), "core", std::string(10241, 'x'), err));
    CHECK(err.find("too large") != std::string::npos);
}

TEST_CASE("boundary: payload exactly max_bytes is accepted") {
    std::string err;
    CHECK(writeSlot(defaultSlots(), "sensor_prox_counters", std::string(4096, 'x'), err));
}

TEST_CASE("unknown stem rejected") {
    std::string err;
    CHECK_FALSE(writeSlot(defaultSlots(), "nonexistent", "x", err));
    CHECK(err == "unknown stem");
}

TEST_CASE("sensor slot max_bytes enforced per-slot") {
    std::string err;
    // core allows 10240, sensor_prox_counters only 4096
    CHECK(writeSlot(defaultSlots(), "sensor_prox_counters", std::string(4096, 'x'), err));
    CHECK_FALSE(writeSlot(defaultSlots(), "sensor_prox_counters", std::string(4097, 'x'), err));
    CHECK(writeSlot(defaultSlots(), "core", std::string(9000, 'x'), err));
}
