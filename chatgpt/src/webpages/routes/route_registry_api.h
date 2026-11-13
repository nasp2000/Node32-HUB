#pragma once
#include <Arduino.h>
#include <functional>
#include "../http_abstraction.h"

typedef void (*ApiRouteHandler)(HttpRequest& req, HttpResponse& res);

#ifndef MAX_API_ROUTES
#define MAX_API_ROUTES 256
#endif

void registerApiRoute(const char* method, const char* path, ApiRouteHandler handler);

void registerApiRoutesToServer(class AsyncWebServer& server, const struct WebRouteContext& context);

ApiRouteHandler findApiRouteHandler(const String& method, const String& path);

// A1 (2026-11-13): registered GET routes are NOT registered one AsyncWebHandler
// per path any more — registerApiRoutesToServer() only installs the non-GET ones
// (they need the onBody stash). Every GET lands in the single server.onNotFound()
// handler, which calls this first: exact method+path lookup in the registry table,
// then the same dispatcher the individual routes used (auth, heartbeat, response).
// Returns true when a route matched and the response was already sent, false to
// let the caller's normal 404 / AP-fallback logic run.
// Do NOT turn this into lazy per-page registration: a route that is missing only
// because nobody visited its page would answer 404 with no diagnostic.
bool dispatchRegisteredGetRoute(class AsyncWebServerRequest* request,
                                const struct WebRouteContext& context);

// How many registered routes are served by that catch-all (GET ones). Reported
// at boot next to the total so the split is visible in the Web Serial Monitor.
size_t registeredApiGetRouteCount();

// ── Page GET routes (A1b, 2026-11-13) ───────────────────────────────────────
// The page handlers used to call server.on("<page>", HTTP_GET, lambda) for every
// page/asset: one AsyncCallbackWebHandler (URI String + std::functions + list
// node, ~200 B) per route. They now hand the same lambda to registerPageGet(),
// which keeps it in a table consulted by the single onNotFound catch-all.
// No behaviour change: insertion order is preserved (first match wins, as
// ESPAsyncWebServer does), the lambda itself still does auth and response.
// Only for HTTP_GET routes with no onBody/onUpload (a GET carries no body).
typedef std::function<void(class AsyncWebServerRequest*)> PageGetHandler;

void registerPageGet(const char* path, PageGetHandler handler);

// Routes held by that table (logged at boot; capped, see PAGE_GET_MAX).
size_t registeredPageGetCount();

// Returns true if the path is known to any registered or legacy route
// (regardless of HTTP method). Used by ethernet_bootstrap_routes.h for
// 405 Method Not Allowed vs 404 Not Found distinction.
bool isKnownApiPath(const String& path);

// Read-only listing of the dynamically registered API routes. Used by the
// test manifest so the AI agent can discover every API without a parallel list.
size_t registeredApiRouteCount();
bool registeredApiRouteAt(size_t i, const char*& method, const char*& path);
