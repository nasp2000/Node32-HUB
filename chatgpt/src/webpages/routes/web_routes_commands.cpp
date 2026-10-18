#include "web_routes_commands.h"
#include "route_registry_api.h"

#include "../../feature_flags.h"

#if ENABLE_PAGE_COMMANDS==1

#include "../handlers_unified/commands_handler.h"
#include "../renderers/commands_datetime_page_renderer.h"
#include "../renderers/commands_page_renderer.h"

void registerCommandRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    registerApiRoute("GET",  "/api/scheduled-datetime-slots", handleScheduledDatetimeSlotsGet);
    registerApiRoute("GET",  "/api/scheduled-datetime-slot",  handleScheduledDatetimeSlotGet);
    registerApiRoute("POST", "/api/scheduled-datetime-slot",  handleScheduledDatetimeSlotPost);
    registerApiRoute("GET",  "/api/daily-reboot",             handleDailyRebootGet);
    registerApiRoute("POST", "/api/daily-reboot",             handleDailyRebootPost);
#if ENABLE_PAGE_COMMANDS_INTERVAL==1
    registerApiRoute("GET",  "/api/scheduled-command-slots",  handleScheduledCommandSlotsGet);
    registerApiRoute("GET",  "/api/scheduled-command-slot",   handleScheduledCommandSlotGet);
    registerApiRoute("POST", "/api/scheduled-command-slot",   handleScheduledCommandSlotPost);
#endif


#if ENABLE_PAGE_COMMANDS_INTERVAL==1
    server.on("/commands", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* stream = request->beginResponseStream("text/html; charset=utf-8");
        if (!stream) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        stream->addHeader("ETag", FIRMWARE_BUILD);
        stream->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        const size_t bytes = streamCommandsPageToPrint(*stream);
        request->send(stream);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/commands", bytes, false);
#endif
    });
#endif // ENABLE_PAGE_COMMANDS_INTERVAL

    server.on("/commands-by-datetime", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* stream = request->beginResponseStream("text/html; charset=utf-8");
        if (!stream) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }

        stream->addHeader("ETag", FIRMWARE_BUILD);
        stream->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        const size_t bytes = streamCommandsByDatetimePageToPrint(*stream);
        request->send(stream);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/commands-by-datetime", bytes, false);
#endif
    });
}

#else
#include "../handlers_unified/commands_handler.h"
#if ENABLE_DAILY_REBOOT==1
// ENABLE_PAGE_COMMANDS=0 but daily reboot on: expose only the daily-reboot API
// (config schema + executor are pack-agnostic; the full page stays off).
void registerCommandRoutes(AsyncWebServer&, const WebRouteContext&) {
    registerApiRoute("GET",  "/api/daily-reboot",             handleDailyRebootGet);
    registerApiRoute("POST", "/api/daily-reboot",             handleDailyRebootPost);
}
#else
// ENABLE_PAGE_COMMANDS=0: provide an empty stub so the linker is satisfied.
void registerCommandRoutes(AsyncWebServer&, const WebRouteContext&) {}
#endif // ENABLE_DAILY_REBOOT
#endif // ENABLE_PAGE_COMMANDS
