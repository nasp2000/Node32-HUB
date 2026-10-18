// src/webpages/routes/web_routes_audio.h
// Audio DAC (UDA1334A) route declarations

#pragma once

#include "../../feature_flags.h"

#if ENABLE_UDA1334A == 1

#include <ESPAsyncWebServer.h>
#include "web_route_context.h"

void registerAudioRoutes(AsyncWebServer& server, const WebRouteContext& context);

#endif // ENABLE_UDA1334A

