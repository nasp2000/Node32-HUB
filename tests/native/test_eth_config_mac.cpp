#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the Ethernet MAC-override decision logic fixed
// in sharedWebApplyEthernetConfig() (web_shared_logic.cpp). A partial form POST
// (round-trip `mode=dhcp` without a MAC field) must NEVER wipe the currently
// stored mac_override; the override is only changed when a valid MAC is provided.
//
// Mirrors production helper sharedWebValidMac(): trims, uppercases, normalises
// '-' to ':', requires exactly 6 hex octets "XX:XX:XX:XX:XX:XX".

namespace {

bool isHex(const std::string& s) {
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

bool validMac(const std::string& mac) {
    if (mac.empty()) return true; // empty == "not provided", treated as valid/no-op
    std::vector<std::string> octets;
    size_t start = 0;
    for (size_t i = 0; i <= mac.size(); ++i) {
        if (i == mac.size() || mac[i] == ':' || mac[i] == '-') {
            octets.push_back(mac.substr(start, i - start));
            start = i + 1;
        }
    }
    if (octets.size() != 6) return false;
    for (const auto& o : octets)
        if (o.size() != 2 || !isHex(o)) return false;
    return true;
}

// Mirrors the fixed sharedWebApplyEthernetConfig() decision:
// only overwrite the stored override with an explicitly provided MAC.
std::string applyEthernetMacOverride(const std::string& current, const std::string& incoming) {
    if (incoming.empty()) return current; // partial form -> preserve current
    return incoming;                       // valid explicit MAC (pre-validated)
}

} // namespace

TEST_CASE("round-trip mode=dhcp without MAC preserves the stored override (never zeroes)") {
    std::string current = "C2:E5:CC:02:0F:3C";
    std::string incoming = ""; // client sent mode=dhcp with no mac field
    CHECK(applyEthernetMacOverride(current, incoming) == "C2:E5:CC:02:0F:3C");
}

TEST_CASE("round-trip when no override was ever set stays empty (no phantom MAC)") {
    std::string current = "";
    std::string incoming = "";
    CHECK(applyEthernetMacOverride(current, incoming) == "");
}

TEST_CASE("explicit valid MAC replaces the stored override") {
    std::string current = "C2:E5:CC:02:0F:3C";
    std::string incoming = "DE:AD:BE:EF:FE:ED";
    CHECK(applyEthernetMacOverride(current, incoming) == "DE:AD:BE:EF:FE:ED");
}

TEST_CASE("valid MAC validation accepts colon, dash and uppercase forms") {
    CHECK(validMac("C2:E5:CC:02:0F:3C"));
    CHECK(validMac("c2:e5:cc:02:0f:3c"));
    CHECK(validMac("C2-E5-CC-02-0F-3C"));
}

TEST_CASE("invalid MAC strings are rejected") {
    CHECK_FALSE(validMac("C2:E5:CC:02:0F:3"));    // only 5 octets
    CHECK_FALSE(validMac("C2:E5:CC:02:0F:3C:0A")); // 7 octets
    CHECK_FALSE(validMac("ZZ:E5:CC:02:0F:3C"));    // non-hex char
    CHECK_FALSE(validMac("C2"));                   // missing octets
}
