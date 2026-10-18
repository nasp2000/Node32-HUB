#pragma once

// ── GcodeSender-S3 (UART default + USB Host/OTG selector) ─────────────────────
// Board lock:   ESP32-S3 (any S3 board — N16R8, 8DI8DO, CAM)
// System name:  GcodeSender-S3
// AP SSID:      NODE32-HUB (default for all packs)
// Email subj:   GcodeSender-S3
// Branding:     enabled, emoji 🛠️, name "GcodeSender-S3"
//
// Features ON:  GRBL, SDCARD, ASYNC_WEBSOCKET, USB_HOST (lazy — see below)
// Features OFF: RS232, TELNET,
//               PAGE_COMMANDS, PAGE_RECIPIENTS, PAGE_SDCARD, SMTP, GOOGLE_EMAIL
// Home page:    GcodeSender button (/grbl-controller), recipients section HIDDEN
// Custom pages: GRBL Controller (/grbl-controller), ~50 GRBL Web API handlers
// Other:        USB-OTG-only, host at boot (plan B, 2026-09-14):
//               • Boot: usbHostBegin() runs in setup (all packs). The old
//                 ~30 s reboot loop was host tasks + Serial2 together —
//                 Serial2 is compiled out (#if 0), so host-alone is safe.
//               • Booting WITH the OTG cable plugged: supported (daemon
//                 enumerates async); 10 kΩ GPIO20→GND recommended (power-up
//                 glitch) and avoid VBUS backpower from the machine.
//               • Runtime begin proved unstable on S3 (INT_WDT); the web
//                 ON/OFF toggle is hidden, host stays on. USB OTG metric
//                 shows the live state; hub/slot UI is P4-only, hidden.
//               Serial monitor via CP2102 UART0 (GPIO43/44) or Telnet.
//
// STORAGE (SD + PSRAM fallback):
//   - SD present (any S3 board with slot): upload files up to 30MB+ via
//     /api/grbl/upload-gcode-stream. G-code is pre-loaded into PSRAM up to
//     GRBL_STREAM_PSRAM_MAX_BYTES (4MB) for fast streaming; remainder streams
//     incrementally from SD.
//   - SD absent (N16R8 has no slot, or no card inserted): upload via
//     /api/grbl/psram-upload (PSRAM only). Max 4MB per file, stored within
//     the 5MB PSRAM VFS budget (shared with logs/email). VFS is volatile:
//     data is lost on reboot.
//   - Upload limit (SD): 50MB per file. Upload limit (PSRAM fallback): 4MB.
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "GcodeSender-S3"
#define PACK_BRAND_NAME "GcodeSender-S3"
#define PACK_EMAIL_SUBJECT "GcodeSender-S3"
#define PACK_EMOJI "🛠️"

#undef ENABLE_GRBL
#define ENABLE_GRBL 1

#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

#undef ENABLE_FTP_CLIENT
#define ENABLE_FTP_CLIENT 0
#undef ENABLE_PAGE_FTP
#define ENABLE_PAGE_FTP 0

// Max G-code file size in PSRAM fallback (within the 5MB VFS budget).
// With SD present, larger files are pre-loaded into PSRAM up to this limit
// and the remainder is streamed incrementally from SD.
#ifndef GRBL_STREAM_PSRAM_MAX_BYTES
#define GRBL_STREAM_PSRAM_MAX_BYTES (4 * 1024 * 1024)  // 4MB (S3 with 5MB VFS)
#endif

#undef GRBL_PSRAM_STRICT
#define GRBL_PSRAM_STRICT 1

#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 1

// USB Host (OTG) compiled in but NOT started at boot (lazy start via the
// web USB OTG toggle). UART (Serial2) is disabled (see grbl_sender_transport
// #if 0 blocks) to avoid the ~30 s reboot loop (host tasks + Serial2).
#undef ENABLE_USB_HOST
#define ENABLE_USB_HOST 1

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
#define ENABLE_PAGE_SDCARD 1  // T12: pagina Configurar storage sempre presente em todos os packs

// Email — disable email page and SMTP/Google email modules (not relevant for CNC)
#undef ENABLE_SMTP
#define ENABLE_SMTP 0
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

// LCD/OLED — not used in CNC pack
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0
