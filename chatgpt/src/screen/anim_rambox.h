#pragma once

// anim_rambox.h - boot animation for the Rambox pack on 2.42" displays.
// Moved verbatim from screen.cpp; behavior unchanged.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include <string.h>
#include "screen.h"

static void drawAsciiBird(Adafruit_SSD1306& display, int16_t x, int16_t y, uint8_t scale, uint8_t flapFrame) {
    if (scale < 1) scale = 1;
    static const char* kBirdFrames[] = {
        "\\^/",
        "<v>",
        "/^\\",
        "<o>",
        "/v\\",
        "<^>"
    };
    const uint8_t frame = flapFrame % 6;
    display.setTextSize(scale);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    display.print(kBirdFrames[frame]);
}

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX) && (SCREEN_TYPE == 2)
static void runRamboxBootAnimation(Adafruit_SSD1306& display) {
    struct Bird {
        int16_t x;
        int16_t y;
        int8_t vx;
        int8_t vy;
        uint8_t scale;
    };

    Bird birds[6];
    randomSeed((uint32_t)micros());
    for (uint8_t i = 0; i < 6; i++) {
        birds[i].scale = (i % 2 == 0) ? 2 : 1;
        int16_t spanX = (int16_t)(3 * 6 * birds[i].scale);
        int16_t spanY = (int16_t)(8 * birds[i].scale);
        birds[i].x = random(-spanX, SSD1306_WIDTH - 1);
        birds[i].y = random(0, max(1, SSD1306_HEIGHT - spanY));
        birds[i].vx = random(1, 4);
        birds[i].vy = random(-1, 2);
        if (birds[i].vy == 0) birds[i].vy = 1;
    }

    const uint32_t animStart = millis();
    uint32_t nextFrame = animStart;
    const char* titleTop = "Node32-";
    const char* titleBottom = "RAMBOX";
    const int16_t titleTopX = (SSD1306_WIDTH - (int16_t)strlen(titleTop) * 12) / 2;
    const int16_t titleBottomX = (SSD1306_WIDTH - (int16_t)strlen(titleBottom) * 12) / 2;
    const uint32_t durationMs = 10000UL;
    while ((millis() - animStart) < durationMs) {
        uint32_t now = millis();
        if ((int32_t)(now - nextFrame) < 0) {
            delay(1);
            continue;
        }
        // Cap redraw to ~15 FPS to reduce boot animation load.
        nextFrame += 66UL;

        display.clearDisplay();

        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(2);
        display.setCursor(titleTopX < 0 ? 0 : titleTopX, 6);
        display.print(titleTop);
        display.setCursor(titleBottomX < 0 ? 0 : titleBottomX, 24);
        display.print(titleBottom);
        display.drawLine(0, 44, SSD1306_WIDTH - 1, 44, SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 54);
        display.print(F("Boot animation"));

        for (uint8_t i = 0; i < 6; i++) {
            int16_t spanX = (int16_t)(3 * 6 * birds[i].scale);
            int16_t spanY = (int16_t)(8 * birds[i].scale);
            birds[i].x += birds[i].vx;
            birds[i].y += birds[i].vy;

            if (birds[i].x > SSD1306_WIDTH) birds[i].x = -spanX;
            if (birds[i].x < -spanX) birds[i].x = SSD1306_WIDTH - 1;
            if (birds[i].y < 0) {
                birds[i].y = 0;
                birds[i].vy = -birds[i].vy;
            }
            if (birds[i].y > (SSD1306_HEIGHT - spanY)) {
                birds[i].y = SSD1306_HEIGHT - spanY;
                birds[i].vy = -birds[i].vy;
            }

            drawAsciiBird(display, birds[i].x, birds[i].y, birds[i].scale, (uint8_t)((now / 140UL + i) % 6));
        }

        display.display();
    }

    display.clearDisplay();
    display.display();
}
#endif

#endif // ENABLE_LCD_SCREEN
