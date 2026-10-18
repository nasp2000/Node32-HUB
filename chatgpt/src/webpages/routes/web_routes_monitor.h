#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerMonitorRoutes(AsyncWebServer& server, const WebRouteContext& context);