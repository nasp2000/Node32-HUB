#pragma once

#include <ESPAsyncWebServer.h>
#include "../routes/web_route_context.h"

// Register the /files GET route.
// Renders a dynamic file browser for SD card, Flash (LittleFS), and PSRAM storage.
// Limits concurrent sessions to prevent heap overflow.
void registerFilesPageRoute(AsyncWebServer& server, const WebRouteContext& context);
