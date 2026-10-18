#pragma once

#include <ESPAsyncWebServer.h>

#include "../routes/web_route_context.h"

// Register RS232 charts/history APIs backed by live buffers and SD archives.
void registerRs232HistoryRoutes(AsyncWebServer& server, const WebRouteContext& context);