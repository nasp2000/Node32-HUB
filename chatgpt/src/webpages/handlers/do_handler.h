// do_handler.h
// HTTP/API handlers for TCA9554 digital output control.

#pragma once
#include <ESPAsyncWebServer.h>
#include "../../feature_flags.h"
#include "../routes/web_route_context.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

// Register GET /api/do/status and POST /api/do/set.
void registerDoApiRoutes(AsyncWebServer& server, const WebRouteContext& context);

#endif // BOARD_HAS_TCA9554_DO
