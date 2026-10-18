// =============================================================
// Serial Sniffer — Header (dual passive channels)
// =============================================================
// Responsibility: listen to two serial channels in parallel (RX only),
// aggregate lines by CR/LF and inject into the global buffer with prefixes [S1]/[S2].
// Typical wiring uses two MAX3232 transceivers (DB9 RS232).
// NOTE: RX only — use RIN inputs and ROUT outputs; do not connect sniffer TX to the bus.
//   - MAX3232 #1 (S1): RIN ← DB9 pin 3 (PC TX), ROUT → ESP32 GPIO4
//   - MAX3232 #2 (S2): RIN ← DB9 pin 2 (PC RX), ROUT → ESP32 GPIO5
//   - T1IN of each MAX3232 must be left floating (or tied to GND); T1OUT must NOT connect to the RS232 bus.
//   - Common GND: connect GND of all devices (PC/device/MAX3232/ESP32)
//   - MAX3232 VCC = 3.3V; capacitors per datasheet.
// Conflicts: S1 uses UART1 (also used by DB25/Nano bridge). S2 uses UART2 (also used by RS232).
// When ENABLE_SNIFFER=1, ensure ENABLE_RS232=0 and ENABLE_DB25=0 to avoid conflicts.
// Reference: see `pinos_hardware.md` -> section "Serial Sniffer (RS232/TTL)" for wiring diagrams and parts.
// =============================================================
#pragma once
#include <Arduino.h>
#include "feature_flags.h"

// Default pins — GPIO4 and GPIO5 are safe general-purpose GPIOs on ESP32-S3 N16R8.
// BANNED pins on N16R8:
//   GPIO16-17: OPI PSRAM interface (driven by bootloader at power-on, corrupt RS232 bus glitch)
//   GPIO33-37: OPI PSRAM data bus (continuously driven by PSRAM controller, UART cannot use them)
//   GPIO26-32: OPI Flash bus
//   GPIO19-20: USB D+/D-
//   GPIO43-44: UART0 TX/RX (Serial debug)
// GPIO4 and GPIO5 are free on the N16R8 devkit with no conflicting peripherals.
#ifndef SNIFFER1_RX_PIN
#define SNIFFER1_RX_PIN 4  // Channel 1 (typically PC→Device)
#endif
#ifndef SNIFFER2_RX_PIN
#define SNIFFER2_RX_PIN 5  // Channel 2 (typically Device→PC)
#endif

// Baudrate and framing to use (default 8N1 at 19200 as project RS232 default)
#ifndef SNIFFER_DEFAULT_BAUD
#define SNIFFER_DEFAULT_BAUD 19200
#endif

// Callback to deliver a completed line ready to inject into the global pipeline
// NOTE: Must be provided by the caller (chatgpt.ino) and must not perform heavy allocations.
typedef void (*SnifferLineCallback)(uint8_t channel, const char* line, size_t len);

#if ENABLE_SNIFFER==1

// Initialize both receivers in RX-only mode
void snifferBegin(uint32_t baud, uint32_t serialCfg, SnifferLineCallback cb);

// Non-blocking poll: read bytes, aggregate lines and invoke callback when CR/LF detected
void snifferPoll();

// Queue a decoded line for the next LittleFS periodic flush (called from snifferInjectLine callback)
void snifferQueueForFlush(uint8_t channel, const char* line, size_t len);

// Flush all pending queued lines to /sniffer/YYYY-MM-DD.log in LittleFS (append mode)
// Call every 30 seconds from the main loop. No-op if nothing was queued.
void snifferFlushToLittleFS();

#else

// Stubs when disabled
static inline void snifferBegin(uint32_t, uint32_t, SnifferLineCallback) {}
static inline void snifferPoll() {}
static inline void snifferQueueForFlush(uint8_t, const char*, size_t) {}
static inline void snifferFlushToLittleFS() {}

#endif
