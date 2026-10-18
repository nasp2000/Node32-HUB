#pragma once

#include "feature_flags.h"
#include <stdint.h>

#ifndef ENABLE_USBIP_SERVER
#define ENABLE_USBIP_SERVER 0
#endif

#if ENABLE_USBIP_SERVER == 1

struct UsbIpServerStats {
    bool running;
    uint16_t listen_port;
    uint32_t accepted_clients;
    uint32_t active_clients;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_bytes_per_sec;
    uint32_t tx_bytes_per_sec;
    bool devices_dirty;
};

struct UsbIpRuntimeConfig {
    bool enabled;
    uint16_t listen_port;
    uint16_t recv_timeout_ms;
    uint16_t in_wait_timeout_ms;
};

// Start the lightweight USB/IP TCP server task (port 3240).
void usbipServerBegin();

// Periodic maintenance hook (currently no-op, kept for module parity).
void usbipServerUpdate();

// Stop server task and close listening/client sockets.
void usbipServerStop();

// Return true while the server task is running.
bool usbipServerIsRunning();

// Return a snapshot of runtime counters.
UsbIpServerStats usbipServerGetStats();

// Runtime configuration getters/setters.
UsbIpRuntimeConfig usbipServerGetRuntimeConfig();
void usbipServerSetRuntimeConfig(const UsbIpRuntimeConfig& cfg);

#else

struct UsbIpServerStats {
    bool running;
    uint16_t listen_port;
    uint32_t accepted_clients;
    uint32_t active_clients;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_bytes_per_sec;
    uint32_t tx_bytes_per_sec;
    bool devices_dirty;
};

struct UsbIpRuntimeConfig {
    bool enabled;
    uint16_t listen_port;
    uint16_t recv_timeout_ms;
    uint16_t in_wait_timeout_ms;
};

static inline void usbipServerBegin() {}
static inline void usbipServerUpdate() {}
static inline void usbipServerStop() {}
static inline bool usbipServerIsRunning() { return false; }
static inline UsbIpServerStats usbipServerGetStats() { return {}; }
static inline UsbIpRuntimeConfig usbipServerGetRuntimeConfig() { return {false, 3240, 1200, 220}; }
static inline void usbipServerSetRuntimeConfig(const UsbIpRuntimeConfig&) {}

#endif
