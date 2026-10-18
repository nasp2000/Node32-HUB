#pragma once

// ── Waveshare-8DI8DO (Node32-OEE) ───────────────────────────────────────────
// Board lock:   ESP32-S3 Waveshare 8DI8DO POE only
// System name:  Node32-OEE
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 🔌, name "8DI8DO"
//
// Features ON:  ETHERNET, PROXIMITY (8ch), SDCARD, SMTP, OTA, NTP,
//               HTTP_BASIC_AUTH, PAGE_MONITOR, PAGE_ACCESS, PAGE_FILES,
//               PAGE_SDCARD, FTP_CLIENT, PAGE_FTP
// Features OFF: BUZZER, PAGE_RECIPIENTS, WIFI, MDNS, GOOGLE_EMAIL,
//               DB25, SNIFFER, MODBUS*, CAN, UNIKON, IPG_LASER,
//               CAMERA, UDA1334A, LCD_SCREEN, TELNET, DEBUG_LOGS,
//               SCHEDULER, PAGE_COMMANDS, STRUCTURES_PAGE,
//               API_REFERENCE_PAGE, ASYNC_WEBSOCKET, all sensors
// Daily reboot: ON (ENABLE_DAILY_REBOOT=1, decoupled from scheduler; config via /api/daily-reboot)
// Home page:    Proximity + Dashboard, Ethernet, SD Card, Monitor
// Custom pages: Proximity config + shift management
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Node32-OEE"
#define PACK_EMAIL_SUBJECT "Node32-OEE"
#define PACK_EMOJI "🔌"
#define PACK_BRAND_NAME "Node32-OEE"

// Board lock: only for TARGET_BOARD_8DI8DO_POE
#if !defined(TARGET_BOARD_8DI8DO_POE) || TARGET_BOARD_8DI8DO_POE!=1
#error "PACK_WAVESHARE is fixed to TARGET_BOARD_8DI8DO_POE. Enable -DTARGET_BOARD_8DI8DO_POE=1."
#endif

// ---- Network ----
#undef ENABLE_WIFI
#define ENABLE_WIFI 1
#undef ENABLE_WIFI_WEB_SERVER
#define ENABLE_WIFI_WEB_SERVER 1
#undef ENABLE_ETHERNET
#define ENABLE_ETHERNET 1
#undef ENABLE_NTP
#define ENABLE_NTP 1

// ---- OTA / Auth ----
#undef ENABLE_OTA
#define ENABLE_OTA 1
#undef ENABLE_HTTP_BASIC_AUTH
#define ENABLE_HTTP_BASIC_AUTH 1
#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

// ---- Email ----
#undef ENABLE_SMTP
#define ENABLE_SMTP 1
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

// ---- Storage ----
#undef ENABLE_FILESYSTEM
#define ENABLE_FILESYSTEM 1
#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

// ---- FTP client + page (upload recordings/logs to FTP server) ----
#undef ENABLE_FTP_CLIENT
#define ENABLE_FTP_CLIENT 1
#undef ENABLE_PAGE_FTP
#define ENABLE_PAGE_FTP 1

// ---- Proximity (8 channels on PCB DI inputs) ----
#undef ENABLE_PROXIMITY
#define ENABLE_PROXIMITY 1
#undef PROXIMITY_CHANNELS
#define PROXIMITY_CHANNELS 8

// ---- Buzzer disabled (not needed for industrial pack) ----
#undef ENABLE_BUZZER
#define ENABLE_BUZZER 0

// ---- Recipients page disabled ----
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 0
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 1

// ---- Pages ----
#undef ENABLE_PAGE_MONITOR
#define ENABLE_PAGE_MONITOR 1
#undef ENABLE_PAGE_FILES
#define ENABLE_PAGE_FILES 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1
#undef ENABLE_PAGE_COMMANDS
#define ENABLE_PAGE_COMMANDS 1

// ---- RS232 / DB25 / Sniffer ----
#undef ENABLE_RS232
#define ENABLE_RS232 0
#undef ENABLE_RS232_XONXOFF
#define ENABLE_RS232_XONXOFF 0
#undef ENABLE_DB25
#define ENABLE_DB25 0
#undef ENABLE_SNIFFER
#define ENABLE_SNIFFER 0

// ---- Modbus / CAN ----
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

// ---- Unikon / IPG / Camera / Audio ----
#undef ENABLE_UNIKON
#define ENABLE_UNIKON 0
#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0
#undef ENABLE_CAMERA
#define ENABLE_CAMERA 0
#undef ENABLE_UDA1334A
#define ENABLE_UDA1334A 0

// ---- Sensors ----
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

// ---- LCD / Telnet / Debug ----
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0
#undef ENABLE_TELNET
#define ENABLE_TELNET 0
#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0

// ---- Scheduler / Structures / API Reference ----
// Date/time commands only: the interval page (/commands) is stripped via
// ENABLE_PAGE_COMMANDS_INTERVAL=0; /commands-by-datetime stays available.
// (Daily reboot remains decoupled — config also via /api/daily-reboot API.)
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 1
#undef ENABLE_PAGE_COMMANDS_INTERVAL
#define ENABLE_PAGE_COMMANDS_INTERVAL 0
#undef ENABLE_DAILY_REBOOT
#define ENABLE_DAILY_REBOOT 1
#undef ENABLE_STRUCTURES_PAGE
#define ENABLE_STRUCTURES_PAGE 0
#undef ENABLE_API_REFERENCE_PAGE
#define ENABLE_API_REFERENCE_PAGE 0

// ---- USB ----
#undef ENABLE_USBIP_SERVER
#define ENABLE_USBIP_SERVER 0
#undef ENABLE_GRBL
#define ENABLE_GRBL 0
