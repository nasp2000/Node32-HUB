#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "string_utils.h"
#include <string>
#include <cstring>

TEST_CASE("buildMessage - simple") {
    CHECK(std::string(buildMessage("hello").c_str()) == "hello");
    CHECK(std::string(buildMessage("").c_str()) == "");
}

TEST_CASE("buildMessage - with arguments") {
    CHECK(std::string(buildMessage("val=%d", 42).c_str()) == "val=42");
    CHECK(std::string(buildMessage("hex=%x", 255).c_str()) == "hex=ff");
    CHECK(std::string(buildMessage("char=%c", 65).c_str()) == "char=A");
}

TEST_CASE("buildMessage - string substitution") {
    CHECK(std::string(buildMessage("name=%s", "world").c_str()) == "name=world");
    CHECK(std::string(buildMessage("%s=%d", "count", 3).c_str()) == "count=3");
}

TEST_CASE("buildMessage - float") {
    CHECK(std::string(buildMessage("pi=%.2f", 3.14159).c_str()) == "pi=3.14");
}

TEST_CASE("buildMessage - buffer boundary") {
    std::string longStr(240, 'A');
    String result = buildMessage("%s", longStr.c_str());
    CHECK(result.length() == 240);
}

TEST_CASE("buildMessage - truncation") {
    std::string veryLong(300, 'B');
    String result = buildMessage("%s%s", veryLong.c_str(), veryLong.c_str());
    CHECK(result.length() == 255);
}

TEST_CASE("buildMessage - mixed types") {
    CHECK(std::string(buildMessage("%s=%d.%02d", "version", 1, 42).c_str()) == "version=1.42");
}
