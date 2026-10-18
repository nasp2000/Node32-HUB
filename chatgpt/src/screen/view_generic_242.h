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
    String netPrefix = (ctx.ssidStr == "Ethernet") ? "ETH:" : "IP:";
    oled.setCursor(0, 0);
    oled.print(mgr.stripAccents(netPrefix + ctx.ipStr).substring(0, 21));

    oled.setCursor(0, 9);
    oled.print(mgr.stripAccents(ctx.ssidStr + " " + ctx.sinalStr).substring(0, 21));

    oled.drawLine(0, 18, 127, 18, SSD1306_WHITE);

    String uptimeFw = "Up:" + mgr.formatUptime(millis() - bootTime) + " " + String(FIRMWARE_VERSION);
    oled.setCursor(0, 20);
    oled.print(mgr.stripAccents(uptimeFw).substring(0, 21));

    char heapCpuBuf[22];
    snprintf(heapCpuBuf, sizeof(heapCpuBuf), "Heap:%dKB CPU:%dMHz",
             (int)(ESP.getFreeHeap() / 1024), (int)ESP.getCpuFreqMHz());
    oled.setCursor(0, 29);
    oled.print(heapCpuBuf);

    oled.drawLine(0, 38, 127, 38, SSD1306_WHITE);

    oled.setCursor(0, 40);
    oled.print(mgr.stripAccents(ctx.storageInfo).substring(0, 21));

    oled.setCursor(0, 49);
    oled.print(mgr.stripAccents(ctx.line3).substring(0, 21));

    oled.display();}

#endif // ENABLE_LCD_SCREEN
