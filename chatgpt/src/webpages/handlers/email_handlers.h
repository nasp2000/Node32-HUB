#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

void registerEmailApiRoutes(AsyncWebServer& server, const WebRouteContext& context);