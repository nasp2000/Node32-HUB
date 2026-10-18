#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

// Register all Modbus web routes:
//   HTML pages: /modbus-rtu (ENABLE_MODBUS_RTU), /modbus-tcp (ENABLE_MODBUS_TCP), /modbus-live
//   REST API:   /api/modbus/config, /api/modbus/live, /api/modbus/poll-now, /api/modbus/scan-tcp, /api/modbus/scan-status
void registerModbusRoutes(AsyncWebServer& server, const WebRouteContext& context);
