// =============================================================
// 🔗 Arduino Nano ↔ ESP32 bridge (UART) — Header
// =============================================================
// ✅ Responsibility: receive bytes from the Nano (Centronics→UART) and push them to bufferRx.
// ✅ Flow: XON/XOFF (ESP32 sends 0x13 pause, 0x11 resume) with free-space thresholds.
// =============================================================
#pragma once
#include <Arduino.h>
// 🔐 Feature flags to enable/disable the DB25/Centronics module
#include "feature_flags.h"

// 📏 Shared receive buffer size with the rest of the firmware
#ifndef BUFFER_RX_SIZE
#define BUFFER_RX_SIZE 131072 // 🚀 OPTIMIZATION: 128KB in PSRAM (was 8KB Heap)
#endif

// 🚦 Thresholds for XON/XOFF flow control
#ifndef NANO_XOFF_THRESHOLD
// Pause the emitter when free buffer space is < 32KB (equals ~96KB used)
#define NANO_XOFF_THRESHOLD 32768 // Pause when free < 32KB (≈ 96KB used)
#endif
#ifndef NANO_XON_THRESHOLD
// Resume the emitter when free space exceeds 48KB (hysteresis to avoid flapping)
#define NANO_XON_THRESHOLD 49152 // Resume when free > 48KB
#endif

// 🔌 Default pins for the Nano-dedicated UART (adjustable)
// 🔸 Note: on ESP32‑S3 some GPIOs (9/10 and 33-37) are reserved/affected by flash/PSRAM.
//         We detect ESP32‑S3 and automatically remap to safe pins (GPIO10/11) to avoid the PSRAM OPI bus.
#ifndef NANO_UART_RX_PIN
  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3) || defined(ARDUINO_ARCH_ESP32S3)
    #define NANO_UART_RX_PIN 41 // ESP32-S3 remapped (receives from Nano TX) — using GPIO41/40 avoids FSPI conflicts
  #else
    #define NANO_UART_RX_PIN 35 // Input only (original ESP32)
  #endif
#endif
#ifndef NANO_UART_TX_PIN
  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3) || defined(ARDUINO_ARCH_ESP32S3)
    #define NANO_UART_TX_PIN 40 // ESP32-S3 remapped (sends to Nano RX) — using GPIO41/40 avoids FSPI conflicts
  #else
    #define NANO_UART_TX_PIN 33
  #endif
#endif

// 🔢 Default baudrate
#ifndef NANO_UART_BAUD
#define NANO_UART_BAUD 500000
#endif

// 📣 API
#if ENABLE_DB25==1
// ✅ Declarations when DB25 is enabled
void nanoBridgeBegin();     // 🚀 Initialize UART and state
void nanoBridgePoll();      // 🔁 Drain UART to bufferRx and manage XON/XOFF
void nanoBridgeSendXon();   // ➡️ Force XON
void nanoBridgeSendXoff();  // ➡️ Force XOFF
void nanoBridgeReboot();    // ♻️ Request Nano reboot (sends dedicated command)

// 📣 Extra API: low-level configuration commands on the Nano
void nanoBridgeSetAckWidth(uint8_t us);      // 0..255 µs (typically 6..20)
void nanoBridgeSetStrobeMinLow(uint8_t us);  // Glitch filter 0..255 µs
void nanoBridgeSetPauseMode(uint8_t mode);   // 0=immediate, 1=graceful
void nanoBridgeForceBusy(bool on);           // Force BUSY high/normal
void nanoBridgeSetXonXoffEnable(bool on);    // Enable/disable XON/XOFF honoring
void nanoBridgeSetAckDelayBefore(uint8_t us); // Delay before ACK
void nanoBridgeSetBusyTail(uint8_t us);       // BUSY tail after ACK
void nanoBridgeResetState();                  // Fast reset of volatile state
void nanoBridgeSetPauseGraceBytes(uint8_t k); // Number of bytes allowed after XOFF

#else

// ✅ No-op stubs when ENABLE_DB25==0, to avoid hardware references and keep linkage
static inline void nanoBridgeBegin() {}            // ✅ No-op: does not init Nano UART
static inline void nanoBridgePoll() {}             // ✅ No-op: does not drain data
static inline void nanoBridgeSendXon() {}          // ✅ No-op: no XON
static inline void nanoBridgeSendXoff() {}         // ✅ No-op: no XOFF
static inline void nanoBridgeReboot() {}           // ✅ No-op: no reboot
static inline void nanoBridgeSetAckWidth(uint8_t) {}       // ✅ No-op
static inline void nanoBridgeSetStrobeMinLow(uint8_t) {}   // ✅ No-op
static inline void nanoBridgeSetPauseMode(uint8_t) {}      // ✅ No-op
static inline void nanoBridgeForceBusy(bool) {}            // ✅ No-op
static inline void nanoBridgeSetXonXoffEnable(bool) {}     // ✅ No-op
static inline void nanoBridgeSetAckDelayBefore(uint8_t) {} // ✅ No-op
static inline void nanoBridgeSetBusyTail(uint8_t) {}       // ✅ No-op
static inline void nanoBridgeResetState() {}               // ✅ No-op
static inline void nanoBridgeSetPauseGraceBytes(uint8_t) {}// ✅ No-op

#endif
