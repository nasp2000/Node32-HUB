#pragma once

// ── GcodeSender-P4 ────────────────────────────────────────────────────────────
// Board lock:   ESP32-P4 only (forces TARGET_BOARD_P4 in user_config.h)
// System name:  GcodeSender-P4
// AP SSID:      NODE32-HUB (default for all packs)
// Email subj:   GcodeSender-P4
// Branding:     enabled, emoji 🛠️, name "GcodeSender-P4"
//
// Features ON:  USB_HOST (OTG20 HS UTMI), GRBL, SDCARD, DEBUG_LOGS, ASYNC_WEBSOCKET
// Features OFF: TELNET, PAGE_COMMANDS, PAGE_RECIPIENTS,
//               PAGE_SDCARD, SMTP, GOOGLE_EMAIL
// Home page:    GcodeSender button (/grbl-controller), recipients section HIDDEN
// Custom pages: GRBL Controller (/grbl-controller), ~50 GRBL Web API handlers
// Other:        USB Host via CH334F hub → Type-A, GRBL over USB-CDC
//
// STORAGE (SD + PSRAM fallback):
//   - SD present (P4 has SDMMC4 slot): upload files up to 30MB+ via
//     /api/grbl/upload-gcode-stream. G-code is pre-loaded into PSRAM up to
//     GRBL_STREAM_PSRAM_MAX_BYTES (12MB) for fast streaming; remainder streams
//     incrementally from SD.
//   - SD absent (no card): upload via /api/grbl/psram-upload (PSRAM only).
//     Max 12MB per file, stored within the 16MB PSRAM VFS budget
//     (shared with logs/email). VFS is volatile: data is lost on reboot.
//   - Upload limit (SD): 50MB per file. Upload limit (PSRAM fallback): 12MB.
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "GcodeSender-P4"
#define PACK_BRAND_NAME "GcodeSender-P4"
#define PACK_EMAIL_SUBJECT "GcodeSender-P4"
#define PACK_EMOJI "🛠️"
#define PSRAM_VFS_BUFFER_SIZE (16 * 1024 * 1024)  // 16MB (half of P4 flash)
#define GRBL_STREAM_PSRAM_MAX_BYTES (12 * 1024 * 1024)  // 12MB max G-code in PSRAM

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
#error "PACK_P4_CNC is fixed to TARGET_BOARD_P4. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_S3_CAM."
#endif

#undef ENABLE_USB_HOST
#define ENABLE_USB_HOST 1

// Waveshare ESP32-P4-Module-DEV-KIT hardware verified (2026-05-31):
// hprt0_raw PLSTS=J-state (D+ HIGH) on OTG20 HS when Nano/CH340G is connected to Type-A.
// OTG11 FS (GPIO26/27) shows SE0 with Nano connected -> nothing on those pads.
// Conclusion: Type-A D+/D- are wired to OTG20 HS UTMI pads (USB20_DP / USB20_DM).
// IDF usb_host_install with skip_phy_setup=false on P4 targets OTG20 HS UTMI -> correct.
// gpio27_level=1 seen in earlier sessions was TinyUSB CDC device-mode D+ pullup, NOT Nano.
#undef USB_HOST_NATIVE_PERIPHERAL_MAP
#define USB_HOST_NATIVE_PERIPHERAL_MAP 1U  // BIT0 = USB-OTG20 HS UTMI = Type-A port 0 (only usable port)

// EXPERIMENTAL (test EN-20260905-FSDUAL): bring up the second USB controller OTG11 FS
// (Type-A ports 1-3). HW test 2026-09-05 (builds 1831/1848/1856): CH340 GRBL on ports 1/3
// shows NO attach on OTG20 HS (HPRT0 PCSTS=0) AND NO attach on OTG11 FS (FS-PCSTS=0 after
// reconnect power cycle) -> ports 1-3 are NOT wired to either accessible controller.
// CONCLUDED: only port 0 (OTG20 HS) is usable. Keep 0 (single-HS production).
#undef USB_HOST_P4_OTG11_FS_HOST
#define USB_HOST_P4_OTG11_FS_HOST 0

// Test knob (EN-20260905-FSDUAL): 3 = HS+FS dual, 2 = FS-only (test), 1 = HS-only.
// Inactive while USB_HOST_P4_OTG11_FS_HOST=0. Keep 1 (HS-only) for production.
#ifndef USB_HOST_P4_DUAL_TEST_MAP
#define USB_HOST_P4_DUAL_TEST_MAP 1
#endif

// USB_WRAP FSLS PHY route for OTG11 FS connect detection.
// Controls which internal FSLS PHY (0=USJ, 1=USB_WRAP/OTG11) is mapped.
// Irrelevant for OTG20 HS UTMI which uses dedicated USBD_P/N pins.
// Type-A ports are behind CH334F hub on OTG20 HS UTMI, NOT FSLS.
#undef USB_HOST_P4_PHY_ROUTE_DEFAULT
#define USB_HOST_P4_PHY_ROUTE_DEFAULT 1U  // FSLS PHY 1 mapped to USB_WRAP (irrelevant for UTMI)

#undef ENABLE_GRBL
#define ENABLE_GRBL 1

#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

#undef ENABLE_FTP_CLIENT
#define ENABLE_FTP_CLIENT 0
#undef ENABLE_PAGE_FTP
#define ENABLE_PAGE_FTP 0

#undef GRBL_PSRAM_STRICT
#define GRBL_PSRAM_STRICT 1

#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 1

// Keep debug logs ON for USB host diagnostics (Type-A on CH334 hubs).
#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 1

#undef ENABLE_TELNET
#define ENABLE_TELNET 0

// No scheduled/hourly commands on CNC: kills the scheduler tick, the daily
// reboot slots and their per-minute [DailyReboot] log spam (2026-09-13).
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 0

// Web pages — disable pages not relevant to CNC use
#undef ENABLE_PAGE_COMMANDS
#define ENABLE_PAGE_COMMANDS 0
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 0
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 0
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1  // T12: Configure storage page always present in all packs
#undef ENABLE_DUALFILE
#define ENABLE_DUALFILE 0  // Dual File só no P4 USB Server (ordem Nuno)

// Email — disable email page and SMTP/Google email modules (not relevant for CNC)
#undef ENABLE_SMTP
#define ENABLE_SMTP 0
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

// LCD/OLED — not used in CNC pack
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0
