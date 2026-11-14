// config_int_field.h — strict integer parsing + error payload for POSTed
// numeric config fields (header-only, dependency-free, host-testable).
//
// Why: the unified config handlers used to read numbers with
// `String::toInt()` and a `getInt(key, min, max)` lambda that returned -1 for
// absent, non-numeric AND out-of-range values alike. The caller then skipped
// every -1, so a malformed or out-of-range POST silently kept the old value
// and the client got `{"success":true}` (BACKLOG ACC-2).
//
// This header separates the three outcomes so the handler can answer with an
// explicit error instead of dropping the field:
//   CONFIG_INT_OK            parsed, inside [min,max]
//   CONFIG_INT_EMPTY         key present but the value is empty/blank
//   CONFIG_INT_NOT_A_NUMBER  not a whole number ("1.5", "12abc", "--3")
//   CONFIG_INT_OUT_OF_RANGE  a whole number outside [min,max]
//
// Absent keys are the caller's business (a partial POST must stay legal); the
// handler only calls this for keys the request actually carries.
//
// Tests: tests/native/test_config_int_field.cpp.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum ConfigIntStatus {
    CONFIG_INT_OK = 0,
    CONFIG_INT_EMPTY = 1,
    CONFIG_INT_NOT_A_NUMBER = 2,
    CONFIG_INT_OUT_OF_RANGE = 3,
};

struct ConfigIntParseResult {
    int value;              // only meaningful when status == CONFIG_INT_OK
    ConfigIntStatus status;
};

// Strict whole-number parser: optional sign, then digits only. Rejects floats,
// trailing text, bare signs and anything that overflows the useful range.
// Leading/trailing ASCII whitespace is tolerated (form values are URL-decoded).
static inline ConfigIntParseResult configIntParse(const char *raw, size_t len,
                                                  int minVal, int maxVal) {
    ConfigIntParseResult r = {0, CONFIG_INT_EMPTY};
    if (!raw) return r;

    size_t first = 0;
    size_t last = len;
    while (first < last && (raw[first] == ' ' || raw[first] == '\t' ||
                            raw[first] == '\r' || raw[first] == '\n')) {
        first++;
    }
    while (last > first && (raw[last - 1] == ' ' || raw[last - 1] == '\t' ||
                            raw[last - 1] == '\r' || raw[last - 1] == '\n')) {
        last--;
    }
    if (first >= last) return r;                 // "" or only blanks

    bool negative = false;
    if (raw[first] == '+' || raw[first] == '-') {
        negative = (raw[first] == '-');
        first++;
    }
    if (first >= last) {                         // "+" / "-" alone
        r.status = CONFIG_INT_NOT_A_NUMBER;
        return r;
    }

    int64_t value = 0;
    bool overflow = false;
    for (size_t i = first; i < last; i++) {
        char c = raw[i];
        if (c < '0' || c > '9') {
            r.status = CONFIG_INT_NOT_A_NUMBER;
            return r;
        }
        if (!overflow) {
            value = value * 10 + (int64_t)(c - '0');
            // Any real config bound is tiny; stop accumulating so a 30-digit
            // payload cannot wrap and land back inside the range.
            if (value > 1000000000LL) overflow = true;
        }
    }
    if (negative) value = -value;

    if (overflow || value < (int64_t)minVal || value > (int64_t)maxVal) {
        r.status = CONFIG_INT_OUT_OF_RANGE;
        return r;
    }
    r.value = (int)value;
    r.status = CONFIG_INT_OK;
    return r;
}

// Builds the JSON body the handler sends when a field is present but unusable:
//   {"success":false,"field":"web_max_sessions","min":1,"max":5,
//    "error":"out_of_range","message":"Invalid value for web_max_sessions: ..."}
// The field name is a compile-time literal, so no escaping is needed.
// Returns snprintf()'s would-be length (buffer is always NUL-terminated).
static inline int configIntErrorJson(char *out, size_t cap, const char *key,
                                     int minVal, int maxVal, ConfigIntStatus status) {
    const char *reason;
    const char *problem;
    switch (status) {
        case CONFIG_INT_EMPTY:
            reason = "empty";
            problem = "must not be empty";
            break;
        case CONFIG_INT_NOT_A_NUMBER:
            reason = "not_a_number";
            problem = "must be a whole number";
            break;
        default:
            reason = "out_of_range";
            problem = "is out of range";
            break;
    }
    return snprintf(out, cap,
                    "{\"success\":false,\"field\":\"%s\",\"min\":%d,\"max\":%d,"
                    "\"error\":\"%s\",\"message\":\"Invalid value for %s: %s (%d-%d).\"}",
                    key, minVal, maxVal, reason, key, problem, minVal, maxVal);
}
