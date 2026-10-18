// web_routes_bricon.h
// Route registration for the Bricon Rambox page (/bricon) and its REST API.

#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

void registerBriconRoutes(AsyncWebServer& server, const WebRouteContext& context);
