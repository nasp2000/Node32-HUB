#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

// TouchMeteo-7 routes (PACK_S3_TOUCH7 only):
//   HTML pages: /falas (local WAV phrases + peers quick config), /ecra (pixel mirror)
//   REST API:  /api/bh1750-config (GET/POST), /api/bh1750-measure (POST),
//              /api/touch7-config (GET/POST), /api/touch7-peer-refresh (POST),
//              /api/touch7-weather-refresh (POST), /api/touch7-frame (GET),
//              /api/falas (GET), /api/falas-play + /api/falas-delete (POST)
void registerTouch7Routes(AsyncWebServer& server, const WebRouteContext& context);
