#pragma once
// lang_screen.h — OLED status-card labels.
//
// Why a separate table: lang_active.cpp holds two POSITIONAL arrays (PT and EN)
// aligned with the LangStrings struct, and the "/* key */" comments are not
// checked by the compiler — one field inserted in the wrong place shifts every
// string after it silently. The cards therefore carry their own small struct.
//
// Sizes: the 0.96" runs text size 2, so a row shows 10 characters; keep the
// entries short (the renderer truncates with '~' anyway).

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1

#include <Arduino.h>
#include "lang_active.h"

struct ScreenCardLabels {
    const char* envTemp;    // "Temp:"
    const char* envHum;     // "Hum:"  (PT "Hum:")
    const char* envPress;   // "P:"    (PT "P:")
    const char* sched;      // "Sched:" / "Agend:"
    const char* schedDest;  // " dest:"
    const char* schedTpl;   // " tpl:"
    const char* noModules;  // "no mods" / "sem mods"  (<=10 chars)
    const char* offline;    // "! OFFLINE" / "! SEM REDE"
};

inline const ScreenCardLabels& screenCardLabels() {
    static const ScreenCardLabels en = {
        "Temp:", "Hum:", "P:", "Sched:", " dest:", " tpl:", "no mods", "! OFFLINE"
    };
    static const ScreenCardLabels pt = {
        "Temp:", "Hum:", "P:", "Agend:", " dest:", " tpl:", "sem mods", "! SEM REDE"
    };
    const char* code = activeLangCode;
    const bool isPt = (code && code[0] == 'p' && code[1] == 't');
    return isPt ? pt : en;
}

#endif // ENABLE_LCD_SCREEN
