#ifndef USER_CONFIG_H_ROOT
#define USER_CONFIG_H_ROOT

// =============================================================================
//  user_config.h -- BUILD CONFIGURATION
//
//  STEP 1 -- Uncomment your board below (or leave all = N16R8 default).
//  STEP 2 -- Enable/disable modules, protocols and web pages.
//  Board hardware (WiFi, Ethernet, SD, screen, RS232) is auto-enabled per board.
//  Everything below the *** marker is automatic -- do not edit.
// =============================================================================

// -----------------------------------------------------------------------------
// STEP 0 -- PACK PROFILE (optional)
//
// Pack profiles pre-select module flags for a specific use case.
// Choose one pack and keep STEP 2 available for manual overrides.
// If you want pack-only workflow, keep STEP 2 fully commented and edit modules
// in packs/pack_<name>.h.
//
// Example: board + Rambox pack
//   1) Select board in STEP 1
//   2) Uncomment the line below
//   3) Edit modules only in packs/pack_rambox.h
//
// #define SELECTED_PACK PACK_RAMBOX
// #define SELECTED_PACK PACK_CAM
// #define SELECTED_PACK PACK_RS232
// #define SELECTED_PACK PACK_ENVIRONMENT
// #define SELECTED_PACK PACK_RADIO
// #define SELECTED_PACK PACK_S3_CNC         // S3 CNC GcodeSender (UART default + USB Host/OTG selector)
// #define SELECTED_PACK PACK_P4_CNC         // P4 GcodeSender pack
// #define SELECTED_PACK PACK_P4_USB_SERVER
// #define SELECTED_PACK PACK_CAM_P4         // P4 camera pack (same pages as Cam-M)
// #define SELECTED_PACK PACK_S3_TOUCH7      // TouchMeteo-7 (Waveshare ESP32-S3-Touch-LCD-7)
#ifndef SELECTED_PACK
#define SELECTED_PACK PACK_S3_CNC // S3 CNC / GcodeSender (GRBL via UART default, USB Host OTG per-session)
// Override for P4 builds: pio run -e esp32p4 uses -DSELECTED_PACK=6 (PACK_P4_CNC)
#endif
// -----------------------------------------------------------------------------

// Pack identifiers are declared here so board preselection can happen
// before board_pins.h is included.
#ifndef PACK_NONE
#define PACK_NONE   0
#endif
#ifndef PACK_RAMBOX
#define PACK_RAMBOX 1
#endif
#ifndef PACK_CAM
#define PACK_CAM    2
#endif
#ifndef PACK_RS232
#define PACK_RS232  3
#endif
#ifndef PACK_ENVIRONMENT
#define PACK_ENVIRONMENT 4
#endif
#ifndef PACK_RADIO
#define PACK_RADIO 5
#endif
#ifndef PACK_P4_CNC
#define PACK_P4_CNC 6
#endif
#ifndef PACK_P4_USB_SERVER
#define PACK_P4_USB_SERVER 7
#endif
#ifndef PACK_S3_CNC
#define PACK_S3_CNC 8
#endif
#ifndef PACK_WAVESHARE
#define PACK_WAVESHARE 10
#endif
#ifndef PACK_CAM_P4
#define PACK_CAM_P4 11
#endif
#ifndef PACK_S3_TOUCH7
#define PACK_S3_TOUCH7 12
#endif

#ifndef SELECTED_PACK
#define SELECTED_PACK PACK_NONE
#endif


// -----------------------------------------------------------------------------
// STEP 1 -- BOARD SELECTION  (uncomment ONE line; leave all commented = N16R8 default)
// -----------------------------------------------------------------------------

// -- A: ESP32-S3 N16R8 devkit ------------------------------------------------
//    DEFAULT (no define)  |  WiFi  |  RS232 UART2 RX=16 TX=17  |  I2C SDA=8 SCL=9  |  No Ethernet

// -- B: Waveshare ESP32-S3 POE-ETH-8DI-8DO -----------------------------------
//    WiFi  |  W5500 Ethernet  |  SD  |  8DI  |  8DO TCA9554  |  Buzzer
//    RS232 UART2 RX=44 TX=43  |  Modbus UART1 TX=17 RX=18  |  I2C SDA=42 SCL=41
//    W5500: MOSI=13 MISO=14 CLK=15 CS=16 RST=39 INT=12  |  CAN TX=1 RX=3
//  #define TARGET_BOARD_8DI8DO_POE  1

// -- C: Waveshare ESP32-P4-Module-DEV-KIT ------------------------------------
//    RMII Ethernet  |  SD SDMMC4 4-bit  |  I2C SDA=7 SCL=8
//    RS232 opt-in (GPIO20=RX, GPIO21=TX -- add #define ENABLE_RS232 1 in STEP 2)
//    Camera (MIPI CSI-2) opt-in -- add #define ENABLE_CAMERA 1 in STEP 2
//    !!! GPIO14-19 are the C6 SDIO bus -- do NOT use for UART or other hardware
//    RISC-V dual-core 360 MHz  |  32 MB PSRAM stacked  |  16 MB Flash
//    arduino-esp32 >= 3.2.0  |  FlashSize=16MB  |  PartitionScheme=Custom
//#define TARGET_BOARD_P4  1

// -- D: ESP32-S3 with onboard DVP camera connector ---------------------------
//    Camera DVP (OV2640/OV3660/OV5640)  |  SD SDMMC 1-bit (CMD=38 CLK=39)
//    8 MB PSRAM  |  WiFi  |  WS2812 GPIO48
//    Camera pins: XCLK=15 SIOD=4 SIOC=5 Y2-Y9=11,9,8,10,12,18,17,16 PCLK=13
//    RS232 on GPIO43/44 (GPIO16=RX would conflict with CAM_Y9)
//    Enable camera in STEP 2: #define ENABLE_CAMERA 1
// #define TARGET_BOARD_S3_CAM  1

// -- E: Waveshare ESP32-S3-Touch-LCD-7 (TouchMeteo-7 pack) --------------------
//    LCD RGB 7" 800x480 + touch + CH422G  |  TF SPI  |  RS485 (reservado)
//    I2C SDA=8 SCL=9 (touch + AHT20/BMP280 + BH1750 + DS3231 + INA226)
//    Audio PWM GPIO6 -> PAM8403  |  PIR em GPIO a validar  |  USB CDC nativo
//    Doc: docs/waveshareLCDTouch7.md
// #define TARGET_BOARD_S3_TOUCH7  1

// PACK_CAM is locked to the ESP32-S3 CAM board profile.
// This must be applied before board_pins.h so camera CAM_PIN_* are available.
#if SELECTED_PACK == PACK_CAM
  #if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
      (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1)
    #error "PACK_CAM is fixed to TARGET_BOARD_S3_CAM. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4."
  #endif
  #ifndef TARGET_BOARD_S3_CAM
    #define TARGET_BOARD_S3_CAM 1
  #endif
#endif

// PACK_S3_TOUCH7 (TouchMeteo-7) is locked to the ESP32-S3-Touch-LCD-7 board.
// This must be applied before board_pins.h so the TOUCH7 pin mapping is active.
#if SELECTED_PACK == PACK_S3_TOUCH7
  #if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
      (defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1) || \
      (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
    #error "PACK_S3_TOUCH7 is fixed to TARGET_BOARD_S3_TOUCH7. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_P4/TARGET_BOARD_S3_CAM."
  #endif
  #ifndef TARGET_BOARD_S3_TOUCH7
    #define TARGET_BOARD_S3_TOUCH7 1
  #endif
#endif

// PACK_S3_CNC is for ESP32-S3 only (any S3 variant). Reject P4 (wrong architecture).
#if SELECTED_PACK == PACK_S3_CNC
  #if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    #error "PACK_S3_CNC requires an ESP32-S3 board, not ESP32-P4."
  #endif
#endif

// PACK_P4_CNC (P4 GcodeSender pack) is locked to the ESP32-P4 board profile.
// This must be applied before board_pins.h so P4 pin mapping is active.
#if SELECTED_PACK == PACK_P4_CNC
  #if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
      (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
    #error "PACK_P4_CNC is fixed to TARGET_BOARD_P4. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_S3_CAM."
  #endif
  #ifndef TARGET_BOARD_P4
    #define TARGET_BOARD_P4 1
  #endif
#endif

// PACK_P4_USB_SERVER is locked to the ESP32-P4 board profile.
// This must be applied before board_pins.h so P4 pin mapping is active.
#if SELECTED_PACK == PACK_P4_USB_SERVER
  #if (defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1) || \
      (defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1)
    #error "PACK_P4_USB_SERVER is fixed to TARGET_BOARD_P4. Disable TARGET_BOARD_8DI8DO_POE/TARGET_BOARD_S3_CAM."
  #endif
  #ifndef TARGET_BOARD_P4
    #define TARGET_BOARD_P4 1
  #endif
#endif

// GPIO pin assignments for the selected board (do not move this line).
#include "src/board_pins.h"


// -----------------------------------------------------------------------------
// STEP 2 -- MODULES, PROTOCOLS AND WEB PAGES
//
//  Board hardware is already ON for the selected board:
//    A: N16R8       -- WiFi  (no screen, no SD; PSRAM VFS used as storage when SD=0)
//    B: 8DI8DO_POE  -- WiFi, W5500 Ethernet, SD, TCA9554 8DO, Buzzer, 8DI Proximity
//    C: P4          -- RMII Ethernet, SD
//
//  IMPORTANT: when a pack is active (SELECTED_PACK != PACK_NONE), pack values
//  have priority and module/page changes must be done only in packs/*.h files.
//
//  Syntax:
//    #define FLAG value    -- active override
//  // #define FLAG value   -- commented = use the automatic default shown in parentheses
// -----------------------------------------------------------------------------

// --- 2A - NETWORK  (uncomment to override the board default) ---------------
// #define ENABLE_WIFI               1   // force WiFi on/off
// #define ENABLE_ETHERNET           0   // force Ethernet on/off
// #define ENABLE_MDNS               1   // mDNS  hostname.local          (default: 0)
// #define ENABLE_NTP                1   // NTP time sync / timezone      (default: 1)

// --- 2B - SENSORS  (external hardware -- all off by default) -------------
//   #define ENABLE_AHT20_BMP280       1   // AHT20 + BMP280 I2C temperature/humidity
//   #define ENABLE_MQ135              1   // MQ-135 air quality (analog ADC)
// #define ENABLE_HX711              1   // HX711 load cell
// #define ENABLE_INA226             1   // INA226 voltage/current monitor (I2C)
// #define ENABLE_SOUND_SENSOR       1   // MAX4466 sound level (analog ADC)
// #define ENABLE_MAX31865           1   // MAX31865 PT100/PT1000 (SPI)
// #define ENABLE_PROXIMITY          1   // Industrial proximity / opto inputs
// #define PROXIMITY_CHANNELS        8   //   channels 1-8                 (default: 4 / Board B auto: 8)
// #define PROXIMITY_USE_PCNT        0   //   0=ISR  1=PCNT hardware       (default: 0)

// --- 2C - AUDIO DAC -------------------------------------------------------
//  #define ENABLE_UDA1334A           1   // UDA1334A passive DAC (I2S audio playback)

// --- 2D - SERIAL PROTOCOLS  (external hardware / optional) -----------------
// #define ENABLE_RS232              1   // RS232 channel (GPIO44=RX GPIO43=TX on S3/8DI8DO; GPIO20=RX GPIO21=TX on P4)
// #define ENABLE_DB25               1   // DB25/Centronics emitter (ext. Arduino Nano)
// #define ENABLE_NANO_REBOOT_ON_BOOT 1  //   reboot Nano at boot          (needs DB25=1)
// #define ENABLE_SNIFFER            1   // Dual-RX passive sniffer (disables RS232+DB25)

// --- 2E - MODBUS  (RTU = UART1 GPIO40/41 on S3; TCP = WiFi or Ethernet) ----
// #define ENABLE_MODBUS             1   // Modbus master enable
// #define ENABLE_MODBUS_RTU         1   //   RTU via RS-485 UART1        (conflicts DB25)
// #define ENABLE_MODBUS_TCP         1   //   Modbus TCP
// #define ENABLE_MODBUS_SLAVE       1   //   Slave mode

// --- 2F - CAN BUS (TWAI) ---------------------------------------------------
// #define ENABLE_CAN                1   // TWAI/CAN 2.0B
// #define CAN_TX_PIN               20   //   TX GPIO -- N16R8 only
// #define CAN_RX_PIN               19   //   RX GPIO -- N16R8 only

// --- 2G - SPECIAL DEVICES --------------------------------------------------
//   #define ENABLE_UNIKON             1   // Unikon RAMBOX pigeon-racing terminal
//    #define ENABLE_IPG_LASER          1   // IPG YLR/Y14 RS232 command console
// #define ENABLE_USB_HOST           0   // USB Host CDC-ACM (S3, P4): USB-RS232 adapters (CP210x, CH343...)
//                                       // Data from USB adapters is injected into the RS232 RX pipeline.
//                                       // S3 uses the native USB port (GPIO19/20) with an OTG adapter/cable.
//                                       // P4 uses the USB-C OTG HS port. On both, Serial CDC becomes unavailable.
// #define ENABLE_USBIP_SERVER       0   // USB/IP TCP server scaffold (P4 only), port 3240.
//                                       // Requires ENABLE_USB_HOST=1 and network connectivity (WiFi/Ethernet).
// #define USB_HOST_MAX_CDC_DEVICES  8   // Max simultaneous USB-CDC devices (default: 4).
//                                       // Increase when using powered USB hubs and multiple GRBL controllers.
// #define ENABLE_GRBL               0   // GRBL G-code sender (SD -> USB Host, S3/P4).
//                                       // S3: native USB port (GPIO19/20) via OTG adapter.
//                                       // P4: USB-C OTG HS port. Character-counting flow control.
//                                       // Streams .nc/.gcode files from SD card line by line.
//                                       // Requires (ENABLE_USB_HOST=1 and ENABLE_SDCARD=1) or local upload.

// --- 2H - CAMERA -- OV2640 / OV3660 / OV5640 / PY260 via esp-camera --------
// Supported boards: TARGET_BOARD_S3_CAM (DVP) and TARGET_BOARD_P4 (MIPI CSI-2).
// Camera pin assignments are defined automatically in board_pins.h for those boards.
// To enable: select the board in STEP 1 above, then uncomment the line below.
// Requires PSRAM >= 4 MB.
// #define ENABLE_CAMERA              1

// --- 2I - WEB PAGES  (set to 0 to remove page + its HTTP handlers from the binary)
//
//  All pages are ON by default. Set a flag to 0 to strip the page and its handlers:
//  saves 5-20 KB of flash per page (PROGMEM HTML assets + route handlers).
//
//  Dependencies:
//    ENABLE_PAGE_COMMANDS  -> automatically 0 when ENABLE_SCHEDULER=0
//    ENABLE_PAGE_COMMANDS_INTERVAL -> automatically 0 when ENABLE_PAGE_COMMANDS=0
//      (datetime page /commands-by-datetime stays on ENABLE_PAGE_COMMANDS alone)
//    ENABLE_PAGE_FILES     -> automatically 0 when ENABLE_SDCARD=0 and ENABLE_FILESYSTEM=0
//    ENABLE_DUALFILE       -> enables Dual File Manager page (/dualfile) — requires SD or Flash
//
// #define ENABLE_PAGE_COMMANDS    0   // /commands + /commands-by-datetime (scheduled auto-commands)
// #define ENABLE_PAGE_COMMANDS_INTERVAL 0 // /commands only (interval auto-commands;
//                                         // datetime page stays when ENABLE_PAGE_COMMANDS=1)
// #define ENABLE_PAGE_RECIPIENTS  0   // /recipients (destination slots & auth)
// #define ENABLE_PAGE_ALERTS      0   // /alerts-config (inactivity alert thresholds)
// #define ENABLE_PAGE_FILES       0   // /files  (LittleFS / SD file explorer)
// #define ENABLE_DUALFILE         1   // /dualfile  (Norton-Commander-style dual panel file manager)
// #define ENABLE_PAGE_SDCARD      0   // /sdcard  (SD card & storage configuration)
// #define ENABLE_PAGE_MONITOR     0   // /monitor + /status  (data reception & live log)
// #define ENABLE_PAGE_GOOGLE_SCRIPT 1 // /google-script (Google App Script page under Networks)

// --- 2J - BUILD OPTIONS ---------------------------------------------------
  #define ENABLE_DEBUG_LOGS         0   // Verbose debug logging      (default: 1, keep 0 in production)
  #define ENABLE_API_REFERENCE_PAGE 0   // /api-reference page        (default: 1, saves flash when 0)
//  #define ENABLE_SENSOR_CONFIG_PAGE 1   // Sensor config web page     (default: 1)
  #define ENABLE_STRUCTURES_PAGE    0   // Amada ISQ20 welding page   (default: 1)
// #define ENABLE_OTA                1   // OTA firmware update        (default: 1)
// #define ENABLE_HTTP_BASIC_AUTH    0   // HTTP Basic Auth            (default: 1)
// #define ENABLE_ASYNC_WEBSOCKET    1   // WebSocket -- high RAM cost  (default: 0)
   #define ENABLE_TELNET             0   // Telnet debug console       (default: 1)
// #define ENABLE_SCHEDULER          1   // Command + time scheduler   (default: 1)
// #define ENABLE_SMTP               1   // TLS+SMTP email             (default: 1)
// #define ENABLE_GOOGLE_EMAIL       1   // Google Apps Script email   (default: 1)
// #define ENABLE_LPT_SERIAL_MONITOR 1   // Mirror LPT data to Serial  (default: 0, keep 0 in production)
// #define FILESYSTEM_PROVIDER       1   // 1=LittleFS  2=FFat  3=SPIFFS (default: 1)
// --- OLED screen type ---
// #define ENABLE_LCD_SCREEN         1   // Enable OLED screen         (default: 1 on N16R8)
// #define SCREEN_TYPE               1   // 1 = SSD1306 0.96" (3 lines, scroll)
//                                       // 2 = SSD1309 2.42" (6 lines, no scroll)
// =============================================================================
// ***  STOP HERE -- DO NOT EDIT BELOW THIS LINE  ***
// =============================================================================


// =============================================================================
// BOARD HARDWARE DEFAULTS -- auto-applied per board; #ifndef = user section wins.
// !!!  DO NOT EDIT THIS SECTION.
// =============================================================================

// -- Board C: Waveshare ESP32-P4-Module-DEV-KIT ------------------------------------
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
  #ifndef ENABLE_WIFI
    #define ENABLE_WIFI             1
  #endif
  #ifndef ENABLE_ETHERNET
    #define ENABLE_ETHERNET         1   // RMII internal MAC + IP101GRI PHY
  #endif
  #ifndef ENABLE_SDCARD
    #define ENABLE_SDCARD           1   // SDMMC4 4-bit
  #endif
  #ifndef ENABLE_RS232
    #define ENABLE_RS232            0
  #endif
  #ifndef ENABLE_LCD_SCREEN
    #define ENABLE_LCD_SCREEN       0   // no screen on P4 by default
  #endif
  #ifndef SCREEN_TYPE
    #define SCREEN_TYPE             1   // SSD1306 0.96"
  #endif
  #ifndef ENABLE_NTP
    #define ENABLE_NTP              1
  #endif

// EN-20260905-FSDUAL conclusion: ports 1-3 not wired to either USB controller.
// Only port 0 (OTG20 HS) usable. 0 = HS-only production (P4 packs override anyway).
  #ifndef USB_HOST_P4_OTG11_FS_HOST
    #define USB_HOST_P4_OTG11_FS_HOST  0
  #endif

// -- Board B: Waveshare ESP32-S3 POE-ETH-8DI-8DO -----------------------------------
#elif defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
  #ifndef ENABLE_WIFI
    #define ENABLE_WIFI             1
  #endif
  #ifndef ENABLE_ETHERNET
    #define ENABLE_ETHERNET         1   // W5500 via SPI
  #endif
  #ifndef ENABLE_SDCARD
    #define ENABLE_SDCARD           1
  #endif
  #ifndef ENABLE_RS232
    #define ENABLE_RS232            0
  #endif
  #ifndef ENABLE_LCD_SCREEN
    #define ENABLE_LCD_SCREEN       0   // OLED module is optional; enable in STEP 2 if fitted (SDA=42 SCL=41)
  #endif
  #ifndef SCREEN_TYPE
    #define SCREEN_TYPE             1
  #endif
  #ifndef ENABLE_NTP
    #define ENABLE_NTP              1
  #endif
  #ifndef BOARD_HAS_TCA9554_DO
    #define BOARD_HAS_TCA9554_DO    1   // 8DO TCA9554 expander onboard
  #endif
  #ifndef ENABLE_BUZZER
    #define ENABLE_BUZZER           1   // Onboard buzzer GPIO46
  #endif

// -- Board A: ESP32-S3 N16R8 devkit (default) --------------------------------------
#else
  #ifndef ENABLE_WIFI
    #define ENABLE_WIFI             1
  #endif
  #ifndef ENABLE_ETHERNET
    #define ENABLE_ETHERNET         0
  #endif
  #ifndef ENABLE_SDCARD
    #define ENABLE_SDCARD           0   // No SD slot on N16R8 devkit; PSRAM VFS is used as fallback storage
  #endif
  #ifndef ENABLE_RS232
    #define ENABLE_RS232            0
  #endif
  #ifndef ENABLE_LCD_SCREEN
    #define ENABLE_LCD_SCREEN       1   // No OLED on bare devkit; enable in STEP 2 if wired (SDA=8 SCL=9)
  #endif
  #ifndef SCREEN_TYPE
    #define SCREEN_TYPE             1
  #endif
  #ifndef ENABLE_NTP
    #define ENABLE_NTP              1
  #endif
#endif

// =============================================================================
// DEFAULTS -- applied only when the user has NOT set a flag in the section above.
// !!!  DO NOT EDIT THIS SECTION.
// =============================================================================

#include "src/config_defaults.h"

// ---------------------------------------------------------------------------
// Pack profile override (applied AFTER all defaults, so a plain #undef/#define
// in the pack file reliably overrides user + board + generic defaults).
// ---------------------------------------------------------------------------
#include "packs/pack_selector.h"

// =============================================================================
// CONFLICT RULES -- automatic dependency enforcement.
// !!!  DO NOT EDIT THIS SECTION.
// =============================================================================

// Sniffer uses UART2; force RS232 and DB25 off to avoid pin conflicts
#if ENABLE_SNIFFER==1
  #if ENABLE_RS232==1
    #undef  ENABLE_RS232
    #define ENABLE_RS232 0
    #warning "ENABLE_SNIFFER=1: RS232 disabled automatically (ENABLE_RS232 forced to 0)."
  #endif
  #if ENABLE_DB25==1
    #undef  ENABLE_DB25
    #define ENABLE_DB25 0
    #warning "ENABLE_SNIFFER=1: DB25/Nano module disabled automatically (ENABLE_DB25 forced to 0)."
  #endif
#endif

// USB/IP server depends on USB host support.
#if ENABLE_USBIP_SERVER==1 && ENABLE_USB_HOST==0
  #undef  ENABLE_USBIP_SERVER
  #define ENABLE_USBIP_SERVER 0
  #warning "ENABLE_USBIP_SERVER=1 requires ENABLE_USB_HOST=1. USBIP server disabled automatically."
#endif

// USB/IP server requires the dual-OTG HS controller (ESP32-P4 only).
// Plain USB Host (FS CDC-ACM) works on any target with OTG support (S3, P4).
#if (!defined(TARGET_BOARD_P4) || TARGET_BOARD_P4==0)
  #if ENABLE_USBIP_SERVER==1
    #undef  ENABLE_USBIP_SERVER
    #define ENABLE_USBIP_SERVER 0
    #warning "ENABLE_USBIP_SERVER is supported only on TARGET_BOARD_P4. Disabled automatically."
  #endif
#endif

// Nano reboot makes no sense without the DB25 module
#if ENABLE_DB25==0
  #if ENABLE_NANO_REBOOT_ON_BOOT==1
    #undef  ENABLE_NANO_REBOOT_ON_BOOT
    #define ENABLE_NANO_REBOOT_ON_BOOT 0
  #endif
#endif

// RS232 dependencies: when RS232 is disabled, XON/XOFF and charts must also be disabled
#if ENABLE_RS232==0
  #if defined(ENABLE_RS232_XONXOFF) && ENABLE_RS232_XONXOFF==1
    #undef  ENABLE_RS232_XONXOFF
    #define ENABLE_RS232_XONXOFF 0
  #endif
  #if defined(ENABLE_RS232_CHARTS) && ENABLE_RS232_CHARTS==1
    #undef  ENABLE_RS232_CHARTS
    #define ENABLE_RS232_CHARTS 0
  #endif
#endif

// RS232 charts persist to SD; the identical-path LittleFS mirror is still open
// (backlog §6.1). Serving already has a PSRAM equivalent (rs232LiveBuffer,
// PSRAM-first when it covers the range) — only the persisted flash history
// is missing, not the live path.
#if ENABLE_SDCARD==0
  #if defined(ENABLE_RS232_CHARTS) && ENABLE_RS232_CHARTS==1
    #undef  ENABLE_RS232_CHARTS
    #define ENABLE_RS232_CHARTS 0
  #endif
#endif

// Proximity charts are independent of RS232 — they record per-channel counts to SD.
// Always re-evaluate after packs (which may change ENABLE_PROXIMITY or ENABLE_SDCARD).
#undef  ENABLE_PROXIMITY_CHARTS
#if ENABLE_PROXIMITY==1 && ENABLE_SDCARD==1
  #define ENABLE_PROXIMITY_CHARTS 1
#else
  #define ENABLE_PROXIMITY_CHARTS 0
#endif

// Modbus RTU uses UART1 (GPIO40/41) -- same pins as DB25 and Sniffer
#if ENABLE_MODBUS_RTU==1 && ENABLE_DB25==1
  #error "ENABLE_MODBUS_RTU and ENABLE_DB25 cannot both be 1 -- both use UART1 (GPIO40/41)"
#endif
#if ENABLE_MODBUS_RTU==1 && ENABLE_SNIFFER==1
  #error "ENABLE_MODBUS_RTU and ENABLE_SNIFFER cannot both be 1 -- both use UART1 (GPIO40/41)"
#endif
// ENABLE_MODBUS acts as master enable; if all submodules (RTU, TCP, and Slave) are off,
// force master off too.  Slave-only builds (ENABLE_MODBUS_SLAVE=1, RTU+TCP=0) are valid
// and must not be silently disabled.
#if ENABLE_MODBUS==1 && ENABLE_MODBUS_RTU==0 && ENABLE_MODBUS_TCP==0
  #if ENABLE_MODBUS_SLAVE==0
    // No submodule is active -- disable the master guard as well.
    #undef  ENABLE_MODBUS
    #define ENABLE_MODBUS 0
  #else
    #warning "MODBUS: RTU and TCP are disabled; running in slave-only mode"
  #endif
#endif

// ENABLE_PAGE_COMMANDS depends on ENABLE_SCHEDULER
#if ENABLE_SCHEDULER==0 && ENABLE_PAGE_COMMANDS==1
  #undef  ENABLE_PAGE_COMMANDS
  #define ENABLE_PAGE_COMMANDS 0
  #warning "ENABLE_SCHEDULER=0: commands page disabled automatically (ENABLE_PAGE_COMMANDS forced to 0)."
#endif

// ENABLE_PAGE_COMMANDS_INTERVAL depends on ENABLE_PAGE_COMMANDS
#if ENABLE_PAGE_COMMANDS==0 && ENABLE_PAGE_COMMANDS_INTERVAL==1
  #undef  ENABLE_PAGE_COMMANDS_INTERVAL
  #define ENABLE_PAGE_COMMANDS_INTERVAL 0
  #warning "ENABLE_PAGE_COMMANDS=0: interval commands page disabled automatically (ENABLE_PAGE_COMMANDS_INTERVAL forced to 0)."
#endif

// ENABLE_PAGE_FILES: strip file explorer when there is no storage backend at all
#if ENABLE_SDCARD==0 && ENABLE_FILESYSTEM==0 && ENABLE_PAGE_FILES==1
  #undef  ENABLE_PAGE_FILES
  #define ENABLE_PAGE_FILES 0
#endif

// ---------------------------------------------------------------------------
// PSRAM dependency guards
// ---------------------------------------------------------------------------
// The firmware uses SpiRamJsonDocument, PsramAllocatorStd and PSRAM VFS
// extensively. All supported boards have PSRAM.  These guards catch
// misconfigured builds that enable PSRAM-dependent modules on boards
// without PSRAM support.

// Camera requires PSRAM >= 4 MB (esp-camera buffers).
#if ENABLE_CAMERA==1
  #if !defined(BOARD_HAS_PSRAM) || BOARD_HAS_PSRAM==0
    #error "ENABLE_CAMERA=1 requires PSRAM (BOARD_HAS_PSRAM=1). Enable PSRAM in menuconfig or select a board with PSRAM."
  #endif
#endif

// GRBL PSRAM upload requires PSRAM for the in-memory G-code buffer.
#if ENABLE_GRBL==1
  #if !defined(BOARD_HAS_PSRAM) || BOARD_HAS_PSRAM==0
    #warning "ENABLE_GRBL=1: PSRAM upload (/api/grbl/psram-upload) unavailable without PSRAM. SD-card streaming will still work."
  #endif
#endif

// USB/IP server uses large PSRAM buffers for USB packet forwarding.
#if ENABLE_USBIP_SERVER==1
  #if !defined(BOARD_HAS_PSRAM) || BOARD_HAS_PSRAM==0
    #error "ENABLE_USBIP_SERVER=1 requires PSRAM (BOARD_HAS_PSRAM=1)."
  #endif
#endif

// Sensor history / charts store 7-day ring buffers in PSRAM.
#if ENABLE_AHT20_BMP280==1 || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
  #if !defined(BOARD_HAS_PSRAM) || BOARD_HAS_PSRAM==0
    #warning "Sensor history ring buffers are allocated in PSRAM. Without PSRAM the ring buffer uses internal DRAM (smaller capacity)."
  #endif
#endif

// ---------------------------------------------------------------------------
// Build-time pack integrity validation
// ---------------------------------------------------------------------------
#include "packs/pack_validator.h"

#endif // USER_CONFIG_H_ROOT
