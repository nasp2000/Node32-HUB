#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ConfigValidator.h"
#include <string>
#include <cstring>

TEST_CASE("isSafeConfigJsonKey: rejects null and empty") {
    CHECK(!ConfigValidator::isSafeConfigJsonKey(nullptr));
    CHECK(!ConfigValidator::isSafeConfigJsonKey(""));
}

TEST_CASE("isSafeConfigJsonKey: rejects too long") {
    std::string longKey(65, 'a');
    CHECK(!ConfigValidator::isSafeConfigJsonKey(longKey.c_str()));
    std::string okKey(64, 'a');
    CHECK(ConfigValidator::isSafeConfigJsonKey(okKey.c_str()));
}

TEST_CASE("isSafeConfigJsonKey: allows valid lowercase") {
    CHECK(ConfigValidator::isSafeConfigJsonKey("abc"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("config_key"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("section.subsection"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("key-name"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("a"));
}

TEST_CASE("isSafeConfigJsonKey: allows valid uppercase") {
    CHECK(ConfigValidator::isSafeConfigJsonKey("ABC"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("MY_CONFIG"));
}

TEST_CASE("isSafeConfigJsonKey: allows digits") {
    CHECK(ConfigValidator::isSafeConfigJsonKey("key42"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("12345"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("cfg_web_pwd"));
}

TEST_CASE("isSafeConfigJsonKey: rejects invalid characters") {
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key space"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key/slash"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key\\backslash"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("tab\tkey"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key\nnewline"));
}

TEST_CASE("isSafeConfigJsonKey: rejects special symbols") {
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key!bang"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key@at"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key#hash"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key$dollar"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key%percent"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key^caret"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key&ampersand"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key*star"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key(paren"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key+plus"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key=equal"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("key{bracket"));
}

TEST_CASE("isSafeConfigJsonKey: allows mixed valid chars") {
    CHECK(ConfigValidator::isSafeConfigJsonKey("My.Config_Key-42"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("sensor.mq135.enabled"));
    CHECK(ConfigValidator::isSafeConfigJsonKey("MODBUS_RTU_BAUD-115200"));
}

TEST_CASE("isSafeConfigJsonKey: rejects unicode") {
    CHECK(!ConfigValidator::isSafeConfigJsonKey("chave\u00E7a"));
    CHECK(!ConfigValidator::isSafeConfigJsonKey("\u00E9"));
}
