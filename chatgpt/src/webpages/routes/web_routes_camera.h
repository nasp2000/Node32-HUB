#pragma once
#include <ESPAsyncWebServer.h>
#include "web_route_context.h"
void registerCameraRoutes(AsyncWebServer& server, const WebRouteContext& context);
// Reset the MJPEG viewer counter (called on camera stop): a leaked
// onDisconnect would otherwise hold slots forever across stop/start cycles.
void cameraStreamResetClients();
