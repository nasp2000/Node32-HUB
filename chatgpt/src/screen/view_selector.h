#pragma once

// view_selector.h - single compile-time switch from (SCREEN_TYPE,
// SELECTED_PACK) to a pack-specific status renderer.
//
// To add a screen for a new pack: create screen/view_<pack>.h with a
//   static inline void screenDraw<Pack>View(ScreenManager&, Adafruit_SSD1306&,
//                                           const ScreenStatusContext&)
// function and add one branch below. Core and other packs stay untouched.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen/view_context.h"
#include "screen/view_generic_096.h"
#include "screen/view_generic_242.h"
#include "screen/view_environment.h"
#include "screen/view_psram.h"
#include "screen/view_rambox.h"
#include "screen/view_touch7.h"

#if SCREEN_TYPE == 3
static inline void screenDrawStatusView(ScreenManager &mgr,
                                        const ScreenStatusContext &ctx) {
    screenDrawTouch7View(mgr, ctx);
}
#else
static inline void screenDrawStatusView(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                        const ScreenStatusContext &ctx) {
#if SCREEN_TYPE != 2
    screenDrawGeneric096(mgr, ctx.line1, ctx.line2, ctx.line3);
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
    screenDrawRamboxView(mgr, oled, ctx);
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_ENVIRONMENT)
    screenDrawEnvironmentView(mgr, oled, ctx);
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RS232)
    screenDrawPsramView(mgr, oled, ctx);
#else
    screenDrawGeneric242(mgr, oled, ctx);
#endif
}
#endif // SCREEN_TYPE == 3

#endif // ENABLE_LCD_SCREEN
