#pragma once
#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

void registerAccessRoutes(AsyncWebServer& server, const WebRouteContext& context);
