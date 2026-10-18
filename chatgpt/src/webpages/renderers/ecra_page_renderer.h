#pragma once
#include <Arduino.h>

// Ecra page renderer: shell + ECRA_PAGE asset + footer.
size_t streamEcraPageToPrint(Print& out);
