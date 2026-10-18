#pragma once

// touch7_display.h — TouchMeteo-7 RGB display + LVGL UI (public API).
//
// Only active for SELECTED_PACK == PACK_S3_TOUCH7. Empty everywhere else so
// shared translation units keep compiling untouched.
// LVGL / panel headers live ONLY in touch7_display.cpp (never here), keeping
// this header light for screen.h / loop_blocks.h / web handlers.
//
// Rule nº1: all framebuffers, icon bitmaps, JPEG slots and JSON docs are
// PSRAM. DRAM holds only task stacks, DMA and ISR state.

#include "feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <Arduino.h>

// Theme ids (Claro / Medio / Escuro) — persisted in touch7.json.
#define TOUCH7_THEME_LIGHT 0
#define TOUCH7_THEME_MEDIUM 1
#define TOUCH7_THEME_DARK 2

// Clock styles: 0-2 digital variants, 3-4 analog variants.
#define TOUCH7_CLOCK_DIG_SEG 0
#define TOUCH7_CLOCK_DIG_MIN 1
#define TOUCH7_CLOCK_DIG_BOLD 2
#define TOUCH7_CLOCK_ANA_CLASSIC 3
#define TOUCH7_CLOCK_ANA_ARC 4

// Peer tiles (viewer): 2 default, 4 max.
#define TOUCH7_PEERS_MAX 4
#define TOUCH7_TILE_W 320
#define TOUCH7_TILE_H 180

class Touch7Display {
public:
    Touch7Display();

    // Full bring-up: Wire(8,9) -> sensors stay usable -> panel board init
    // (touch/expander skip host I2C, share Arduino Wire) -> board begin ->
    // LVGL init -> UI build. Returns true when the panel answers.
    bool begin();

    // Pump LVGL every loop() iteration (tick + timer handler). Cheap no-op
    // until begin() succeeds.
    void tick();

    // Backlight 0..255 (maps to panel 0..100%). BH1750 auto-mode scales it.
    void setBrightness(uint8_t brightness);
    // Manual override flag (brightness bar drag pauses auto mode).
    void setBrightManual(bool manual);

    // Presence power policy: dimmed = backlight floor + animations paused.
    void setDimmed(bool dimmed);
    bool isDimmed() const { return _dimmed; }
    bool isSaverShown() const;

    // Status footer (from ScreenManager context lines).
    void showStatus(const String& line1, const String& line2, const String& line3);

    // Toast overlay for displayMessage().
    void showToast(const String& l1, const String& l2, const String& l3);
    void hideToast();

    // Agora tab values. condCode follows Open-Meteo weathercode mapping
    // (0 clear, 1..3 cloudy steps, 45/48 fog, 51..67 rain, 71..77 snow,
    // 80..82 showers, 95..99 storm); isDay selects day/night variant.
    void setAgora(float tempC, float humPct, float pressHpa, float lux,
                  int condCode, bool isDay);
    void setWeatherNow(float tempC, float humPct, float pressureHpa, int condCode,
                       bool isDay, const String& condition, const String& place,
                       const String& sunset);

    // Forecast tab: 5 day tiles (labels pre-formatted by touch7_weather).
    void setForecast(const String& todayLine, const String dayLines[5]);

    // Peer tiles: RGB565 PSRAM buffer owned by the display; peers module
    // decodes snapshots into it, then calls invalidatePeerTile().
    uint16_t* peerTileBuffer(uint8_t idx);
    void invalidatePeerTile(uint8_t idx);
    void setPeerStatus(uint8_t idx, const String& name, const String& status, bool live);

    // WiFi tile + header state.
    void setWifiInfo(const String& info);
    void setClockNow(const String& hhmm, const String& date);

    // Theme / clock style (applied immediately; physical control writes defer
    // to the hidden-screen flush window, web POSTs persist under PanelHold).
    void applyTheme(uint8_t theme);
    void applyClockStyle(uint8_t style);

    // Screensaver clock (PIR timeout). Any touch wakes (LVGL event).
    void showScreensaver(bool show);

    // Fullscreen peer snapshot (PSRAM buffer, transient).
    void openFullscreen(uint8_t idx);
    void closeFullscreen();

    // Remote tap injection (HTTP /api/touch7-tap -> virtual pointer indev).
    // One-shot press delivered on the next LVGL poll, then released.
    void remoteTap(uint16_t x, uint16_t y);
    // Stateful remote pointer for /ecra gestures: pressed remains held across
    // LVGL polls until release; stale holds auto-release after 500 ms.
    void remotePointer(uint16_t x, uint16_t y, bool pressed);

    // Flash-write guard: while held, tick() advances time but skips ALL
    // rendering (no PSRAM traffic from LVGL). Set around LittleFS/NVS writes
    // issued from other tasks: S3 SPI1 is shared flash+PSRAM, so any flash
    // op suspends the cache and concurrent PSRAM access panics.
    void setFlashHold(bool hold);

    // OTA: stop RGB DMA+ISR before firmware flash writes (reboot follows).
    void shutdown();

    // T7-28 PanelHold: del the RGB panel (stops VSYNC+DMA+ISR), caller does
    // its flash write with zero ISR exposure, then resumePanelAfterWrite()
    // rebuilds the panel and repaints. Display blacks out ~2-3 s; no reboot,
    // no corruption. resume returns false only if the rebuild failed, in
    // which case it already triggered a controlled reboot (data was saved).
    void suspendPanelForWrite();
    bool resumePanelAfterWrite();

    // Render activity probe (for the quiesce waiter below).
    bool renderActive() const;

    // Runtime RGB pixel-clock tuning (live; no rebuild). Use /api/touch7-pclk.
    void setPclkHz(uint32_t hz);

    // Web mirror (/ecra): copy the last presented LCD framebuffer (RGB565)
    // into dst. Returns false if the panel/frame isn't available.
    bool captureFrame(uint8_t* dst, size_t dstLen);
    size_t frameBytes() const;

    bool isInitialized() const { return _ready; }
    uint8_t theme() const { return _theme; }
    uint8_t clockStyle() const { return _clockStyle; }

private:
    bool _ready;
    bool _dimmed;
    uint8_t _theme;
    uint8_t _clockStyle;
    void* _impl; // Touch7Impl* (LVGL objects, panel board) — hidden from headers

    void buildUi();
    void refreshClock();
    void refreshAgora();
    void autoBrightness();
};

extern Touch7Display touch7Display;

// Free functions for call sites that only have the header (web handlers).
void touch7FlashHold(bool hold);
bool touch7FlashHeld();

// T7-29 anti-tearing cross-task flash lock. Every flash writer/reader running
// on a task OTHER than the LVGL pump takes this for the operation's duration;
// the flush try-locks through framebuffer switch + VSYNC confirmation (the
// switch runs flash-hosted GDMA code). On contention the flush skips that frame — it
// never blocks (deadlock-free) and never races a cache-suspend. The mutex is
// recursive, so guards may nest (central dispatcher + handler guard).
void touch7FlashLock();
void touch7FlashUnlock();

// OTA: stops the panel (safe before Update.begin()); on FAILURE call
// touch7OtaAbort() to rebuild the panel + release the lock (success reboots).
void touch7OtaBegin();
void touch7OtaAbort();

// Panel del/rebuild hold (declared here: Touch7FlashGuard below uses it).
void touch7PanelSuspend();
bool touch7PanelResume();

// RAII guard for handlers that touch LittleFS/flash. With the RGB bounce buffer
// (T7) the bounce ISR copies PSRAM every chunk, so ANY flash op while the panel
// streams panics (cache suspend). This suspends (del) the panel for the scope and
// rebuilds it after, so flash ops are safe. Idempotent/nestable.
struct Touch7FlashGuard {
    Touch7FlashGuard() { touch7PanelSuspend(); }
    ~Touch7FlashGuard() { touch7PanelResume(); }
};

// Strong hold for config-save paths: dels the RGB panel (stops VSYNC+DMA),
// runs the flash op with zero ISR exposure, then rebuilds the panel and
// repaints. Display blacks out ~2-3 s; no reboot, no corruption. If the
// rebuild fails, the display stays off while web/network continue working.
// Idempotent/nestable via a depth counter (T7-29).
void touch7PanelSuspend();
bool touch7PanelResume();
struct Touch7PanelHold {
    Touch7PanelHold() { touch7PanelSuspend(); }
    ~Touch7PanelHold() { touch7PanelResume(); }
    Touch7PanelHold(const Touch7PanelHold&) = delete;
    Touch7PanelHold& operator=(const Touch7PanelHold&) = delete;
};

struct Touch7FlashLock {
    Touch7FlashLock() { touch7FlashLock(); }
    ~Touch7FlashLock() { touch7FlashUnlock(); }
    Touch7FlashLock(const Touch7FlashLock&) = delete;
    Touch7FlashLock& operator=(const Touch7FlashLock&) = delete;
};

// Called once from setup (fullBoot) and every loop() iteration.
void touch7SystemBegin();
void touch7SystemTick();

// Touch7 TF slot: asserts the external CS (CH422G EXIO4, active LOW) via the
// Board's expander handle. Returns false when the panel/expander is not up
// yet (caller must defer the SD mount). No second CH422G is ever created.
bool touch7SdAssertCs();

// NVS "all_configs" blob length cached at boot (before the RGB panel starts
// streaming). /api/status serves from this so it never flash-reads while the
// display scans (the VSYNC ISR runs flash code -> cache-off panic).
extern size_t touch7NvsAllConfigsBytes;

// Boot-cached flash/NVS stats (LittleFS usage + NVS entries). Flash reads are
// unsafe while the RGB panel streams with the bounce buffer, so /api/status
// serves these instead of calling littlefsTotalBytes()/nvs_get_stats().
extern bool touch7StatFlashMounted;
extern uint64_t touch7StatFlashTotal, touch7StatFlashUsed, touch7StatFlashFree;
extern int touch7StatNvsTotalKb, touch7StatNvsFreeKb;

// True when it is a good moment for the hidden config flush (screen off:
// dimmed or screensaver). Lets ConfigManager::loop() persist without a visible
// panel del/rebuild blackout.
bool touch7ConfigFlushAllowed();

#endif // SELECTED_PACK == PACK_S3_TOUCH7
