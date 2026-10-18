#pragma once

// anim_environment.h - boot animation for the Environment pack.
// Moved verbatim from screen.cpp; behavior unchanged.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_ENVIRONMENT)
static void runEnvironmentBootAnimation(Adafruit_SSD1306& display) {
    const uint32_t animStart = millis();
    uint32_t nextFrame = animStart;
    const uint32_t durationMs = 12000UL;
    const int16_t sunBaseY = 10;
    const int16_t cloudY = 26;

    while ((millis() - animStart) < durationMs) {
        uint32_t now = millis();
        if ((int32_t)(now - nextFrame) < 0) {
            delay(1);
            continue;
        }
        nextFrame += 66UL; // ~15 FPS

        uint32_t t = now - animStart;
        int16_t sunX = (int16_t)(8 + ((t / 3U) % 112U));
        int16_t cloudX = (int16_t)(128 - ((t / 5U) % 160U));
        int16_t leafX = (int16_t)((t / 4U) % 140U) - 10;
        int16_t leafY = (int16_t)(46 + ((t / 450U) % 2U));
        uint8_t thermoLevel = (uint8_t)((t / 450U) % 11U);

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);

        // Title block
        display.setTextSize(1);
        display.setCursor(12, 0);
        display.print(F("HomeE32"));
        display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

        // Sun icon (moving)
        display.drawCircle(sunX, sunBaseY + 6, 5, SSD1306_WHITE);
        display.drawLine(sunX - 8, sunBaseY + 6, sunX - 12, sunBaseY + 6, SSD1306_WHITE);
        display.drawLine(sunX + 8, sunBaseY + 6, sunX + 12, sunBaseY + 6, SSD1306_WHITE);
        display.drawLine(sunX, sunBaseY - 2, sunX, sunBaseY - 6, SSD1306_WHITE);
        display.drawLine(sunX, sunBaseY + 14, sunX, sunBaseY + 18, SSD1306_WHITE);

        // Cloud icon (moving)
        int16_t cx = cloudX;
        display.fillCircle(cx + 8, cloudY, 5, SSD1306_WHITE);
        display.fillCircle(cx + 14, cloudY - 2, 6, SSD1306_WHITE);
        display.fillCircle(cx + 22, cloudY, 5, SSD1306_WHITE);
        display.fillRect(cx + 8, cloudY, 14, 5, SSD1306_WHITE);

        // Thermometer icon with animated level
        display.drawRoundRect(104, 16, 10, 26, 3, SSD1306_WHITE);
        display.fillCircle(109, 44, 5, SSD1306_WHITE);
        for (uint8_t i = 0; i < thermoLevel; i++) {
            display.drawFastHLine(106, 40 - (int16_t)i * 2, 6, SSD1306_WHITE);
        }

        // Leaf icon (moving near bottom)
        int16_t lx = leafX;
        int16_t ly = leafY;
        display.drawCircle(lx + 4, ly, 3, SSD1306_WHITE);
        display.drawCircle(lx + 8, ly + 2, 3, SSD1306_WHITE);
        display.drawLine(lx + 2, ly + 6, lx + 10, ly + 10, SSD1306_WHITE);

        // Footer text
        display.setCursor(0, 54);
        display.print(F("Sensors and environment"));

        display.display();
    }

    display.clearDisplay();
    display.display();
}
#endif

#endif // ENABLE_LCD_SCREEN
