// Native tests for the Modbus register type/byte-order mapping (BACKLOG MODBUS-1).
//
// The /modbus-tcp editor renders `type`/`order` as names ("FLOAT32", "CDAB") and
// posts them back that way, but the firmware stores and parses the uint8 codes
// from modbus_core.h. The POST handler used `r["type"] | (uint8_t)0`; ArduinoJson
// cannot convert a string with operator|, so it always produced 0 — every save
// from that page silently collapsed every register to UINT16/ABCD.
//
// The mapping now lives in modbus_reg_types.h (one translation point) and the
// handler accepts names *and* codes. The editor also had a display half: it
// compared the stored numeric code against a name, and it built every row's
// type select from register 0, so the editor always showed the first type.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/modbus_reg_types.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string readSrc(const char *rel) {
    std::string path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot read " << path);
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

} // namespace

TEST_CASE("every type name the editor offers maps to its stored code") {
    CHECK(modbusTypeFromName("UINT16") == 0);
    CHECK(modbusTypeFromName("INT16") == 1);
    CHECK(modbusTypeFromName("UINT32") == 2);
    CHECK(modbusTypeFromName("INT32") == 3);
    CHECK(modbusTypeFromName("FLOAT32") == 4);
    CHECK(modbusTypeFromName("BIT") == 5);
    // 1-bit register flavours the editor lists as "types".
    CHECK(modbusTypeFromName("COIL") == 5);
    CHECK(modbusTypeFromName("DISCRETE") == 5);
    // Case-insensitive and null-safe.
    CHECK(modbusTypeFromName("float32") == 4);
    CHECK(modbusTypeFromName("Uint16") == 0);
    CHECK(modbusTypeFromName(nullptr) == MB_REG_TYPE_UNKNOWN);
    CHECK(modbusTypeFromName("NOPE") == MB_REG_TYPE_UNKNOWN);
}

TEST_CASE("byte order names map to the wire order codes") {
    CHECK(modbusOrderFromName("ABCD") == 0);
    CHECK(modbusOrderFromName("CDAB") == 1);
    CHECK(modbusOrderFromName("BADC") == 2);
    CHECK(modbusOrderFromName("DCBA") == 3);
    CHECK(modbusOrderFromName("cdab") == 1);
    CHECK(modbusOrderFromName(nullptr) == MB_REG_ORDER_UNKNOWN);
    CHECK(modbusOrderFromName("ABCDE") == MB_REG_ORDER_UNKNOWN);
}

TEST_CASE("names round-trip and out-of-range codes fall back safely") {
    for (uint8_t code = 0; code < MB_REG_TYPE_COUNT; ++code) {
        INFO("type code " << (int)code);
        CHECK(modbusTypeFromName(modbusTypeName(code)) == code);
    }
    for (uint8_t code = 0; code < MB_REG_ORDER_COUNT; ++code) {
        INFO("order code " << (int)code);
        CHECK(modbusOrderFromName(modbusOrderName(code)) == code);
    }
    CHECK(std::string(modbusTypeName(200)) == "UINT16");
    CHECK(std::string(modbusOrderName(200)) == "ABCD");
}

TEST_CASE("the POST handler maps both shapes instead of collapsing to 0") {
    const std::string handler = readSrc("webpages/handlers_unified/modbus_handler.cpp");
    CHECK(handler.find("#include \"../../modbus_reg_types.h\"") != std::string::npos);
    CHECK(handler.find("nreg[\"type\"]   = modbusRegTypeFromJson(r[\"type\"]);") != std::string::npos);
    CHECK(handler.find("nreg[\"order\"]  = modbusRegOrderFromJson(r[\"order\"]);") != std::string::npos);
    // The old coercion (the bug) must not come back.
    CHECK(handler.find("nreg[\"type\"]   = r[\"type\"]   | (uint8_t)0;") == std::string::npos);
    CHECK(handler.find("nreg[\"order\"]  = r[\"order\"]  | (uint8_t)0;") == std::string::npos);
    // Storage stays numeric: the runtime (and modbus_core semantics) parse uint8.
    const std::string runtime = readSrc("modbus_runtime.cpp");
    CHECK(runtime.find("(ModbusDataType)   (reg[\"type\"]  | (uint8_t)0)") != std::string::npos);
}

TEST_CASE("the editor translates the stored code back to a name") {
    const std::string page = readSrc("webpages/assets/page_modbus_asset.cpp");
    CHECK(page.find("function _mbTypeName(") != std::string::npos);
    CHECK(page.find("function _mbOrderName(") != std::string::npos);
    CHECK(page.find("var rt=_mbTypeName(r.type)") != std::string::npos);
    CHECK(page.find("ro=_mbOrderName(r.order)") != std::string::npos);
    // Per-row selects: the old code built every row from register 0's type.
    CHECK(page.find("d.regs[0].type===t") == std::string::npos);
    CHECK(page.find("var typeOpts=_mbOptions(") != std::string::npos);
}
