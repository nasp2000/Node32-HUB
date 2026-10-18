#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

void registerIpgLaserApiRoutes(AsyncWebServer& server, const WebRouteContext& context);