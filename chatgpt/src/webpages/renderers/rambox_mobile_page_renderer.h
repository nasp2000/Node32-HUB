#pragma once

#include <Arduino.h>

// Streams the Rambox32 /m phone page (Status + Arrivals + Command + System).
size_t streamRamboxMobilePageToPrint(Print& out);
