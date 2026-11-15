#pragma once
// Register type / byte-order mapping for the Modbus TCP editor (BACKLOG MODBUS-1).
//
// `/modbus-tcp` renders `type` and `order` as NAMES ("FLOAT32", "CDAB") and
// posts them back the same way, while the firmware stores and parses them as
// the uint8 codes from modbus_core.h. The POST handler used
// `r["type"] | (uint8_t)0`, and `operator|` cannot convert a string, so every
// save from that page collapsed every register to 0 (UINT16/ABCD) — silently,
// because the write succeeds.
//
// These helpers are the single translation point, dependency-free so the matrix
// is unit-tested on the host (tests/native/test_modbus_reg_types.cpp).
// The numeric codes MUST stay in sync with ModbusDataType / ModbusByteOrder.

#include <stdint.h>
#include <stddef.h>

// ModbusDataType: UINT16=0 INT16=1 UINT32=2 INT32=3 FLOAT32=4 BIT=5
// ModbusByteOrder: ABCD=0 CDAB=1 BADC=2 DCBA=3
static const uint8_t MB_REG_TYPE_COUNT = 6;
static const uint8_t MB_REG_ORDER_COUNT = 4;
static const uint8_t MB_REG_TYPE_UNKNOWN = 0xFF;
static const uint8_t MB_REG_ORDER_UNKNOWN = 0xFF;

// Case-insensitive compare without <strings.h> (not on every target).
inline bool modbusNameEq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

// Names as the page offers them. COIL/DISCRETE are function-code concepts the
// editor also lists as "types"; they are 1-bit reads, so they map to BIT and
// the saved `fc` keeps the actual function code.
inline uint8_t modbusTypeFromName(const char* name) {
    if (!name) return MB_REG_TYPE_UNKNOWN;
    static const char* const kNames[MB_REG_TYPE_COUNT] = {"UINT16", "INT16", "UINT32", "INT32", "FLOAT32", "BIT"};
    for (uint8_t i = 0; i < MB_REG_TYPE_COUNT; ++i) {
        if (modbusNameEq(name, kNames[i])) return i;
    }
    if (modbusNameEq(name, "COIL") || modbusNameEq(name, "DISCRETE")) return 5; // BIT
    return MB_REG_TYPE_UNKNOWN;
}

inline const char* modbusTypeName(uint8_t code) {
    static const char* const kNames[MB_REG_TYPE_COUNT] = {"UINT16", "INT16", "UINT32", "INT32", "FLOAT32", "BIT"};
    return code < MB_REG_TYPE_COUNT ? kNames[code] : "UINT16";
}

inline uint8_t modbusOrderFromName(const char* name) {
    if (!name) return MB_REG_ORDER_UNKNOWN;
    static const char* const kNames[MB_REG_ORDER_COUNT] = {"ABCD", "CDAB", "BADC", "DCBA"};
    for (uint8_t i = 0; i < MB_REG_ORDER_COUNT; ++i) {
        if (modbusNameEq(name, kNames[i])) return i;
    }
    return MB_REG_ORDER_UNKNOWN;
}

inline const char* modbusOrderName(uint8_t code) {
    static const char* const kNames[MB_REG_ORDER_COUNT] = {"ABCD", "CDAB", "BADC", "DCBA"};
    return code < MB_REG_ORDER_COUNT ? kNames[code] : "ABCD";
}
