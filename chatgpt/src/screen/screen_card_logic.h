#pragma once
// screen_card_logic.h — pure helpers for the status screen (host-testable).
//
// The status screen shows one "card" per line of information and rotates them
// so a 3-line OLED still shows everything. Two decisions live here, away from
// Arduino, so they can be unit-tested:
//   - how a text is fitted into a panel width (truncate + '~', never mid-glyph),
//   - which card is due at a given tick (deterministic, weighted by priority).
#include <stdint.h>
#include <stddef.h>

// Fit `in` into `cols` characters (NUL included). When it does not fit, the
// last visible character becomes '~' so a glance shows the text is cut.
// Returns the number of characters written (excluding the NUL).
inline size_t screenFitText(const char* in, char* out, size_t cols) {
    if (!out || cols == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    size_t len = 0;
    while (in[len] && (len + 1) < cols) ++len;   // room for the NUL
    const bool cut = (in[len] != '\0');
    if (cut && cols >= 2) len = cols - 2;        // room for '~' + NUL
    for (size_t i = 0; i < len; ++i) out[i] = in[i];
    if (cut && cols >= 2) out[len++] = '~';
    out[len] = '\0';
    return len;
}

// Which card is due. `weights[i]` = how many turns card i gets per cycle
// (0 is treated as 1). Cards are expanded in push order (the caller pushes the
// most important first), so the order is deterministic and the primary card
// reappears sooner.
inline uint8_t screenRotationIndex(const uint8_t* weights, uint8_t n, uint32_t tick) {
    if (!weights || n == 0) return 0;
    uint32_t total = 0;
    for (uint8_t i = 0; i < n; ++i) total += (weights[i] ? weights[i] : 1);
    if (total == 0) return 0;
    uint32_t pos = tick % total;
    for (uint8_t i = 0; i < n; ++i) {
        const uint32_t w = (weights[i] ? weights[i] : 1);
        if (pos < w) return i;
        pos -= w;
    }
    return 0;
}
