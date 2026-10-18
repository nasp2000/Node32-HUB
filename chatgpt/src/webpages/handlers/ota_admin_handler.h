#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register OTA admin API routes.
void registerOtaAdminRoutes(AsyncWebServer& server, const WebRouteContext& context);