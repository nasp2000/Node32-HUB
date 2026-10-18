// Printer Emulation Module - Implementation
// IBM Proprinter & Epson ESC/P command interpretation
#include <cstdint>
#include "feature_flags.h"
#include "printer_emulation.h"
using std::uint32_t;
using std::uint8_t;

#if ENABLE_DB25==1

// List of available emulation modes
const char* PRINTER_EMULATION_MODES[] = { "Genérico", "Epson ESC/P", "IBM Proprinter" };
const int PRINTER_EMULATION_MODES_COUNT = sizeof(PRINTER_EMULATION_MODES) / sizeof(PRINTER_EMULATION_MODES[0]);

// ==============================================================================
// GENERIC MODE - Pass-through (no command processing)
// ==============================================================================
bool processGenericCommand(uint32_t codepoint, uint8_t &outputByte) {
    // Generic mode: pass all characters directly
    outputByte = (uint8_t)codepoint;
    return true; // Always output
}

// ==============================================================================
// EPSON ESC/P MODE - Epson printer command interpretation
// ==============================================================================
bool processEpsonCommand(uint32_t codepoint, uint8_t &outputByte) {
    static bool inEscapeSequence = false;
    static bool inGraphicsMode = false;
    static String escapeBuffer = "";
    static int graphicsDataExpected = 0;
    static int graphicsDataReceived = 0;
    
    outputByte = (uint8_t)codepoint;
    
    // If in graphics mode, collect data
    if (inGraphicsMode) {
        graphicsDataReceived++;
        if (graphicsDataReceived >= graphicsDataExpected) {
            inGraphicsMode = false;
            graphicsDataExpected = 0;
            graphicsDataReceived = 0;
            sendVirtualMessage(String(F("EPSON: Graphics data received completely (")) + String(graphicsDataExpected) + String(F(" bytes)")), DEBUG);
        }
        return false; // Don't output graphics data as text
    }
    
    if (codepoint == 0x1B) { // ESC (escape)
        inEscapeSequence = true;
        escapeBuffer = "";
        return false; // Don't output ESC
    }
    
    if (inEscapeSequence) {
        escapeBuffer += (char)codepoint;
        
        // Basic ESC/P commands
        if (escapeBuffer == "@") { // ESC @ - Reset printer
            inEscapeSequence = false;
            inGraphicsMode = false;
            sendVirtualMessage(String(F("EPSON: Reset printer command")), DEBUG);
            return false;
        } else if (escapeBuffer == "A") { // ESC A - Line spacing 1/6 inch
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Line spacing 1/6 inch")), DEBUG);
            return false;
        } else if (escapeBuffer == "2") { // ESC 2 - Line spacing 1/6 inch
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Line spacing 1/6 inch (ESC 2)")), DEBUG);
            return false;
        } else if (escapeBuffer == "0") { // ESC 0 - Line spacing 1/8 inch
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Line spacing 1/8 inch")), DEBUG);
            return false;
        } else if (escapeBuffer == "E") { // ESC E - Emphasized on
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Emphasized ON")), DEBUG);
            return false;
        } else if (escapeBuffer == "F") { // ESC F - Emphasized off
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Emphasized OFF")), DEBUG);
            return false;
        } else if (escapeBuffer == "G") { // ESC G - Double strike on
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Double strike ON")), DEBUG);
            return false;
        } else if (escapeBuffer == "H") { // ESC H - Double strike off
            inEscapeSequence = false;
            sendVirtualMessage(String(F("EPSON: Double strike OFF")), DEBUG);
            return false;
        } else if (escapeBuffer.startsWith("*") && escapeBuffer.length() >= 4) { // ESC * - Graphics commands
            if (escapeBuffer.length() == 4) {
                int graphicsMode = escapeBuffer[1] - '0';
                int n1 = escapeBuffer[2];
                int n2 = escapeBuffer[3];
                graphicsDataExpected = n1 + (n2 * 256);
                
                inEscapeSequence = false;
                inGraphicsMode = true;
                graphicsDataReceived = 0;
                
                String graphicsMsg = String(F("EPSON: Graphics command ESC * "));
                graphicsMsg += String(graphicsMode);
                graphicsMsg += String(F(" (expecting "));
                graphicsMsg += String(graphicsDataExpected);
                graphicsMsg += String(F(" bytes of graphics data)"));
                sendVirtualMessage(graphicsMsg, DEBUG);
                return false;
            }
        } else if (escapeBuffer.length() > 4) { // Sequence too long, reset
            inEscapeSequence = false;
            escapeBuffer = "";
        }
        
        return false;
    }
    
    // Basic control commands
    if (codepoint == 0x0C) { // Form Feed
        sendVirtualMessage(String(F("EPSON: Form Feed")), DEBUG);
        return true;
    } else if (codepoint == 0x0A) { // Line Feed
        return true;
    } else if (codepoint == 0x0D) { // Carriage Return
        return true;
    }
    
    return true;
}

// ==============================================================================
// IBM PROPRINTER MODE - IBM printer command interpretation
// ==============================================================================
bool processIBMCommand(uint32_t codepoint, uint8_t &outputByte) {
    static bool inEscapeSequence = false;
    static bool inGraphicsMode = false;
    static String escapeBuffer = "";
    static int graphicsDataExpected = 0;
    static int graphicsDataReceived = 0;
    
    outputByte = (uint8_t)codepoint;
    
    if (inGraphicsMode) {
        graphicsDataReceived++;
        if (graphicsDataReceived >= graphicsDataExpected) {
            inGraphicsMode = false;
            graphicsDataExpected = 0;
            graphicsDataReceived = 0;
            sendVirtualMessage(String(F("IBM: Graphics data received completely (")) + String(graphicsDataExpected) + String(F(" bytes)")), DEBUG);
        }
        return false;
    }
    
    if (codepoint == 0x1B) {
        inEscapeSequence = true;
        escapeBuffer = "";
        return false;
    }
    
    if (inEscapeSequence) {
        escapeBuffer += (char)codepoint;
        
        if (escapeBuffer == "@") {
            inEscapeSequence = false;
            inGraphicsMode = false;
            sendVirtualMessage(String(F("IBM: Reset printer command")), DEBUG);
            return false;
        } else if (escapeBuffer == "A") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Line spacing 1/6 inch")), DEBUG);
            return false;
        } else if (escapeBuffer == "2") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Line spacing 1/6 inch (ESC 2)")), DEBUG);
            return false;
        } else if (escapeBuffer == "0") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Line spacing 1/8 inch")), DEBUG);
            return false;
        } else if (escapeBuffer == "E") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Emphasized ON")), DEBUG);
            return false;
        } else if (escapeBuffer == "F") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Emphasized OFF")), DEBUG);
            return false;
        } else if (escapeBuffer == "G") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Double strike ON")), DEBUG);
            return false;
        } else if (escapeBuffer == "H") {
            inEscapeSequence = false;
            sendVirtualMessage(String(F("IBM: Double strike OFF")), DEBUG);
            return false;
        } else if (escapeBuffer.startsWith("*") && escapeBuffer.length() >= 4) {
            if (escapeBuffer.length() == 4) {
                int graphicsMode = escapeBuffer[1] - '0';
                int n1 = escapeBuffer[2];
                int n2 = escapeBuffer[3];
                graphicsDataExpected = n1 + (n2 * 256);
                
                inEscapeSequence = false;
                inGraphicsMode = true;
                graphicsDataReceived = 0;
                
                String graphicsMsg = String(F("IBM: Graphics command ESC * "));
                graphicsMsg += String(graphicsMode);
                graphicsMsg += String(F(" (expecting "));
                graphicsMsg += String(graphicsDataExpected);
                graphicsMsg += String(F(" bytes of graphics data)"));
                sendVirtualMessage(graphicsMsg, DEBUG);
                return false;
            }
        } else if (escapeBuffer.length() > 4) {
            inEscapeSequence = false;
            escapeBuffer = "";
        }
        
        return false;
    }
    
    if (codepoint == 0x0C) {
        sendVirtualMessage(String(F("IBM: Form Feed")), DEBUG);
        return true;
    } else if (codepoint == 0x0A) {
        return true;
    } else if (codepoint == 0x0D) {
        return true;
    }
    
    return true;
}

#else
// Stubs when DB25 is disabled — ensures callers always link.
const char* PRINTER_EMULATION_MODES[] = { "Genérico" };
const int PRINTER_EMULATION_MODES_COUNT = 1;
bool processGenericCommand(uint32_t codepoint, uint8_t &outputByte) {
    outputByte = (uint8_t)codepoint;
    return true;
}
bool processEpsonCommand(uint32_t codepoint, uint8_t &outputByte) {
    outputByte = (uint8_t)codepoint;
    return true;
}
bool processIBMCommand(uint32_t codepoint, uint8_t &outputByte) {
    outputByte = (uint8_t)codepoint;
    return true;
}
#endif
