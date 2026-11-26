#pragma once

// ── Node32-RS232 ─────────────────────────────────────────────────────────────
// Board lock:   ESP32-S3 N16R8 only
// System name:  Node32-RS232
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 🤖, name "Node32-HUB" (same as default)
// Access page:  NOT restricted
//
// Features ON:  WIFI, WIFI_WEB_SERVER, NTP, OTA, HTTP_BASIC_AUTH, SMTP,
//               FILESYSTEM, LCD_SCREEN, PAGE_MONITOR, PAGE_ACCESS, PAGE_FILES,
//               PAGE_COMMANDS, PAGE_RECIPIENTS, RS232, RS232_CHARTS, SCHEDULER,
//               TDK_LAMBDA (SCPI console + CSV), FTP_CLIENT + PAGE_FTP
// Features OFF: ETHERNET, GOOGLE_EMAIL, SDCARD, DB25, SNIFFER, IPG_LASER,
//               UNIKON, CAMERA, MODBUS*, CAN, TELNET, DEBUG_LOGS,
//               API_REFERENCE_PAGE, ASYNC_WEBSOCKET, UDA1334A, all sensors,
//               STRUCTURES_PAGE (Amada disabled 2026-11-26 by request)
// Home page:    Commands button, RS232 button, Monitor button, TDK-Lambda, FTP
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Node32-RS232"
#define PACK_EMAIL_SUBJECT "Node32-RS232"
#define PACK_EMOJI "🤖"
#define PACK_BRAND_NAME "Node32-HUB"

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1) || \
    (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
#error "PACK_RS232 is fixed to ESP32-S3 N16R8 class builds (env n16r8). Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4/TARGET_BOARD_S3_CAM."
#endif

#undef ENABLE_WIFI
#define ENABLE_WIFI 1
#undef ENABLE_WIFI_WEB_SERVER
#define ENABLE_WIFI_WEB_SERVER 1

#undef ENABLE_ETHERNET
#define ENABLE_ETHERNET 0

#undef ENABLE_NTP
#define ENABLE_NTP 1

#undef ENABLE_OTA
#define ENABLE_OTA 1

#undef ENABLE_HTTP_BASIC_AUTH
#define ENABLE_HTTP_BASIC_AUTH 1
#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

#undef ENABLE_SMTP
#define ENABLE_SMTP 1
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

#undef ENABLE_FILESYSTEM
#define ENABLE_FILESYSTEM 1
#undef ENABLE_SDCARD
#define ENABLE_SDCARD 0

#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 1

#undef ENABLE_PAGE_MONITOR
#define ENABLE_PAGE_MONITOR 1
#undef ENABLE_PAGE_FILES
#define ENABLE_PAGE_FILES 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1  // T12: Configure storage page always present in all packs
#undef ENABLE_PAGE_COMMANDS
#define ENABLE_PAGE_COMMANDS 1
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 1
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 1

// Amada welding structures: disabled by request (2026-11-26). The page, its
// routes and the home button are all gated by this flag.
#undef ENABLE_STRUCTURES_PAGE
#define ENABLE_STRUCTURES_PAGE 0
#undef ENABLE_API_REFERENCE_PAGE
#define ENABLE_API_REFERENCE_PAGE 0

#undef ENABLE_RS232
#define ENABLE_RS232 1
#undef ENABLE_RS232_XONXOFF
#define ENABLE_RS232_XONXOFF 1
#undef ENABLE_DB25
#define ENABLE_DB25 0
#undef ENABLE_SNIFFER
#define ENABLE_SNIFFER 0

#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0
#undef ENABLE_UNIKON
#define ENABLE_UNIKON 0
#undef ENABLE_CAMERA
#define ENABLE_CAMERA 0

#undef ENABLE_MODBUS
#define ENABLE_MODBUS 0
#undef ENABLE_MODBUS_RTU
#define ENABLE_MODBUS_RTU 0
#undef ENABLE_MODBUS_TCP
#define ENABLE_MODBUS_TCP 0
#undef ENABLE_MODBUS_SLAVE
#define ENABLE_MODBUS_SLAVE 0
#undef ENABLE_CAN
#define ENABLE_CAN 0

#undef ENABLE_AHT20_BMP280
#define ENABLE_AHT20_BMP280 0
#undef ENABLE_MQ135
#define ENABLE_MQ135 0
#undef ENABLE_HX711
#define ENABLE_HX711 0
#undef ENABLE_INA226
#define ENABLE_INA226 0
#undef ENABLE_SOUND_SENSOR
#define ENABLE_SOUND_SENSOR 0
#undef ENABLE_MAX31865
#define ENABLE_MAX31865 0
#undef ENABLE_PROXIMITY
#define ENABLE_PROXIMITY 0
#undef ENABLE_UDA1334A
#define ENABLE_UDA1334A 0

#undef ENABLE_TELNET
#define ENABLE_TELNET 0
#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 1

// TDK-Lambda / Genesys SCPI module (page /tdk-lambda, CSV on LittleFS here:
// this pack has no SD slot). Shares UART2 with the RS232 page by design.
#undef ENABLE_TDK_LAMBDA
#define ENABLE_TDK_LAMBDA 1

// FTP client + /ftp page. No SD in this build: uploads resolve through the
// LittleFS mirror + PSRAM hot tier (ARCHITECTURE §6.1 rule 7).
#undef ENABLE_FTP_CLIENT
#define ENABLE_FTP_CLIENT 1
#undef ENABLE_PAGE_FTP
#define ENABLE_PAGE_FTP 1
