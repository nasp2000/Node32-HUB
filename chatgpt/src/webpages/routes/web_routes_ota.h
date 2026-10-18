#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerOtaRoutes(AsyncWebServer& server, const WebRouteContext& context);
