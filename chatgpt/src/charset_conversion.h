// Character Set Conversion Module
// UTF-8 decoding and Unicode to Latin1/CP850 conversion
// Extracted from chatgpt.ino for better modularity

#ifndef CHARSET_CONVERSION_H
#define CHARSET_CONVERSION_H

#include <Arduino.h>

// UTF-8 decoder state machine
// Decodes multi-byte UTF-8 sequences into Unicode codepoints
// Returns: codepoint (complete sequence), 0 (incomplete), 0xFFFD (invalid)
uint32_t decodeUtf8Byte(uint8_t byte);

// Convert Unicode codepoint to Latin-1 (ISO-8859-1)
// Direct mapping for 0x00-0xFF range, '?' for out of range
uint8_t unicodeToLatin1(uint32_t codepoint);

// Convert Unicode codepoint to CP850 (DOS Latin)
// Comprehensive mapping with Portuguese characters, box drawing, math symbols
// ASCII passthrough for 0x20-0x7E, '?' for unmapped characters
uint8_t unicodeToCP850(uint32_t codepoint);

// Reset UTF-8 decoder state (useful for error recovery)
void resetUtf8Decoder();

#endif // CHARSET_CONVERSION_H
