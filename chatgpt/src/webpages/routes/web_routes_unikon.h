// web_routes_unikon.h
// Route registration for the Unikon RAMBOX page (/unikon) and its REST API.

#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

void registerUnikonRoutes(AsyncWebServer& server, const WebRouteContext& context);
