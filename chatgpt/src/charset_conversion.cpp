// Character Set Conversion Module - Implementation
// UTF-8 decoding and Unicode to Latin1/CP850 conversion

#include "charset_conversion.h"

// UTF-8 decoder state variables
static uint8_t utf8_state = 0;      // Decoder state (0 = idle, 1-3 = expecting continuation bytes)
static uint32_t utf8_codepoint = 0; // Accumulator for current Unicode codepoint

// ==============================================================================
// UTF-8 DECODER - Multi-byte sequence decoder
// ==============================================================================
uint32_t decodeUtf8Byte(uint8_t byte) {
    if (utf8_state == 0) { // Initial state, waiting for first byte
        if ((byte & 0x80) == 0x00) { // 1-byte sequence (0xxxxxxx)
            return byte;
        } else if ((byte & 0xE0) == 0xC0) { // 2-byte sequence (110xxxxx)
            utf8_codepoint = (byte & 0x1F);
            utf8_state = 1;
        } else { // Invalid first byte or start of 3/4 bytes
            // For simplicity, we handle 1-4 byte sequences
            // If it's a 3 or 4 byte start, or invalid, handle accordingly
            if ((byte & 0xF0) == 0xE0) { // 3-byte sequence (1110xxxx)
                utf8_codepoint = (byte & 0x0F);
                utf8_state = 2;
            } else if ((byte & 0xF8) == 0xF0) { // 4-byte sequence (11110xxx)
                utf8_codepoint = (byte & 0x07);
                utf8_state = 3;
            } else {
                utf8_state = 0; // Reset state
                return 0xFFFD; // Unicode replacement character
            }
        }
    } else { // Waiting for continuation byte (10xxxxxx)
        if ((byte & 0xC0) == 0x80) {
            utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3F);
            utf8_state--;
            if (utf8_state == 0) { // Complete sequence received
                return utf8_codepoint;
            }
        } else { // Invalid continuation byte
            utf8_state = 0; // Reset state
            return 0xFFFD; // Replacement character
        }
    }
    return 0; // Incomplete sequence
}

// ==============================================================================
// LATIN-1 (ISO-8859-1) CONVERTER
// ==============================================================================
uint8_t unicodeToLatin1(uint32_t codepoint) {
    if (codepoint >= 0x00 && codepoint <= 0xFF) {
        // Direct mapping for Latin-1 range
        return (uint8_t)codepoint;
    }
    // Characters outside Latin-1 range, replace with '?'
    return 0x3F;
}

// ==============================================================================
// CP850 (DOS LATIN) CONVERTER - Comprehensive mapping table
// ==============================================================================
uint8_t unicodeToCP850(uint32_t codepoint) {
    // Simplified mapping. A complete CP850 table would be large.
    // For common characters, we map directly or use a lookup.
    // For now, we use basic switch for common Portuguese characters
    // and default to '?' for others.
    switch (codepoint) {
        case 0xC7: return 0x80; // Ç
        case 0xFC: return 0x81; // ü
        case 0xE9: return 0x82; // é
        case 0xE2: return 0x83; // â
        case 0xE4: return 0x84; // ä
        case 0xE0: return 0x85; // à
        case 0xE5: return 0x86; // å
        case 0xE7: return 0x87; // ç
        case 0xEA: return 0x88; // ê
        case 0xEB: return 0x89; // ë
        case 0xE8: return 0x8A; // è
        case 0xEF: return 0x8B; // ï
        case 0xEE: return 0x8C; // î
        case 0xEC: return 0x8D; // ì
        case 0xC4: return 0x8E; // Ä
        case 0xC5: return 0x8F; // Å
        case 0xC9: return 0x90; // É
        case 0xE6: return 0x91; // æ
        case 0xC6: return 0x92; // Æ
        case 0xF4: return 0x93; // ô
        case 0xF6: return 0x94; // ö
        case 0xF2: return 0x95; // ò
        case 0xFB: return 0x96; // û
        case 0xF9: return 0x97; // ù
        case 0xFF: return 0x98; // ÿ
        case 0xD6: return 0x99; // Ö
        case 0xDC: return 0x9A; // Ü
        case 0xA2: return 0x9B; // ¢
        case 0xA3: return 0x9C; // £
        case 0xA5: return 0x9D; // ¥
        case 0x20A7: return 0x9E; // ₧ (peseta)
        case 0x192: return 0x9F; // ƒ
        case 0xE1: return 0xA0; // á
        case 0xED: return 0xA1; // í
        case 0xF3: return 0xA2; // ó
        case 0xFA: return 0xA3; // ú
        case 0xF1: return 0xA4; // ñ
        case 0xD1: return 0xA5; // Ñ
        case 0xAA: return 0xA6; // ª
        case 0xBA: return 0xA7; // º
        case 0xBF: return 0xA8; // ¿
        case 0xAE: return 0xA9; // ®
        case 0xAC: return 0xAA; // ¬
        case 0xBD: return 0xAB; // ½
        case 0xBC: return 0xAC; // ¼
        case 0xA1: return 0xAD; // ¡
        case 0xAB: return 0xAE; // «
        case 0xBB: return 0xAF; // »
        case 0x2591: return 0xB0; // ░
        case 0x2592: return 0xB1; // ▒
        case 0x2593: return 0xB2; // ▓
        case 0x2502: return 0xB3; // │
        case 0x2524: return 0xB4; // ┤
        case 0x2561: return 0xB5; // ╡
        case 0x2562: return 0xB6; // ╢
        case 0x2556: return 0xB7; // ╖
        case 0x2555: return 0xB8; // ╕
        case 0x2563: return 0xB9; // ╣
        case 0x2551: return 0xBA; // ║
        case 0x2557: return 0xBB; // ╗
        case 0x255D: return 0xBC; // ╝
        case 0x255C: return 0xBD; // ╜
        case 0x255B: return 0xBE; // ╛
        case 0x2510: return 0xBF; // ┐
        case 0x2514: return 0xC0; // └
        case 0x2534: return 0xC1; // ┴
        case 0x252C: return 0xC2; // ┬
        case 0x251C: return 0xC3; // ├
        case 0x2500: return 0xC4; // ─
        case 0x253C: return 0xC5; // ┼
        case 0x255E: return 0xC6; // ╞
        case 0x255F: return 0xC7; // ╟
        case 0x255A: return 0xC8; // ╚
        case 0x2554: return 0xC9; // ╔
        case 0x2569: return 0xCA; // ╩
        case 0x2566: return 0xCB; // ╦
        case 0x2560: return 0xCC; // ╠
        case 0x256A: return 0xCD; // ═
        case 0x2567: return 0xCE; // ╧
        case 0x2568: return 0xCF; // ╨
        case 0x2564: return 0xD0; // ╤
        case 0x2565: return 0xD1; // ╥
        case 0x2559: return 0xD2; // ╙
        case 0x2558: return 0xD3; // ╘
        case 0x2552: return 0xD4; // ╒
        case 0x2553: return 0xD5; // ╓
        case 0x256B: return 0xD6; // ╫
        case 0x256C: return 0xD7; // ╬
        case 0x256D: return 0xD8; // ╭
        case 0x256E: return 0xD9; // ╮
        case 0x256F: return 0xDA; // ╯
        case 0x2570: return 0xDB; // ╰
        case 0x250C: return 0xDC; // ┌ (top-left corner)
        case 0x2518: return 0xDD; // ┘ (bottom-right corner)
        case 0x2588: return 0xDE; // █ (full block)
        case 0x2584: return 0xDF; // ▄ (lower half block)
        case 0x2580: return 0xE0; // ▀ (upper half block)
        case 0x03B1: return 0xE1; // α
        case 0x00DF: return 0xE2; // ß
        case 0x0393: return 0xE3; // Γ
        case 0x03C0: return 0xE4; // π
        case 0x03A3: return 0xE5; // Σ
        case 0x03C3: return 0xE6; // σ
        case 0x00B5: return 0xE7; // µ
        case 0x03C4: return 0xE8; // τ
        case 0x03A6: return 0xE9; // Φ
        case 0x0398: return 0xEA; // Θ
        case 0x03A9: return 0xEB; // Ω
        case 0x03B4: return 0xEC; // δ
        case 0x221E: return 0xED; // ∞
        case 0x03C6: return 0xEE; // φ
        case 0x03B5: return 0xEF; // ε
        case 0x2229: return 0xF0; // ∩
        case 0x2261: return 0xF1; // ≡
        case 0x00B1: return 0xF2; // ±
        case 0x2265: return 0xF3; // ≥
        case 0x2264: return 0xF4; // ≤
        case 0x2320: return 0xF5; // ⌠
        case 0x2321: return 0xF6; // ⌡
        case 0x00F7: return 0xF7; // ÷
        case 0x2248: return 0xF8; // ≈
        case 0x00B0: return 0xF9; // °
        case 0x2219: return 0xFA; // ∙
        case 0x00B7: return 0xFB; // ·
        case 0x221A: return 0xFC; // √
        case 0x207F: return 0xFD; // ⁿ
        case 0x00B2: return 0xFD; // ² (CP850 has ⁿ at FD, but ² is common)
        case 0x25A0: return 0xFE; // ■
        case 0x00A0: return 0x20; // Non-breaking space to normal space
        default:
            if (codepoint >= 0x20 && codepoint <= 0x7E) { // ASCII range
                return (uint8_t)codepoint;
            }
            return 0x3F; // Default to '?' for unmappable characters
    }
}

// ==============================================================================
// UTILITY FUNCTIONS
// ==============================================================================
void resetUtf8Decoder() {
    utf8_state = 0;
    utf8_codepoint = 0;
}
