#ifndef SCREEN_H
#define SCREEN_H

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
// Display characteristics (geometry, VCC mode, pins, timings) live in
// src/screen/display_profiles.h - one profile per display type. To support
// a new panel, extend that file; this header keeps the public API stable.
#include "screen/display_profiles.h"

#if SCREEN_TYPE == 3
// TouchMeteo-7 RGB panel: Touch7Display is forward-declared; LVGL/panel
// headers live solely in touch7_display.cpp. The OLED member stays declared
// (never begun on RGB builds) so all OLED methods keep compiling untouched.
class Touch7Display;
#endif

class ScreenManager {
private:
  Adafruit_SSD1306 oled; // Single OLED instance (unused on RGB builds, never begun)
#if SCREEN_TYPE == 3
  Touch7Display* touch7; // RGB 800x480 engine (touch7_display.cpp)
#endif
    uint8_t detectedAddress = LCD_ADDRESS;
    unsigned long lastUpdate;
    unsigned long bootTime;
    bool lcdInitialized;

    String tempStr;
    String lastDisplayLine1;
    String lastDisplayLine2;
    String lastDisplayLine3;
    bool forceNextUpdate;
    
    unsigned long receivingDataStartTime;
    bool showingReceivingData;
    
    unsigned long sendingCommandStartTime;
    bool showingSendingCommand;
    String lastSentCommand; // Last sent command (truncated)
    String lastSentType; // Type: "RS232" or "System"
  unsigned long pendingCommandTimestamp; // Timestamp for pending command delayed by RX
  bool pendingShowSendingCommand; // Flag: RS232 command waiting for a free window
  unsigned long minReceivingVisibleMs = 250; // Minimum display time to ensure 'Receiving Data' visibility and align with silence window
    
    unsigned long emailSentStartTime;
    bool showingEmailSent;

    unsigned long heapAlertStartTime;
    bool showingHeapAlert;

    unsigned long heapDegradationAlertStartTime;
    bool showingHeapDegradation;

    unsigned long configSavedStartTime;
    bool showingConfigSaved;
    
    unsigned long nvsWarningStartTime;
    bool showingNVSWarning;
    int nvsUsagePercent; // Store usage % for display

  // Internal state for blink effect
  unsigned long blinkStartTime; // 🧠 Effect start moment
  unsigned long blinkLastToggle; // Last toggle executed
  uint8_t blinkToggleCount; // 🧠 Quantidade de toggles já feitos
  bool blinkActive; // 🧠 Indicador se o efeito está ativo
  bool blinkVisible; // Current visible state (true = visible, false = cleared)
    
    String scrollLine1, scrollLine2, scrollLine3;
    int scrollPos1, scrollPos2, scrollPos3;
    unsigned long lastScrollTime;
    bool scrollActive;

public:
    ScreenManager();

    bool begin();
    void scanI2CBus();
    uint8_t getDetectedAddress() const { return detectedAddress; }
    void setBrightness(uint8_t brightness = 255);

    void update();
    void forceUpdate();
    void clear();
    void displayMessage(const String& line1, const String& line2, const String& line3 = "");

    void updateScrolling();
    String getScrolledText(const String& fullText, int& scrollPos, int maxChars);
    void setScrollEnabled(bool enabled) { scrollActive = enabled; }
    bool isScrollEnabled() const { return scrollActive; }

    void showStatusScreen();
    void showReceivingData();
    void showSendingCommand();
    void showEmailSent();
    void showHeapAlert();
    void showHeapDegradation();  // 🚨 Degradation alert (>40% loss)
    void showConfigSaved();
    void showNVSWarning();  // ⚠️ Warning NVS >75% full

  // Trigger the temporary 'RS232 Command Sent' screen externally
  void triggerSendingCommand(const String& command = "", const String& type = "RS232");

    void triggerConfigSaved();
    void triggerHeapDegradation();  // 🚨 External trigger for degradation alert
    void triggerNVSWarning(int usagePercent);  // ⚠️ External trigger for NVS warning

    // Start blink effect (reset counters)
    void startBlink();
    // Update blink state (called before drawing)
    void updateBlink(unsigned long now);

    String formatUptime(unsigned long uptime);
    String formatBytes(size_t bytes);
    int getWiFiRSSI();
    String getWiFiSignalBars();
  String stripAccents(const String &s);

    bool isInitialized() const { return lcdInitialized; }
};

extern ScreenManager screenManager;

void initScreen();
void updateScreen();
void displayScreenMessage(const String& line1, const String& line2 = "", const String& line3 = "");
void setScreenScrollEnabled(bool enabled);
bool isScreenScrollEnabled();

#else

// Stubs when the display is disabled by build (ENABLE_LCD_SCREEN=0)
#include <Arduino.h>
class ScreenManager {
public:
  ScreenManager() {}
  bool begin() { return false; }
  void scanI2CBus() {}
  uint8_t getDetectedAddress() const { return 0; }
  void setBrightness(uint8_t brightness = 255) { (void)brightness; }
  void update() {}
  void forceUpdate() {}
  void clear() {}
  void displayMessage(const String&, const String&, const String& = "") {}
  void updateScrolling() {}
  String getScrolledText(const String& fullText, int& scrollPos, int maxChars) { (void)scrollPos; (void)maxChars; return fullText; }
  void setScrollEnabled(bool) {}
  bool isScrollEnabled() const { return false; }
  void showStatusScreen() {}
  void showReceivingData() {}
  void showSendingCommand() {}
  void showEmailSent() {}
  void showHeapAlert() {}
  void showConfigSaved() {}
  void triggerSendingCommand() {}
  void triggerConfigSaved() {}
  void startBlink() {}
  void updateBlink(unsigned long) {}
  String formatUptime(unsigned long) { return String("0s"); }
  String formatBytes(size_t bytes) { return String(bytes) + "B"; }
  int getWiFiRSSI() { return -100; }
  String getWiFiSignalBars() { return String("|"); }
  String stripAccents(const String &s) { return s; }
  bool isInitialized() const { return false; }
};

extern ScreenManager screenManager;

inline void initScreen() {}
inline void updateScreen() {}
inline void displayScreenMessage(const String& line1, const String& line2 = "", const String& line3 = "") { (void)line1; (void)line2; (void)line3; }
inline void setScreenScrollEnabled(bool) {}
inline bool isScreenScrollEnabled() { return false; }

#endif // ENABLE_LCD_SCREEN

#endif