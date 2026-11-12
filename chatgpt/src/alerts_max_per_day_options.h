// alerts_max_per_day_options.h — the HTML an alert slot renders for its
// "alerts/day" control and its live-state line.
//
// Extracted from alerts_page_renderer.cpp so the markup that actually ships can
// be unit-tested without dragging the whole page (ConfigManager, AsyncWebServer)
// into a host build: a native test writes into a String-backed Print and checks
// the options, the selected value and the live-state placeholder.
#pragma once

#include <Arduino.h>

#include "alerts_limits.h"

// <option> list for the alerts/day select.
//   - always the documented list (3..100);
//   - a stored value that is not in the list (API-set, or a legacy config) is
//     inserted in ascending order and selected, so the page never shows a
//     neighbour and the next save never overwrites the real value (ALERTS-4).
inline size_t alertsMaxPerDayOptions(Print& out, int selected) {
    size_t n = 0;
    char numBuf[8];
    bool emittedExtra = false;
    const int extra = alertsMaxPerDayExtraOption(selected);

    auto emitOption = [&](int v, bool isSelected) {
        snprintf(numBuf, sizeof(numBuf), "%d", v);
        out.print(F("<option value='"));
        n += 15;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print('\'');
        n += 1;
        if (isSelected) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print(F(" alerts/day</option>"));
        n += 21;
    };

    for (int i = 0; i < alertsMaxPerDayOptionCount(); ++i) {
        const int v = alertsMaxPerDayOptionAt(i);
        if (extra > 0 && !emittedExtra && extra < v) {
            emitOption(extra, true);
            emittedExtra = true;
        }
        emitOption(v, v == selected);
    }
    if (extra > 0 && !emittedExtra) emitOption(extra, true);
    return n;
}

// Live state line for inactivity slot idx (filled later by the page from
// /api/status). Kept next to the options so both are covered by one test.
inline size_t alertsLiveStateDiv(Print& out, int idx) {
    size_t n = 0;
    char idBuf[8];
    snprintf(idBuf, sizeof(idBuf), "%d", idx);
    out.print(F("<div id='alert_live_"));
    n += 24;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='font-size:10px;line-height:1.3;color:#94a3b8;margin-top:2px'>--</div>"));
    n += 82;
    return n;
}
