#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerCommandRoutes(AsyncWebServer& server, const WebRouteContext& context);
