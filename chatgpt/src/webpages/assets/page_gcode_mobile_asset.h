#pragma once

#include <pgmspace.h>

// CNC mobile wizard. The page is streamed in two halves so the shared System
// screen (mobile_system_asset.h) can be inserted between them:
//   PAGE_GCODE_MOBILE       CSS + header/strip + screens 1..5
//   MOBILE_SYSTEM_HTML      shared System screen (summary/OTA/log/restart)
//   PAGE_GCODE_MOBILE_TAIL  message bar, progress, primary action, nav, script
extern const char PAGE_GCODE_MOBILE[];
extern const char PAGE_GCODE_MOBILE_TAIL[];
