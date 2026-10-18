#pragma once

// ── Node32-RAMBOX ────────────────────────────────────────────────────────────
// Board lock:   ESP32-S3 N16R8 only
// System name:  Rambox32
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 🐦, name "Rambox32"
// RS232 baud:   9600
//
// Features ON:  RS232, RS232_XONXOFF, UNIKON, MDNS, SMTP, GOOGLE_EMAIL, OTA,
//               LCD_SCREEN, HTTP_BASIC_AUTH, NTP, PAGE_MONITOR, PAGE_ACCESS
// Features OFF: DB25, SNIFFER, MODBUS*, CAN, SCHEDULER,
//               PAGE_COMMANDS, PAGE_RECIPIENTS, PAGE_FILES, PAGE_SDCARD,
//               DEBUG_LOGS, API_REFERENCE_PAGE, STRUCTURES_PAGE, TELNET,
//               ASYNC_WEBSOCKET, all sensors
// Home page:    RS232 button, Unikon button, Monitor button
// Custom pages: Unikon terminal
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Rambox32"
#define PACK_BRAND_NAME "Rambox32"
#define PACK_EMAIL_SUBJECT "Rambox32"
#define PACK_EMOJI "🐦"

// Network and time
#define ENABLE_NTP 1

// Sensors and peripherals
#define ENABLE_AHT20_BMP280 0
#define ENABLE_MQ135 0
#define ENABLE_HX711 0
#define ENABLE_INA226 0
#define ENABLE_SOUND_SENSOR 0
#define ENABLE_MAX31865 0
#define ENABLE_PROXIMITY 0
#define ENABLE_UDA1334A 0

// Fixed board policy: Rambox pack is valid only for the ESP32-S3 N16R8 class.
// TARGET_BOARD_S3_CAM is accepted because it maps to the same n16r8 build profile.
#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
	(defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1)
#error "PACK_RAMBOX is fixed to ESP32-S3 N16R8 class builds (env n16r8). Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4."
#endif


#undef ENABLE_NTP
#define ENABLE_NTP 1

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

#undef ENABLE_RS232
#define ENABLE_RS232 1
#undef ENABLE_RS232_XONXOFF
#define ENABLE_RS232_XONXOFF 1

#undef ENABLE_DB25
#define ENABLE_DB25 0
#undef ENABLE_SNIFFER
#define ENABLE_SNIFFER 0

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

#undef ENABLE_UNIKON
#define ENABLE_UNIKON 1
#undef ENABLE_BRICON
#define ENABLE_BRICON 1
#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0

#undef ENABLE_SMTP
#define ENABLE_SMTP 1
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 1

#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 0

#undef ENABLE_PAGE_COMMANDS
#define ENABLE_PAGE_COMMANDS 0
#undef ENABLE_PAGE_RECIPIENTS
#define ENABLE_PAGE_RECIPIENTS 0
#undef ENABLE_PAGE_ALERTS
#define ENABLE_PAGE_ALERTS 0
#undef ENABLE_PAGE_FILES
#define ENABLE_PAGE_FILES 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1  // T12: Configure storage page always present in all packs
#undef ENABLE_PAGE_MONITOR
#define ENABLE_PAGE_MONITOR 1

#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0
#undef ENABLE_API_REFERENCE_PAGE
#define ENABLE_API_REFERENCE_PAGE 0
#undef ENABLE_STRUCTURES_PAGE
#define ENABLE_STRUCTURES_PAGE 0

#undef ENABLE_TELNET
#define ENABLE_TELNET 0

#undef ENABLE_OTA
#define ENABLE_OTA 1

#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 1
#undef SCREEN_TYPE
#define SCREEN_TYPE 1

#undef ENABLE_HTTP_BASIC_AUTH
#define ENABLE_HTTP_BASIC_AUTH 1
#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

