#pragma once

// view_generic_242.h - fallback status layout for 2.42" displays when no
// pack defines its own view. Moved verbatim from showStatusScreen.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen.h"
#include "screen/view_context.h"

extern const char *FIRMWARE_VERSION;
extern unsigned long bootTime;

static inline void screenDrawGeneric242(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                        const ScreenStatusContext &ctx) {
    // Generic 2.42" SSD1309 layout.
    const int jitter = (int)((millis() / 600000UL) % 2);   // burn-in protection
    String netPrefix = (ctx.ssidStr == "Ethernet") ? "ETH:" : "IP:";
    oled.setCursor(jitter, 0);
    oled.print(mgr.fitForPanel(netPrefix + ctx.ipStr));

    oled.setCursor(jitter, 9);
    oled.print(mgr.fitForPanel(ctx.ssidStr + " " + ctx.sinalStr));

    oled.drawLine(0, 18, 127, 18, SSD1306_WHITE);

    String uptimeFw = "Up:" + mgr.formatUptime(millis() - bootTime) + " " + String(FIRMWARE_VERSION);
    oled.setCursor(jitter, 20);
    oled.print(mgr.fitForPanel(uptimeFw));

    char heapCpuBuf[22];
    snprintf(heapCpuBuf, sizeof(heapCpuBuf), "Heap:%dKB CPU:%dMHz",
             (int)(ESP.getFreeHeap() / 1024), (int)ESP.getCpuFreqMHz());
    oled.setCursor(jitter, 29);
    oled.print(heapCpuBuf);

    oled.drawLine(0, 38, 127, 38, SSD1306_WHITE);

    oled.setCursor(jitter, 40);
    oled.print(mgr.fitForPanel(ctx.storageInfo));

    oled.setCursor(jitter, 49);
    oled.print(mgr.fitForPanel(ctx.line3));

    oled.display();}

#endif // ENABLE_LCD_SCREEN
