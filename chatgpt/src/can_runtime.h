// can_runtime.h
// CAN bus runtime state: PSRAM ring buffer, FreeRTOS RX task,
// LittleFS config persistence, aggregate statistics for the web UI.

#pragma once
#include "feature_flags.h"
#include <stdint.h>

#ifndef ENABLE_CAN
  #define ENABLE_CAN  0
#endif

#if ENABLE_CAN==1

#include <Arduino.h>
#include "can_core.h"

// Maximum frames kept in the PSRAM ring buffer (oldest dropped when full)
#define CAN_FRAME_BUFFER_SIZE  1024

// Config loaded from / saved to LittleFS /config/can.json
struct CanConfig {
    bool     enabled;        // Activates the driver on boot
    uint16_t bitrate_kbps;   // 25/50/100/125/250/500/800/1000
    CanMode  mode;           // Normal / Listen-only / Self-test
    int      tx_pin;
    int      rx_pin;
    uint32_t filter_id;      // Hardware acceptance filter ID
    uint32_t filter_mask;    // 0xFFFFFFFF = accept all
    bool     filter_ext;     // Apply filter to extended (29-bit) frames
    bool     log_to_sd;      // Write every received frame to SD/PSRAM data pipeline
};

// Runtime totals (in-RAM, not persisted)
struct CanRuntimeStats {
    uint32_t total_rx;
    uint32_t total_tx;
    uint32_t total_tx_errors;
    uint32_t bus_off_events;
    uint32_t buffer_drops;  // frames lost because ring buffer was full
};

// --- Public API ---

// Load config + allocate PSRAM buffer + start driver + start RX task.
// Call once from setup().
void canRuntimeInit();

// Graceful shutdown: stop driver and delete RX task.
void canRuntimeStop();

// Send a frame. Convenience wrapper that also increments tx stats.
bool canRuntimeSend(const CanFrame& frame);

// Copy up to 'max_frames' most-recent frames from PSRAM ring buffer into 'out'.
// Returns actual number of frames copied (0 if buffer empty).
// Thread-safe: acquires internal mutex.
size_t canRuntimeGetFrames(CanFrame* out, size_t max_frames);

// Return the current number of frames in the ring buffer.
size_t canRuntimeFrameCount();

// Clear the PSRAM ring buffer.
void canRuntimeClearFrames();

// Return runtime stats accumulator.
CanRuntimeStats canRuntimeGetStats();

// Return current live bus stats from the TWAI driver.
CanBusStats canRuntimeGetBusStats();

// Return a const-ref to the active config.
const CanConfig& canRuntimeGetConfig();

// Load config from LittleFS, reinitialise driver with new settings.
// If 'save' is true, write the config to /config/can.json first.
bool canRuntimeApplyConfig(const CanConfig& cfg, bool save);

// Return true if driver is installed and running.
bool canRuntimeIsRunning();

#endif // ENABLE_CAN==1
