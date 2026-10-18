#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerCoreRoutes(AsyncWebServer& server, const WebRouteContext& context);