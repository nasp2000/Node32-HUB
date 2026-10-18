#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>
#include <cstdint>

// Standalone reimplementation of the DO config logic added for the
// Waveshare 8DI8DO board (TCA9554):
//   - handlers/do_handler.cpp POST /api/do/config (name truncation, invert cast)
//   - tca9554_do driver (physical = logical ^ invert)
//   - page_do_asset.h + page_proximity_asset.h JS (label fallback, invert bit)
// The production code uses ArduinoJson + String; here we mirror only the pure
// decision logic so it can run on the host.

namespace {

constexpr int DO_CHANNELS = 8;
constexpr int DO_NAME_MAX_LEN = 16;

// --- mirrors POST /api/do/config handler ---
// For each channel: take names[i] when present, else ""; truncate to 16 chars.
std::vector<std::string> normalizeNames(const std::vector<std::string>& input) {
    std::vector<std::string> out;
    for (int i = 0; i < DO_CHANNELS; i++) {
        std::string nm = (i < (int)input.size()) ? input[i] : std::string();
        if (nm.length() > DO_NAME_MAX_LEN) nm = nm.substr(0, DO_NAME_MAX_LEN);
        out.push_back(nm);
    }
    return out;
}

// --- mirrors tca9554SetAll(): physical bus value is logical mask XOR invert ---
uint8_t physicalMask(uint8_t logical, uint8_t invert) {
    return (uint8_t)(logical ^ invert);
}

// --- mirrors page_do_asset.h applyDoNames()/loadDoConfig() label fallback ---
std::string channelLabel(int index, const std::string& name) {
    return name.length() ? name : "Relay " + std::to_string(index + 1);
}

// --- mirrors page_do_asset.h loadDoConfig() invert bit extraction ---
bool invertBit(uint8_t invert, int i) {
    return !!((invert >> i) & 1);
}

// --- mirrors page_do_asset.h saveDoConfig(): trim + substring(0,16) ---
std::string jsNameValue(const std::string& raw) {
    size_t b = raw.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = raw.find_last_not_of(" \t\r\n");
    std::string t = raw.substr(b, e - b + 1);
    if (t.length() > DO_NAME_MAX_LEN) t = t.substr(0, DO_NAME_MAX_LEN);
    return t;
}

// --- mirrors page_proximity_asset.h select option label ---
std::string proxLabel(int v, const std::string& name) {
    std::string lbl = "DO" + std::to_string(v + 1);
    if (!name.empty()) lbl += " \u00b7 " + name;
    return lbl;
}

} // namespace

TEST_CASE("names array shorter than 8 channels -> remaining are empty") {
    std::vector<std::string> n = normalizeNames({"Motor", "", "Valve 3"});
    REQUIRE(n.size() == (size_t)DO_CHANNELS);
    CHECK(n[0] == "Motor");
    CHECK(n[1].empty());
    CHECK(n[2] == "Valve 3");
    CHECK(n[3].empty());
    CHECK(n[7].empty());
}

TEST_CASE("names are truncated to 16 characters") {
    std::vector<std::string> n = normalizeNames({std::string(32, 'x')});
    REQUIRE(n.size() == (size_t)DO_CHANNELS);
    CHECK(n[0].length() == (size_t)DO_NAME_MAX_LEN);
    CHECK(n[0] == std::string(16, 'x'));
}

TEST_CASE("names array longer than 8 channels -> extra entries ignored") {
    std::vector<std::string> n = normalizeNames({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});
    REQUIRE(n.size() == (size_t)DO_CHANNELS);
    CHECK(n[7] == "h");
    CHECK(n[0] != "i");
}

TEST_CASE("physical output = logical XOR invert (driver semantics)") {
    // invert bit 2 set -> channel 2 written inverted on the bus
    CHECK(physicalMask(0x04, 0x04) == 0x00);
    CHECK(physicalMask(0x00, 0x04) == 0x04);
    // non-inverted channels pass through untouched
    CHECK(physicalMask(0xFF, 0x00) == 0xFF);
    CHECK(physicalMask(0b00110011, 0b00001100) == 0b00111111);
}

TEST_CASE("invert bit extraction per channel (JS loadDoConfig)") {
    uint8_t inv = 0x85; // bits 0, 2, 7
    CHECK(invertBit(inv, 0));
    CHECK_FALSE(invertBit(inv, 1));
    CHECK(invertBit(inv, 2));
    CHECK_FALSE(invertBit(inv, 5));
    CHECK(invertBit(inv, 7));
    CHECK_FALSE(invertBit(0x00, 3));
}

TEST_CASE("empty name -> 'Relay N' fallback label") {
    CHECK(channelLabel(0, "") == "Relay 1");
    CHECK(channelLabel(7, "") == "Relay 8");
    CHECK(channelLabel(3, "Pump") == "Pump");
    CHECK(channelLabel(5, "A") == "A");
}

TEST_CASE("JS saveDoConfig name value: trim whitespace and cap at 16") {
    CHECK(jsNameValue("   Pump A  ") == "Pump A");
    CHECK(jsNameValue("   ") == "");
    CHECK(jsNameValue("") == "");
    std::string longV = "  " + std::string(30, 'z') + "  ";
    CHECK(jsNameValue(longV) == std::string(16, 'z'));
    CHECK(jsNameValue("Valve") == "Valve");
}

TEST_CASE("proximity page label: starts at DO1, name appended with separator only when set") {
    CHECK(proxLabel(3, "") == "DO4");
    CHECK(proxLabel(0, "Bomba") == "DO1 \u00b7 Bomba");
    CHECK(proxLabel(7, "V") == "DO8 \u00b7 V");
}

TEST_CASE("round trip: saved names survive normalize+label unchanged") {
    std::vector<std::string> saved = normalizeNames({"Motor", "Compressor"});
    CHECK(channelLabel(0, saved[0]) == "Motor");
    CHECK(channelLabel(1, saved[1]) == "Compressor");
    CHECK(channelLabel(2, saved[2]) == "Relay 3");
}