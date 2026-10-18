// Printer Emulation Module - IBM Proprinter & Epson ESC/P
// Handles printer command interpretation for multiple emulation modes
// Extracted from chatgpt.ino for better modularity

#ifndef PRINTER_EMULATION_H
#define PRINTER_EMULATION_H

#include <Arduino.h>
#include "commands.h" // For LogLevel enum and sendVirtualMessage declaration

// Printer emulation modes (prefixed to avoid conflicts with ESP32 SDK)
#define PRINTER_MODE_GENERIC 0 // Generic mode (pass-through)
#define PRINTER_MODE_EPSON   1 // Epson ESC/P mode
#define PRINTER_MODE_IBM     2 // IBM Proprinter mode

// List of available emulation modes
extern const char* PRINTER_EMULATION_MODES[];
extern const int PRINTER_EMULATION_MODES_COUNT;

// Command processing functions for each emulation mode
// Returns true if the character should be output, false if consumed by command processing
// outputByte contains the processed byte value

bool processGenericCommand(uint32_t codepoint, uint8_t &outputByte);
bool processEpsonCommand(uint32_t codepoint, uint8_t &outputByte);
bool processIBMCommand(uint32_t codepoint, uint8_t &outputByte);

#endif // PRINTER_EMULATION_H
