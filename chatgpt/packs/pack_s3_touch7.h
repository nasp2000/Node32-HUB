#pragma once

// ── TouchMeteo-7 ─────────────────────────────────────────────────────────────
// Board lock:   Waveshare ESP32-S3-Touch-LCD-7 only (TARGET_BOARD_S3_TOUCH7)
// System name:  TouchMeteo-7
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 🌤️, name "TouchMeteo-7"
//
// Features ON:  WIFI, WIFI_WEB_SERVER, NTP, OTA, HTTP_BASIC_AUTH, FILESYSTEM,
//               SDCARD, LCD_SCREEN, PAGE_MONITOR, PAGE_FILES, PAGE_SDCARD,
//               SCHEDULER
// Features OFF: ETHERNET, MDNS, SMTP, GOOGLE_EMAIL, PAGE_SDCARD,
//               PAGE_COMMANDS, PAGE_RECIPIENTS, STRUCTURES_PAGE,
//               API_REFERENCE_PAGE, RS232, RS232_XONXOFF, DB25, SNIFFER,
//               IPG_LASER, UNIKON, CAMERA, MODBUS*, CAN, TELNET, DEBUG_LOGS,
//               ASYNC_WEBSOCKET, MQ135, HX711, SOUND, MAX31865, PROXIMITY,
//               UDA1334A, INA226 (power monitor removido do pack 17-09-2026),
//               AHT20_BMP280, BH1750 (sensores I2C locais removidos 17-09-2026:
//               sem sensores no bus partilhado 8/9, ver T7-24)
// Home page:    Meteo tiles + Peers viewer + Monitor button
// Custom pages: sensor charts, peers config, falas (F2c), wifi
// Doc:          docs/waveshareLCDTouch7.md (F0-F4 plan)
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "TouchMeteo-7"
#define PACK_EMAIL_SUBJECT "TouchMeteo-7"
#define PACK_EMOJI "🌤️"
#define PACK_BRAND_NAME "TouchMeteo-7"

// Board lock: only for TARGET_BOARD_S3_TOUCH7
#if !defined(TARGET_BOARD_S3_TOUCH7) || TARGET_BOARD_S3_TOUCH7!=1
#error "PACK_S3_TOUCH7 is fixed to TARGET_BOARD_S3_TOUCH7. Enable -DTARGET_BOARD_S3_TOUCH7=1."
#endif

// ---- Network ----
#undef ENABLE_WIFI
#define ENABLE_WIFI 1
#undef ENABLE_WIFI_WEB_SERVER
#define ENABLE_WIFI_WEB_SERVER 1
#undef ENABLE_ETHERNET
#define ENABLE_ETHERNET 0
#undef ENABLE_MDNS
#define ENABLE_MDNS 0
#undef ENABLE_NTP
#define ENABLE_NTP 1

// ---- OTA / Auth ----
#undef ENABLE_OTA
#define ENABLE_OTA 1
#undef ENABLE_HTTP_BASIC_AUTH
#define ENABLE_HTTP_BASIC_AUTH 1
#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

// ---- Email (off in this phase; local speaker phrases cover alarms) ----
#undef ENABLE_SMTP
#define ENABLE_SMTP 0
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

// ---- Storage ----
// SDCARD stays ON: the /sdcard page requires it (pack validator), and the
// TF slot exists on this board. Boot contentions are handled by the
// flash-race package (FB-direct + panel-last + flash-hold), not by
// disabling storage. LittleFS (6 MB) holds configs, history and /falas WAVs.
#undef ENABLE_FILESYSTEM
#define ENABLE_FILESYSTEM 1
#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

// ---- Display (RGB 800x480 LVGL profile, F1) ----
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 1
// 7" RGB panel profile (DISPLAY_S3_TOUCH7_800x480): selects the Touch7Display
// engine instead of the Adafruit OLED path (see display_profiles.h).
#undef SCREEN_TYPE
#define SCREEN_TYPE 3

// ---- Pages ----
#undef ENABLE_PAGE_MONITOR
#define ENABLE_PAGE_MONITOR 1
#undef ENABLE_PAGE_FILES
#define ENABLE_PAGE_FILES 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1
#undef ENABLE_PAGE_COMMANDS
#define ENABLE_PAGE_COMMANDS 0
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 0
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 0

// ---- Scheduler (display sleep + future reminders; software only) ----
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 1
#undef ENABLE_STRUCTURES_PAGE
#define ENABLE_STRUCTURES_PAGE 0
#undef ENABLE_API_REFERENCE_PAGE
#define ENABLE_API_REFERENCE_PAGE 0

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

// ---- Unikon / IPG / Camera / Audio DAC ----
#undef ENABLE_UNIKON
#define ENABLE_UNIKON 0
#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0
#undef ENABLE_CAMERA
#define ENABLE_CAMERA 0
#undef ENABLE_UDA1334A
#define ENABLE_UDA1334A 0

// ---- Sensors: no local I2C on this pack (17-09-2026) ----
#undef ENABLE_AHT20_BMP280
#define ENABLE_AHT20_BMP280 0
#undef ENABLE_BH1750
#define ENABLE_BH1750 0
#undef ENABLE_MQ135
#define ENABLE_MQ135 0
#undef ENABLE_HX711
#define ENABLE_HX711 0
#undef ENABLE_INA226
#define ENABLE_INA226 0 // power monitor fora do pack (17-09-2026)
#undef ENABLE_SOUND_SENSOR
#define ENABLE_SOUND_SENSOR 0
#undef ENABLE_MAX31865
#define ENABLE_MAX31865 0
#undef ENABLE_PROXIMITY
#define ENABLE_PROXIMITY 0

// ---- Telnet / Debug ----
#undef ENABLE_TELNET
#define ENABLE_TELNET 0
#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0

// ---- USB / GRBL ----
#undef ENABLE_USBIP_SERVER
#define ENABLE_USBIP_SERVER 0
#undef ENABLE_GRBL
#define ENABLE_GRBL 0
