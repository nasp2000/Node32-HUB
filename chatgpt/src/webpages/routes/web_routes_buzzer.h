// web_routes_buzzer.h
// Route registration for the buzzer configuration page and API.

#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

void registerBuzzerRoutes(AsyncWebServer& server, const WebRouteContext& context);
