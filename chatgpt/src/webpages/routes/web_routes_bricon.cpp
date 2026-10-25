// web_routes_bricon.cpp
// Route registration for the Bricon Rambox page (/bricon) and its REST API.

#include "web_routes_bricon.h"
#include "../handlers_unified/bricon_handler.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerBriconRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_BRICON == 1
    registerApiRoute("GET",  "/api/bricon-status",       handleBriconStatusGet);
    registerApiRoute("GET",  "/api/bricon-arrivals",      handleBriconArrivalsGet);
    registerApiRoute("GET",  "/api/bricon-registrations", handleBriconRegistrationsGet);
    registerApiRoute("GET",  "/api/bricon-contests",      handleBriconContestsGet);
    registerApiRoute("GET",  "/api/bricon-pombal",        handleBriconPombalGet);
    registerApiRoute("POST", "/api/bricon-config",        handleBriconConfigPost);
    registerApiRoute("POST", "/api/bricon-poll",          handleBriconPollPost);
    registerApiRoute("POST", "/api/bricon-sync-pombal",   handleBriconSyncPombalPost);
    registerApiRoute("POST", "/api/bricon-sync-calendar", handleBriconSyncCalendarPost);

    // HTML page.
    // One page only: the unified Rambox page owns this device's UI.
    server.on("/bricon", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/rambox?dev=bricon");
    });


#else
    (void)server;
    (void)context;
#endif // ENABLE_BRICON
}
