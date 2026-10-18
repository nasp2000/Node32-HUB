#pragma once
#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

// Registers GET /networks and all /api/networks-* endpoints.
void registerNetworksRoutes(AsyncWebServer& server, const WebRouteContext& context);
