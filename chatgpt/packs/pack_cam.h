#pragma once

// ── Cam-M ───────────────────────────────────────────────────────────────────
// Board lock:   ESP32-S3 CAM only (enforced in user_config.h)
// System name:  Cam-M
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 📷, name "Cam-M"
//
// Features ON:  WIFI, WIFI_WEB_SERVER, MDNS, CAMERA, SDCARD, OTA,
//               PAGE_MONITOR, PAGE_ACCESS, PAGE_FILES,
//               PAGE_SDCARD, NTP, HTTP_BASIC_AUTH
// Features OFF: ETHERNET, PAGE_COMMANDS, PAGE_RECIPIENTS,
//               SMTP, GOOGLE_EMAIL (no email in this pack: no /email page,
//               no motion email alerts), STRUCTURES_PAGE, API_REFERENCE_PAGE,
//               DEBUG_LOGS, TELNET, SCHEDULER, RS232, DB25, SNIFFER, MODBUS*,
//               CAN, UNIKON, IPG_LASER, ASYNC_WEBSOCKET, all sensors
// Home page:    Camera button, Monitor button
// Custom pages: Camera stream (/camera)
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Cam-M"
#define PACK_BRAND_NAME "Cam-M"
#define PACK_EMAIL_SUBJECT "Cam-M"
#define PACK_EMOJI "📷"

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1)
#error "PACK_CAM is fixed to TARGET_BOARD_S3_CAM. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4."
#endif

#undef ENABLE_WIFI
#define ENABLE_WIFI 1
#undef ENABLE_WIFI_WEB_SERVER
#define ENABLE_WIFI_WEB_SERVER 1

#undef ENABLE_ETHERNET
#define ENABLE_ETHERNET 0

#undef ENABLE_NTP
#define ENABLE_NTP 1

#undef ENABLE_CAMERA
#define ENABLE_CAMERA 1

#undef ENABLE_OTA
#define ENABLE_OTA 1

#undef ENABLE_SMTP
#define ENABLE_SMTP 0
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

#undef ENABLE_FTP_CLIENT
#define ENABLE_FTP_CLIENT 1
#undef ENABLE_PAGE_FTP
#define ENABLE_PAGE_FTP 1

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

#undef ENABLE_STRUCTURES_PAGE
#define ENABLE_STRUCTURES_PAGE 0
#undef ENABLE_API_REFERENCE_PAGE
#define ENABLE_API_REFERENCE_PAGE 0

#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0

#undef ENABLE_HTTP_BASIC_AUTH
#define ENABLE_HTTP_BASIC_AUTH 1
#undef ENABLE_ASYNC_WEBSOCKET
#define ENABLE_ASYNC_WEBSOCKET 0

#undef ENABLE_TELNET
#define ENABLE_TELNET 0
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 0

#undef ENABLE_RS232
#define ENABLE_RS232 0
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
#define ENABLE_UNIKON 0
#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0

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

// LCD/OLED — not used in CAM pack
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0
