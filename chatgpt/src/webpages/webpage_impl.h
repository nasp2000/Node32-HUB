#ifndef WEBPAGE_IMPL_H
#define WEBPAGE_IMPL_H
#include "heap_trace.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "../direct_log.h"
#include "handlers/config_api_handler.h"
#include "routes/web_route_context.h"
#include "routes/route_registry_api.h"
#include "routes/web_routes_config.h"
#include "routes/web_routes_core.h"
#include "routes/web_routes_storage.h"
#if ENABLE_PAGE_MONITOR==1
#include "routes/web_routes_monitor.h"
#endif
#include "routes/web_routes_network.h"
#include "routes/web_routes_networks.h"
#include "routes/web_routes_ota.h"
#if ENABLE_PAGE_COMMANDS==1 || ENABLE_DAILY_REBOOT==1
#include "routes/web_routes_commands.h"
#endif
#include "routes/web_routes_modbus.h"
#include "routes/web_routes_access.h"
#if ENABLE_PROXIMITY==1
#include "routes/web_routes_proximity.h"
#endif
#if ENABLE_BUZZER == 1
#include "routes/web_routes_buzzer.h"
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "routes/web_routes_touch7.h"
#endif
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
#include "routes/web_routes_do.h"
#endif
#if ENABLE_CAN == 1
#include "routes/web_routes_can.h"
#endif
#if ENABLE_CAMERA==1
#include "routes/web_routes_camera.h"
#endif
#if ENABLE_DUALFILE==1
#include "routes/web_routes_dualfile.h"
#endif
#if ENABLE_UNIKON == 1
#include "routes/web_routes_unikon.h"
#endif
#if ENABLE_BRICON == 1
#include "routes/web_routes_bricon.h"
#include "routes/web_routes_rambox.h"
#endif
#if ENABLE_IPG_LASER == 1
#include "routes/web_routes_ipg_laser.h"
#endif
#if ENABLE_UDA1334A == 1
#include "routes/web_routes_audio.h"
#endif

bool ensureBasicAuth(AsyncWebServerRequest* request);
bool allowHtmlClient(AsyncWebServerRequest* request);
void decrementActiveHtmlSessions();
void webLocalPageHeartbeat(AsyncWebServerRequest* request);

inline void logServed(AsyncWebServerRequest* request, const char* url, size_t bytes, bool fromPgm) {
    if (request == nullptr) return;
    if (url && strcmp(url, "/api/sd-status") == 0) return;

    char buffer[128];
    float kb = (float)bytes / 1024.0f;
    if (fromPgm) snprintf(buffer, sizeof(buffer), "🌐 %s - served %.2f KB (PROGMEM)", url, kb);
    else snprintf(buffer, sizeof(buffer), "🌐 %s - served %.2f KB", url, kb);
    directLogPrintln(buffer);
}

inline void setupWebServer(AsyncWebServer& server) {
    static const WebRouteContext routeContext = {
        ensureBasicAuth,
        allowHtmlClient,
        decrementActiveHtmlSessions,
        logServed,
    };

    registerCoreRoutes(server, routeContext);
    registerNetworkRoutes(server, routeContext);
    registerNetworksRoutes(server, routeContext);
    registerStorageRoutes(server, routeContext);
    registerConfigRoutes(server, routeContext);
#if ENABLE_PAGE_MONITOR==1
    registerMonitorRoutes(server, routeContext);
#endif
#if ENABLE_PAGE_COMMANDS==1 || ENABLE_DAILY_REBOOT==1
    registerCommandRoutes(server, routeContext);
#endif
    registerOtaRoutes(server, routeContext);
    registerModbusRoutes(server, routeContext);
    registerAccessRoutes(server, routeContext);
#if ENABLE_PROXIMITY==1
    registerProximityRoutes(server, routeContext);
#endif
#if ENABLE_BUZZER == 1
    registerBuzzerRoutes(server, routeContext);
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    registerTouch7Routes(server, routeContext);
#endif
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    registerDoRoutes(server, routeContext);
#endif
#if ENABLE_CAN == 1
    registerCanRoutes(server, routeContext);
#endif
#if ENABLE_CAMERA==1
    registerCameraRoutes(server, routeContext);
#endif
#if ENABLE_UNIKON == 1
    registerUnikonRoutes(server, routeContext);
#endif
#if ENABLE_BRICON == 1
    registerBriconRoutes(server, routeContext);
#endif

#if ENABLE_UNIKON == 1 || ENABLE_BRICON == 1
    registerRamboxRoutes(server, routeContext);
#endif
#if ENABLE_IPG_LASER == 1
    registerIpgLaserRoutes(server, routeContext);
#endif
#if ENABLE_UDA1334A == 1
    registerAudioRoutes(server, routeContext);
#endif
#if ENABLE_DUALFILE==1
    registerDualFileRoutes(server, routeContext);
#endif

    registerApiRoutesToServer(server, routeContext);

    sendVirtualMessage(F("WebServer ON"), INFO);
    heapTraceMark("webserver");
}

inline void showDataSampleInWebMonitor() {
}

#endif // WEBPAGE_IMPL_H

