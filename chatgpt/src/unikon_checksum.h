#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cstdio>

#ifndef UNIKON_STX
#define UNIKON_STX   0x02
#define UNIKON_ETX   0x03
#define UNIKON_ADDR  0x81
#endif

static inline void computeChecksum(const uint8_t* data, uint8_t len,
                                     uint8_t* ck1, uint8_t* ck2) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += data[i];
    // CK1 (low byte) must never be a control character: frames are delimited by
    // STX(0x02)/ETX(0x03) and the device answers with single-byte ACK(0x06)/NAK(0x15),
    // so any value < 0x20 inside a frame is ambiguous. The real sender -- and
    // unikonDrv.dll's frame builder FUN_10003a70 -- escapes it with |0x80.
    // Verified against the sniffer captures (docs/Unikon Sniffer/): 159 of 2766 frames
    // carry an escaped CK1, e.g. sum=0x60A -> CK1 0x0A transmitted as 0x8A, CK2 0x86.
    uint8_t lo = (uint8_t)(sum & 0xFF);
    if (lo < 0x20) lo = (uint8_t)(lo | 0x80);
    *ck1 = lo;
    *ck2 = (uint8_t)(0x80 | ((sum >> 8) & 0x7F));
}

static inline uint8_t buildFrame(const uint8_t* content, uint8_t contentLen, uint8_t* buf) {
    buf[0] = UNIKON_STX;
    memcpy(buf + 1, content, contentLen);
    uint8_t ck1, ck2;
    computeChecksum(content, contentLen, &ck1, &ck2);
    buf[contentLen + 1] = ck1;
    buf[contentLen + 2] = ck2;
    buf[contentLen + 3] = UNIKON_ETX;
    return (uint8_t)(contentLen + 4);
}

static inline bool isAsciiDigitsExact(const char* value, size_t len) {
    if (!value) return false;
    for (size_t i = 0; i < len; i++) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return value[len] == '\0';
}

static inline bool isAsciiHexPrefixExact(const char* value, size_t len) {
    if (!value) return false;
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        bool isDigit = (c >= '0' && c <= '9');
        bool isUpperHex = (c >= 'A' && c <= 'F');
        if (!isDigit && !isUpperHex) return false;
    }
    return true;
}

static inline void unikonFormatDisplayRing(const char* rawRing, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!rawRing) return;
    if (isAsciiDigitsExact(rawRing, 11) && rawRing[2] == '0' && rawRing[3] == '0') {
        snprintf(out, outSize, "%.2s-%.7s", rawRing, rawRing + 4);
        return;
    }
    snprintf(out, outSize, "%s", rawRing);
}

static inline void unikonFormatDisplayChip(const char* rawChip, bool hasChip, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!rawChip || !hasChip) return;
    if (strlen(rawChip) == 11 && isAsciiHexPrefixExact(rawChip, 8) &&
        isAsciiDigitsExact(rawChip + 8, 3)) {
        snprintf(out, outSize, "%.8s", rawChip);
        return;
    }
    snprintf(out, outSize, "%s", rawChip);
}

static inline void sanitizeAsciiText(const char* input, char* output, size_t outputSize) {
    if (!output || outputSize == 0) return;
    output[0] = '\0';
    if (!input) return;
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j + 1 < outputSize; i++) {
        unsigned char c = (unsigned char)input[i];
        output[j++] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    output[j] = '\0';
}
