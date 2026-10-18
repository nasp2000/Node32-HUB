#pragma once
#include <Arduino.h>
#include "../http_abstraction.h"

typedef void (*ApiRouteHandler)(HttpRequest& req, HttpResponse& res);

#ifndef MAX_API_ROUTES
#define MAX_API_ROUTES 256
#endif

void registerApiRoute(const char* method, const char* path, ApiRouteHandler handler);

void registerApiRoutesToServer(class AsyncWebServer& server, const struct WebRouteContext& context);

ApiRouteHandler findApiRouteHandler(const String& method, const String& path);

// Returns true if the path is known to any registered or legacy route
// (regardless of HTTP method). Used by ethernet_bootstrap_routes.h for
// 405 Method Not Allowed vs 404 Not Found distinction.
bool isKnownApiPath(const String& path);

// Read-only listing of the dynamically registered API routes. Used by the
// test manifest so the AI agent can discover every API without a parallel list.
size_t registeredApiRouteCount();
bool registeredApiRouteAt(size_t i, const char*& method, const char*& path);
