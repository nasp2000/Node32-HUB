#pragma once

// display_profiles.h - characteristics of each supported display type.
//
// One profile block per physical panel. SCREEN_TYPE (feature flags / packs)
// selects the active one. To support a new panel:
//   1. Append a new DISPLAY_* id + profile branch below.
//   2. If the layout differs, add a renderer in view_*.h + one line in
//      view_selector.h. Core (ScreenManager) and pack code stay untouched.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include <Arduino.h> // Base Arduino types (String, millis, etc.)
#include <Wire.h>    // I2C bus

// Both SSD1306 and SSD1309 use the Adafruit_SSD1306 driver via I2C.
// The difference is the VCC mode passed to begin():
//   SSD1306 (0.96"): SSD1306_SWITCHCAPVCC — uses internal charge pump
//   SSD1309 (2.42"): SSD1306_EXTERNALVCC  — module has onboard boost converter
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if SCREEN_TYPE == 2
  // SSD1309 2.42" 128x64 I2C 4-pin (VCC/GND/SDA/SCL) — drop-in compatible with Adafruit_SSD1306
  #define OLED_VCC_MODE    SSD1306_EXTERNALVCC
  #define SCREEN_LABEL     "SSD1309 2.42in"
#else
  // SSD1306 0.96" 128x64 I2C (default)
  #define OLED_VCC_MODE    SSD1306_SWITCHCAPVCC
  #define SCREEN_LABEL     "SSD1306 0.96in"
#endif

#define LCD_ADDRESS 0x3C  // Default I2C address; auto-detected at runtime

// OLED-specific parameters
#define LCD_COLS 21 // Kept for logical compatibility
#define LCD_ROWS 3  // Number of logical rows used
#define SSD1306_WIDTH 128 // 📐 Largura OLED
#define SSD1306_HEIGHT 64  // 📐 Altura OLED
#define SSD1306_RESET -1   // ♻️ Sem pino de reset dedicado
#define OLED_TEXT_SIZE 2   // 🔠 Tamanho base texto
#define OLED_LINE1_HEIGHT 21 // 📏 Linha 1 (px)
#define OLED_LINE2_HEIGHT 22 // 📏 Linha 2 (px)
#define OLED_LINE3_HEIGHT 21 // 📏 Linha 3 (px)
#define OLED_SCROLL_ENABLE true // 🔁 Scroll ativo
// OLED contrast default (0..255). Lower than max to reduce glare and panel stress.
#define OLED_DEFAULT_BRIGHTNESS 160

#ifndef LCD_SDA
#define LCD_SDA 21 // GPIO21 SDA I2C (N16R8 default; override in board_pins.h)
#endif
#ifndef LCD_SCL
#define LCD_SCL 48 // GPIO48 SCL I2C (N16R8: remapped from 14; override in board_pins.h)
#endif

class ConfigManager; // 🔎 Declaração forward apenas; definição real no .cpp

#define LCD_FALLBACK_SDA -1
#define LCD_FALLBACK_SCL -1

#define SCREEN_UPDATE_INTERVAL 5000
#define SCREEN_INIT_DELAY 5000
#define SCROLL_INTERVAL 150
#define SCROLL_SEPARATOR_SPACES 4

// Blink effect configuration for temporary screens (3 flashes in 2.5s)
// 🧠 Even spread: 3 short OFFs (BLINK_OFF_MS) at ~0ms, ~833ms and ~1666ms within the 2500ms.
// Each OFF lasts BLINK_OFF_MS; remaining interval remains visible (ON). Total duration 2500ms.
#define BLINK_TOTAL_MS 2500   // 🧠 Duração total do efeito ajustada para 2.5s
#define BLINK_FLASHES 3       // Number of flashes (OFF transitions)
#define BLINK_OFF_MS 250      // 🧠 Duração que fica apagado em cada flash (aumentado para ~250ms)

// Display registry: SCREEN_TYPE selects one profile below.
//   1 = SSD1306 0.96" 128x64 (default)    2 = SSD1309 2.42" 128x64
//   3 = Waveshare ESP32-S3-Touch-LCD-7 7" 800x480 RGB+touch (TouchMeteo-7)
//   4+ reserved for future panels (e.g. SH1106 1.3", ST7789 TFT):
//   add the profile branch here; ScreenManager core and views stay untouched.
#define DISPLAY_SSD1306_096 1
#define DISPLAY_SSD1309_242 2
#define DISPLAY_S3_TOUCH7_800x480 3

// Touch-LCD-7 geometry (ST7262 RGB panel + GT911 touch, via CH422G expander).
// Framebuffer RGB565 800x480x2 = 768 KB in PSRAM (see touch7_display.cpp).
#if SCREEN_TYPE == 3
#undef SCREEN_LABEL
#define TOUCH7_WIDTH_PX  800
#define TOUCH7_HEIGHT_PX 480
#define SCREEN_LABEL     "ST7262 7in RGB"
#endif

struct ScreenDisplayProfile {
    uint8_t id;
    const char *label;
    uint16_t widthPx;
    uint16_t heightPx;
    int16_t resetPin;
    uint8_t vccMode;
    uint8_t textSize;
    uint8_t colorDepth; // bits per pixel (1 = mono OLED, 16 = RGB565)
    uint8_t hasTouch;   // 0 = none, 1 = capacitive touch
};

inline ScreenDisplayProfile activeDisplayProfile() {
#if SCREEN_TYPE == 3
    return ScreenDisplayProfile{DISPLAY_S3_TOUCH7_800x480, SCREEN_LABEL, TOUCH7_WIDTH_PX,
                                TOUCH7_HEIGHT_PX, -1, 0, 2, 16, 1};
#elif SCREEN_TYPE == 2
    return ScreenDisplayProfile{DISPLAY_SSD1309_242, SCREEN_LABEL, SSD1306_WIDTH,
                                SSD1306_HEIGHT, SSD1306_RESET,
                                (uint8_t)OLED_VCC_MODE, OLED_TEXT_SIZE, 1, 0};
#else
    return ScreenDisplayProfile{DISPLAY_SSD1306_096, SCREEN_LABEL, SSD1306_WIDTH,
                                SSD1306_HEIGHT, SSD1306_RESET,
                                (uint8_t)OLED_VCC_MODE, OLED_TEXT_SIZE, 1, 0};
#endif
}

#endif // ENABLE_LCD_SCREEN
