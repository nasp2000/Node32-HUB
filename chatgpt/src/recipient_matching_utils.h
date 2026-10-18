#pragma once
#include <Arduino.h>

static inline double parseFormattedNumber(const String& s) {
    return s.toDouble();
}

static inline String getNumberShape(const String& s) {
    String shape;
    for (int i = 0; i < (int)s.length(); i++) {
        char c = s.charAt(i);
        if (isDigit(c)) shape += 'd';
        else if (c == '.') shape += c;
        else break;
    }
    while (shape.endsWith(" ")) shape.remove(shape.length() - 1);
    return shape;
}

static inline bool tryMatchShape(const String& line, int pos, const String& shape,
                                  String& outStr, double& outVal) {
    int len      = line.length();
    int shapeLen = shape.length();
    if (shapeLen == 0 || pos + shapeLen > len) return false;
    for (int i = 0; i < shapeLen; i++) {
        char sc = shape.charAt(i);
        char lc = line.charAt(pos + i);
        if      (sc == 'd' && !isDigit(lc)) return false;
        else if (sc == '.' && lc != '.')    return false;
        else if (sc == ' ' && lc != ' ')    return false;
    }
    bool isNegative = false;
    if (pos > 0) {
        char b = line.charAt(pos - 1);
        if (isDigit(b) || b == '.') return false;
        if (b == '-') {
            if (pos >= 2) { char bb = line.charAt(pos - 2); if (isDigit(bb) || bb == '.') return false; }
            isNegative = true;
        }
    }
    int endPos = pos + shapeLen;
    if (endPos < len) { char a = line.charAt(endPos); if (isDigit(a) || a == '.') return false; }
    outStr = line.substring(pos, endPos);
    outVal = parseFormattedNumber(outStr);
    if (isNegative) outVal = -outVal;
    return true;
}

static inline int opStrToInt(const String &op) {
    if (op == "-")  return 0;
    if (op == "+")  return 1;
    if (op == "D")  return 2;
    if (op == "I")  return 3;
    if (op == "G")  return 4;
    if (op == "L")  return 5;
    if (op == "R")  return 6;
    if (op == "DF") return 7;
    if (op == "V")  return 8;
    if (op == "C")  return 9;
    if (op == "P")  return 10;
    if (op == "Cw") return 11;
    if (op == "Hy") return 12;
    return -1;
}

static inline bool parseHysteresisThresholds(const String &thresholdStr, double &highThreshold, double &lowThreshold) {
    int sep = thresholdStr.indexOf("..");
    if (sep <= 0) return false;
    highThreshold = thresholdStr.substring(0, sep).toFloat();
    lowThreshold = thresholdStr.substring(sep + 2).toFloat();
    if (lowThreshold > highThreshold) {
        double tmp = highThreshold;
        highThreshold = lowThreshold;
        lowThreshold = tmp;
    }
    return true;
}
