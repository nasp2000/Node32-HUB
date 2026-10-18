// =============================================================
// 🔑 json_keys.h — JSON key literals in PROGMEM (heap‑friendly)
// =============================================================
// ✅ Purpose: avoid multiple copies of the same strings in RAM,
//    using FPSTR(K_...) to access ArduinoJson keys.
// ⚠️ Use only for keys heavily repeated in hot paths.

#pragma once
#include <Arduino.h>   // PROGMEM / FPSTR

// Common slots and fields
static const char K_COMMAND[]        PROGMEM = "command";
static const char K_INTERVAL[]       PROGMEM = "interval";
static const char K_UNIT[]           PROGMEM = "unit";
static const char K_DAYS[]           PROGMEM = "days";
static const char K_TEMPLATE[]       PROGMEM = "template";
static const char K_ENABLED[]        PROGMEM = "enabled";

// sched_dt fields
static const char K_DATE[]           PROGMEM = "date";
static const char K_TIME[]           PROGMEM = "time";

// Miscellaneous frequently used fields
static const char K_NAME[]           PROGMEM = "name";
static const char K_BAUDRATE_INDEX[] PROGMEM = "baudrate_index";
static const char K_DATA_BITS[]      PROGMEM = "data_bits";
static const char K_PARITY[]         PROGMEM = "parity";
static const char K_STOP_BITS[]      PROGMEM = "stop_bits";
static const char K_INVERT_PINS[]    PROGMEM = "invert_pins";
static const char K_READ_TIMEOUT_MS[]PROGMEM = "read_timeout_ms";
static const char K_USE_XON_XOFF[]   PROGMEM = "use_xon_xoff";
