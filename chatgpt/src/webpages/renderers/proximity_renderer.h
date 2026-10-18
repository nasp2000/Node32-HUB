#pragma once

#include <Arduino.h>

// Stream the /proximity HTML page to the given Print sink.
// Returns the number of bytes written.
size_t streamProximityPageToPrint(Print& out);
