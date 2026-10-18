#pragma once

// ── Harmonica Box ────────────────────────────────────────────────────────────
// Board lock:   ESP32-S3 N16R8 only
// System name:  Harmonica Box
// AP SSID:      NODE32-HUB
// Branding:     enabled, emoji 🎵, name "Harmonica Box" + dark blue CSS theme
// Access page:  NOT restricted
// /monitor:     REMOVED (route not registered)
//
// Features ON:  WIFI, WIFI_WEB_SERVER, MDNS, NTP, OTA, HTTP_BASIC_AUTH, FILESYSTEM,
//               SDCARD, PAGE_MONITOR, PAGE_FILES, UDA1334A (I2S DAC)
// Features OFF: ETHERNET, SMTP, GOOGLE_EMAIL, PAGE_ACCESS, PAGE_SDCARD,
//               PAGE_COMMANDS, PAGE_RECIPIENTS, STRUCTURES_PAGE,
//               API_REFERENCE_PAGE, RS232, RS232_XONXOFF, DB25, SNIFFER,
//               IPG_LASER, UNIKON, CAMERA, MODBUS*, CAN, TELNET, DEBUG_LOGS,
//               LCD_SCREEN (OLED not used in this pack), SCHEDULER,
//               SCHEDULER, ASYNC_WEBSOCKET, all other sensors
// Home page:    Radio UI button (/radio-ui), File browser button
// Custom pages: Internet radio player (/radio-ui), file browser with radio
//               controls (Play/Stop/Random/Next)
// Other:        radio_audio_runtime.cpp compiled for MP3 streaming
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Harmonica Box"
#define PACK_BRAND_NAME "Harmonica Box"
#define PACK_EMAIL_SUBJECT "Harmonica Box"
#define PACK_EMOJI "🎵"

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1) || \
    (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
#error "PACK_RADIO is fixed to ESP32-S3 N16R8 class builds (env n16r8). Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4/TARGET_BOARD_S3_CAM."
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
#define ENABLE_SMTP 0
#undef ENABLE_GOOGLE_EMAIL
#define ENABLE_GOOGLE_EMAIL 0

#undef ENABLE_FILESYSTEM
#define ENABLE_FILESYSTEM 1
#undef ENABLE_SDCARD
#define ENABLE_SDCARD 1

#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0 // OLED not used in this pack

#undef ENABLE_PAGE_MONITOR
#define ENABLE_PAGE_MONITOR 1
#undef ENABLE_PAGE_FILES
#define ENABLE_PAGE_FILES 1
#undef ENABLE_PAGE_SDCARD
#define ENABLE_PAGE_SDCARD 1  // T12: Configure storage page always present in all packs
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

#undef ENABLE_RS232
#define ENABLE_RS232 0
#undef ENABLE_RS232_XONXOFF
#define ENABLE_RS232_XONXOFF 0
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
#define ENABLE_UDA1334A 1

// =====================================================================
// UDA1334A I2S DAC — pins (module <-> ESP32-S3 N16R8)
// Override in user_config.h before this pack (uses #ifndef).
// This board does NOT expose GPIO25/26/27 — we use 41/42/19 (free).
//   ESP32 GPIO41  ->  UDA1334A BCLK  (bit clock)
//   ESP32 GPIO42  ->  UDA1334A WSEL  (word select / LRCK)
//   ESP32 GPIO19  ->  UDA1334A DIN   (serial data in)
//   ESP32 GPIO1   ->  UDA1334A MUTE  (OPTIONAL — see below)
//   MCLK          ->  do not connect (disabled; most modules derive
//                     the clock from BCLK internally)
// Module pins NOT going to the ESP32:
//   VIN  -> 3V3 (module accepts 3.3-5V)      GND -> GND
//   MUTE -> GPIO1 (OPTIONAL)  or  GND (no software control)
//   SMT  -> GND (de-emphasis off)
//   SF0  -> GND      SF1 -> GND               (I2S format, 00)
// Note: microSD uses 1-bit SDMMC (CLK=48 CMD=47 D0=45). MUTE uses
//       GPIO1 (CAN_TX_PIN, free because CAN is disabled in this
//       pack). OLED is disabled in this pack (ENABLE_LCD_SCREEN=0),
//       so GPIO21/48 are also free.
// =====================================================================
#ifndef UDA1334A_I2S_BCLK_PIN
  #define UDA1334A_I2S_BCLK_PIN   41
#endif
#ifndef UDA1334A_I2S_LRCK_PIN
  #define UDA1334A_I2S_LRCK_PIN   42
#endif
#ifndef UDA1334A_I2S_DOUT_PIN
  #define UDA1334A_I2S_DOUT_PIN   19
#endif
#ifndef UDA1334A_I2S_MCLK_PIN
  #define UDA1334A_I2S_MCLK_PIN   -1
#endif
// MUTE control (UDA1334A pin 8) -> GPIO1 (CAN_TX_PIN; CAN disabled
// in this pack, so it is free and next to free 0/2/3).
// OPTIONAL: the pin may be left unconnected (MUTE -> GND) with -1 restored here;
// firmware works in both cases. HIGH = silence, LOW = audio active.
#ifndef UDA1334A_MUTE_PIN
  #define UDA1334A_MUTE_PIN       1
#endif

#undef ENABLE_TELNET
#define ENABLE_TELNET 0
#undef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 0
#undef ENABLE_SCHEDULER
#define ENABLE_SCHEDULER 0