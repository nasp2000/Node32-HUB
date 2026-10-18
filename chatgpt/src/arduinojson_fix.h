// =============================================================
// arduinojson_fix.h — Temporary fix for ArduinoJson + GCC 12.2.0
// =============================================================
// Problem: GCC 12.2.0 for ESP32 has a conflict with std::string::append
// used by StdStringWriter in ArduinoJson.
// Solution: Include this header BEFORE any ArduinoJson include to disable
// std::string support in serialization.
// Context: https://github.com/bblanchon/ArduinoJson/issues/1814
// =============================================================

#ifndef ARDUINOJSON_FIX_H
#define ARDUINOJSON_FIX_H

// Disable std::string support in ArduinoJson serialization.
// Guard the defines so files that already included ArduinoJson do not emit
// macro redefinition warnings during preprocessing.
#ifndef ARDUINOJSON_ENABLE_STD_STRING
#define ARDUINOJSON_ENABLE_STD_STRING 0
#endif

#ifndef ARDUINOJSON_ENABLE_STD_STREAM
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#endif

#endif // ARDUINOJSON_FIX_H
