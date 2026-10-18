#pragma once

#include <stdint.h>

// Pure weekly-window logic for the radio schedule. No Arduino/ESP32 deps so it
// can be unit-tested natively. A slot selects week days (bitmask, bit0=Sunday)
// and a daily time window [start, end). End <= start means the window wraps
// over midnight (overnight slot, e.g. 22:00 -> 07:00).

enum {
    SCHED_WINDOW_NONE = 0,  // outside the slot's window
    SCHED_WINDOW_START = 1, // in the START day (day the window begins)
    SCHED_WINDOW_TAIL = 2   // in the TAIL day (overnight continuation)
};

// Returns SCHED_WINDOW_NONE / START / TAIL for the given day.
// daysMask: bit0=Sunday .. bit6=Saturday. minuteOfDay: 0..1439.
// On the START day the window counts from startMin to 24:00 (end==start means
// full-day: TAIL runs 00:00..start-1 on the same day, then START takes over).
static inline uint8_t schedWindowState(uint32_t daysMask, uint8_t sh, uint8_t sm,
                                       uint8_t eh, uint8_t em,
                                       uint8_t wday, uint16_t minuteOfDay) {
    if (!(daysMask & (1u << (wday & 7u)))) return SCHED_WINDOW_NONE;
    uint16_t startMin = (uint16_t)sh * 60u + sm;
    uint16_t endMin = (uint16_t)eh * 60u + em;
    if (endMin <= startMin) {
        // Overnight (or full-day when end == start)
        if (minuteOfDay >= startMin) return SCHED_WINDOW_START;
        if (minuteOfDay < endMin) return SCHED_WINDOW_TAIL;
        return SCHED_WINDOW_NONE;
    }
    if (minuteOfDay >= startMin && minuteOfDay < endMin) return SCHED_WINDOW_START;
    return SCHED_WINDOW_NONE;
}
