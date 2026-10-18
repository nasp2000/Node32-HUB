#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerIpgLaserRoutes(AsyncWebServer& server, const WebRouteContext& context);