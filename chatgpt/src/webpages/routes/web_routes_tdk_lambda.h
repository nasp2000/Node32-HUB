#pragma once

#include <ESPAsyncWebServer.h>

#include "web_route_context.h"

void registerTdkLambdaRoutes(AsyncWebServer& server, const WebRouteContext& context);
