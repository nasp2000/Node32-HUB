// web_routes_unikon.cpp
// Route registration for the Unikon RAMBOX page (/unikon) and its REST API.

#include "web_routes_unikon.h"
#include "../handlers/unikon_handler.h"
#include "../handlers_unified/unikon_handler.h"
#include "../../feature_flags.h"
#include "route_registry_api.h"

void registerUnikonRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UNIKON == 1
    registerApiRoute("GET",  "/api/unikon-status",       handleUnikonStatusGet);
    registerApiRoute("GET",  "/api/unikon-arrivals",      handleUnikonArrivalsGet);
    registerApiRoute("GET",  "/api/unikon-registrations", handleUnikonRegistrationsGet);
    registerApiRoute("GET",  "/api/unikon-contests",      handleUnikonContestsGet);
    registerApiRoute("GET",  "/api/unikon-pombal",        handleUnikonPombalGet);
    registerApiRoute("POST", "/api/unikon-sync-pombal",   handleUnikonSyncPombalPost);
    registerApiRoute("POST", "/api/unikon-email-pombal",  handleUnikonEmailPombalPost);
    registerApiRoute("POST", "/api/unikon-poll",          handleUnikonPollPost);
    registerApiRoute("POST", "/api/unikon-sync-zp",       handleUnikonSyncZpPost);
    registerApiRoute("POST", "/api/unikon-poll-w",        handleUnikonPollWPost);
    registerApiRoute("POST", "/api/unikon-sync-calendar", handleUnikonSyncCalendarPost);
    registerApiRoute("POST", "/api/unikon-reset",         handleUnikonResetPost);
    // Training endpoints (fictitious training race + the ticked pigeon records, explicit request only).
    registerApiRoute("POST", "/api/unikon-send-training",  handleUnikonSendTrainingPost);
    registerApiRoute("POST", "/api/unikon-clear-training", handleUnikonClearTrainingPost);
    // Push (device-initiated arrivals): P1/P0 probe, per-device state.
    registerApiRoute("POST", "/api/unikon-push",           handleUnikonPushPost);

    // HTML page.
    // One page only: the unified Rambox page owns this device's UI.
    server.on("/unikon", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/rambox?dev=unikon");
    });


    // POST /api/unikon-config stays manual (body params)
    registerUnikonApiRoutes(server, context);

#else
    (void)server;
    (void)context;
#endif // ENABLE_UNIKON
}
