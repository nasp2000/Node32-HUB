#pragma once

// ── Node32-USB-Server (P4_USB_SERVER) ────────────────────────────────────────
// Board lock:   ESP32-P4 only (forces TARGET_BOARD_P4 in user_config.h)
// System name:  Node32-HUB (default)
// AP SSID:      NODE32-HUB (default)
// Branding:     disabled (default Node32-HUB 🤖)
// Access page:  NOT restricted
//
// Features ON:  USB_HOST (OTG20 HS UTMI), USBIP_SERVER (TCP port 3240)
// Features OFF: GRBL, ASYNC_WEBSOCKET, DEBUG_LOGS, TELNET
// Home page:    Standard HUB (no special buttons)
// Custom pages: USB/IP server page (/usb-server)
// Other:        USB sharing over network (usbip protocol)
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Node32-HUB"
#define PACK_BRAND_NAME "Node32-HUB"
#define PACK_EMAIL_SUBJECT "Node32-Hub"
#define PACK_EMOJI "🤖"

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
#error "PACK_P4_USB_SERVER is fixed to TARGET_BOARD_P4. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_S3_CAM."
#endif

#undef ENABLE_USB_HOST
#define ENABLE_USB_HOST 1

#undef ENABLE_USBIP_SERVER
#define ENABLE_USBIP_SERVER 1

// USB/IP server pack focuses on network USB sharing and keeps GRBL disabled by default.
#undef ENABLE_GRBL
#define ENABLE_GRBL 0

#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0

#undef ENABLE_TELNET
#define ENABLE_TELNET 0

// Type-A host path on ESP32-P4 (OTG20 HS UTMI -> CH334F hub -> Type-A ports).
// NOT on OTG11 FS FSLS (GPIO26/27 are regular GPIOs on P4, not USB pins).
#undef USB_HOST_NATIVE_PERIPHERAL_MAP
#define USB_HOST_NATIVE_PERIPHERAL_MAP 1U

#undef USB_HOST_P4_OTG11_FS_HOST
#define USB_HOST_P4_OTG11_FS_HOST 0

#undef USB_HOST_P4_PHY_ROUTE_DEFAULT
#define USB_HOST_P4_PHY_ROUTE_DEFAULT 1U

// LCD/OLED — not used in USB server pack
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0

// Page flags
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 1
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1  // T12: Configure storage page always present in all packs
#undef ENABLE_DUALFILE
#define ENABLE_DUALFILE 1  // T20: /dualfile page no USB Server (reaproveita T14 do P4 CNC)
