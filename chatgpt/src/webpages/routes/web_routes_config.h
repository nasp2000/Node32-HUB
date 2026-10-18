#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerConfigRoutes(AsyncWebServer& server, const WebRouteContext& context);
void registerSensorApiRoutes(AsyncWebServer& server, const WebRouteContext& context);