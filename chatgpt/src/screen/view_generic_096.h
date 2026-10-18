#pragma once

// view_generic_096.h - status layout for 0.96" displays: 3 scrolled lines.
// Moved verbatim from ScreenManager::showStatusScreen.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen.h"

static inline void screenDrawGeneric096(ScreenManager &mgr, const String &line1,
                                        const String &line2, const String &line3) {
    String outLine3 = mgr.stripAccents(line3);
    mgr.displayMessage(line1, line2, outLine3);
}

#endif // ENABLE_LCD_SCREEN
