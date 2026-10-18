#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerStorageRoutes(AsyncWebServer& server, const WebRouteContext& context);