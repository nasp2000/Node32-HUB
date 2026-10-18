#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class DisplayRotation : uint8_t {
    Normal = 0,
    Rotate90 = 1,
    Rotate180 = 2,
    Rotate270 = 3
};

struct DisplayConfig {
    uint8_t address = 0x3C;
    uint8_t width = 128;
    uint8_t height = 64;
    int8_t rstPin = -1;
    int8_t dcPin = -1;
    int8_t csPin = -1;
    DisplayRotation rotation = DisplayRotation::Normal;
    bool invert = false;
    uint8_t contrast = 128;
};

class IDisplay {
public:
    virtual ~IDisplay() = default;

    virtual bool init(const DisplayConfig& config) = 0;
    virtual void loop() = 0;

    virtual void clear() = 0;
    virtual void display() = 0;

    virtual void drawText(uint8_t x, uint8_t y, const String& text, uint8_t size = 1) = 0;
    virtual void drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) = 0;
    virtual void drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill = false) = 0;
    virtual void drawCircle(uint8_t x, uint8_t y, uint8_t r, bool fill = false) = 0;
    virtual void drawBitmap(uint8_t x, uint8_t y, const uint8_t* bitmap, uint8_t w, uint8_t h) = 0;

    virtual void setCursor(uint8_t x, uint8_t y) = 0;
    virtual void setTextSize(uint8_t size) = 0;
    virtual void setTextColor(uint8_t color) = 0;
    virtual void println(const String& text) = 0;
    virtual void printf(const char* format, ...) = 0;

    virtual void showStatus(const String& module, const String& status) = 0;
    virtual void showSplash(const String& title, const String& version) = 0;
    virtual void showIP(const String& ip) = 0;
    virtual void showSensorReadings(const String& label, float value, const String& unit) = 0;

    virtual uint8_t width() const = 0;
    virtual uint8_t height() const = 0;
    virtual bool isReady() const = 0;
    virtual void setBrightness(uint8_t level) = 0;
};
