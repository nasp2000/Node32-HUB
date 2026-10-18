#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

// Register all proximity sensor web routes:
//   HTML page: /proximity
//   REST API:  /api/proximity, /api/proximity/reset, /api/proximity/reset-all
void registerProximityRoutes(AsyncWebServer& server, const WebRouteContext& context);
