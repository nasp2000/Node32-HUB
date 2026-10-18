#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register lightweight command endpoints used by the web monitor.
void registerCommandApiRoutes(AsyncWebServer& server, const WebRouteContext& context);