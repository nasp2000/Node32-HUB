// Native unit tests for i2c_scan_logic.h — compiles the production header
// (JSON builder + known-name table). Run via run_native_tests.py.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "i2c_scan_logic.h"

#include <string.h>

TEST_CASE("i2cKnownName identifies common chips, returns null for unknown") {
    CHECK(i2cKnownName(0x3C) != nullptr);
    CHECK(strcmp(i2cKnownName(0x3C), "SSD1306/SH1106 OLED") == 0);
    CHECK(i2cKnownName(0x20) != nullptr);
    CHECK(i2cKnownName(0x76) != nullptr);
    CHECK(i2cKnownName(0x07) == nullptr);
    CHECK(i2cKnownName(0x4A) == nullptr);
}

TEST_CASE("scan JSON marks known/configured devices and empty missing list") {
    String out;
    uint8_t found0[] = {0x3C, 0x4A};
    I2cBusReport buses[1];
    buses[0].label = "I2C0 (Wire)";
    buses[0].sda = 21;
    buses[0].scl = 48;
    buses[0].found = found0;
    buses[0].foundCount = 2;

    I2cExpectedDevice expected[1];
    expected[0].addr = 0x3C;
    expected[0].name = "OLED";
    i2cAppendScanJson(out, expected, 1, buses, 1);

    CHECK(out.indexOf("\"bus\":\"I2C0 (Wire)\"") >= 0);
    CHECK(out.indexOf("\"sda\":21") >= 0);
    CHECK(out.indexOf("\"scl\":48") >= 0);
    // OLED detected: known + configured
    CHECK(out.indexOf("\"addr\":\"0x3C\"") >= 0);
    CHECK(out.indexOf("\"known\":true") >= 0);
    CHECK(out.indexOf("\"configured\":true") >= 0);
    // 0x4A not in the known table: unknown name, not configured
    CHECK(out.indexOf("\"addr\":\"0x4A\"") >= 0);
    CHECK(out.indexOf("\"name\":\"unknown\"") >= 0);
    CHECK(out.indexOf("\"configured\":false") >= 0);
    // OLED was found -> nothing missing
    CHECK(out.indexOf("\"missing\":[]") >= 0);
}

TEST_CASE("scan JSON reports configured-but-missing across two buses") {
    String out;
    uint8_t found0[] = {};
    uint8_t found1[] = {0x38};
    I2cBusReport buses[2];
    buses[0].label = "I2C0 (Wire)";
    buses[0].sda = 42;
    buses[0].scl = 41;
    buses[0].found = found0;
    buses[0].foundCount = 0;
    buses[1].label = "I2C1 (Wire1)";
    buses[1].sda = 8;
    buses[1].scl = 9;
    buses[1].found = found1;
    buses[1].foundCount = 1;

    I2cExpectedDevice expected[2];
    expected[0].addr = 0x38;
    expected[0].name = "AHT20";
    expected[1].addr = 0x76;
    expected[1].name = "BMP280";
    i2cAppendScanJson(out, expected, 2, buses, 2);

    CHECK(out.indexOf("\"bus\":\"I2C1 (Wire1)\"") >= 0);
    // AHT20 found on bus1 -> configured, not missing
    CHECK(out.indexOf("\"addr\":\"0x38\"") >= 0);
    CHECK(out.indexOf("\"configured\":true") >= 0);
    // BMP280 configured but not detected -> present in missing[]
    CHECK(out.indexOf("\"missing\"") >= 0);
    CHECK(out.indexOf("\"addr\":\"0x76\"") >= 0);
    CHECK(out.indexOf("\"name\":\"BMP280\"") >= 0);
    // Empty bus0 -> empty devices array
    CHECK(out.indexOf("\"devices\":[]") >= 0);
}

TEST_CASE("scan JSON keeps valid shape on edge cases (empty buses, all present)") {
    String out;
    uint8_t found0[] = {0x76};
    I2cBusReport buses[1];
    buses[0].label = "I2C0 (Wire)";
    buses[0].sda = 21;
    buses[0].scl = 48;
    buses[0].found = found0;
    buses[0].foundCount = 1;
    I2cExpectedDevice expected[1];
    expected[0].addr = 0x76;
    expected[0].name = "BMP280";
    i2cAppendScanJson(out, expected, 1, buses, 1);

    CHECK(out.length() > 0);
    CHECK(out.indexOf("{\"buses\":") == 0);
    CHECK(out.indexOf("\"addr\":\"0x76\"") >= 0);
    CHECK(out.indexOf("\"known\":true") >= 0);
    CHECK(out.indexOf("\"configured\":true") >= 0);
    CHECK(out.endsWith("]}"));
    CHECK(out.indexOf("\"missing\":[]") >= 0);
}
