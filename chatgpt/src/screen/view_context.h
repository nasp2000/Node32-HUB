#pragma once

// view_context.h - data shared by all pack-specific status views.
// The core builds the lines once; each view only renders them.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include <Arduino.h>

class ScreenManager; // full type in screen.h; views only take a reference

struct ScreenStatusContext {
    const String &line1;
    const String &line2;
    const String &line3;
    const String &ipStr;
    const String &ssidStr;
    const String &sinalStr;
    const String &storageInfo;
    bool line3UnikonOnly;
};

#endif // ENABLE_LCD_SCREEN
