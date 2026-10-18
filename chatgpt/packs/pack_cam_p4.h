#pragma once

// ── Cam-P4 ──────────────────────────────────────────────────────────────────
// Board lock:   ESP32-P4 only (enforced below)
// System name:  Cam-P4
// Branding:     enabled, emoji 📷, name "Cam-P4"
//
// Same pages and concept as Cam-M (PACK_CAM): /camera (MJPEG stream, snapshot,
// AVI recording, dashcam, timelapse, motion), /hub, /monitor, /files, /sdcard.
// Hardware differences vs Cam-M (S3):
//   - Sensor: PY260 via MIPI CSI-2 (CAM_IS_MIPI_CSI=1, XCLK GPIO27).
//     DVP data pins are -1 (internal silicon lanes).
//   - Network: Ethernet RMII onboard (primary) + WiFi via C6 SDIO companion.
//     The P4 has no native WiFi radio.
//   - RAM: larger PSRAM; frame budgets scale via the same w*h/5 cache rule.
//   - No lamp pin (CAM_PIN_LAMP=-1): lamp slider auto-hides, lamp-auto no-op.
//   - No USB Host / GRBL in this pack (camera-only, like Cam-M).
//
// Features ON:  WIFI, WIFI_WEB_SERVER, ETHERNET, CAMERA, SDCARD, OTA,
//               PAGE_MONITOR, PAGE_ACCESS, PAGE_FILES,
//               PAGE_SDCARD, NTP, HTTP_BASIC_AUTH,
//               MODBUS + MODBUS_TCP master (vision counts -> S3 slave)
// Features OFF: MDNS, PAGE_COMMANDS, PAGE_RECIPIENTS,
//               SMTP, GOOGLE_EMAIL (no email in this pack: no /email page,
//               no motion email alerts), STRUCTURES_PAGE, API_REFERENCE_PAGE,
//               DEBUG_LOGS, TELNET, SCHEDULER, RS232, DB25, SNIFFER,
//               MODBUS_RTU, MODBUS_SLAVE,
//               CAN, UNIKON, IPG_LASER, ASYNC_WEBSOCKET, USB_HOST, GRBL,
//               all sensors
// Home page:    Camera button, Monitor button (via ENABLE_CAMERA)
// Custom pages: Camera stream (/camera), Camera hub (/hub)
// ─────────────────────────────────────────────────────────────────────────────
#define PACK_SYSTEM_NAME "Cam-P4"
#define PACK_BRAND_NAME "Cam-P4"
#define PACK_EMAIL_SUBJECT "Cam-P4"
#define PACK_EMOJI "📷"

#if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
    (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
#error "PACK_CAM_P4 is fixed to TARGET_BOARD_P4. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_S3_CAM."
#endif

#undef ENABLE_WIFI
#define ENABLE_WIFI 1
#undef ENABLE_WIFI_WEB_SERVER
#define ENABLE_WIFI_WEB_SERVER 1

#undef ENABLE_ETHERNET
#define ENABLE_ETHERNET 1

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

// Modbus TCP master (Tarefa 5 Fase 6): the vision block pushes OK/NOK
// counts to the Waveshare S3 slave. RTU/SLAVE stay off — TCP master only.
#undef ENABLE_MODBUS
#define ENABLE_MODBUS 1
#undef ENABLE_MODBUS_RTU
#define ENABLE_MODBUS_RTU 0
#undef ENABLE_MODBUS_TCP
#define ENABLE_MODBUS_TCP 1
#undef ENABLE_MODBUS_SLAVE
#define ENABLE_MODBUS_SLAVE 0
#undef ENABLE_CAN
#define ENABLE_CAN 0

#undef ENABLE_UNIKON
#define ENABLE_UNIKON 0
#undef ENABLE_IPG_LASER
#define ENABLE_IPG_LASER 0

#undef ENABLE_USB_HOST
#define ENABLE_USB_HOST 1  // UVC webcam source (OTG port becomes host; USB CDC
                           // Serial is then unavailable — debug via UART0 or
                           // the web monitor). Matches P4 env USB_MODE=0.

// CAM-P4: the webcam sits behind the CH334 hub on the dedicated FSLS pads
// (USB1). Running the OTG20 HS controller against the FSLS PHY (default
// route=1) enables the root port (PENA=1) but the HS Chirp handshake never
// completes on an FS-only line, so enumeration hangs forever. Use the OTG11
// FS controller — the native owner of the FSLS pads (peripheral_map BIT1,
// FS-only) — for a clean FS enumeration of hub + camera. The PHY-route /
// pin-exchange fallback ladder is OTG20-HS-only and stays inert with map 2.
// CAM-P4 USB host: the webcam sits behind the CH334 hub on the OTG20 HS
// (UTMI) port — the same native host config used by the P4-CNC pack
// (map=1, route=1; the CNC GRBL boards enumerate fine on it). The OTG11
// FS / FSLS route (DUAL_TEST_MAP=2) was tested: the FSLS pads see NO device
// (FS PCSTS=0 in both route 0 and 1), and the OTG20 HS still detects + enables
// the port (PCSTS=1,PENA=1) but never completes enumeration — the hub driver
// enables the port but no device tag ever appears. Baseline = CNC host config.
#undef USB_HOST_P4_OTG11_FS_HOST
#define USB_HOST_P4_OTG11_FS_HOST 0
#undef USB_HOST_P4_DUAL_TEST_MAP
#define USB_HOST_P4_DUAL_TEST_MAP 1  // 1 = HS-only native (CNC baseline), 2 = FS-only OTG11
// 1 = install+post-task PPWR cycles (P4 workaround, P4-CNC baseline). The CAM
// pack had 0 (v1.06-style clean start); with 0 the OTG20 HS port reports
// PCSTS=1/PENA=1 but the device is NEVER enumerated (0 addresses). Match the
// working P4-CNC host config: root-port power is cycled so the port comes up
// in a clean state.
#undef USB_HOST_P4_ROOT_POWER_CYCLE
#define USB_HOST_P4_ROOT_POWER_CYCLE 1
#undef ENABLE_GRBL
#define ENABLE_GRBL 0

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

// LCD/OLED — not used in CAM packs
#undef ENABLE_LCD_SCREEN
#define ENABLE_LCD_SCREEN 0
