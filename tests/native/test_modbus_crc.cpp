#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "modbus_crc.h"

TEST_CASE("modbusCrc16 - known vectors") {
    SUBCASE("empty buffer") {
        uint8_t data[] = {};
        CHECK(modbusCrc16(data, 0) == 0xFFFF);
    }

    SUBCASE("single byte 0x00") {
        uint8_t data[] = {0x00};
        CHECK(modbusCrc16(data, 1) == 0x40BF);
    }

    SUBCASE("single byte 0x41 (ASCII A)") {
        uint8_t data[] = {0x41};
        CHECK(modbusCrc16(data, 1) == 0x707F);
    }

    SUBCASE("Modbus FC04 query: 01 04 00 00 00 01") {
        uint8_t data[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01};
        CHECK(modbusCrc16(data, 6) == 0xCA31);
    }

    SUBCASE("Modbus FC03 query: 01 03 00 00 00 01") {
        uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
        CHECK(modbusCrc16(data, 6) == 0x0A84);
    }

    SUBCASE("Modbus FC03 response: 01 03 02 00 FF") {
        uint8_t data[] = {0x01, 0x03, 0x02, 0x00, 0xFF};
        CHECK(modbusCrc16(data, 5) == 0x04F8);
    }

    SUBCASE("all ones 0xFF x4") {
        uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(modbusCrc16(data, 4) == 0xB001);
    }

    SUBCASE("incremental 0x00..0x0F") {
        uint8_t data[16];
        for (int i = 0; i < 16; i++) data[i] = (uint8_t)i;
        CHECK(modbusCrc16(data, 16) == 0xE7B4);
    }
}

TEST_CASE("modbusCrc16 - deterministic") {
    uint8_t req1[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};
    CHECK(modbusCrc16(req1, 6) == 0x8776);

    uint8_t req2[] = {0x11, 0x01, 0x00, 0x6B, 0x00, 0x03};
    CHECK(modbusCrc16(req2, 6) == 0x470F);
}

TEST_CASE("modbusCrc16 - append to frame") {
    uint8_t req[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0, 0};
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);
    CHECK(req[6] == 0x84);
    CHECK(req[7] == 0x0A);
}

TEST_CASE("modbusCrc16 - stability under repeated calls") {
    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t first = modbusCrc16(data, 6);
    for (int i = 0; i < 100; i++) {
        CHECK(modbusCrc16(data, 6) == first);
    }
}

TEST_CASE("modbusCrc16 - long buffer") {
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)(i * 37 + 13);
    CHECK(modbusCrc16(buf, 256) == 0xBF7F);
}
