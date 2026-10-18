// can_handlers.h
// HTTP/API handlers for the CAN bus (TWAI) page.
//
// Routes registered:
//   GET  /api/can/status  — live bus stats (state, TEC/REC, counters)
//   GET  /api/can/frames  — last N frames from PSRAM ring buffer
//   POST /api/can/send    — transmit one CAN frame (JSON body)
//   POST /api/can/clear   — clear the PSRAM frame buffer
//   GET  /api/can/config  — active config as JSON
//   POST /api/can/config  — save new config + restart driver
//   POST /api/can/restart — restart driver with current config

#pragma once
#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

void registerCanApiRoutes(AsyncWebServer& server, const WebRouteContext& context);
