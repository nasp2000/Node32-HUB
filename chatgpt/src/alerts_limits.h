// alerts_limits.h — one source of truth for the alert "max per day" range.
//
// ALERTS-4: the page offered 3..60 while the API accepted 1..100 and the docs
// documented 1..100 (PAGES/inactivity_alerts.md), so an API-set value above 60
// could not be seen or edited in the UI. The documented limit (100) wins; the
// select now covers it and — because an API may still store any value in range
// — the renderer appends the stored value when the list does not contain it,
// so the UI always shows the truth instead of silently snapping to the closest
// option on the next save.
#pragma once

#include <stddef.h>

// Values offered in the select, ascending. It must end at the documented limit.
static const int ALERTS_MAX_PER_DAY_VALUES[] = {3, 6, 9, 12, 15, 20, 25, 30, 45, 60, 75, 90, 100};
static const int ALERTS_MAX_PER_DAY_LIMIT = 100;   // same bound as the config handlers
static const int ALERTS_MAX_PER_DAY_MIN = 1;

inline int alertsMaxPerDayOptionCount() {
    return (int)(sizeof(ALERTS_MAX_PER_DAY_VALUES) / sizeof(ALERTS_MAX_PER_DAY_VALUES[0]));
}

inline int alertsMaxPerDayOptionAt(int index) {
    if (index < 0 || index >= alertsMaxPerDayOptionCount()) return 0;
    return ALERTS_MAX_PER_DAY_VALUES[index];
}

inline bool alertsMaxPerDayIsOffered(int value) {
    for (int i = 0; i < alertsMaxPerDayOptionCount(); i++) {
        if (ALERTS_MAX_PER_DAY_VALUES[i] == value) return true;
    }
    return false;
}

// A stored value the select must still be able to show (API-set, or a legacy
// config written before the list was extended). 0 means "no extra option".
inline int alertsMaxPerDayExtraOption(int stored) {
    if (stored < ALERTS_MAX_PER_DAY_MIN || stored > ALERTS_MAX_PER_DAY_LIMIT) return 0;
    return alertsMaxPerDayIsOffered(stored) ? 0 : stored;
}

// Range check shared with the config handlers (1..100 inclusive).
inline bool alertsMaxPerDayInRange(int value) {
    return value >= ALERTS_MAX_PER_DAY_MIN && value <= ALERTS_MAX_PER_DAY_LIMIT;
}
