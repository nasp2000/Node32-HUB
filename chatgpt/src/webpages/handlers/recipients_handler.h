#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register recipient and inactivity-alert APIs.
void registerRecipientRoutes(AsyncWebServer& server, const WebRouteContext& context);