#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/nvs_write_guard.h"

// Pure decision helper behind ConfigManager::save() write-only-on-change:
// NVS keys (wifi JSON, saved-networks JSON, passwords) are rewritten only
// when the fresh value differs from the stored one. Dependency-free header
// compiled as-is so the exact production comparison is verified here.

TEST_CASE("needs-write: identical values skip the flash write") {
    CHECK_FALSE(nvsStringNeedsWrite("abc", "abc"));
    CHECK_FALSE(nvsStringNeedsWrite("", ""));
    CHECK_FALSE(nvsStringNeedsWrite(nullptr, nullptr));
    CHECK_FALSE(nvsStringNeedsWrite(nullptr, ""));
    CHECK_FALSE(nvsStringNeedsWrite("", nullptr));
    CHECK_FALSE(nvsStringNeedsWrite("{\"ssid\":\"x\"}", "{\"ssid\":\"x\"}"));
}

TEST_CASE("needs-write: any difference forces the write") {
    CHECK(nvsStringNeedsWrite("", "abc"));
    CHECK(nvsStringNeedsWrite("abc", ""));
    CHECK(nvsStringNeedsWrite(nullptr, "abc"));
    CHECK(nvsStringNeedsWrite("abc", nullptr));
    CHECK(nvsStringNeedsWrite("abc", "abd"));
    CHECK(nvsStringNeedsWrite("{\"ssid\":\"a\"}", "{\"ssid\":\"b\"}"));
    // Prefix-only difference (length check alone would miss it).
    CHECK(nvsStringNeedsWrite("pass", "pass2"));
    CHECK(nvsStringNeedsWrite("pass2", "pass"));
}
