#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerDualFileRoutes(AsyncWebServer& server, const WebRouteContext& context);
