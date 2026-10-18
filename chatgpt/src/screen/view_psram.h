#pragma once

// view_psram.h - PSRAM pack status layout for 2.42" displays.
// Moved verbatim from ScreenManager::showStatusScreen; only local line
// variables were rewritten to the shared ScreenStatusContext.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen.h"
#include "screen/view_context.h"
#include "structures.h"
#include "sdcard.h"
#include "psram_vfs.h"
#include "littlefs_manager.h"
#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif

extern volatile StorageMode currentStorageMode;
extern unsigned long bootTime;

// Same compile exposure as the original showStatusScreen branch.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RS232)

static inline void screenDrawPsramView(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                       const ScreenStatusContext &ctx) {
    // Dedicated 2.42" profile for PSRAM/IPG/Amada pack.
    const int rowWidthChars = 21;
    String storageCompact = "ST:N/A";
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        uint64_t totalBytes = sdcardTotalBytes();
        uint64_t usedBytes = sdcardUsedBytes();
        uint64_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        storageCompact = String("SD:") + String((uint32_t)(freeBytes / (1024ULL * 1024ULL))) + "M";
    } else {
        uint64_t freeBytes = littlefsFreeBytes();
        uint64_t totalBytesFS = littlefsTotalBytes();
        if (totalBytesFS > 0) {
            storageCompact = String("FS:") + String((uint32_t)(freeBytes / 1024U)) + "K";
        }
    }

    String uptimeLine = String("Up:") + mgr.formatUptime(millis() - bootTime);
    String heapLine = String("Heap:") + String(ESP.getFreeHeap() / 1024U) + "K";
    String rsLine;
    if (ctx.line3UnikonOnly) {
#if ENABLE_UNIKON==1
        UnikonStatus us = unikonGetStatus();
        rsLine = String("Unikon:") + (us.connected ? "ON" : "OFF") +
                 " +" + String(us.lastNewCount) +
                 " T:" + String(us.totalKnown);
#else
        rsLine = String("Unikon: N/A");
#endif
    } else {
        rsLine = String("RS232:") + (ENABLE_RS232==1 ? "ON" : "OFF") +
                 " XON:" + (ENABLE_RS232_XONXOFF==1 ? "ON" : "OFF");
    }

    oled.setCursor(0, 0);
    oled.print(mgr.stripAccents(String(PACK_SYSTEM_NAME)).substring(0, rowWidthChars));
    oled.setCursor(0, 9);
    oled.print(mgr.stripAccents(String("IP:") + ctx.ipStr + " " + ctx.sinalStr).substring(0, rowWidthChars));
    oled.setCursor(0, 20);
    oled.print(mgr.stripAccents(ctx.line3).substring(0, rowWidthChars));
    oled.setCursor(0, 29);
    oled.print(mgr.stripAccents(rsLine).substring(0, rowWidthChars));
    oled.setCursor(0, 40);
    oled.print(mgr.stripAccents(heapLine + " " + storageCompact).substring(0, rowWidthChars));
    oled.setCursor(0, 49);
    oled.print(mgr.stripAccents(uptimeLine).substring(0, rowWidthChars));
    oled.display();}

#endif // SELECTED_PACK == PACK_RS232

#endif // ENABLE_LCD_SCREEN
