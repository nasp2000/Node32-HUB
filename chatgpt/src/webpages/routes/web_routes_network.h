#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerNetworkRoutes(AsyncWebServer& server, const WebRouteContext& context);