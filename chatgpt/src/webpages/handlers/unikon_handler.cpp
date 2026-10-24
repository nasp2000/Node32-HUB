#include "unikon_handler.h"
#include "../../feature_flags.h"
#include "../../direct_log.h"
#include "../../languages/lang_active.h"

#if ENABLE_UNIKON == 1
#include "../../unikon_protocol.h"
#include "../../ConfigManager.h"
#include "../../littlefs_manager.h"
extern ConfigManager configManager;
#endif
#if ENABLE_BRICON == 1
#include "../../bricon_protocol.h"
#endif

void registerUnikonApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UNIKON == 1
    // POST /api/unikon-config — save configuration from form fields
    server.on("/api/unikon-config", HTTP_POST, [&context](AsyncWebServerRequest* request) {
        directLogPrintln(F("[UNIKON] POST /api/unikon-config received"));
        if (!context.ensureBasicAuth(request)) {
            directLogPrintln(F("[UNIKON] POST /api/unikon-config auth failed"));
            return;
        }
        directLogPrintln(F("[UNIKON] POST /api/unikon-config auth OK"));
        UnikonConfig cfg = unikonGetConfig();
        bool hasEnabled  = request->hasParam("enabled",  true);
        bool hasInterval = request->hasParam("interval", true);
        Serial.printf("[UNIKON] POST params: enabled=%d interval=%d\n", (int)hasEnabled, (int)hasInterval);
        if (hasEnabled)  cfg.enabled         = request->getParam("enabled",  true)->value().toInt() != 0;
        if (hasInterval) cfg.pollIntervalSec = (uint16_t)request->getParam("interval",true)->value().toInt();
        if (request->hasParam("passive", true))
            cfg.passiveListen = request->getParam("passive", true)->value().toInt() != 0;
        if (request->hasParam("official_format", true))
            cfg.officialFormat = request->getParam("official_format", true)->value().toInt() != 0;
        if (request->hasParam("manual", true))
            cfg.manualSerial = request->getParam("manual", true)->value().toInt() != 0;

        unikonSetConfig(cfg);
        // Exclusive module rule (rambox XOR bricon): enabling Unikon disables Bricon.
#if ENABLE_BRICON == 1
        if (cfg.enabled) briconSetEnabled(false);
#endif
        // Verify the file was actually written to LittleFS.
        // saveConfig() may fail (e.g. FS not mounted); detect that here.
        if (!littlefsBegin()) {
            directLogPrintln(F("[UNIKON] POST /api/unikon-config: save verification FAILED - LittleFS mount error"));
            request->send(200, F("application/json"),
                F("{\"success\":true,\"message\":\"LittleFS unavailable. Config saved in memory and will persist when Flash mounts.\"}"));
            return;
        }
        if (!getLittleFS().exists("/config/unikon.json")) {
            directLogPrintln(F("[UNIKON] POST /api/unikon-config: save verification FAILED - file not found after save"));
            request->send(200, F("application/json"),
                (String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiFileNotWritten) + "\"}"));
            return;
        }
        request->send(200, F("application/json"),
            F("{\"success\":true,\"message\":\"\\u2705 Configura\\u00e7\\u00e3o Unikon guardada!\"}"));
    });

#else
    (void)server;
    (void)context;
#endif // ENABLE_UNIKON
}
