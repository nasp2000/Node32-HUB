// web_routes_can.h
// Route registration for CAN bus (TWAI) pages and API.

#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

void registerCanRoutes(AsyncWebServer& server, const WebRouteContext& context);
