#pragma once

#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

// Register the /api/sd/* routes used by the /files browser.
// Keeps storage mutations and downloads out of webpage_impl.h.
void registerFilesApiRoutes(AsyncWebServer& server, const WebRouteContext& context);
