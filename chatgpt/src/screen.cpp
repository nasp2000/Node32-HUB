// ✅ Main header of the screen module
#include "screen.h"

#include "feature_flags.h"
#if ENABLE_LCD_SCREEN==1
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "languages/lang_active.h"
#include "config_manager_runtime.h"
#include "nvs_flash.h"
#include <esp_partition.h>
#include "commands.h" // for runI2CScan wrapper / sendVirtualMessage
#include "feature_flags.h"
#include "structures.h" // StorageMode enum
#include "eth_w5500.h"
#include "network_runtime_state.h"
#include "sdcard.h" // SD card functions
#include "psram_vfs.h" // PSRAM VFS functions
#include "littlefs_manager.h"
#include "languages/lang_active.h"

#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif

#if ENABLE_BRICON==1
#include <string.h>
#include "bricon_protocol.h"   // BriconStatus / briconActiveModuleName() for the OLED line 3
#endif

#if ENABLE_MQ135==1
#include "sensors/sensor_mq135.h"
#endif

#if ENABLE_UDA1334A==1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
#include "radio_audio_runtime.h"
#endif

#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_config.h" // Environmental sensor data
#endif

#if ENABLE_PROXIMITY==1
#include "sensors/proximity_manager.h"
#endif

#if ENABLE_MODBUS==1
#include "modbus_runtime.h"
#endif

#if ENABLE_CAN==1
#include "can_runtime.h"
#endif

#if ENABLE_USB_HOST==1
#include "usb_host_manager.h"
#endif

// Screen modularization: boot animations (anim_*.h) and pack-specific status
// views (view_*.h, picked by view_selector.h). Header-only, no new build units.
#include "screen/anim_rambox.h"
#include "screen/anim_environment.h"
#include "screen/view_selector.h"

#if SCREEN_TYPE == 3
#include "touch7_display.h"
#endif

#define QUIET_SCREEN_LOGS

ScreenManager screenManager;

// Boot animation moved to screen/anim_rambox.h (Rambox pack).

// Boot animation moved to screen/anim_environment.h (Environment pack).

extern const char* FIRMWARE_VERSION;
extern unsigned long bootTime;
extern volatile StorageMode currentStorageMode; // Storage mode (SD or PSRAM)
extern unsigned long lastReceptionTime; // 🕒 Global timestamp of last reception (DB25/RS232)
extern volatile unsigned long bufferDadosSeq; // Total bytes inserted in the RX buffer
extern int bufferDadosLineCount; // Total line count collected in the RX buffer

#if ENABLE_AHT20_BMP280==1
extern SensorConfigManager sensorConfigManager; // Environmental sensor manager
#endif

ScreenManager::ScreenManager() :
    oled(SSD1306_WIDTH, SSD1306_HEIGHT, &Wire, SSD1306_RESET), // Apenas OLED
#if SCREEN_TYPE == 3
    touch7(nullptr),
#endif
    lastUpdate(0),
    bootTime(0),
    lcdInitialized(false),
    forceNextUpdate(false),
    scrollPos1(0), scrollPos2(0), scrollPos3(0),
    lastScrollTime(0),
    scrollActive(true),
    receivingDataStartTime(0),
    showingReceivingData(false),
    sendingCommandStartTime(0),
    showingSendingCommand(false),
    lastSentCommand(""), // 🧠 Initialize empty
    lastSentType("RS232"), // 🧠 Default RS232
    pendingCommandTimestamp(0), // Initialize pending timestamp
    pendingShowSendingCommand(false), // Sem comando pendente inicial
    emailSentStartTime(0),
    showingEmailSent(false),
    heapAlertStartTime(0),
    showingHeapAlert(false),
    heapDegradationAlertStartTime(0),
    showingHeapDegradation(false),
    configSavedStartTime(0),
    showingConfigSaved(false),
    nvsWarningStartTime(0),
    showingNVSWarning(false),
    nvsUsagePercent(0),
    blinkStartTime(0), // Initialize blink start
    blinkLastToggle(0), // Last toggle
    blinkToggleCount(0), // Toggle counter
    blinkActive(false), // Effect active flag
    blinkVisible(true) { // Visible by default
}

void ScreenManager::scanI2CBus() {
#if SCREEN_TYPE == 3
    // Touch-LCD-7: never touch Arduino Wire (port 0) — owned by the display
    // stack (legacy driver). Use /api/i2c-scan (Wire1) instead.
    log_i("[TOUCH7] scanI2CBus: skipped (port 0 owned by panel)");
    return;
#endif
    Serial.printf("I2C scan (SDA=%d, SCL=%d)\n", LCD_SDA, LCD_SCL);
    uint8_t foundCount = 0;
    for(uint8_t addr=1; addr<127; ++addr){
        Wire.beginTransmission(addr);
        if(Wire.endTransmission()==0){
            Serial.printf("  -> Dispositivo em 0x%02X\n", addr);
            if(foundCount==0) detectedAddress = addr;
            foundCount++;
        }
        delay(2);
    }
    if(foundCount==0){
        Serial.println(F("I2C: nenhum dispositivo"));
    } else {
        Serial.printf("%u dispositivo(s) encontrado(s). Usando 0x%02X para LCD.\n", foundCount, detectedAddress);
    }
}


bool ScreenManager::begin() {
#if SCREEN_TYPE == 3
    // TouchMeteo-7 RGB panel: Wire1 sensors first, then panel+LVGL.
    log_i("[TOUCH7] ScreenManager::begin -> Touch7Display");
    Serial.println(F("[TOUCH7] ScreenManager::begin -> Touch7Display"));
    touch7 = &touch7Display;
    if (!touch7Display.begin()) {
        Serial.println(F("[TOUCH7] display begin FAILED"));
        return false;
    }
    lcdInitialized = true;
    bootTime = millis();
    lastUpdate = 0;
    Serial.println(F("[TOUCH7] display pronto (800x480 LVGL)"));
    return true;
#endif
    // Compact OLED init message
    Serial.println(F("OLED init"));
    Wire.begin(LCD_SDA, LCD_SCL); // Start I2C
    Wire.setClock(100000); // Clock I2C reduzido (estabilidade)
    delay(100); // Small wait

    // Quick probe: check whether 0x3C or 0x3D respond before trying to init the library
    uint8_t probeAddrs[] = {0x3C, 0x3D};
    bool found = false;
    for (auto a : probeAddrs) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            detectedAddress = a;
            found = true;
            break;
        }
    }
    if (!found) {
        Serial.println(F("I2C: nenhum 0x3C/0x3D detectado; skip OLED init"));
        return false; // No compatible I2C device
    }

    // Try init at the detected address
    Serial.printf("OLED (%s): tentativa de init em 0x%02X\n", SCREEN_LABEL, detectedAddress);
    if(!oled.begin(OLED_VCC_MODE, detectedAddress)){
        Serial.printf("OLED 0x%02X sem resposta (begin() falhou)\n", detectedAddress);
        return false; // Abort
    }

    // Mark initialized right after successful begin so we can apply brightness/contrast.
    lcdInitialized = true;
    setBrightness(OLED_DEFAULT_BRIGHTNESS);

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX) && (SCREEN_TYPE == 2)
    runRamboxBootAnimation(oled);
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_ENVIRONMENT)
    runEnvironmentBootAnimation(oled);
#else
    oled.clearDisplay(); // Clear
    oled.display(); // Atualiza
    oled.setTextSize(2); // Tamanho texto
    oled.setTextColor(SSD1306_WHITE); // Cor
    oled.setCursor(0,0); // Cursor
    oled.println(L(activeLang->screenBoot)); // Mensagem boot
    oled.display(); // Mostra
    delay(800); // Delay
    oled.clearDisplay(); // Clear
    oled.setCursor(0,0); // Topo
    {
        char _buf[32];
        snprintf(_buf, sizeof(_buf), "I2C: 0x%02X", detectedAddress);
        oled.println(_buf); // Endereco
    }
    oled.display(); // Mostra
    delay(600); // Delay
#endif
    bootTime = millis(); // Regista tempo
    lastUpdate = 0; // Reinicia marca
    Serial.printf("OLED pronto (0x%02X)\n", detectedAddress); // Log final
    return true; // Success
}

void ScreenManager::setBrightness(uint8_t brightness) {
    if (!lcdInitialized) return;
#if SCREEN_TYPE == 3
    if (touch7) touch7->setBrightness(brightness);
    return;
#endif
    // SSD1306/SSD1309 brightness is controlled by contrast (0..255).
    oled.ssd1306_command(SSD1306_SETCONTRAST);
    oled.ssd1306_command(brightness);
    // Enable dim mode only at very low brightness levels.
    oled.dim(brightness < 32);
}

void ScreenManager::update() {
    if (!lcdInitialized) return;
#if SCREEN_TYPE == 3
    // RGB panel: LVGL pump runs every loop via touch7SystemTick(); the slow
    // status path below only refreshes footer values.
    showStatusScreen();
    return;
#endif

    // One-time Wire recovery on the first loop() call.
    // Background tasks started during setup() (SD_MMC, NanoBridge UART) may interact
    // with the ESP32-S3 IDF5 peripheral manager (periman) in a way that invalidates the
    // I2C0 bus handle obtained during screenManager.begin().
    // Forcing Wire.end() + Wire.begin() here ensures a fresh handle before any OLED write.
    static bool wireRecoveryDone = false;
    if (!wireRecoveryDone) {
        Wire.end();
        Wire.begin(LCD_SDA, LCD_SCL);
        Wire.setClock(100000);
        wireRecoveryDone = true;
    }

    unsigned long currentTime = millis();
    extern bool isReceivingData;
#if ENABLE_RS232==1
    extern bool rs232_sending;
#else
    bool rs232_sending = false;
#endif
    extern int currentEmailSendingState;
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    // 🧠 Persistent state for "Email Enviado" popup (high priority)
    static int lastEmailState = 0;                 // Last known send state
    static bool emailSuccessPending = false;       // 📨 Success waiting for a window to show
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL

    // 🧠 Update blink state (before deciding what to draw)
    bool prevBlinkVisible = blinkVisible; // 🧠 Guarda estado anterior de visibilidade
    updateBlink(currentTime); // 🧠 Gere toggles e visibilidade
    if (blinkActive && !prevBlinkVisible && blinkVisible) { // ✅ OFF->ON transition detected
        forceNextUpdate = true; // 🧠 Force an immediate redraw to avoid staying blank until the next interval
    }

    // 🧠 New rule: RX has priority but only replaces TX after 2s (TX min visibility)
    // 🧠 Immediate cancel was removed; the override decision happens in the TX branch below

    // PRIORITY: If the TX sending screen is active, show it (up to 2.5s),
    // 🧠 but if RX is active, RX has immediate priority and replaces TX without delay
    // before handling other temporary messages (ensures the OLED displays "Comando RS232 Enviado").
    if (showingSendingCommand) {
        // 🔀 Active RX → cancel TX immediately to make room for RX
        if (isReceivingData) { // 🧠 Immediate RX override
            showingSendingCommand = false; // 🧠 Cancel TX
            forceNextUpdate = true; // 🧠 Force immediate redraw
            blinkActive = false; // ♻️ End effect when leaving TX
            // 🚪 Do not return: let the RX block below take over
        } else if (currentTime - sendingCommandStartTime >= 2500) {
            showingSendingCommand = false;
            forceNextUpdate = true;
            blinkActive = false; // ♻️ Termina efeito ao expirar
        } else {
            // Re-render the sending command screen immediately and skip other updates
            if (blinkActive) { // 🧠 Enquanto efeito ativo
                if (!blinkVisible) { // 🧠 Fase OFF do blink
                    oled.clearDisplay(); // 🧹 Clear
                    oled.display(); // 📤 Atualiza
                } else {
                    showSendingCommand(); // ✅ Show content in the ON phase
                }
            } else {
                showSendingCommand(); // ✅ No active blink, show normally
            }
            // 🧠 Keep TX until 2.5s elapse (no defer window if RX arrives)
            return;
        }
    }

    // ♻️ Heap alert: Show only when free heap < 65KB to reduce noise
    static unsigned long lastHeapAlert = 0; // 🧠 Último instante em que mostramos alerta
    size_t freeHeapNow = ESP.getFreeHeap(); // 🧠 Heap livre atual em bytes
    // 🧠 New limits: main threshold 65KB; minimum interval between alerts 70s to avoid spam
    const size_t HEAP_ALERT_THRESHOLD = 65UL * 1024UL; // 🔐 Limiar ajustado solicitado (<65KB)
    const unsigned long HEAP_ALERT_MIN_INTERVAL_MS = 70000UL; // ⏳ Minimum window between alerts
    if (freeHeapNow < HEAP_ALERT_THRESHOLD && !showingHeapAlert && (currentTime - lastHeapAlert > HEAP_ALERT_MIN_INTERVAL_MS)) {
        // ✅ Heap below threshold and not already showing — fire popup
        showHeapAlert(); // 📟 Show immediately
        heapAlertStartTime = currentTime; // 🕒 Mark start
        showingHeapAlert = true; // 🔔 Flag active
        lastHeapAlert = currentTime; // 📝 Record last time
        startBlink(); // 🚀 Visual effect for emphasis
        return; // 🚪 Priority: exit after drawing
    }

    // 📨 HIGH PRIORITY: "Email Enviado" popup (overlays everything, like RX)
    if (showingEmailSent) {
        if (currentTime - emailSentStartTime >= 4000) {
            showingEmailSent = false;
            forceNextUpdate = true;
            blinkActive = false; // ♻️ End effect
        } else {
            // 🧠 Re-render with blink
            if (blinkActive) { if (!blinkVisible) { oled.clearDisplay(); oled.display(); } else showEmailSent(); }
            else { showEmailSent(); }
            return; // 🚪 Exit after drawing
        }
    }
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    // 🧭 Early success detection to fire the popup with priority
    if (currentEmailSendingState == 10 && lastEmailState != 10) {
        emailSuccessPending = true; // 🧠 Agendar popup
    }
    if (currentEmailSendingState == 11 && lastEmailState != 11) {
        emailSuccessPending = false; // ♻️ Cancela em caso de falha
    }
    if (emailSuccessPending && !showingEmailSent && (currentEmailSendingState == 12 || currentEmailSendingState == 0)) {
        showEmailSent();                 // ✅ Show immediately
        emailSentStartTime = currentTime; // 🕒 Mark start
        showingEmailSent = true;          // 🧠 Raise flag
        startBlink();                     // 🚀 Visual highlight
        emailSuccessPending = false;      // ♻️ Clear pending
        lastEmailState = currentEmailSendingState; // 📝 Update state
        return; // 🚪 Exit after drawing
    }

#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL
    // Screen overrides for temporary status
    // ✅ MAX PRIORITY: During reception (DB25/RS232), keep the screen fixed with "Receber Dados"
    if (isReceivingData) { // ✅ Active RX → always show
        if (!showingReceivingData) { // ✅ First activation: mark state (no blink effect)
            receivingDataStartTime = currentTime; // 🕒 Mark start
            showingReceivingData = true; // 🧠 Flag active
            blinkActive = false; // ✅ No blink to keep the message fixed
        }
        // 🧹 Clear other temporary states and force priority RX content
        showingHeapAlert = false;
        showingConfigSaved = false;
        showingEmailSent = false;
        showingSendingCommand = false;
        showReceivingData(); // ✅ Render imediato
        return; // 🚪 Exit after drawing (stay fixed during RX)
    } else {
        // No reception: keep "Receiving Data" until QUIET_MS has passed since the last actual reception
        const unsigned long RECEIVING_QUIET_MS = 250; // Silence window after RX
        if (showingReceivingData) {
            if ((currentTime - lastReceptionTime) < RECEIVING_QUIET_MS) {
                // Still within the silence window — continue showing "Receiving Data"
                showReceivingData();
                return; // Only switch to scroll lines after 250ms without RX
            }
            // Silence window passed — stop showing and return to status/scroll screen
            // Keep rotation continuity: DO NOT reset lastScrollTime to continue immediately
            // The scroll positions scrollPos1/2/3 and the timestamp remain for smooth rotation without restart

            showingReceivingData = false;
            forceNextUpdate = true; // Force update to show next priority screen
            blinkActive = false; // Ensure blink effect ended
        }
    }

    // 🧠 "RS232 Command Sent" pending management
    // 🧠 Goal: only show after reception had at least minReceivingVisibleMs or finished
    if (pendingShowSendingCommand) { // 🧠 A send screen is pending
    static const unsigned long PENDING_CMD_STALE_MS = 6000; // 🧠 Limit to discard stale pending items (2.5s of screen)
        if (currentTime - pendingCommandTimestamp > PENDING_CMD_STALE_MS) { // ⚠️ Pending expired
            pendingShowSendingCommand = false; // 🧠 Discard
        } else if (!isReceivingData && (!showingReceivingData || (currentTime - receivingDataStartTime >= minReceivingVisibleMs))) { // ✅ Conditions to show
            showSendingCommand(); // ✅ Finally show
            sendingCommandStartTime = currentTime; // 🧠 Normal start mark
            showingSendingCommand = true; // 🧠 Flag active
            pendingShowSendingCommand = false; // ♻️ Clear pending
            startBlink(); // 🚀 Blink for emphasis
            return; // 🚪 Já desenhamos prioridade
        } else {
            // 🧠 Still waiting: do nothing for now
        }
    }

    // Original logic for other temporary messages (now lower priority)
    // 📊 Degradation alert takes priority over normal low heap
    if (showingHeapDegradation) {
        updateBlink(currentTime); // 💡 Atualiza estado de piscar
        if (blinkActive) {
            if (blinkVisible) showHeapDegradation(); // ✅ ON
            else clear(); // ❌ OFF
        } else {
            showHeapDegradation(); // ✅ Sem blink
        }
        return; // 🎯 Prioridade alta
    }
    
    if (showingHeapAlert) {
    if (currentTime - heapAlertStartTime >= 2500) {
            showingHeapAlert = false;
            forceNextUpdate = true;
            blinkActive = false; // ♻️ Termina efeito
        } else {
            // 🧠 Re-render with blink
            if (blinkActive) { // 🧠 Efeito blink ativo
                if (!blinkVisible) { oled.clearDisplay(); oled.display(); } // ⚠️ OFF
                else showHeapAlert(); // ✅ ON
            } else {
                showHeapAlert(); // ✅ Sem blink
            }
            return; // 🚪 Exit after drawing/clearing
        }
    }
    if (showingConfigSaved) {
    if (currentTime - configSavedStartTime >= 2500) {
            showingConfigSaved = false;
            forceNextUpdate = true;
            blinkActive = false; // ♻️ Termina efeito
        } else {
            if (blinkActive) { if (!blinkVisible) { oled.clearDisplay(); oled.display(); } else showConfigSaved(); } else showConfigSaved();
            return;
        }
    }
    
    if (showingNVSWarning) {
        if (currentTime - nvsWarningStartTime >= 2500) {
            showingNVSWarning = false;
            forceNextUpdate = true;
            blinkActive = false;
        } else {
            if (blinkActive) { if (!blinkVisible) { oled.clearDisplay(); oled.display(); } else showNVSWarning(); } else showNVSWarning();
            return;
        }
    }
    
    if (showingSendingCommand) {
    if (currentTime - sendingCommandStartTime >= 2500) {
            showingSendingCommand = false;
            forceNextUpdate = true;
            blinkActive = false; // ♻️ Termina efeito
        } else return;
    }

    // Check for new temporary status messages (if not already showing a higher priority one)
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    lastEmailState = currentEmailSendingState; // 📝 Update for next comparisons
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL

    if (rs232_sending && !showingSendingCommand && !pendingShowSendingCommand) { // 🧠 Novo evento de envio
        if (isReceivingData || showingReceivingData) { // 🧠 RX ativa -> adiar
            pendingShowSendingCommand = true; // 🧠 Mark pending
            pendingCommandTimestamp = currentTime; // 🧠 Regista tempo de pedido
        } else { // ✅ No RX, can show immediately
            showSendingCommand(); // ✅ Mostra imediatamente
            sendingCommandStartTime = currentTime; // 🧠 Mark start
            showingSendingCommand = true; // 🧠 Flag
            startBlink(); // 🚀 Efeito
            return; // 🚪 Prioridade desenhada
        }
    }
    
    // Main screen update logic
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX) && (SCREEN_TYPE == 2)
    // Rambox 2.42" layout has its own per-line animation logic inside showStatusScreen().
    // Do not call generic updateScrolling(), otherwise all rows may appear to scroll/blink.
    const unsigned long ramboxRefreshMs = SCROLL_INTERVAL;
    if (currentTime - lastUpdate >= ramboxRefreshMs || forceNextUpdate) {
        lastUpdate = currentTime;
        showStatusScreen();
        forceNextUpdate = false;
    }
#else
    updateScrolling();
    if (currentTime - lastUpdate >= SCREEN_UPDATE_INTERVAL || forceNextUpdate) {
        lastUpdate = currentTime;
        showStatusScreen();
        forceNextUpdate = false;
    }
#endif
}

void ScreenManager::forceUpdate() {
    forceNextUpdate = true;
    lastUpdate = 0;
    update();
}

void ScreenManager::clear() {
    if (!lcdInitialized) return; // 🧠 Protection
#if SCREEN_TYPE == 3
    if (touch7) touch7->hideToast();
    return;
#endif
    oled.clearDisplay(); // 🧹 Clear content
}

void ScreenManager::displayMessage(const String& line1, const String& line2, const String& line3) {
    if (!lcdInitialized) return;
#if SCREEN_TYPE == 3
    if (touch7) touch7->showToast(line1, line2, line3);
    return;
#endif
    
    scrollLine1 = line1; scrollLine2 = line2; scrollLine3 = line3;
    
    if (!forceNextUpdate && line1 == lastDisplayLine1 && line2 == lastDisplayLine2 && line3 == lastDisplayLine3) return;
    lastDisplayLine1 = line1; lastDisplayLine2 = line2; lastDisplayLine3 = line3;
    forceNextUpdate = false;

    oled.clearDisplay();
    oled.setTextSize(OLED_TEXT_SIZE);
    oled.setTextColor(SSD1306_WHITE);

    // center each visible string horizontally
    String display1 = getScrolledText(scrollLine1, scrollPos1, 10);
    int charW = 6 * OLED_TEXT_SIZE; // approx character width in pixels (5px font + 1px spacing)
    int disp1WidthPx = display1.length() * charW;
    int x1 = (SSD1306_WIDTH - disp1WidthPx) / 2;
    if (x1 < 0) x1 = 0;
    oled.setCursor(x1, 0);
    oled.print(display1);

    String display2 = getScrolledText(scrollLine2, scrollPos2, 10);
    int disp2WidthPx = display2.length() * charW;
    int x2 = (SSD1306_WIDTH - disp2WidthPx) / 2;
    if (x2 < 0) x2 = 0;
    oled.setCursor(x2, 24);
    oled.print(display2);

    String display3 = getScrolledText(scrollLine3, scrollPos3, 10);
    int disp3WidthPx = display3.length() * charW;
    int x3 = (SSD1306_WIDTH - disp3WidthPx) / 2;
    if (x3 < 0) x3 = 0;
    oled.setCursor(x3, 48);
    oled.print(display3);

    oled.display(); // 📤 Atualiza OLED
}

String ScreenManager::getScrolledText(const String& fullText, int& scrollPos, int maxChars) {
    if (fullText.length() <= maxChars) {
        scrollPos = 0;
        return fullText;
    }
    
    String loopText = fullText + "    ";
    int totalLen = loopText.length();
    
    if (scrollPos >= totalLen) scrollPos = 0;
    
    String result = "";
    for (int i = 0; i < maxChars; i++) {
        int charIndex = (scrollPos + i) % totalLen;
        result += loopText.charAt(charIndex);
    }
    
    return result;
}

void ScreenManager::updateScrolling() {
    if (!scrollActive || !lcdInitialized) return; 
    
    unsigned long currentTime = millis();
    bool needsScroll = (scrollLine1.length() > 10) || (scrollLine2.length() > 10) || (scrollLine3.length() > 10);
    
    if (!needsScroll) return;
    
    if (currentTime - lastScrollTime >= SCROLL_INTERVAL) {
        lastScrollTime = currentTime;
        
        if (scrollLine1.length() > 10) {
            scrollPos1++;
            int totalLen1 = scrollLine1.length() + SCROLL_SEPARATOR_SPACES;
            if (scrollPos1 >= totalLen1) scrollPos1 = 0;
        }
        if (scrollLine2.length() > 10) {
            scrollPos2++;
            int totalLen2 = scrollLine2.length() + SCROLL_SEPARATOR_SPACES;
            if (scrollPos2 >= totalLen2) scrollPos2 = 0;
        }
        if (scrollLine3.length() > 10) {
            scrollPos3++;
            int totalLen3 = scrollLine3.length() + SCROLL_SEPARATOR_SPACES;
            if (scrollPos3 >= totalLen3) scrollPos3 = 0;
        }

        oled.clearDisplay();
        oled.setTextSize(OLED_TEXT_SIZE);
        oled.setTextColor(SSD1306_WHITE);

        String display1 = getScrolledText(scrollLine1, scrollPos1, 10);
        int charW = 6 * OLED_TEXT_SIZE;
        int disp1WidthPx = display1.length() * charW;
        int x1 = (SSD1306_WIDTH - disp1WidthPx) / 2;
        if (x1 < 0) x1 = 0;
        oled.setCursor(x1, 0);
        oled.print(display1);

        String display2 = getScrolledText(scrollLine2, scrollPos2, 10);
        int disp2WidthPx = display2.length() * charW;
        int x2 = (SSD1306_WIDTH - disp2WidthPx) / 2;
        if (x2 < 0) x2 = 0;
        oled.setCursor(x2, 24);
        oled.print(display2);

        String display3 = getScrolledText(scrollLine3, scrollPos3, 10);
        int disp3WidthPx = display3.length() * charW;
        int x3 = (SSD1306_WIDTH - disp3WidthPx) / 2;
        if (x3 < 0) x3 = 0;
        oled.setCursor(x3, 48);
        oled.print(display3);

        oled.display(); // 📤 Atualiza OLED
    }
}

void ScreenManager::showStatusScreen() {
    String line1, line2, line3;

    // Line 1: network status with WiFi/AP priority, Ethernet fallback when WiFi STA is unavailable.
    String ipStr = "N/A";
    String ssidStr = "N/A";
    String sinalStr = "N/A";
    bool ethernetConnected = false;

#if ENABLE_ETHERNET==1
    ethernetConnected = (ethernetLocalIP() != IPAddress(0, 0, 0, 0));
#endif

    wifi_mode_t wmode = WiFi.getMode();
    if (wmode == WIFI_MODE_AP) { // 🧠 AP only active
        ipStr = WiFi.softAPIP().toString(); // 🌐 IP do AP
        ssidStr = WiFi.softAPSSID(); // 🧠 Usar diretamente SSID (evita hostname 'espressif')
        if (ssidStr.length() == 0) { // ⚠️ Se por algum motivo vier vazio
            String cfgSSID = configManager.getAccessPointSSID(); // 📦 Get from ConfigManager
            if (cfgSSID.length()) ssidStr = cfgSSID; // ✅ Fallback to persisted configuration
        }
        if (ssidStr == "espressif") { // ⚠️ Default hostname still present
            String cfgSSID = configManager.getAccessPointSSID(); // 📦 Pesquisa SSID configurado
            if (cfgSSID.length()) ssidStr = cfgSSID; // ✅ Replace to avoid showing default
        }
        sinalStr = "-"; // 🧠 Sem RSSI em modo AP puro
    } else if (wmode == WIFI_MODE_APSTA) { // 🧠 Hybrid mode
        if (WiFi.status() == WL_CONNECTED) { // ✅ Connected to STA
            ipStr = WiFi.localIP().toString(); // 🌐 IP STA
            ssidStr = WiFi.SSID(); // 📡 SSID da rede cliente
            sinalStr = getWiFiSignalBars(); // 📶 Barras
        } else { // 🧠 No STA link, show AP
            ipStr = WiFi.softAPIP().toString(); // 🌐 IP AP
            ssidStr = WiFi.softAPSSID(); // 📡 SSID AP direto
            if (ssidStr.length() == 0 || ssidStr == "espressif") { // ⚠️ Vazio ou default
                String cfgSSID = configManager.getAccessPointSSID(); // 📦 Config persistida
                if (cfgSSID.length()) ssidStr = cfgSSID; // ✅ Usa configurado
            }
            sinalStr = "-"; // 🧠 Sem RSSI
        }
    } else { // 🧠 WIFI_MODE_STA or other
        bool connected = (WiFi.status() == WL_CONNECTED); // 🧠 Link state
        if (connected) {
            ipStr = WiFi.localIP().toString();
            ssidStr = WiFi.SSID();
            sinalStr = getWiFiSignalBars();
        } else if (ethernetConnected) {
            ipStr = ethernetLocalIP().toString();
            ssidStr = "Ethernet";
            sinalStr = "LINK";
        } else if (apActive) {
            ssidStr = WiFi.softAPSSID();
            sinalStr = "-";
        }
        if (!connected && !ethernetConnected && apActive && (ssidStr.length() == 0 || ssidStr == "espressif")) { // ⚠️ Corrigir fallback
            String cfgSSID = configManager.getAccessPointSSID(); // 📦 Config SSID AP
            if (cfgSSID.length()) ssidStr = cfgSSID; // ✅ Substitui
        }
    }

    line1 = "IP: " + ipStr + " | SSID: " + ssidStr + " | Sinal de Rede: " + sinalStr;

    // Line 2: Uptime and System Status with Storage Info
    // Storage information: show MicroSD or PSRAM mode with free space
    String storage_info; // Storage mode and free space text
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        // SD Card mode: show free space
        uint64_t totalBytes = sdcardTotalBytes();
        uint64_t usedBytes = sdcardUsedBytes();
        uint64_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        uint32_t freeMB = (uint32_t)(freeBytes / (1024ULL * 1024ULL));
        // Display rule: show MB up to 99; if >=100MB switch to GB with 2-character style
        if (freeMB <= 99U) {
            storage_info = "SD: " + String(freeMB) + "MB livre";
        } else {
            double freeGB = (double)freeBytes / (1024.0 * 1024.0 * 1024.0);
            char buf[32];
            if (freeGB < 10.0) {
                // e.g. 1.2GB (one decimal to keep two significant chars)
                snprintf(buf, sizeof(buf), "SD: %.1fGB livre", freeGB);
            } else {
                // e.g. 12GB (integer, two digits)
                snprintf(buf, sizeof(buf), "SD: %dGB livre", (int)round(freeGB));
            }
            storage_info = String(buf);
        }
    } else {
        // No SD: show LittleFS free space on every pack/screen
        uint64_t freeBytes = littlefsFreeBytes();
        uint64_t totalBytesFS = littlefsTotalBytes();
        if (totalBytesFS > 0) {
            uint32_t freeKB = (uint32_t)(freeBytes / 1024ULL);
            storage_info = "FS: " + String(freeKB) + "KB livre";
        } else {
            storage_info = "Storage: N/A";
        }
    }
    
    line2 = "Uptime: " + formatUptime(millis() - bootTime) +
            " | CPU: " + String(ESP.getCpuFreqMHz()) + "MHz" + 
            " | Heap: " + String(ESP.getFreeHeap() / 1024) + "KB" +
            " | " + storage_info +
            " | Firmware: " + String(FIRMWARE_VERSION);

    // Line 3: module-specific runtime info — rotates between compiled modules.
    // RS232/LPT raw-byte counters intentionally excluded; shown in /monitor page instead.
    // In RAMBOX/Unikon builds, keep line 3 focused on Unikon metrics only.
    String moduleLines[16];
    int moduleCount = 0;

#if (defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)) || (ENABLE_UNIKON==1)
    const bool line3UnikonOnly = true;
#else
    const bool line3UnikonOnly = false;
#endif

#if ENABLE_BRICON==1
    // Rambox: line 3 names the module that owns the RS232 port (Unikon RAMBOX or
    // Bricon) and its arrival counters — only one module can be active at a time.
    const bool line3BriconActive = (strcmp(briconActiveModuleName(), "bricon") == 0);
#else
    const bool line3BriconActive = false;
#endif
#if ENABLE_UNIKON==0 && ENABLE_BRICON==0
    (void)line3BriconActive;   // no module line in this build
#endif

#if ENABLE_UNIKON==1
    if (!line3BriconActive) {
        UnikonStatus us = unikonGetStatus();
        if (us.connected) {
            moduleLines[moduleCount++] = L(activeLang->screenUnikonPlus) + String(us.lastNewCount) +
                                         " total " + String(us.totalKnown) +
                                         " reg " + String(us.registeredCount);
        } else {
            moduleLines[moduleCount++] = L(activeLang->screenUnikonOffline) + String(us.totalKnown);
        }
    }
#endif

#if ENABLE_BRICON==1
    if (line3BriconActive) {
        BriconStatus bs = briconGetStatus();
        uint16_t briconTotal = briconGetArrivalCount();
        if (bs.connected) {
            moduleLines[moduleCount++] = L(activeLang->screenBriconPlus) + String(bs.lastNewCount) +
                                         " total " + String(briconTotal) +
                                         " reg " + String(briconGetRegistrationCount());
        } else {
            moduleLines[moduleCount++] = L(activeLang->screenBriconOffline) + String(briconTotal);
        }
    }
#endif

    if (!line3UnikonOnly) {
#if ENABLE_AHT20_BMP280==1
        {
            const SensorConfig &cfg = sensorConfigManager.getConfig();
            if (cfg.last_reading_time > 0) {
                char envBuf[64];
                snprintf(envBuf, sizeof(envBuf), "Temp: %.1fC Hum: %.0f%% P: %.0fhPa",
                         cfg.last_temp_avg, cfg.last_humidity, cfg.last_pressure_hpa);
                moduleLines[moduleCount++] = String(envBuf);
            } else {
                moduleLines[moduleCount++] = L(activeLang->screenTempNoReading);
            }
        }
#endif

#if ENABLE_MQ135==1
        {
            if (mq135Sensor.isInitialized()) {
                MQ135Reading gas = mq135Sensor.getLastReading();
                if (gas.valid) {
                    char gasBuf[64];
                    snprintf(gasBuf, sizeof(gasBuf), "Gas: CO2 %.0fppm AQI %.0f",
                             gas.ppm_co2_corrected, gas.aqi);
                    moduleLines[moduleCount++] = String(gasBuf);
                } else {
                    unsigned long warmupSec = mq135Sensor.getWarmupRemaining() / 1000UL;
                    moduleLines[moduleCount++] = L(activeLang->screenGasWarmup) + String(warmupSec) + "s";
                }
            } else {
                moduleLines[moduleCount++] = L(activeLang->screenGasNoInit);
            }
        }
#endif

#if ENABLE_PROXIMITY==1
        {
            uint8_t nCh = proximityChannelCount();
            if (nCh > 0) {
                uint32_t totalIn = 0, totalOut = 0;
                for (uint8_t c = 0; c < nCh; c++) {
                    ProximityChannel ch = proximityGetChannel(c);
                    totalIn  += ch.count_in;
                    totalOut += ch.count_out;
                }
                char proxBuf[64];
                if (nCh == 1) {
                    ProximityChannel ch0 = proximityGetChannel(0);
                    snprintf(proxBuf, sizeof(proxBuf), "Prox %s: IN:%lu OUT:%lu %.1frpm",
                             ch0.name, (unsigned long)ch0.count_in,
                             (unsigned long)ch0.count_out, ch0.rate_ppm);
                } else {
                    snprintf(proxBuf, sizeof(proxBuf), "Prox: %u ch IN:%lu OUT:%lu",
                             (unsigned)nCh, (unsigned long)totalIn, (unsigned long)totalOut);
                }
                moduleLines[moduleCount++] = String(proxBuf);
            }
        }
#endif

#if ENABLE_MODBUS==1
        {
            if (g_modbusDeviceCount > 0) {
                uint32_t totalPolls = 0, totalErrors = 0;
                for (uint8_t d = 0; d < g_modbusDeviceCount; d++) {
                    totalPolls  += g_modbusDevices[d].pollCount;
                    totalErrors += g_modbusDevices[d].errorCount;
                }
                char mbBuf[64];
                snprintf(mbBuf, sizeof(mbBuf), "Modbus: %u dev poll:%lu err:%lu",
                         (unsigned)g_modbusDeviceCount,
                         (unsigned long)totalPolls,
                         (unsigned long)totalErrors);
                moduleLines[moduleCount++] = String(mbBuf);
            }
        }
#endif

#if ENABLE_CAN==1
        {
            if (canRuntimeIsRunning()) {
                CanRuntimeStats cs = canRuntimeGetStats();
                char canBuf[64];
                snprintf(canBuf, sizeof(canBuf), "CAN: RX:%lu TX:%lu err:%lu",
                         (unsigned long)cs.total_rx,
                         (unsigned long)cs.total_tx,
                         (unsigned long)cs.total_tx_errors);
                moduleLines[moduleCount++] = String(canBuf);
            }
        }
#endif

#if ENABLE_USB_HOST==1
        {
            if (usbHostIsRunning()) {
                UsbHostStats us = usbHostGetStats();
                char usbBuf[64];
                snprintf(usbBuf, sizeof(usbBuf), "USB:%u dev RX:%lu TX:%lu",
                         (unsigned)us.devices_connected,
                         (unsigned long)us.total_rx_bytes,
                         (unsigned long)us.total_tx_bytes);
                moduleLines[moduleCount++] = String(usbBuf);
            }
        }
#endif

#if ENABLE_UDA1334A==1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
        {
            if (radioAudioIsActive()) {
                const char* title = radioAudioGetCurrentTitle();
                const char* artist = radioAudioGetCurrentArtist();
                if (title && title[0]) {
                    if (artist && artist[0] && strcmp(artist, title) != 0) {
                        moduleLines[moduleCount++] = L(activeLang->screenRadioLabel) + artist + " - " + title;
                    } else {
                        moduleLines[moduleCount++] = L(activeLang->screenRadioLabel) + title;
                    }
                } else {
                    moduleLines[moduleCount++] = L(activeLang->screenRadioPlaying);
                }
            } else {
                moduleLines[moduleCount++] = L(activeLang->screenRadioStopped);
            }
        }
#endif

        // Scheduler/email summary — always added as a fallback card.
        {
            int intervalActiveCount = 0;
            for (int i = 1; i <= 10; i++) {
                String cmd, unit, days, tpl;
                int interval = 0;
                bool en = false;
                if (configManager.getSchedCmdSlot(i, cmd, interval, unit, days, tpl, en)) {
                    if (en && cmd.length()) intervalActiveCount++;
                }
            }
            int datetimeActiveCount = 0;
            for (int i = 1; i <= 16; i++) {
                String c, d, t, tpl;
                bool en = false;
                if (configManager.getSchedDtSlot(i, c, d, t, tpl, en)) {
                    if (en && c.length()) datetimeActiveCount++;
                }
            }
            size_t recipientsCount = configManager.countRecipientSlotsNonEmpty();
            size_t templatesCount = configManager.getRs232Templates().size();
            int totalSched = intervalActiveCount + datetimeActiveCount;
            moduleLines[moduleCount++] = L(activeLang->screenSchedLabel) + totalSched +
                                         " dest:" + recipientsCount +
                                         " tpl:" + templatesCount;
        }
    }

    if (moduleCount > 0) {
        static bool randomSeeded = false;
        static unsigned long lastRotateMs = 0;
        static int currentModuleIndex = 0;
        const unsigned long MODULE_ROTATE_MS = 6000UL;

        if (!randomSeeded) {
            randomSeed((uint32_t)micros());
            randomSeeded = true;
        }

        if (currentModuleIndex < 0 || currentModuleIndex >= moduleCount) {
            currentModuleIndex = 0;
            lastRotateMs = millis();
        }

        unsigned long nowMs = millis();
        if (moduleCount > 1 && (nowMs - lastRotateMs) >= MODULE_ROTATE_MS) {
            int nextIndex = currentModuleIndex;
            for (uint8_t guard = 0; guard < 8 && nextIndex == currentModuleIndex; guard++) {
                nextIndex = random(moduleCount);
            }
            if (nextIndex == currentModuleIndex) {
                nextIndex = (currentModuleIndex + 1) % moduleCount;
            }
            currentModuleIndex = nextIndex;
            lastRotateMs = nowMs;
        }

        line3 = moduleLines[currentModuleIndex];
    } else {
        line3 = "Status: sem modulos ativos";
    }

    // Pack-specific status layouts live in src/screen/view_*.h (one file
    // per pack, selected by screen/view_selector.h). Only the shared line
    // builders stay here; rendering is delegated so new packs or displays
    // never touch this core file.
    ScreenStatusContext statusCtx{line1, line2, line3, ipStr, ssidStr, sinalStr,
                                  storage_info, line3UnikonOnly};
#if SCREEN_TYPE == 3
    screenDrawStatusView(*this, statusCtx);
#else
    screenDrawStatusView(*this, oled, statusCtx);
#endif
}

void ScreenManager::showReceivingData() {
    // Show on two lines: line1="Receber", line2="Dados"
    if (blinkActive && !blinkVisible) { // 🧠 Hidden during blink
        oled.clearDisplay(); // 🧹 Clear
        oled.display(); // 📤 Atualiza
        return; // 🔁 Exit without drawing text
    }
    displayMessage("Receber", "Dados");
}

void ScreenManager::showSendingCommand() {
    if (blinkActive && !blinkVisible) { // 🧠 Oculto
        oled.clearDisplay(); // 🧹 Clear
        oled.display(); // 📤 Atualiza
        return; // 🔁 Exit
    }
#if SCREEN_TYPE == 2
    // 2.42" SSD1309: type in large text, full command text on 2 lines, no scrolling
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    String typeStr = lastSentType.length() > 0 ? lastSentType : String("RS232");
    int tx = (128 - (int)typeStr.length() * 12) / 2;
    if (tx < 0) tx = 0;
    oled.setCursor(tx, 2);
    oled.print(typeStr);
    oled.setTextSize(1);
    String cmd = lastSentCommand.length() > 0 ? lastSentCommand : L(activeLang->screenCommand);
    oled.setCursor(0, 24);
    oled.print(cmd.substring(0, 21));
    if (cmd.length() > 21) {
        oled.setCursor(0, 33);
        oled.print(cmd.substring(21, 42));
    }
    oled.setCursor(0, 55);
    oled.print(L(activeLang->screenSent));
    oled.display();
#else
    // 0.96" SSD1306: scrolling 3-line layout
    if (lastSentCommand.length() > 0) {
        displayMessage(lastSentType, lastSentCommand, L(activeLang->screenSent));
    } else {
        displayMessage("RS232", L(activeLang->screenCommand), L(activeLang->screenSent));
    }
#endif
}

void ScreenManager::showEmailSent() {
    if (blinkActive && !blinkVisible) { // 🧠 Oculto
        oled.clearDisplay(); // 🧹 Clear
        oled.display(); // 📤 Atualiza
        return; // 🔁 Exit
    }
    String line3 = "";
#if ENABLE_UNIKON==1
    UnikonStatus us = unikonGetStatus();
    line3 = L(activeLang->screenArrivals) + String(us.totalKnown);
#endif
    displayMessage("Email", L(activeLang->screenSent), line3);
}

void ScreenManager::showHeapAlert() {
    // Two-line centered message indicating low heap
    if (blinkActive && !blinkVisible) { // 🧠 Oculto
        oled.clearDisplay(); // 🧹 Clear
        oled.display(); // 📤 Atualiza
        return; // 🔁 Exit
    }
    // ✅ Message consistent with new threshold (<65KB)
    displayMessage("Heap", "Livre", "<65KB");
}

void ScreenManager::triggerHeapDegradation() {
    showingHeapDegradation = true;
    heapDegradationAlertStartTime = millis();
    startBlink(); // 🚀 Start blink effect
    showHeapDegradation(); // 📟 Mostra imediatamente
}

void ScreenManager::showHeapDegradation() {
    if (blinkActive && !blinkVisible) { // 🧠 Hidden during blink
        oled.clearDisplay();
        oled.display();
        return;
    }
    // 🚨 Degradation message (>40% loss vs baseline)
    displayMessage("HEAP", "DEGRAD", ">40%");
}

void ScreenManager::triggerConfigSaved() {
    showingConfigSaved = true;
    configSavedStartTime = millis();
    startBlink(); // 🚀 Start blink effect for 'Options Saved'
    showConfigSaved(); // 🧠 Show the message immediately (do not wait for the loop)
}

void ScreenManager::showConfigSaved() {
    if (blinkActive && !blinkVisible) { // 🧠 Oculto
        oled.clearDisplay(); // 🧹 Clear
        oled.display(); // 📤 Atualiza
        return; // 🔁 Exit
    }
    // 🧠 OLED font is ASCII → use accent-free version to avoid artifacts; future alternative: custom font
    displayMessage("Opcoes", "Guardadas", "");
}

void ScreenManager::triggerNVSWarning(int usagePercent) {
    // DEPRECATED: Screen now shows storage (SD/PSRAM) instead of NVS
    // Keeping function for backward compatibility but does nothing
    (void)usagePercent; // Suppress unused parameter warning
}

void ScreenManager::showNVSWarning() {
    // DEPRECATED: Screen now shows storage (SD/PSRAM) instead of NVS
    // Keeping function for backward compatibility but does nothing
}

String ScreenManager::formatUptime(unsigned long uptime) {
    unsigned long seconds = uptime / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    tempStr = "";
    
    if (days > 0) {
        tempStr = String(days) + "d" + String(hours % 24) + "h";
    } else if (hours > 0) {
        tempStr = String(hours) + "h" + String(minutes % 60) + "m";
    } else {
        tempStr = String(minutes) + "m" + String(seconds % 60) + "s";
    }
    
    return tempStr;
}

// Simple accent-stripping helper for ASCII OLED fonts
String ScreenManager::stripAccents(const String &s) {
    String out = s;
    // crude replacements for common Portuguese accented characters
    out.replace("á","a"); out.replace("à","a"); out.replace("ã","a"); out.replace("â","a"); out.replace("Á","A"); out.replace("À","A"); out.replace("Ã","A"); out.replace("Â","A");
    out.replace("é","e"); out.replace("ê","e"); out.replace("É","E"); out.replace("Ê","E");
    out.replace("í","i"); out.replace("Í","I");
    out.replace("ó","o"); out.replace("õ","o"); out.replace("ô","o"); out.replace("Ó","O"); out.replace("Õ","O"); out.replace("Ô","O");
    out.replace("ú","u"); out.replace("Ú","U");
    out.replace("ç","c"); out.replace("Ç","C");
    return out;
}

String ScreenManager::formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + "B";
    } else if (bytes < 1024 * 1024) {
        return String(bytes / 1024) + "KB";
    } else {
        return String(bytes / (1024 * 1024)) + "MB";
    }
}

int ScreenManager::getWiFiRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return -100;
}

String ScreenManager::getWiFiSignalBars() {
    int rssi = getWiFiRSSI();
    
    if (rssi >= -50) {
        return "|||||";
    } else if (rssi >= -60) {
        return "||||";
    } else if (rssi >= -70) {
        return "|||";
    } else if (rssi >= -80) {
        return "||";
    } else {
        return "|";
    }
}

// Wrapper to run I2C scan from the command
void runI2CScan() {
#if ENABLE_LCD_SCREEN==1
    screenManager.scanI2CBus();
#else
    sendVirtualMessage(String(F("I2CSCAN not available (OLED disabled in build).")), WARN);
#endif
}

void initScreen() {
    screenManager.begin();
}

void updateScreen() {
    screenManager.update();
}

void displayScreenMessage(const String& line1, const String& line2, const String& line3) {
    screenManager.displayMessage(line1, line2, line3);
}

void displayScreenMessage(const String& line1, const String& line2) {
    screenManager.displayMessage(line1, line2, "");
}
// Utility functions for scroll control
void setScreenScrollEnabled(bool enabled) {
    screenManager.setScrollEnabled(enabled);
}

bool isScreenScrollEnabled() {
    return screenManager.isScrollEnabled();
}

// Hook called by ConfigManager when a save completes.
// Implemented here to avoid circular includes; safe no-op if screen not initialized.
void triggerScreenConfigSaved() {
    if (screenManager.isInitialized()) {
        screenManager.triggerConfigSaved();
    }
}

// Hook to trigger NVS warning when usage > 75% - DEPRECATED: NVS no longer displayed on screen
void triggerScreenNVSWarning(int usagePercent) {
    // Disabled: Screen now shows storage (SD/PSRAM) instead of NVS
    // if (screenManager.isInitialized()) {
    //     screenManager.triggerNVSWarning(usagePercent);
    // }
    (void)usagePercent; // Suppress unused parameter warning
}

// Hook to trigger the sending-command temporary screen from other modules.
void triggerScreenCommandSent(const String& command, const String& type) {
    if (screenManager.isInitialized()) {
        screenManager.triggerSendingCommand(command, type);
    }
}

// Implementation of the ScreenManager trigger for sending command
void ScreenManager::triggerSendingCommand(const String& command, const String& type) {
    // 🧠 Truncate command to fit the screen (max 21 chars OLED)
    if (command.length() > 0) {
        lastSentCommand = (command.length() > 21) ? command.substring(0, 18) + "..." : command;
    } else {
        lastSentCommand = ""; // 🧠 Sem comando fornecido
    }
    lastSentType = type.length() > 0 ? type : "RS232"; // 🧠 Default: RS232
    showingSendingCommand = true;
    sendingCommandStartTime = millis();
    // Show the message immediately
    showSendingCommand();
    startBlink(); // 🚀 Start effect
}

// 🧠 Initialize blink effect parameters
void ScreenManager::startBlink() {
    blinkActive = true; // 🧠 Activate
    blinkStartTime = millis(); // 🕒 Mark start
    blinkLastToggle = blinkStartTime; // 🕒 Initialize last toggle
    blinkToggleCount = 0; // ♻️ Reset counter (OFF flashes already done)
    blinkVisible = true; // ✅ Start visible
}

// 🧠 Atualiza estado de piscar (chamado em update())
void ScreenManager::updateBlink(unsigned long now) {
    if (!blinkActive) return; // 🧠 Nothing to do
    unsigned long elapsed = now - blinkStartTime; // 🕒 Elapsed time
    if (elapsed >= BLINK_TOTAL_MS) { // ⏱️ Total end
        blinkActive = false; // ♻️ Disable
        blinkVisible = true; // ✅ Keep visible
        return; // 🚪 Exit
    }

    // 🧠 Planned instants for the start of each OFF (distributed almost uniformly)
    unsigned long slotDuration = BLINK_TOTAL_MS / BLINK_FLASHES; // 🧮 ~666ms
    // 🧠 Compute which flash we are processing (0..BLINK_FLASHES-1)
    for (uint8_t i = 0; i < BLINK_FLASHES; i++) { // 🔁 Percorre flashes
        unsigned long flashStart = blinkStartTime + (i * slotDuration); // 🕒 Inicio janela flash
        if (elapsed >= (i * slotDuration) && elapsed < (i * slotDuration + BLINK_OFF_MS)) { // 🧠 Dentro da janela OFF
            blinkVisible = false; // ❌ Off
            return; // 🚪 Exit (final state of this frame)
        }
    }
    blinkVisible = true; // ✅ Outside any OFF window it stays visible
}

#else // ENABLE_LCD_SCREEN==0

// Global stub instance (declared extern in screen.h)
ScreenManager screenManager;

// Trigger stubs not covered by inline stubs in screen.h
void triggerScreenConfigSaved() {}
void triggerScreenNVSWarning(int) {}
void triggerScreenCommandSent(const String&, const String&) {}
void runI2CScan() {}

#endif // ENABLE_LCD_SCREEN