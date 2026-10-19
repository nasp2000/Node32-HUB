#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

// Unified RAMBOX page (selector: Unikon / Bricon / future clocks).
void registerRamboxRoutes(AsyncWebServer& server, const WebRouteContext& context);
