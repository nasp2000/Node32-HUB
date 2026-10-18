// grbl_sender_transport.cpp
// USB Host/OTG transport layer for GRBL communication (lazy start).
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
// #include <HardwareSerial.h>  // UART disabled — USB OTG only

// ── S3 CNC: USB-only, lazy host ─────────────────────────────────────────────
// The USB Host stack is compiled in (ENABLE_USB_HOST==1) but NOT started at
// boot — grbl_sender_api::grblSenderInit() only records the default transport
// mode (USB). When the user selects "USB Host (OTG)" in the web transport
// selector, grblSetTransportMode() launches usbHostBeginAsync() to bring up
// the host stack on-demand in a background task (never blocks the caller).
// On reboot, the host is NOT restarted — the user must
// re-select USB each session (never persisted).
//
// UART (Serial2 GPIO16/17) is disabled for this pack — the code is preserved
// below in #if 0 blocks so it can be re-enabled later if the ~30 s reboot
// loop (host tasks + Serial2) is resolved.
//
// P4 CNC: always USB Host only (no UART wired — GPIO14-19 are C6 SDIO bus).
// ─────────────────────────────────────────────────────────────────────────────

HardwareSerial* s_grblUart = nullptr;  // kept for future UART re-enable (matches extern in grbl_sender_internal.h)

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
GrblTransportMode s_grblTransport = GrblTransportMode::USB;  // default: host not started
#else
GrblTransportMode s_grblTransport = GrblTransportMode::USB;
#endif

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// ── UART helpers (disabled — kept for future re-enable) ──────────────────────
#if 0  // UART disabled for S3 CNC (USB OTG only)
static void ensureUartTransport() {
    if (s_grblUart) return;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial2.begin(115200, SERIAL_8N1, 16, 17);
    s_grblUart = &Serial2;
    directLogPrintf("[GRBL] UART transport on Serial2 (GPIO16=RX GPIO17=TX) 115200 baud\n");
#else
    directLogPrintf("[GRBL] UART transport not wired on this pack — USB transport only\n");
#endif
}
#endif  // UART disabled

// ── USB Host lazy start/stop (S3 CNC) ───────────────────────────────────────

// extern declarations — implemented in usb_host_manager.cpp.
// usbHostBeginAsync() runs the blocking PHY sequencing in a background task
// so HTTP handlers never wedge (see usb_host_manager.h).
extern void usbHostStop();
extern int usbHostBeginAsync();
extern void usbHostCancelPendingBegin();
extern void usbHostClearBeginCancelled();
extern void usbHostBeginStageMark(const char* stage);

// Start the USB Host stack on-demand. Called when the user selects "USB Host
// (OTG)" in the web transport selector. The host enumerates whatever is
// plugged into the OTG port (GPIO19/20). No PHY init at boot = no reboot loop.
// Async: returns true when the host is up OR a begin is already underway;
// the web UI follows up via usb_running status polls.
static bool startHostIfNeeded() {
    if (usbHostIsRunning()) return true;
    directLogPrintf("[GRBL] lazy USB Host start (user-selected)\n");
    const int rc = usbHostBeginAsync();
    if (rc == 1) return true;   // raced into running state
    if (rc == 0) return true;   // begin launched, follow up via status
    directLogPrintf("[GRBL] USB Host begin rejected (in progress)\n");
    return false;
}

// Stop the USB Host stack. Called when the user switches back to UART (future)
// or when the transport is no longer needed.
static void stopHostIfNeeded() {
    if (!usbHostIsRunning()) return;
    directLogPrintf("[GRBL] stopping USB Host\n");
    usbHostStop();
}

// ── Transport API ────────────────────────────────────────────────────────────

// Write len bytes to the active GRBL transport.
size_t grblTransportWrite(uint8_t slot, const uint8_t* data, size_t len) {
    if (s_grblTransport == GrblTransportMode::UART) {
        // UART disabled — this path is unreachable while UART is commented out.
        // To re-enable: uncomment the #if 0 UART block above and restore
        // ensureUartTransport() + s_grblUart->write() here.
        (void)slot; (void)data; (void)len;
        return 0;
    }
    return usbHostSend(slot, data, len);
}

// Write a null-terminated string to the active GRBL transport.
size_t grblTransportWriteStr(uint8_t slot, const char* str) {
    if (!str) return 0;
    return grblTransportWrite(slot, (const uint8_t*)str, strlen(str));
}

// Change baud rate on the active transport.
bool grblTransportSetBaud(uint8_t slot, uint32_t baud) {
    if (s_grblTransport == GrblTransportMode::UART) {
        (void)slot; (void)baud;
        return false;  // UART disabled
    }
    return usbHostSetLineCoding(slot, baud, 8, 1, 0);
}

// Returns a "connected" UsbCdcDevice when transport is active.
UsbCdcDevice grblTransportGetDevice(uint8_t slot) {
    if (s_grblTransport == GrblTransportMode::UART) {
        (void)slot;
        return {};  // UART disabled
    }
    return usbHostGetDevice(slot);
}

// Returns true if the GRBL transport is active and ready.
bool grblTransportIsRunning() {
    if (s_grblTransport == GrblTransportMode::UART) {
        return false;  // UART disabled
    }
    return usbHostIsRunning();
}

// USB Host RX is handled by TinyUSB/native callbacks — nothing to poll here.
void grblTransportPollRx() {
    // no-op: USB Host RX callbacks handle incoming data
}

// ── Transport mode switch (S3 CNC: lazy USB Host start/stop) ─────────────────

GrblTransportMode grblGetTransportMode() {
    return s_grblTransport;
}

// Session-only transport switch for the S3 CNC pack.
// • "usb"  → launches usbHostBeginAsync() on-demand (lazy, non-blocking);
//            the UI follows up via usb_running status polls.
// • "uart" → stops the host (UART wire itself stays disabled); transport
//            reports UART with nothing running until USB is selected again.
// Never persisted — every reboot starts with host NOT running.
// Rejected while a stream/queue job is active.
// The P4 pack always returns false (USB Host only, no selection needed).
bool grblSetTransportMode(GrblTransportMode mode) {

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    if (grblStreamIsActive() || grblQueueIsActive()) return false;
    if (s_resumeBusy) return false;  // alarm-resume flow owns the wire now
    if (mode == GrblTransportMode::USB) {
        // Lazy start: bring up the host stack in a background task (was
        // idle since boot). startHostIfNeeded() never blocks the caller.
        usbHostClearBeginCancelled();
        if (!startHostIfNeeded()) return false;
        s_grblTransport = GrblTransportMode::USB;
        directLogPrintf("[GRBL] transport switched to USB Host/OTG (session-only, not persisted)\n");
        if (grblWebPublishConsoleLine) {
            grblWebPublishConsoleLine(F("transport: USB Host (OTG) starting — plug the OTG adapter to connect GRBL"));
        }
        return true;
    }

    if (mode == GrblTransportMode::UART) {
        // OFF switch: cancel a mid-flight begin, then stop the host
        // (bounded, safe on the caller).
        // The UART wire stays disabled — transport reports UART with the
        // host down until the user selects USB again.
        // Marks "off" so RTC retention can be validated without a begin.
        usbHostBeginStageMark("off");
        usbHostCancelPendingBegin();
        stopHostIfNeeded();
        s_grblTransport = GrblTransportMode::UART;
        directLogPrintf("[GRBL] transport switched off (USB Host stopped)\n");
        if (grblWebPublishConsoleLine) {
            grblWebPublishConsoleLine(F("transport: USB Host (OTG) stopped"));
        }
        return true;
    }
#else
    (void)mode;
#endif
    return false;  // P4 pack: USB Host only, no switching needed
}

// ── UART transport helper (disabled — for future re-enable) ──────────────────
#if 0  // UART disabled for S3 CNC (USB OTG only)
void grblSetUartTransport(HardwareSerial& port, int rxPin, int txPin, int baud) {
    if (s_grblUart) {
        directLogPrintf("[GRBL] UART transport already active, ignoring grblSetUartTransport\n");
        return;
    }
    port.begin((unsigned long)baud, SERIAL_8N1, rxPin, txPin);
    s_grblUart = &port;
    directLogPrintf("[GRBL] UART transport on port (RX=%d TX=%d) %d baud\n", rxPin, txPin, baud);
}
#endif  // UART disabled

#endif // ENABLE_GRBL == 1
