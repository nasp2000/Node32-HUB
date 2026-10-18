// usb_host_manager.h
// USB Host stack manager for ESP32-P4 (USB 2.0 OTG HS).
// Supports CDC-ACM class devices (USB-RS232 adapters: CP210x, CH343, FTDI VCP).
// Data received from CDC-ACM devices is injected into the RS232 RX pipeline
// via injectDataToRXBuffer(), making them transparent to the rest of the firmware.
//
// IMPORTANT — USB CDC vs USB Host conflict:
//   The ESP32-P4 has a single USB OTG HS port (the USB-C connector on Waveshare DevKit).
//   When ENABLE_USB_HOST=1, the OTG port is configured as a USB Host and the USB CDC
//   serial port (Serial, used with ARDUINO_USB_CDC_ON_BOOT=1) becomes unavailable.
//   Use UART0 (GPIO43/44 at 115200 baud) or Telnet for serial debug when USB Host is active.
//
// Depends on: feature_flags.h, commands.h (for injectDataToRXBuffer)
// Used by: setup_impl.h, chatgpt.ino

#pragma once
#include "feature_flags.h"
#include <stdint.h>   // uint8_t, uint32_t — needed by stubs even when module is disabled
#include <stddef.h>   // size_t

#ifndef ENABLE_USB_HOST
  #define ENABLE_USB_HOST 0
#endif

#if ENABLE_USB_HOST == 1
  #if defined(__has_include)
    #if __has_include(<usb/usb_host.h>)
      #define USB_HOST_NATIVE_AVAILABLE 1
    #else
      #define USB_HOST_NATIVE_AVAILABLE 0
    #endif

    #if __has_include(<tusb.h>) && __has_include(<class/cdc/cdc_host.h>)
      #define USB_HOST_TINYUSB_AVAILABLE 1
    #elif __has_include(<arduino_tinyusb/tinyusb/src/tusb.h>) && __has_include(<arduino_tinyusb/tinyusb/src/class/cdc/cdc_host.h>)
      #define USB_HOST_TINYUSB_AVAILABLE 1
    #else
      #define USB_HOST_TINYUSB_AVAILABLE 0
    #endif
  #else
    #define USB_HOST_NATIVE_AVAILABLE 0
    #define USB_HOST_TINYUSB_AVAILABLE 0
  #endif
#else
  #define USB_HOST_NATIVE_AVAILABLE 0
  #define USB_HOST_TINYUSB_AVAILABLE 0
#endif

// UsbCdcDevice stub for when USB Host is disabled
#if ENABLE_USB_HOST == 0
struct UsbCdcDevice {
    bool     connected;
    uint8_t  slot;
    uint8_t  dev_addr;
    char     description[48];
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t baud;
    uint8_t  speed;
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd_device;
    uint8_t  iface_class;
    uint8_t  iface_subclass;
    uint8_t  iface_protocol;
    uint8_t  in_ep_addr;
    uint8_t  out_ep_addr;
};
#endif

#if ENABLE_USB_HOST == 1 && (USB_HOST_NATIVE_AVAILABLE == 1 || USB_HOST_TINYUSB_AVAILABLE == 1)

#include <Arduino.h>

// Maximum USB-RS232 devices tracked simultaneously.
// Can be overridden in user_config.h (example: #define USB_HOST_MAX_CDC_DEVICES 8).
#ifndef USB_HOST_MAX_CDC_DEVICES
  #define USB_HOST_MAX_CDC_DEVICES 4
#endif

#if USB_HOST_MAX_CDC_DEVICES < 1
  #undef USB_HOST_MAX_CDC_DEVICES
  #define USB_HOST_MAX_CDC_DEVICES 1
#endif

// Default CDC line coding applied when a device connects.
// The user can change baud/format at runtime via the web page.
#define USB_HOST_DEFAULT_BAUD     115200
#define USB_HOST_DEFAULT_DATA_BITS 8
#define USB_HOST_DEFAULT_STOP_BITS 1   // 1 stop bit
#define USB_HOST_DEFAULT_PARITY    0   // none

// Per-device runtime state (read-only snapshot via usbHostGetDevice()).
struct UsbCdcDevice {
    bool     connected;          // True while device is enumerated.
    uint8_t  slot;               // Slot index (for UART/GRBL mode).
    uint8_t  dev_addr;           // USB device address (1-127).
    char     description[48];    // Human-readable: "CP210x @ addr 2" etc.
    uint32_t rx_bytes;           // Total bytes received from this device.
    uint32_t tx_bytes;           // Total bytes sent to this device.
    uint32_t baud;               // Current line coding baud rate.
    uint8_t  speed;              // USB/IP speed code (1=low,2=full,3=high,5=super).
    uint16_t vid;                // Vendor ID (0 when unavailable).
    uint16_t pid;                // Product ID (0 when unavailable).
    uint16_t bcd_device;         // Device release number (0 when unavailable).
    uint8_t  iface_class;        // Active interface class.
    uint8_t  iface_subclass;     // Active interface subclass.
    uint8_t  iface_protocol;     // Active interface protocol.
    uint8_t  in_ep_addr;         // Active bulk IN endpoint address.
    uint8_t  out_ep_addr;        // Active bulk OUT endpoint address.
};

// Aggregate stats for the web status page.
struct UsbHostStats {
    uint8_t  devices_connected;  // Current count of connected CDC devices.
    uint32_t total_rx_bytes;     // Cumulative bytes received across all devices.
    uint32_t total_tx_bytes;     // Cumulative bytes sent across all devices.
    uint32_t connect_events;     // Total connect events since boot.
    uint32_t disconnect_events;  // Total disconnect events since boot.
};

// Low-level host diagnostics for debugging enumeration issues on ESP32-P4.
struct UsbHostDiag {
  uint32_t peripheral_map;         // host_cfg.peripheral_map currently in use.
  uint8_t  phy_route_idx;          // Current USB_WRAP PHY route index (P4 specific).
  bool     phy_fallback_attempted; // True once fallback route switch was attempted.
  uint32_t phy_route_switches;     // Number of runtime PHY route switches applied.
  bool     pin_exchg_enabled;      // True if D+/D- pin exchange override is enabled.
  bool     pin_exchg_fallback_attempted; // True once pin-exchange fallback was attempted.
  bool     auto_restart_attempted;    // True once auto-restart fallback was triggered.
  uint32_t new_dev_events;         // Count of USB_HOST_CLIENT_EVENT_NEW_DEV events.
  uint32_t dev_gone_events;        // Count of USB_HOST_CLIENT_EVENT_DEV_GONE events.
  uint32_t scan_cycles;            // Polling scans executed.
  uint32_t scan_no_device_cycles;  // Polling scans that found zero devices.
  uint32_t scan_addr_failures;     // usb_host_device_addr_list_fill() failures.
  uint8_t  last_addr_count;        // Last address count returned by addr_list_fill.
  int32_t  lib_num_devices;        // usb_host_lib_info().num_devices snapshot.
  int32_t  lib_num_clients;        // usb_host_lib_info().num_clients snapshot.
  uint32_t hprt0_raw;              // Raw HPRT0 DWC2 HS register value: bit12=PPWR(port power), bit0=PCSTS(connected), bit2=PENA(enabled).
  uint32_t hprt0_fs_raw;           // Raw HPRT0 DWC2 FS register (OTG11). If pcsts=1 here, Type-A is on FS pads (GPIO26/27) not UTMI.
  uint32_t ghwcfg2_raw;            // Raw GHWCFG2 (OTG20 HS). bits7:6=hsphytype (1=UTMI+). Confirms HS PHY compiled in.
  int32_t  root_pwr_on_err;        // esp_err_t from usb_host_lib_set_root_port_power(true) at startup.
  int8_t   gpio27_level;           // GPIO27 level sampled at startup (OTG11 FS D+ pad). 1=HIGH means D+ from device routes to FS, not UTMI.
  int8_t   gpio26_level;           // GPIO26 level sampled at startup (OTG11 FS D- pad).
};

// Optional RX mirror hook: called for each USB CDC RX chunk after counters are updated.
// This does not replace GRBL/monitor routing; it only mirrors bytes to an external consumer.
typedef void (*UsbHostRxHook)(uint8_t slot, const uint8_t* data, size_t len, void* ctx);

// --- Public API ---

// Initialise the IDF USB Host library, register the CDC-ACM class driver,
// and start the host daemon task and client task.
// Must be called once from setup() after Arduino init is complete.
// No-op (safe to call) if USB OTG hardware is not present on this board.
void usbHostBegin();

// Graceful shutdown: deregister CDC-ACM client, uninstall host library.
// Called from factory reset or on explicit DISABLE command.
void usbHostStop();

// Start the host stack in a background task and return immediately.
// Returns 1 if already running, 0 if a begin was launched, -1 if a begin is
// already in progress (or the task could not be created). Use with
// usbHostBeginInProgress()/usbHostIsRunning() to follow up. Required because
// usbHostBegin() may block for seconds (PHY/root-port sequencing) and must
// never run on an HTTP handler task.
int usbHostBeginAsync();

// True while a background usbHostBegin() has not finished yet.
bool usbHostBeginInProgress();

// Live async counters (RAM, reset on boot): launches vs task-body entries.
uint32_t usbHostBeginCalls();
uint32_t usbHostBeginRuns();

// Mark a pending background begin as cancelled (OFF pressed mid-start).
void usbHostCancelPendingBegin();

// Clear a stale cancel (fresh ON press after an OFF).
void usbHostClearBeginCancelled();

// Bring-up stage marker for crash forensics (RTC memory, survives WDT
// resets, no NVS/flash). Returns "" when no begin ever ran.
const char* usbHostBeginStage();

// Record a stage (public so setup() can drop the "boot" validation marker).
void usbHostBeginStageMark(const char* stage);

// Setup-time marker: records "boot" only when RTC holds no known stage,
// preserving a crash point (or "off") across reboots for post-mortem reads.
void usbHostBeginStageBootMark();

// Return true if the host stack is running.
bool usbHostIsRunning();

// Return a snapshot of one device slot (0-based, up to USB_HOST_MAX_CDC_DEVICES-1).
// Returns a zeroed struct with connected=false if slot is empty.
UsbCdcDevice usbHostGetDevice(uint8_t slot);

// Return aggregate stats.
UsbHostStats usbHostGetStats();

// Return low-level diagnostics snapshot.
UsbHostDiag usbHostGetDiag();

// Send bytes to a specific CDC device (slot index).
// Returns number of bytes actually written (0 on error or device not connected).
size_t usbHostSend(uint8_t slot, const uint8_t* data, size_t len);

// Convenience: send a null-terminated string to a specific device slot.
size_t usbHostSendStr(uint8_t slot, const char* str);

// Apply new line coding (baud/data/stop/parity) to a connected CDC device.
// Values that are 0 keep the current setting.
bool usbHostSetLineCoding(uint8_t slot, uint32_t baud, uint8_t data_bits,
                          uint8_t stop_bits, uint8_t parity);

// Set DTR (bit 0) and/or RTS (bit 1) control line state.
// For CH340 devices, this uses vendor MODEM_CTRL (0xA4).
bool usbHostSetDtrRts(uint8_t slot, bool dtr, bool rts);

// Diagnostic helpers for web/API status.
const char* usbHostBackendName();
const char* usbHostLastError();

// Request a graceful stop + restart of the USB host stack (e.g. after config change).
// Safe to call from any context; the restart is handled asynchronously by the client task.
void usbHostRequestRestart();

// Register or clear a RX mirror hook.
// Pass nullptr to disable the hook.
void usbHostSetRxHook(UsbHostRxHook hook, void* ctx);

// Map USB VID/PID to a friendly chip name (e.g. "CH340", "CP2102/04", "FT232R").
// Returns a pointer to a static string; safe to call even when USB host is disabled.
const char* usbChipName(uint16_t vid, uint16_t pid);

#else // ENABLE_USB_HOST == 0 or no USB Host backend is available — compile-time stubs

struct UsbHostStats { uint8_t devices_connected; uint32_t total_rx_bytes;
                      uint32_t total_tx_bytes; uint32_t connect_events;
                      uint32_t disconnect_events; };
struct UsbHostDiag { uint32_t _dummy; };
typedef void (*UsbHostRxHook)(uint8_t slot, const uint8_t* data, size_t len, void* ctx);

#ifndef USB_HOST_MAX_CDC_DEVICES
  #define USB_HOST_MAX_CDC_DEVICES 4
#endif

static inline void        usbHostBegin()                           {}
static inline void        usbHostRequestRestart()                  {}
static inline void        usbHostStop()                            {}
static inline void        usbHostSetRxHook(UsbHostRxHook, void*)   {}
static inline bool        usbHostIsRunning()                       { return false; }
static inline UsbCdcDevice usbHostGetDevice(uint8_t)               { return {}; }
static inline UsbHostStats usbHostGetStats()                       { return {}; }
static inline UsbHostDiag  usbHostGetDiag()                        { return {}; }
static inline size_t      usbHostSend(uint8_t, const uint8_t*, size_t) { return 0; }
static inline size_t      usbHostSendStr(uint8_t, const char*)    { return 0; }
static inline bool        usbHostSetLineCoding(uint8_t, uint32_t, uint8_t,
                                               uint8_t, uint8_t)  { return false; }
static inline bool        usbHostSetDtrRts(uint8_t, bool, bool)  { return false; }
static inline const char* usbHostBackendName()                     { return "stub"; }
static inline const char* usbHostLastError()                       { return "backend_unavailable"; }
static inline const char* usbHostBeginStage()                      { return ""; }
static inline void        usbHostBeginStageMark(const char*)          {}
static inline int         usbHostBeginAsync()                      { return -1; }
static inline bool        usbHostBeginInProgress()                 { return false; }
static inline uint32_t    usbHostBeginCalls()                      { return 0; }
static inline uint32_t    usbHostBeginRuns()                       { return 0; }
static inline void        usbHostCancelPendingBegin()              {}
static inline void        usbHostClearBeginCancelled()             {}
static inline const char* usbChipName(uint16_t, uint16_t)          { return "USB-UART"; }

#endif // ENABLE_USB_HOST
