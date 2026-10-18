// sd_lib.h — SD backend selector.
// Include this instead of <SD_MMC.h> in all SD-using source files.
//
// SDCARD_USE_SPI_MODE == 0 (default): native SDMMC driver (<SD_MMC.h>)
//   All code uses SD_MMC.open/exists/mkdir/... directly — no aliasing.
//
// SDCARD_USE_SPI_MODE == 1 (generic SPI boards): SPI-based SD driver (<SD.h>)
//   SD_MMC.h is deliberately NOT included so that the macro #define SD_MMC SD
//   can safely alias the SPI SD object without triggering a type-conflict.
//   Arduino auto-scans #include directives, so both libraries end up in the
//   build path; the key is that SD_MMC.h must never be included in the same
//   translation unit as #define SD_MMC SD (they declare different types).
//
// IMPORTANT: Never add #define SD SD_MMC or #define SD_MMC SD aliases here for
// MMC mode — Arduino IDE auto-includes both SD.h and SD_MMC.h, and macro
// aliasing between two differently-typed extern globals causes a hard type
// conflict.  Files that used SD.xxx() with the old alias must use SD_MMC.xxx()
// directly in MMC mode.
#pragma once

#include "feature_flags.h" // Pulls in user_config.h -> board_pins.h -> SDCARD_USE_SPI_MODE

#ifndef SDCARD_USE_SPI_MODE
#define SDCARD_USE_SPI_MODE 0
#endif

#if SDCARD_USE_SPI_MODE == 1

// --- SPI-based SD backend (Waveshare 8DI8DO onboard slot) ---
// SD_MMC.h is intentionally excluded here; if it were included alongside
// SD.h and #define SD_MMC SD, their extern declarations would conflict.

// Pin defaults — override in board_pins.h for the target board.
#ifndef SDCARD_SPI_CS_PIN
#define SDCARD_SPI_CS_PIN    5
#endif
#ifndef SDCARD_SPI_MISO_PIN
#define SDCARD_SPI_MISO_PIN 19
#endif
#ifndef SDCARD_SPI_MOSI_PIN
#define SDCARD_SPI_MOSI_PIN 23
#endif
#ifndef SDCARD_SPI_SCLK_PIN
#define SDCARD_SPI_SCLK_PIN 18
#endif

#include <SD.h>
#include <SPI.h>
// Make all existing SD_MMC.open/exists/mkdir/cardSize/... calls use the SPI SD object.
// Safe here because SD_MMC.h was never included, so there is no extern fs::SDMMCFS SD_MMC
// declaration that would conflict with the expansion of fs::SDFS SD -> SD_MMC.
#define SD_MMC SD

#else

// --- Native SDMMC backend (default, e.g. N16R8 dev board) ---
#include <SD_MMC.h>
// No aliasing. All code uses SD_MMC.xxx() directly.
// Do NOT define #define SD SD_MMC here: Arduino IDE adds both SD and SD_MMC
// libraries to the build, so SD.h (extern fs::SDFS SD) is available in TUs;
// macro-aliasing would expand that extern declaration to fs::SDFS SD_MMC which
// conflicts with fs::SDMMCFS SD_MMC from SD_MMC.h.

#endif // SDCARD_USE_SPI_MODE
