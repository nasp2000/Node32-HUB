#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

size_t streamSensorChartsPageToPrint(Print& out);

// Send the charts page using beginResponse_P — streams from PROGMEM without
// allocating a 97KB heap buffer. Use this instead of AsyncResponseStream.
size_t sendSensorChartsPageResponse(AsyncWebServerRequest* request);
