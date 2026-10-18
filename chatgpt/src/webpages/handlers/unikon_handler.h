// unikon_handler.h
// HTTP/API handlers for the Unikon RAMBOX monitoring and configuration page.

#pragma once
#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

void registerUnikonApiRoutes(AsyncWebServer& server, const WebRouteContext& context);
