#pragma once

#include <Arduino.h>

// Mobile G-code sender, wizard step 1 ("Send G-code"). Separate from the PC
// controller page on purpose: /grbl-controller must stay untouched.
size_t streamGcodeMobilePageToPrint(Print& out);
