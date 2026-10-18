#pragma once

// view_rambox.h - Rambox pack status layout for 2.42" displays (HUD style).
// Moved verbatim from ScreenManager::showStatusScreen; only local line
// variables were rewritten to the shared ScreenStatusContext.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include <string.h>
#include <time.h>
#include "screen.h"
#include "screen/view_context.h"
#include "structures.h"
#include "sdcard.h"
#include "psram_vfs.h"
#include "littlefs_manager.h"
#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif
#include "languages/lang_active.h"

extern volatile StorageMode currentStorageMode;

// Same compile exposure as the original showStatusScreen branch: this view
// only exists in Rambox builds (it uses Unikon APIs unconditionally).
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)

static inline void screenDrawRamboxView(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                        const ScreenStatusContext &ctx) {
    // Dedicated 2.42" Rambox layout (watch-style):
    // 1) Compact top status bar
    // 2) Large centered HH:MM (blink each second)
    // 3) Centered date
    // 4) Bottom rotating status line
    static uint16_t currentArrivalIndex = 0;
    static unsigned long lastArrivalRotateMs = 0;
    static uint8_t bottomPage = 0;
    static unsigned long lastBottomRotateMs = 0;

    const unsigned long arrivalRotateMs = 3000UL;
    const unsigned long bottomRotateMs = 3000UL;
    const int rowWidthChars = 21;
    const int rowCharWidthPx = 6;
    const int16_t leftPaneX = 2;
    const int16_t leftPaneW = 68;
    const int16_t rightPaneX = 74;
    const int16_t rightPaneW = 50;

    auto formatHms = [&](const char* hhmmss, bool colonOn, bool withSeconds) -> String {
        if (!hhmmss || strlen(hhmmss) < 6) return String("--:--:--");
        char t[10];
        const char sep = colonOn ? ':' : ' ';
        if (withSeconds) {
            snprintf(t, sizeof(t), "%c%c%c%c%c%c%c%c", hhmmss[0], hhmmss[1], sep, hhmmss[2], hhmmss[3], sep, hhmmss[4], hhmmss[5]);
        } else {
            snprintf(t, sizeof(t), "%c%c%c%c%c", hhmmss[0], hhmmss[1], sep, hhmmss[2], hhmmss[3]);
        }
        return String(t);
    };

    auto formatClockNow = [&](bool colonOn, bool withSeconds) -> String {
        time_t epoch = time(nullptr);
        if (epoch < 100000) return String("--:--:--");
        struct tm ti;
        localtime_r(&epoch, &ti);
        char b[16];
        const char sep = colonOn ? ':' : ' ';
        if (withSeconds) {
            snprintf(b, sizeof(b), "%02d%c%02d%c%02d", ti.tm_hour, sep, ti.tm_min, sep, ti.tm_sec);
        } else {
            snprintf(b, sizeof(b), "%02d%c%02d", ti.tm_hour, sep, ti.tm_min);
        }
        return String(b);
    };

    auto formatDateNow = [&]() -> String {
        time_t epoch = time(nullptr);
        if (epoch < 100000) return String("----.--.--");
        struct tm ti;
        localtime_r(&epoch, &ti);
        char b[16];
        snprintf(b, sizeof(b), "%04d.%02d.%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        return String(b);
    };

    auto drawCenteredRow = [&](int16_t y, const String& text) {
        String clean = mgr.stripAccents(text);
        if ((int)clean.length() > rowWidthChars) clean = clean.substring(0, rowWidthChars);
        int16_t widthPx = (int16_t)clean.length() * rowCharWidthPx;
        int16_t x = (SSD1306_WIDTH - widthPx) / 2;
        if (x < 0) x = 0;
        oled.setCursor(x, y);
        oled.print(clean);
    };

    auto drawLeftRow = [&](int16_t y, const String& text) {
        oled.setCursor(0, y);
        String out = mgr.stripAccents(text);
        if ((int)out.length() > rowWidthChars) out = out.substring(0, rowWidthChars);
        oled.print(out);
    };

    auto drawCenteredInBox = [&](int16_t x, int16_t w, int16_t y, const String& text, uint8_t size) {
        String clean = mgr.stripAccents(text);
        int16_t charPx = (int16_t)(6 * size);
        int16_t maxChars = (w > charPx) ? (w / charPx) : 1;
        if (maxChars < 1) maxChars = 1;
        if ((int)clean.length() > maxChars) clean = clean.substring(0, maxChars);
        int16_t textW = (int16_t)clean.length() * charPx;
        int16_t drawX = x + (w - textW) / 2;
        if (drawX < x) drawX = x;
        oled.setTextSize(size);
        oled.setCursor(drawX, y);
        oled.print(clean);
    };

    auto drawHudFrame = [&]() {
        oled.drawRect(0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, SSD1306_WHITE);
        oled.drawLine(0, 10, SSD1306_WIDTH - 1, 10, SSD1306_WHITE);
        oled.drawLine(72, 11, 72, 51, SSD1306_WHITE);
        oled.drawLine(0, 51, SSD1306_WIDTH - 1, 51, SSD1306_WHITE);

        // Corner accents for a technical HUD look.
        oled.drawLine(2, 2, 10, 2, SSD1306_WHITE);
        oled.drawLine(2, 2, 2, 8, SSD1306_WHITE);
        oled.drawLine(SSD1306_WIDTH - 11, 2, SSD1306_WIDTH - 3, 2, SSD1306_WHITE);
        oled.drawLine(SSD1306_WIDTH - 3, 2, SSD1306_WIDTH - 3, 8, SSD1306_WHITE);
        oled.drawLine(2, SSD1306_HEIGHT - 3, 10, SSD1306_HEIGHT - 3, SSD1306_WHITE);
        oled.drawLine(2, SSD1306_HEIGHT - 9, 2, SSD1306_HEIGHT - 3, SSD1306_WHITE);
        oled.drawLine(SSD1306_WIDTH - 11, SSD1306_HEIGHT - 3, SSD1306_WIDTH - 3, SSD1306_HEIGHT - 3, SSD1306_WHITE);
        oled.drawLine(SSD1306_WIDTH - 3, SSD1306_HEIGHT - 9, SSD1306_WIDTH - 3, SSD1306_HEIGHT - 3, SSD1306_WHITE);
    };

    unsigned long now = millis();

    if ((now - lastBottomRotateMs) >= bottomRotateMs) {
        lastBottomRotateMs = now;
        bottomPage = (uint8_t)((bottomPage + 1) % 2);
    }

    bool colonOn = true;
    time_t epochNow = time(nullptr);
    if (epochNow > 100000) {
        colonOn = ((epochNow % 2) == 0);
    } else {
        colonOn = (((now / 1000UL) % 2UL) == 0UL);
    }

    UnikonStatus us = unikonGetStatus();
    static UnikonArrival arrivalsBuf[UNIKON_MAX_ARRIVALS];
    uint16_t arrivalCount = unikonGetArrivals(arrivalsBuf, UNIKON_MAX_ARRIVALS);

    if (arrivalCount == 0) {
        currentArrivalIndex = 0;
    } else if (currentArrivalIndex >= arrivalCount) {
        currentArrivalIndex = 0;
    }

    if (arrivalCount > 1 && (now - lastArrivalRotateMs) >= arrivalRotateMs) {
        lastArrivalRotateMs = now;
        currentArrivalIndex = (uint16_t)((currentArrivalIndex + 1) % arrivalCount);
    }

    String storageCompact = "ST:N/A";
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        uint64_t totalBytes = sdcardTotalBytes();
        uint64_t usedBytes = sdcardUsedBytes();
        uint64_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        uint32_t freeMB = (uint32_t)(freeBytes / (1024ULL * 1024ULL));
        if (freeMB <= 999U) {
            storageCompact = String("SD:") + String(freeMB) + "M";
        } else {
            float freeGB = (float)freeBytes / (1024.0f * 1024.0f * 1024.0f);
            char gbBuf[10];
            snprintf(gbBuf, sizeof(gbBuf), "SD:%.1fG", freeGB);
            storageCompact = String(gbBuf);
        }
    } else {
        uint64_t freeBytes = littlefsFreeBytes();
        uint64_t totalBytesFS = littlefsTotalBytes();
        if (totalBytesFS > 0) {
            uint32_t freeKB = (uint32_t)(freeBytes / 1024U);
            if (freeKB <= 9999U) {
                storageCompact = String("FS:") + String(freeKB) + "K";
            } else {
                float freeMB = (float)freeKB / 1024.0f;
                char mbBuf[10];
                snprintf(mbBuf, sizeof(mbBuf), "FS:%.1fM", freeMB);
                storageCompact = String(mbBuf);
            }
        }
    }
    String lblContest = L(activeLang->screenContest);
    String lblTraining = L(activeLang->screenTraining);
    String lblArrivals = L(activeLang->screenArrivals);
    String lblNoPigeons = L(activeLang->screenNoPigeons);
    String lblRing = L(activeLang->screenRing);

    String netType = (ctx.ssidStr == "Ethernet") ? "ETH" : "WIFI";
    String signalShort = ctx.sinalStr;
    signalShort.replace("dBm", "");
    signalShort.replace(" ", "");
    if (signalShort.length() > 4) signalShort = signalShort.substring(0, 4);
    String linkToken = us.connected ? "ON" : "OFF";
    String heapShort = String("H:") + String(ESP.getFreeHeap() / 1024U) + "K";
    String topLeft = String(PACK_SYSTEM_NAME);
    String topRight = heapShort;

    String fancier = String(us.fancierName);
    fancier.trim();
    if (fancier.length() == 0) fancier = "N/A";

    String modeText = lblContest;
    if (arrivalCount > 0 && currentArrivalIndex < arrivalCount && arrivalsBuf[currentArrivalIndex].isTraining) {
        modeText = lblTraining;
    }

    String bigTime;
    if (arrivalCount > 0 && currentArrivalIndex < arrivalCount) {
        bigTime = formatHms(arrivalsBuf[currentArrivalIndex].arrivalTime, colonOn, false);
    } else {
        bigTime = formatClockNow(colonOn, false);
    }

    String dateLine = formatDateNow();

    String networkLine = netType + " " + signalShort + " IP:" + ctx.ipStr;
    if (networkLine.length() > 30) networkLine = networkLine.substring(0, 30);

    String arrivalDetail;
    if (arrivalCount > 0 && currentArrivalIndex < arrivalCount) {
        String arrTime = formatHms(arrivalsBuf[currentArrivalIndex].arrivalTime, true, true);
        arrivalDetail = lblRing + ":" + String(arrivalsBuf[currentArrivalIndex].ring) + " " + arrTime;
    } else {
        arrivalDetail = lblNoPigeons;
    }

    String bottomLine;
    if (bottomPage == 0) {
        bottomLine = networkLine;
    } else {
        bottomLine = arrivalDetail;
    }

    drawHudFrame();

    oled.setTextSize(1);
    drawCenteredInBox(2, 88, 2, topLeft, 1);
    drawCenteredInBox(92, 34, 2, topRight, 1);

    drawCenteredInBox(leftPaneX, leftPaneW, 18, bigTime, 2);
    drawCenteredInBox(leftPaneX, leftPaneW, 38, dateLine, 1);

    oled.drawRect(rightPaneX, 16, rightPaneW, 30, SSD1306_WHITE);
    drawCenteredInBox(rightPaneX + 2, rightPaneW - 4, 18, String("COL:") + fancier, 1);
    if (arrivalCount > 0) {
        drawCenteredInBox(rightPaneX + 2, rightPaneW - 4, 28, modeText, 1);
    } else {
        drawCenteredInBox(rightPaneX + 2, rightPaneW - 4, 28, lblNoPigeons, 1);
    }
    drawCenteredInBox(rightPaneX + 2, rightPaneW - 4, 38, lblArrivals + ":" + String(arrivalCount), 1);

    int16_t baseX = (SSD1306_WIDTH - 14) / 2;
    for (uint8_t i = 0; i < 2; i++) {
        int16_t x = baseX + (i * 7);
        if (i == bottomPage) {
            oled.fillRect(x, 50, 4, 2, SSD1306_WHITE);
        } else {
            oled.drawRect(x, 50, 4, 2, SSD1306_WHITE);
        }
    }
    drawCenteredInBox(2, 124, 54, bottomLine, 1);

    oled.display();}

#endif // SELECTED_PACK == PACK_RAMBOX

#endif // ENABLE_LCD_SCREEN
