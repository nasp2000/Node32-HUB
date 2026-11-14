// alerts_slot_merge.h — what a save does to a DISABLED alert slot.
//
// The alerts page posts every row, disabled ones included (their inputs are not
// disabled), carrying whatever the form shows — the defaults when the row was
// never edited. Writing those back overwrote the tuned timeout/max/e-mails of a
// disabled slot (BACKLOG ALERTS-6). Rule: `enabled` always follows the request,
// the remaining fields of a disabled row keep what is stored; re-enable the row
// to edit them.
#pragma once

#include <Arduino.h>

inline int alertsMergeInt(bool enabled, int formValue, int storedValue) {
    return enabled ? formValue : storedValue;
}

inline String alertsMergeText(bool enabled, const String& formValue, const String& storedValue) {
    return enabled ? formValue : storedValue;
}
