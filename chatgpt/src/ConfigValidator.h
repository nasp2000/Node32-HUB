#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace ConfigValidator {

// Validate that a JSON key contains only safe characters [a-zA-Z0-9 _ - .]
// and is between 1 and 64 characters.
bool isSafeConfigJsonKey(const char* key);

// Deep-copy a JSON value with bounds checking:
// - Max nesting depth 10
// - Max string length 4096
// - Skips unsafe keys in objects
void copyJsonSanitized(JsonVariantConst src, JsonVariant dst, int depth = 0);

} // namespace ConfigValidator
