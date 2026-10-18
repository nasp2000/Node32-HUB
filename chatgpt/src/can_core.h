// can_core.h
// TWAI (CAN 2.0B) hardware driver wrapper.
// Manages driver install/start/stop, frame send, and bus status queries.
// All real code is guarded by ENABLE_CAN==1.
// Requires an external SN65HVD230 / MCP2551 (or compatible) transceiver.

#pragma once
#include "feature_flags.h"
#include <stdint.h>
#include <stddef.h>

// Default CAN GPIO — can be overridden in board_pins.h or user_config.h
#ifndef CAN_TX_PIN
  #define CAN_TX_PIN  20
#endif
#ifndef CAN_RX_PIN
  #define CAN_RX_PIN  19
#endif
#ifndef ENABLE_CAN
  #define ENABLE_CAN  0
#endif

#if ENABLE_CAN==1

#include <Arduino.h>
#include "driver/twai.h"

// Operating mode for the TWAI controller
enum CanMode : uint8_t {
    CAN_MODE_NORMAL   = 0, // Normal bidirectional TX+RX
    CAN_MODE_LISTEN   = 1, // Passive sniffer: no TX, no ACK sent
    CAN_MODE_SELFTEST = 2, // Internal loopback: no external bus needed
};

// Driver state returned by canCoreGetState()
enum CanBusState : uint8_t {
    CAN_BUS_STOPPED   = 0,
    CAN_BUS_RUNNING   = 1,
    CAN_BUS_BUS_OFF   = 2,
    CAN_BUS_RECOVERING = 3,
    CAN_BUS_ERROR     = 4,
};

// A single CAN frame (TX or RX)
struct CanFrame {
    uint32_t id;           // CAN identifier (11-bit standard or 29-bit extended)
    uint32_t timestamp_ms; // millis() when received / enqueued for TX
    uint8_t  dlc;          // Data Length Code (0-8)
    uint8_t  data[8];      // Payload bytes
    bool     ext;          // true = 29-bit extended ID
    bool     rtr;          // true = Remote Transmission Request
};

// Real-time bus statistics
struct CanBusStats {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t bus_off_count;
    uint32_t arb_lost_count;
    uint32_t msgs_to_rx;   // frames waiting in TWAI RX queue right now
    uint8_t  tec;          // Transmit Error Counter (0-255)
    uint8_t  rec;          // Receive Error Counter (0-255)
    CanBusState state;
};

// Install and start the TWAI driver with the given parameters.
// Returns true on success.
// tx_pin / rx_pin: GPIO numbers for the transceiver TX/RX lines.
// bitrate_kbps: 25, 50, 100, 125, 250, 500, 800, 1000
// mode: CAN_MODE_NORMAL / CAN_MODE_LISTEN / CAN_MODE_SELFTEST
// filter_id / filter_mask: hardware ID filter (filter_mask=0xFFFFFFFF accepts all)
// filter_ext: true = apply filter to extended (29-bit) IDs
bool canCoreInit(int tx_pin, int rx_pin, uint16_t bitrate_kbps, CanMode mode,
                 uint32_t filter_id, uint32_t filter_mask, bool filter_ext);

// Stop and uninstall the TWAI driver (safe to call even if not running)
void canCoreStop();

// Send a single CAN frame. Returns true on success.
// Blocks at most tx_timeout_ms milliseconds waiting for TX queue space.
bool canCoreSend(const CanFrame& frame, uint32_t tx_timeout_ms = 10);

// Receive a single frame from the TWAI RX hardware queue (non-blocking).
// Returns true and fills 'out' if a frame was waiting, false otherwise.
bool canCoreReceive(CanFrame& out);

// Fill 'stats' with current counters and TEC/REC from the driver.
void canCoreGetStats(CanBusStats& stats);

// Return current bus state (STOPPED / RUNNING / BUS_OFF / etc.)
CanBusState canCoreGetState();

// Attempt bus-off recovery. Call when state is CAN_BUS_BUS_OFF.
void canCoreInitiateRecovery();

// True if the driver is currently installed and started.
bool canCoreIsRunning();

#endif // ENABLE_CAN==1
