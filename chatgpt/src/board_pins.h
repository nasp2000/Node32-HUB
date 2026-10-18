// board_pins.h — Pin overrides per target board.
// Included by user_config.h before module defaults take effect.
// All defines here override the #ifndef defaults in each module header.
//
// Supported boards:
//   TARGET_BOARD_8DI8DO_POE = 1  ->  Waveshare ESP32-S3-POE-ETH-8DI-8DO industrial board
//
// Default (nothing defined) = N16R8 devkit — no changes applied.

#pragma once

// ---------------------------------------------------------------------------
// Waveshare ESP32-S3 POE-ETH-8DI-8DO
// ---------------------------------------------------------------------------
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE == 1

// ---- Pin definitions for Waveshare ESP32-S3-POE-ETH-8DI-8DO ----
// Feature overrides (ENABLE_*, PROXIMITY_CHANNELS, etc.) are in pack_waveshare.h.

// ---- W5500 Ethernet SPI pins ----
#define ETH_SPI_MOSI_PIN          13
#define ETH_SPI_MISO_PIN          14
#define ETH_SPI_SCLK_PIN          15
#define ETH_CS_PIN                16
#define ETH_RST_PIN               39
#define ETH_INT_PIN               12

// ---- Modbus RTU / RS485 (UART1) ----
// dePin = -1 means auto-direction module (no explicit DE/RE pin needed).
#define GPIO_MB_TX                17
#define GPIO_MB_RX                18

// ---- I2C bus (shared: RTC DS3231 + TCA9554 expander) ----
#define LCD_SDA                   42
#define LCD_SCL                   41
#define SENSOR_I2C0_SDA           42
#define SENSOR_I2C0_SCL           41

// ---- Digital inputs: proximity manager default GPIOs for all 8 channels ----
// The user can still override individual channels via the web UI after first boot.
#define PROXIMITY_GPIO_CH0         4
#define PROXIMITY_GPIO_CH1         5
#define PROXIMITY_GPIO_CH2         6
#define PROXIMITY_GPIO_CH3         7
#define PROXIMITY_GPIO_CH4         8
#define PROXIMITY_GPIO_CH5         9
#define PROXIMITY_GPIO_CH6        10
#define PROXIMITY_GPIO_CH7        11

// ---- Digital outputs: relays are on TCA9554 I2C expander (not direct GPIO) ----
// Set gpio_out = 255 (disabled) on all proximity channels.
// Relay control is handled separately by the tca9554_do module.
#define BOARD_HAS_TCA9554_DO       1
#define TCA9554_I2C_ADDR        0x20

// ---- RTC DS3231 (onboard, I2C 0x68, same bus as TCA9554) ----
// Provides accurate timekeeping when NTP is unavailable (e.g. no network at boot).
// NTP syncs are written back to the DS3231 to keep it accurate over time.
#define BOARD_HAS_DS3231           1

// ---- Onboard active buzzer (GPIO46) ----
// GPIO46 is a strapping pin on ESP32-S3 (safe to use as output after boot).
// Buzzer disabled in pack_waveshare.h; BUZZER_PIN kept for pin reference.
#define BUZZER_PIN                46

// ---- SD card: native SDMMC 1-bit via onboard TF slot ----
#define SDCARD_CLK_PIN         48   // SDMMC CLK
#define SDCARD_CMD_PIN         47   // SDMMC CMD
#define SDCARD_D0_PIN          45   // SDMMC D0 (1-bit mode)
// SDCARD_USE_1BIT_MODE defaults to true in sdcard.h — explicit override not needed.
// GPIO21 is DAT3/CS: drive HIGH before SDMMC init so the card selects SD mode.
#define SDCARD_DAT3_HIGHPIN    21

// ---- RS232 — UART2 routed to expansion header RXD/TXD pins ----
// Connect a MAX3232 (3.3V) between these pins and the DB9/RS232 device.
// UART0 (GPIO43/44) is free because Serial = USB CDC when CDC_ON_BOOT=1.
// Override with #define RS232_DEFAULT_RX_PIN / RS232_DEFAULT_TX_PIN in user_config.h
// BEFORE the #include "src/board_pins.h" line if your board uses different pins.
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN      44   // Header "RXD" screw terminal
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN      43   // Header "TXD" screw terminal
#endif

// ---- MQ-135 gas sensor (analog ADC) ----
// GPIO1/2/3 are the only free ADC-capable pins on this board (ADC1, 12-bit, 0-3.3V).
// GPIO4-11 (DI inputs) go through optocouplers — NOT suitable for analog reading.
// GPIO40 has no ADC on ESP32-S3. Default: GPIO2 (ADC1_CH1).
// WARNING: MQ135 AOUT can exceed 3.3V when powered at 5V.
//   Use a voltage divider: R1=10kOhm + R2=13kOhm -> Vout = 5V x 13/23 = 2.83V safe.
#define SENSOR_MQ135_PIN          2    // GPIO2 — ADC1_CH1 (GPIO5 conflicts with DI2 optocoupler!)

// ---- CAN bus (TWAI hardware, requires external SN65HVD230 / MCP2551 transceiver) ----
// GPIO1 and GPIO3 are the only remaining free GPIOs on this board that are not
// assigned to Ethernet SPI, RS485, I2C, SDMMC, RS232, DI inputs, buzzer, or MQ135.
// Connect: ESP32 GPIO1 -> TXD on transceiver, GPIO3 -> RXD on transceiver.
// Transceiver CANH/CANL go to the CAN bus wires; power transceiver at 3.3V.
#define CAN_TX_PIN                 1   // GPIO1  — to transceiver TXD
#define CAN_RX_PIN                 3   // GPIO3  — from transceiver RXD
// Enable CAN in STEP 2 of user_config.h when an external transceiver is connected.

// ---- HX711 load cell / weight sensor (bit-bang DATA + CLK) ----
// GPIO19 and GPIO20 are the last free GPIOs on this board.
// If CAN is enabled (GPIO1/3) these remain free; use them for HX711.
// HX711 and MAX31865 both use these pins -> they are mutually exclusive.
#define SENSOR_HX711_DATA_PIN     19   // GPIO19 — HX711 DOUT
#define SENSOR_HX711_CLK_PIN      20   // GPIO20 — HX711 PD_SCK

// ---- MAX31865 RTD sensor (SPI, shared bus with W5500, dedicated CS) ----
// CS uses GPIO20; SPI MOSI/MISO/CLK shared with W5500 (GPIO13/14/15).
// MAX31865 and HX711 share GPIO19/20 -> enable only one at a time.
#define SENSOR_MAX31865_CS_PIN    20   // GPIO20 — MAX31865 CS (chip select)
#define SENSOR_MAX31865_MOSI_PIN  13   // GPIO13 — shared with W5500 SPI MOSI
#define SENSOR_MAX31865_MISO_PIN  14   // GPIO14 — shared with W5500 SPI MISO
#define SENSOR_MAX31865_CLK_PIN   15   // GPIO15 — shared with W5500 SPI CLK

// ---- Sound sensor (analog ADC) — shares GPIO2 with MQ135 ----
// Only enable ENABLE_SOUND_SENSOR or ENABLE_MQ135, not both simultaneously.
#define SENSOR_SOUND_PIN           2   // GPIO2 — same ADC pin as MQ135 (mutually exclusive)

// --- USB CDC (serial output) ---
// This board has NO UART bridge chip. Serial output ONLY works via native USB CDC.
// UART0 default pins (GPIO43 TX / GPIO44 RX) are reused for RS232 on this board,
// so Serial MUST be USB CDC to avoid a hardware conflict that silences all output.
//
// Arduino IDE:  Tools -> USB CDC On Boot -> Enabled   (recommended)
// PlatformIO:   use 'waveshare_8di8do' env (sets CDC=1 automatically)
//
// IMPORTANT: ARDUINO_USB_CDC_ON_BOOT must come from the compiler -D flag (via
// boards.local.txt read at IDE startup). It must NOT be forced here in a header
// because core.a and the sketch would then be compiled with different values,
// causing an 'undefined reference to USBSerial' linker error.
//
// If compilation fails with 'undefined reference to USBSerial':
//   -> CLOSE and REOPEN the Arduino IDE so boards.local.txt is re-read into cache.
//   -> The file already contains -DARDUINO_USB_CDC_ON_BOOT=1 in extra_flags.

#endif // TARGET_BOARD_8DI8DO_POE

// ---------------------------------------------------------------------------
// Waveshare ESP32-P4-Module-DEV-KIT
// ---------------------------------------------------------------------------
// Reference: vendor ESP32-P4 Module DEV KIT user guide, sections Start Page / Hardware Interface.
// Schematic: vendor ESP32-P4 Module DEV KIT schematic, sections Pin Header / Power Supply.
//
// Key differences from ESP32-S3:
//   - WiFi via ESP32-C6 SDIO slave (transparent via arduino-esp32 >= 3.2.0)
//   - Ethernet: internal RMII MAC + external IP101GRI PHY (no W5500)
//   - 32 MB PSRAM stacked internally (no cache-fix flags needed)
//   - SD SDMMC 4-wire SDIO3.0 on high GPIO range (39-44)
//   - I2S audio codec ES8311 onboard (GPIO9-13)
//
// IMPORTANT: RS232/Modbus/free GPIO assignments below are TENTATIVE.
//   Verify against the physical pinout diagram before connecting hardware.
//   See Docs/esp32_p4_port_plan.md for the full migration plan.
// ---------------------------------------------------------------------------
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1

// ---- Ethernet: internal RMII MAC + IP101GRI PHY ----
// All RMII pins are fixed by the board schematic — do not change.
#undef  ENABLE_ETHERNET
#define ENABLE_ETHERNET           1
#define ENABLE_ETHERNET_RMII      1        // Select RMII path (vs W5500 SPI)
#define ETH_RMII_TXD0_PIN        34
#define ETH_RMII_TXD1_PIN        35
#define ETH_RMII_RXD0_PIN        30
#define ETH_RMII_RXD1_PIN        29
#define ETH_RMII_TX_EN_PIN       49
#define ETH_RMII_CRS_DV_PIN      28
#define ETH_RMII_REF_CLK_PIN     50        // 50 MHz from 25 MHz crystal x2 via PHY
#define ETH_RMII_MDC_PIN         31
#define ETH_RMII_MDIO_PIN        52
#define ETH_RMII_PHY_RESET_PIN   51
#define ETH_RMII_PHY_ADDR         1        // IP101GRI default PHY address

// ---- SD card: native SDMMC 4-wire SDIO3.0 ----
#undef  ENABLE_SDCARD
#define ENABLE_SDCARD             1
// 4-bit mode: D0-D3 all wired on this board — use full bandwidth.
#undef  SDCARD_USE_1BIT_MODE
#define SDCARD_USE_1BIT_MODE      false
#define SDCARD_CLK_PIN           43
#define SDCARD_CMD_PIN           44
#define SDCARD_D0_PIN            39
#define SDCARD_D1_PIN            40
#define SDCARD_D2_PIN            41
#define SDCARD_D3_PIN            42

// ---- I2C header (onboard connector SH1.0 4P) ----
#define LCD_SDA                   7
#define LCD_SCL                   8
#define SENSOR_I2C0_SDA           7
#define SENSOR_I2C0_SCL           8

// ---- I2S audio (ES8311 codec + NS4150B amplifier — onboard) ----
// These pins are NOT routable to other functions when the codec is in use.
// I2S is reserved for future audio features; not used by this firmware yet.
#define P4_I2S_MCLK_PIN          13
#define P4_I2S_SCLK_PIN          12
#define P4_I2S_ASDOUT_PIN        11
#define P4_I2S_LRCK_PIN          10
#define P4_I2S_DSDIN_PIN          9
#define P4_I2S_PA_CTRL_PIN       53        // Power amplifier enable (active high)

// ---- RS232 — UART2 on free header pins (TENTATIVE) ----
// GPIO14 and GPIO15 are candidate free GPIOs on the 40-pin header.
// Verify against the board pinout image before wiring hardware.
// Override by defining RS232_DEFAULT_RX_PIN / TX_PIN before #include board_pins.h.
// IMPORTANT: GPIO14-19 are used by the C6 SDIO internal bus (WiFi co-processor).
// Using any of GPIO14-19 for UART or other peripherals causes H_SDIO_DRV failures
// and a crash loop. Use GPIO20+ for all user-facing UARTs.
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN     20        // GPIO20 — free on 40-pin header
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN     21        // GPIO21 — free on 40-pin header
#endif

// ---- Modbus RTU UART1 ----
// GPIO22/23 are free on the 40-pin header.
// Must not conflict with RMII (GPIO28-35, 49-52), SD (GPIO39-44), or C6 SDIO (GPIO14-19).
#define GPIO_MB_TX               22        // GPIO22 — free on 40-pin header
#define GPIO_MB_RX               23        // GPIO23 — free on 40-pin header

// ---- WiFi: ESP32-C6 companion chip via SDIO ----
// No pin defines needed here — the C6 SDIO link is internal to the P4 module.
// arduino-esp32 >= 3.2.0 routes WiFi.h calls transparently through esp_wifi_remote.
// If WiFi does not initialise, add esp_hosted + esp_wifi_remote to lib_deps.
#define WIFI_USES_C6_SDIO         1        // Documents the architecture; used in network_hybrid_manager

// Feature flags for P4 are set by user_config.h (ENABLE_RS232=0 and ENABLE_LCD_SCREEN=0
// by default). Do NOT set them here — board_pins.h is included before the user_config.h
// defaults block, so any #undef/#define here would override user choices silently.
// To enable RS232 on P4: add '#define ENABLE_RS232 1' in the STEP 2 section of user_config.h.
// RS232 pins (GPIO20=RX, GPIO21=TX) are defined above and are safe (no C6 SDIO conflict).
//
// To enable Unikon RAMBOX: add '#define ENABLE_UNIKON 1' in user_config.h.
// Cable: 3-wire crossed (MAX3232 pin3->RAMBOX pin2, MAX3232 pin2->RAMBOX pin3, GND->GND)
// + internal jumper pin9->pin5(GND) inside the RAMBOX-side DB9 connector.
// Pins 4, 6, 7, 8 must be left disconnected. No RTS GPIO needed.

// ---- Camera (MIPI CSI-2 via ESP32-P4 hardware lanes) ----
// The ESP32-P4 has a hardware MIPI CSI-2 controller. DVP-style data pins (D0-D7,
// VSYNC, HREF, PCLK) are set to -1 because the driver uses internal silicon lanes.
// Only SCCB I2C (shared bus) and XCLK require user-facing GPIOs.
// Requires esp-camera >= 2.0.13 with P4 MIPI CSI support.
// GPIO27 is free on the 40-pin header and PWM-capable (good for XCLK).
// CAUTION: verify against physical camera connector before enabling.
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    27          // free GPIO on 40-pin header, PWM-capable
#define CAM_PIN_SIOD     7          // I2C SDA — shared with LCD/RTC bus
#define CAM_PIN_SIOC     8          // I2C SCL — shared with LCD/RTC bus
#define CAM_PIN_D7      -1          // MIPI CSI-2: internal lanes, not GPIO-mapped
#define CAM_PIN_D6      -1
#define CAM_PIN_D5      -1
#define CAM_PIN_D4      -1
#define CAM_PIN_D3      -1
#define CAM_PIN_D2      -1
#define CAM_PIN_D1      -1
#define CAM_PIN_D0      -1
#define CAM_PIN_VSYNC   -1
#define CAM_PIN_HREF    -1
#define CAM_PIN_PCLK    -1
#define CAM_PIN_LAMP    -1
#define CAM_IS_MIPI_CSI  1          // tells camera_manager.cpp to use MIPI init path

#endif // TARGET_BOARD_P4

// ---------------------------------------------------------------------------
// ESP32-S3 with onboard DVP camera connector + onboard SD + 8 MB PSRAM
// ---------------------------------------------------------------------------
// Reference pinout: GPIO silkscreen labels CAM_SIOD/SIOC/VSYNC/HREF/XCLK/Y2-Y9/PCLK.
// SD card uses SDMMC 1-bit (CMD=38, CLK=39, D0=40) per PCB silkscreen.
// PSRAM on GPIO35/36/37 (handled by hardware — no firmware defines needed).
// GPIO16/17/18 are consumed by the camera (Y9/Y8/Y7): do NOT use for RS232 or UART2.
// RS232 must use GPIO43/44 (UART0 repurposed; safe when Serial = USB CDC).
// ---------------------------------------------------------------------------
#if defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1

// ---- Camera DVP interface — all labels visible on PCB silkscreen ----
#define CAM_PIN_PWDN    -1          // no power-down pin wired
#define CAM_PIN_RESET   -1          // no reset pin wired
#define CAM_PIN_XCLK    15          // CAM_XCLK
#define CAM_PIN_SIOD     4          // CAM_SIOD (I2C SDA)
#define CAM_PIN_SIOC     5          // CAM_SIOC (I2C SCL)
#define CAM_PIN_D7      16          // CAM_Y9
#define CAM_PIN_D6      17          // CAM_Y8
#define CAM_PIN_D5      18          // CAM_Y7
#define CAM_PIN_D4      12          // CAM_Y6
#define CAM_PIN_D3      10          // CAM_Y5
#define CAM_PIN_D2       8          // CAM_Y4
#define CAM_PIN_D1       9          // CAM_Y3
#define CAM_PIN_D0      11          // CAM_Y2
#define CAM_PIN_VSYNC    6          // CAM_VSYNC
#define CAM_PIN_HREF     7          // CAM_HREF
#define CAM_PIN_PCLK    13          // CAM_PCLK
#define CAM_PIN_LAMP    -1          // no dedicated lamp; GPIO48 = WS2812 (not a PWM lamp)

// ---- SD card — 1-bit SDMMC (per PCB silkscreen: CMD=38, CLK=39, D0=40) ----
// GPIO38-40 overlap with JTAG (MTDI/MTCK/MTDO) but JTAG is not used at runtime.
#undef  ENABLE_SDCARD
#define ENABLE_SDCARD             1
#define SDCARD_CLK_PIN           39  // SD_CLK
#define SDCARD_CMD_PIN           38  // SD_CMD
#define SDCARD_D0_PIN            40  // SD_DATA (1-bit mode)

// ---- RS232 on UART0 pins (GPIO16=RX is taken by CAM_Y9; use GPIO43/44 instead) ----
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN     44  // U0RXD — safe when Serial = USB CDC
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN     43  // U0TXD — safe when Serial = USB CDC
#endif

#endif // TARGET_BOARD_S3_CAM

// ---------------------------------------------------------------------------
// Waveshare ESP32-S3-Touch-LCD-7 (TouchMeteo-7 pack)
// ---------------------------------------------------------------------------
// Reference: vendor ESP32-S3 Touch LCD 7 user guide, sections Hardware Interface / Pinout.
// S3N16R8 240 MHz, 16 MB Flash, 8 MB PSRAM. LCD RGB 7" 800x480 + touch,
// CH422G expander, TF (SPI), RS485, native USB (CDC). Bench detail and wiring
// diagram in docs/waveshareLCDTouch7.md §2/§9.
//
// Taken by the LCD: 0,1,2,3,5,7,10,14,17,18,21,38,39,40,41,42,45,46,47,48.
// Do NOT reuse any of them for sensors/audio/PIR.
// ---------------------------------------------------------------------------
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1

// ---- Shared I2C (touch + CH422G + ambient sensors) ----
// Arduino uses Wire1 (port 1) for ALL sensors; I2C0 (Wire, port 0)
// belongs to the panel stack (touch GT911 + CH422G) and is never started by Arduino.
#define LCD_SDA                   8
#define LCD_SCL                   9
#define SENSOR_I2C0_SDA           8
#define SENSOR_I2C0_SCL           9

// ---- RTC DS3231 (external module on I2C header, addr 0x68) ----
// NTP writes back; rtc_ds3231.cpp uses Wire1 on this board (see RTC_WIRE).
#define BOARD_HAS_DS3231           1

// ---- TF card: SPI via onboard slot (CS on CH422G expander EXIO4) ----
// The 1-bit SDMMC path of the other boards does NOT apply here: the slot is SPI
// (MOSI 11 / SCK 12 / MISO 13) and CS is CH422G EXIO4 (active LOW,
// driven via the Board expander handle — never create a 2nd CH422G).
// The generic backend is SPI (SD.h); SDCARD_SPI_CS_PIN=-1 flags external CS
// (the Arduino lib expects a GPIO; the card is selected via EXIO4 LOW).
#define SDCARD_USE_SPI_MODE      1
#define SDCARD_SPI_MOSI_PIN      11  // SPI MOSI
#define SDCARD_SPI_MISO_PIN      13  // SPI MISO
#define SDCARD_SPI_SCLK_PIN      12  // SPI SCK
#define SDCARD_SPI_CS_PIN        -1  // external: CH422G EXIO4 (see TOUCH7_SD_CS_EXIO)
#define TOUCH7_SD_CS_EXIO        4   // EXIO4 = SD_CS (active LOW)
#define TOUCH7_SD_SPI_FREQ       8000000
#define SDCARD_CLK_PIN           12  // SDMMC legacy (unused on this board)
#define SDCARD_CMD_PIN           11  // SDMMC legacy (unused on this board)
#define SDCARD_D0_PIN            13  // SDMMC legacy (unused on this board)
#define SDCARD_DAT3_HIGHPIN      -1  // no DAT3; CS via CH422G EXIO4

// ---- RS232 — off in this pack (pins 15/16 reserved for future) ----
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN     16
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN     15
#endif

// ---- Modbus RTU UART1 (reserved, off in this pack) ----
#define GPIO_MB_TX               15
#define GPIO_MB_RX               16

// ---- Local audio: 1-pin PWM -> PAM8403 -> 4 ohm speaker (F2) ----
#define TOUCH7_AUDIO_PWM_PIN      6   // GPIO6, only obvious free pin off RGB

// ---- PIR presence: GPIO to validate on schematic (6 taken by audio) ----
// Provisional value -1 = to be defined on F0 hardware.
#define TOUCH7_PIR_PIN           -1

// ---- USB CDC (serial output) ---
// No UART bridge: Serial ONLY via native USB CDC (GPIO19/20).
// PlatformIO: use env 's3_touch7' (automatic CDC=1). CAN OFF (EXIO5=USB).

#endif // TARGET_BOARD_S3_TOUCH7

// ---------------------------------------------------------------------------
// Default board: ESP32-S3 N16R8 devkit (no TARGET_BOARD_* defined)
// ---------------------------------------------------------------------------
#if !defined(TARGET_BOARD_8DI8DO_POE) && !defined(TARGET_BOARD_P4) && !defined(TARGET_BOARD_S3_CAM) && !defined(TARGET_BOARD_S3_TOUCH7)

// ---- RS232 — UART2 on GPIO16/17 (standard UART2 pins on N16R8 devkit) ----
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN      16   // UART2 RX — connect to MAX3232 R1OUT
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN      17   // UART2 TX — connect to MAX3232 T1IN
#endif

#endif // !TARGET_BOARD_8DI8DO_POE && !TARGET_BOARD_P4 && !TARGET_BOARD_S3_TOUCH7
