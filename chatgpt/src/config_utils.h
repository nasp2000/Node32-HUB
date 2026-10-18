#pragma once
#include <Arduino.h>
#include <stdint.h>

static inline uint32_t fnv1aHash(const String& s) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < s.length(); i++) {
        hash ^= (uint8_t)s[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline String shortKey(const String& key) {
    if (key == "server") return "s";
    if (key == "port") return "p";
    if (key == "user") return "u";
    if (key == "password") return "pw";
    if (key == "pass") return "pw";
    if (key == "ssid") return "id";
    if (key == "encryption") return "enc";
    if (key == "enabled") return "en";
    if (key == "interval") return "i";
    if (key == "command") return "c";
    if (key == "template") return "t";
    if (key == "recipients") return "rcp";
    if (key == "from_name") return "fn";
    if (key == "from_email") return "fe";
    if (key == "secure") return "sec";
    if (key == "baud_rate") return "b";
    if (key == "data_bits") return "db";
    if (key == "stop_bits") return "sb";
    if (key == "parity") return "par";
    if (key == "flow_control") return "fc";
    if (key == "days") return "d";
    if (key == "unit") return "un";
    if (key == "subject") return "sub";
    if (key == "body") return "bd";
    if (key == "include_timestamp") return "its";
    if (key == "max_lines") return "ml";
    if (key == "inactivity_timeout") return "it";
    return key;
}

static inline String longKey(const String& sk) {
    if (sk == "s") return "server";
    if (sk == "p") return "port";
    if (sk == "u") return "user";
    if (sk == "pw") return "password";
    if (sk == "id") return "ssid";
    if (sk == "enc") return "encryption";
    if (sk == "en") return "enabled";
    if (sk == "i") return "interval";
    if (sk == "c") return "command";
    if (sk == "t") return "template";
    if (sk == "rcp") return "recipients";
    if (sk == "fn") return "from_name";
    if (sk == "fe") return "from_email";
    if (sk == "sec") return "secure";
    if (sk == "b") return "baud_rate";
    if (sk == "db") return "data_bits";
    if (sk == "sb") return "stop_bits";
    if (sk == "par") return "parity";
    if (sk == "fc") return "flow_control";
    if (sk == "d") return "days";
    if (sk == "un") return "unit";
    if (sk == "sub") return "subject";
    if (sk == "bd") return "body";
    if (sk == "its") return "include_timestamp";
    if (sk == "ml") return "max_lines";
    if (sk == "it") return "inactivity_timeout";
    return sk;
}

static inline String normalizeOp(const String& opRaw) {
    String op = opRaw; op.trim();
    if (op.length() == 0) return "I";
    if (op.equalsIgnoreCase("i"))        return "I";
    if (op.equalsIgnoreCase("d"))        return "D";
    if (op.equalsIgnoreCase("g"))        return "G";
    if (op.equalsIgnoreCase("l"))        return "L";
    if (op.equalsIgnoreCase("r"))        return "R";
    if (op.equalsIgnoreCase("df") || op.equalsIgnoreCase("dr")) return "DF";
    if (op.equalsIgnoreCase("v"))        return "V";
    if (op.equalsIgnoreCase("c"))        return "C";
    if (op.equalsIgnoreCase("p"))        return "P";
    if (op.equalsIgnoreCase("cw"))       return "Cw";
    if (op.equalsIgnoreCase("hy"))       return "Hy";
    if (op != "I" && op != "D" && op != "+" && op != "-" &&
        op != "G" && op != "L" && op != "R" && op != "DF" &&
        op != "V" && op != "C" && op != "P" && op != "Cw" && op != "Hy") {
        return "I";
    }
    return op;
}

static inline int clampSchedCmdActive(int v) {
    if (v < 1) return 1;
    if (v > 10) return 10;
    return v;
}

static inline int clampSchedDtActive(int v) {
    if (v < 1) return 1;
    if (v > 8) return 8;
    return v;
}
