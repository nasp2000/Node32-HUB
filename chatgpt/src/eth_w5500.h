// =============================================================
// eth_w5500.h — W5500 / Ethernet configuration for ESP32 (module header)
// =============================================================
// Purpose: define pins and the minimal API to initialize a W5500 module.
// This file exposes: ethernetBegin(), ethernetStatus(), ethernetLoop().
// When Ethernet is active, the module also serves a small bootstrap HTTP page on port 80.
// Note: to avoid collisions with Arduino <Ethernet.h> on Windows, this header is named 'eth_w5500.h'.

#pragma once
#include "feature_flags.h"

#if ENABLE_ETHERNET==1
#include <Arduino.h> // 🧰 For IPAddress

// SPI/W5500 pins — shared with microSD (same FSPI bus)
//   • SCLK -> GPIO12 (FSPI CLK)
//   • MOSI -> GPIO11 (FSPI MOSI)
//   • MISO -> GPIO13 (FSPI MISO)
//   • CS   -> GPIO14 (CS different from microSD which uses GPIO10)
//   • RST  -> GPIO4
//   • INT  -> GPIO25

#ifndef ETH_SPI_MISO_PIN
#define ETH_SPI_MISO_PIN 13 // 🔄 MISO FSPI (GPIO13)
#endif
#ifndef ETH_SPI_MOSI_PIN
#define ETH_SPI_MOSI_PIN 11 // 🔄 MOSI FSPI (GPIO11)
#endif
#ifndef ETH_SPI_SCLK_PIN
#define ETH_SPI_SCLK_PIN 12 // 🔄 SCLK FSPI (GPIO12)
#endif
#ifndef ETH_CS_PIN
#define ETH_CS_PIN 14        // 🔄 Ethernet CS moved to 14 (free on N16R8)
#endif
#ifndef ETH_RST_PIN
#define ETH_RST_PIN 4       // Optional module reset
#endif
#ifndef ETH_INT_PIN
#define ETH_INT_PIN 25      // Interrupto do W5500 (opcional)
#endif

// Minimal API
void ethernetBegin();              // Initialize hardware and Ethernet stack
int  ethernetStatus();             // Return 0 if OK, !=0 on error
void ethernetLoop();               // Run DHCP/link maintenance and bootstrap HTTP handling
IPAddress ethernetLocalIP();       // Return current local IP (0.0.0.0 if unavailable)
void ethernetFlushBootLogToSD();   // Flush [ETH_BOOT] buffer to SD daily log (call after ethernetBegin)
void ethernetBootLogLine(const char* msg); // Push a raw line to the [ETH_BOOT] buffer (for callers outside ethernet.cpp)

// Shared state: set true while init/reinit task is running so ethernetLoop() skips SPI reads.
extern volatile bool _ethReinitTaskRunning;

#else
// Stubs when disabled
static inline void ethernetBegin() {}
static inline int ethernetStatus() { return -1; }
static inline void ethernetLoop() {}
static inline IPAddress ethernetLocalIP(){ return IPAddress(0,0,0,0); }
static inline void ethernetFlushBootLogToSD() {}
static inline void ethernetBootLogLine(const char*) {}
#endif
