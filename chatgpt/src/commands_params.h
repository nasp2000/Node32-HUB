#pragma once
// Parameter rules for the scheduled-commands API (BACKLOG CMD-6).
//
// `interval` used to be read with getParamInt() → String::toInt(), which turns
// "12x" into 12, " 5" into 5 and "abc"/"" into 0. Two consequences:
//   * on an ENABLED row a typo silently became a cadence nobody asked for
//     ("12x" was stored as 12), and
//   * a missing interval was indistinguishable from an explicit 0: both ended
//     in the same 400 "interval", not because the input was understood but
//     because 0 happened to fail the range check.
//
// The rule is explicit here, not scattered in the handler:
//   * Missing  — the parameter is absent (or empty);
//   * Invalid  — anything that is not a run of decimal digits: "0", "-3",
//                "+5", " 5", "12x", "1e3", "5 s", or more than 9 digits
//                (int overflow);
//   * Ok       — 1..999999999, returned through *outValue.
//   * An ENABLED row must carry Ok (otherwise 400 "interval"): a row saved
//     with a cadence of 0 can never fire, so accepting it would be a silent
//     no-op saved as success.
//   * A DISABLED row accepts anything and stores 0 — the field is irrelevant
//     until the row is switched on, and refusing it would break a Save All that
//     only wants to park a command.
//
// Dependency-free on purpose: the parsing and the accept/reject decision are
// unit-tested on the host (tests/native/test_commands_param_interval.cpp).

#include <stdint.h>

enum class IntervalParam : uint8_t { Missing, Invalid, Ok };

// Strict parsing of one `interval` value. Never reads past the terminator.
inline IntervalParam parseIntervalParam(const char *raw, int *outValue) {
    if (outValue) *outValue = 0;
    if (raw == nullptr || raw[0] == '\0') return IntervalParam::Missing;

    long value = 0;
    int digits = 0;
    for (const char *p = raw; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return IntervalParam::Invalid; // sign, space, unit, exponent…
        if (++digits > 9) return IntervalParam::Invalid;         // would overflow int
        value = value * 10 + (*p - '0');
    }
    if (value <= 0) return IntervalParam::Invalid;               // "0" is not a cadence
    if (outValue) *outValue = (int)value;
    return IntervalParam::Ok;
}

struct IntervalDecision {
    bool accept;   // false → the POST answers 400 "interval" and stores nothing
    int interval;  // value handed to setSchedCmdSlot()
};

// What POST /api/scheduled-command-slot does with the parsed value.
inline IntervalDecision decideIntervalForSave(bool enabled, IntervalParam state, int parsed) {
    if (state == IntervalParam::Ok) {
        IntervalDecision d;
        d.accept = true;
        d.interval = parsed;
        return d;
    }
    IntervalDecision d;
    d.accept = !enabled; // disabled row: park it with 0; enabled row: refuse
    d.interval = 0;
    return d;
}
