#pragma once
#include <Arduino.h>

// Falas page renderer: shell + FALAS_PAGE asset + footer.
size_t streamFalasPageToPrint(Print& out);
