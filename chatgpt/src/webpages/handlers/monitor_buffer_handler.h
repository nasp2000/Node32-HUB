#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register monitor buffer/status APIs.
void registerMonitorBufferRoutes(AsyncWebServer& server, const WebRouteContext& context);