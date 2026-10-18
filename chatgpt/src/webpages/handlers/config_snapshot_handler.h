#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register the unified config snapshot API.
void registerConfigSnapshotRoutes(AsyncWebServer& server, const WebRouteContext& context);

// Invalidate the PSRAM response cache.
// Must be called after any successful config save so the next GET rebuilds the snapshot.
void invalidateConfigsCache();