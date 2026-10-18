#include "arduino_bridge.h"
#include "feature_flags.h"
#include "commands.h" // sendVirtualMessage()

#if ENABLE_DB25==1

// 📦 External core dependencies
extern uint8_t* bufferRx; // defined in chatgpt.ino (now a uint8_t* pointer)
extern volatile int writePos;
extern volatile int readPos;
extern volatile unsigned long overflowCount;
extern unsigned long lastReceptionTime;
extern volatile bool isReceivingData;
extern unsigned long lastDataReceivedForInactivityAlert; // Inactivity alert timestamp
extern bool inactivityAlertSent; // Inactivity alert flag (reset on new data)
extern bool inactivityAlertSentSlot[]; // Per-slot inactivity period flags

// 🔐 Local state
static bool nanoPaused = false; // emitter logical state (per our request)
static bool sentXoff = false;   // whether XOFF was already sent
static bool sentXon  = true;    // last command was XON

// 🖧 We select Serial1 for the bridge (Serial2 may already be in use by other modules)
HardwareSerial NanoUART(1);
// 🔁 Special reserved command for Nano reboot
static const uint8_t NANO_CMD_REBOOT = 0xF5;
static const uint8_t NANO_CMD_SET_ACK_WIDTH  = 0xA0;
static const uint8_t NANO_CMD_SET_STROBE_MIN = 0xA1;
static const uint8_t NANO_CMD_SET_PAUSE_MODE = 0xA2;
static const uint8_t NANO_CMD_FORCE_BUSY     = 0xA3;
static const uint8_t NANO_CMD_SET_XONXOFF_EN = 0xA4;
static const uint8_t NANO_CMD_SET_ACK_DELAY  = 0xA5;
static const uint8_t NANO_CMD_SET_BUSY_TAIL  = 0xA6;
static const uint8_t NANO_CMD_RESET_STATE    = 0xA8;
static const uint8_t NANO_CMD_SET_PAUSE_GRACE= 0xA9;

static inline int bufferFree() {
    int used = (writePos - readPos + BUFFER_RX_SIZE) % BUFFER_RX_SIZE;
    return BUFFER_RX_SIZE - 1 - used; // keep one slot empty to distinguish full/empty
}

void nanoBridgeSendXoff(){ if(!sentXoff){ NanoUART.write(0x13); sentXoff=true; sentXon=false; } }
void nanoBridgeSendXon(){  if(!sentXon){  NanoUART.write(0x11); sentXon=true;  sentXoff=false; } }
void nanoBridgeReboot(){   NanoUART.write(NANO_CMD_REBOOT); }
void nanoBridgeSetAckWidth(uint8_t us){      NanoUART.write(NANO_CMD_SET_ACK_WIDTH);  NanoUART.write(us); }
void nanoBridgeSetStrobeMinLow(uint8_t us){  NanoUART.write(NANO_CMD_SET_STROBE_MIN); NanoUART.write(us); }
void nanoBridgeSetPauseMode(uint8_t mode){   NanoUART.write(NANO_CMD_SET_PAUSE_MODE); NanoUART.write(mode ? 1 : 0); }
void nanoBridgeForceBusy(bool on){           NanoUART.write(NANO_CMD_FORCE_BUSY);     NanoUART.write(on ? 1 : 0); }
void nanoBridgeSetXonXoffEnable(bool on){    NanoUART.write(NANO_CMD_SET_XONXOFF_EN); NanoUART.write(on ? 1 : 0); }
void nanoBridgeSetAckDelayBefore(uint8_t us){ NanoUART.write(NANO_CMD_SET_ACK_DELAY);  NanoUART.write(us); }
void nanoBridgeSetBusyTail(uint8_t us){       NanoUART.write(NANO_CMD_SET_BUSY_TAIL);  NanoUART.write(us); }
void nanoBridgeResetState(){                  NanoUART.write(NANO_CMD_RESET_STATE); }
void nanoBridgeSetPauseGraceBytes(uint8_t k){ NanoUART.write(NANO_CMD_SET_PAUSE_GRACE); NanoUART.write(k); }

void nanoBridgeBegin(){
    // Initialize UART with custom pins
    NanoUART.begin(NANO_UART_BAUD, SERIAL_8N1, NANO_UART_RX_PIN, NANO_UART_TX_PIN);
    // Information log about pins and baud used (useful for debug and S3 vs ESP32 verification)
    {
        char _buf[64];
        snprintf(_buf, sizeof(_buf), "NanoBridge: UART RX=%d TX=%d @%u", NANO_UART_RX_PIN, NANO_UART_TX_PIN, (unsigned)NANO_UART_BAUD);
        sendVirtualMessage(String(_buf), INFO, false, false);
    }
    // Initial state: allow sending
    nanoPaused = false; sentXon = true; sentXoff = false; NanoUART.write(0x11);

    // Robust boot sequence to start ESP32 and Nano together
    // Strategy: optionally reboot the Nano, hold BUSY high, apply tunables,
    // and only at the end release BUSY. Repeat the setup twice to tolerate lost bytes during Nano boot.

#if ENABLE_NANO_REBOOT_ON_BOOT==1
    nanoBridgeReboot();             // ♻️ Controlled Nano reset
    delay(700);                     // ⏱️ Wait for bootloader + setup (~700ms)
    while (NanoUART.available()) { (void)NanoUART.read(); } // 🧹 Purge echoes/boot messages
#endif

    // 1) Hold emitter: forced BUSY ON so the UNO does not start while we stabilize
    nanoBridgeForceBusy(true);
    delay(10);
    // 2) (No /ERROR control) — simplified to the minimum signal set

    auto applyTunables = [](){
        nanoBridgeSetAckWidth(20);        // ACK ~20 µs (tolerant)
        nanoBridgeSetStrobeMinLow(4);     // Anti-glitch filter
        nanoBridgeSetPauseMode(1);        // ✅ Graceful pause
        nanoBridgeSetXonXoffEnable(true); // Honor XON/XOFF
        nanoBridgeSetAckDelayBefore(0);   // No delay before ACK
        nanoBridgeSetBusyTail(0);         // No BUSY tail
        nanoBridgeSetPauseGraceBytes(0);  // No extra grace bytes
    };

    // 3) Apply tunables 2x with a short gap (tolerance for initial sync)
    applyTunables(); delay(20); applyTunables();

    // 4) Release sender
    nanoBridgeForceBusy(false);
}

void nanoBridgePoll(){
    // 🚦 Free-space based XON/XOFF management
    int freeB = bufferFree();
    if (freeB < NANO_XOFF_THRESHOLD) { nanoBridgeSendXoff(); nanoPaused = true; }
    else if (freeB > NANO_XON_THRESHOLD) { nanoBridgeSendXon(); nanoPaused = false; }

    // 📥 Drain UART into the circular buffer
    while (NanoUART.available()) {
        uint8_t b = (uint8_t)NanoUART.read();
        int nextPos = (writePos + 1) % BUFFER_RX_SIZE;
        if (nextPos != readPos) { 
            bufferRx[writePos] = b; 
            writePos = nextPos; 
            lastReceptionTime = millis(); 
#if ENABLE_RS232==1 || ENABLE_DB25==1
            // Only reset the inactivity timer from the UART bridge when the
            // bridge carries REAL external production data (RS232/LPT boards).
            // On sensor-only boards (e.g. Waveshare 8DI8DO) the bridge is not a
            // production source, so it must NOT reset the inactivity alert —
            // only real sensor DI1 pulses do (see proximity_manager.cpp).
            lastDataReceivedForInactivityAlert = millis(); // Update for inactivity alert
            inactivityAlertSent = false; // Reset flag to allow new alerts after activity resumes
            for (int _i = 0; _i < 6; ++_i) inactivityAlertSentSlot[_i] = false;
            // DIAGNOSTIC: Log inactivity timestamp update (rate-limited to avoid spam)
            static unsigned long lastInactivityUpdateLogBridge = 0;
            if (millis() - lastInactivityUpdateLogBridge > 15000) { // Log every 15s for better visibility
                char logBuf[100];
                snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by UART Bridge byte (ts=%lu)", lastDataReceivedForInactivityAlert);
                sendVirtualMessage(String(logBuf), INFO);
                lastInactivityUpdateLogBridge = millis();
            }
#endif
            isReceivingData = true;
        }
        else { overflowCount++; /* buffer full; XOFF should already be active */ break; }
        // Lightly re-evaluate thresholds
        freeB = bufferFree();
        if (freeB < NANO_XOFF_THRESHOLD) { nanoBridgeSendXoff(); nanoPaused = true; }
    }
}

#else
// ✅ No-op stubs when ENABLE_DB25==0 live in the header as static inline
// No implementation needed here
#endif
