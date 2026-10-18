#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register lightweight RS232 template endpoints.
void registerRs232TemplateRoutes(AsyncWebServer& server, const WebRouteContext& context);