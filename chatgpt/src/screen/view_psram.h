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
#include "ConfigManager.h"
#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif

extern volatile StorageMode currentStorageMode;
extern ConfigManager configManager;
extern unsigned long bootTime;

// Same compile exposure as the original showStatusScreen branch.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RS232)

static inline void screenDrawPsramView(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                       const ScreenStatusContext &ctx) {
    // Dedicated 2.42" layout. Width comes from the panel object (no hardcoded
    // 21) and truncation marks the cut with '~' (ScreenManager::fitForPanel).
    const int jitter = (int)((millis() / 600000UL) % 2);   // burn-in protection
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
    } else if (ENABLE_RS232==1) {
        // The old "RS232:ON XON:ON" echoed the build flags; the baud rate is
        // what an operator actually needs to see next to the port.
        static const int kBauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        const int bi = configManager.getRs232BaudrateIndex();
        const int baud = (bi >= 0 && bi < 8) ? kBauds[bi] : 9600;
        rsLine = String("RS232 ") + String(baud);
    } else {
        rsLine = String("RS232 off");
    }

    oled.setCursor(jitter, 0);
    oled.print(mgr.fitForPanel(String(PACK_SYSTEM_NAME)));
    oled.setCursor(jitter, 9);
    oled.print(mgr.fitForPanel(String("IP:") + ctx.ipStr + " " + ctx.sinalStr));
    oled.setCursor(jitter, 20);
    oled.print(mgr.fitForPanel(ctx.line3));
    oled.setCursor(jitter, 29);
    oled.print(mgr.fitForPanel(rsLine));
    oled.setCursor(jitter, 40);
    oled.print(mgr.fitForPanel(heapLine + " " + storageCompact));
    oled.setCursor(jitter, 49);
    oled.print(mgr.fitForPanel(uptimeLine));
    oled.display();}

#endif // SELECTED_PACK == PACK_RS232

#endif // ENABLE_LCD_SCREEN
