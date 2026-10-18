// web_routes_do.h
// Route registration for TCA9554 digital output page and API.

#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"
#include "../../feature_flags.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

void registerDoRoutes(AsyncWebServer& server, const WebRouteContext& context);

#endif // BOARD_HAS_TCA9554_DO
