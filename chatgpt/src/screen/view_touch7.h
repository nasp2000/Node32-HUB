#pragma once

// view_touch7.h — TouchMeteo-7 status layout (RGB 800x480 LVGL).
// Header-only, static inline, no new build units (repo rule for src/screen/).
// The LVGL engine owns the real layout; this view forwards the shared
// ScreenStatusContext lines to it (footer values).

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen/view_context.h"

#if SCREEN_TYPE == 3
#include "touch7_display.h"

static inline void screenDrawTouch7View(ScreenManager& mgr,
                                        const ScreenStatusContext& ctx) {
    (void)mgr;
    touch7Display.showStatus(ctx.line1, ctx.line2, ctx.line3);
    touch7Display.setWifiInfo(ctx.ssidStr + " " + ctx.ipStr + " " + ctx.sinalStr);
}

#else

static inline void screenDrawTouch7View(ScreenManager& mgr,
                                        const ScreenStatusContext& ctx) {
    (void)mgr;
    (void)ctx;
}

#endif // SCREEN_TYPE == 3
#endif // ENABLE_LCD_SCREEN
