#pragma once
#include <Arduino.h>

static inline String extractEventTimestampFromSourceLine(const String &sourceLine) {
    if (sourceLine.length() >= 8 &&
        isDigit(sourceLine.charAt(0)) && isDigit(sourceLine.charAt(1)) &&
        sourceLine.charAt(2) == ':' &&
        isDigit(sourceLine.charAt(3)) && isDigit(sourceLine.charAt(4)) &&
        sourceLine.charAt(5) == ':' &&
        isDigit(sourceLine.charAt(6)) && isDigit(sourceLine.charAt(7))) {
        return sourceLine.substring(0, 8);
    }

    if (sourceLine.length() >= 19 &&
        isDigit(sourceLine.charAt(0)) && isDigit(sourceLine.charAt(1)) &&
        isDigit(sourceLine.charAt(2)) && isDigit(sourceLine.charAt(3)) &&
        sourceLine.charAt(4) == '-' && sourceLine.charAt(7) == '-' &&
        sourceLine.charAt(10) == ' ' &&
        sourceLine.charAt(13) == ':' && sourceLine.charAt(16) == ':') {
        return sourceLine.substring(0, 19);
    }

    return String();
}

static inline String applyHighlightToSourceLine(const String &sourceLine, const String &highlightVal) {
    String displayLine = sourceLine;
    if (highlightVal.length() > 0) {
        int idx = displayLine.indexOf(highlightVal);
        if (idx >= 0) {
            displayLine = displayLine.substring(0, idx) +
                          "***" + highlightVal + "***" +
                          displayLine.substring(idx + (int)highlightVal.length());
        }
    }
    return displayLine;
}
