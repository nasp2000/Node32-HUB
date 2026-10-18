#include "ipg_laser_handler.h"
#include "../../feature_flags.h"

#if ENABLE_IPG_LASER == 1
#include "../../ipg_laser_module.h"
#endif

void handleIpgLaserAvailableGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_IPG_LASER == 1
    res.sendJson(F("{\"available\":true,\"message\":\"IPG Laser module is enabled.\"}"));
#else
    res.sendJson(F("{\"available\":false,\"message\":\"IPG Laser module is disabled. Enable ENABLE_IPG_LASER in user_config.h.\"}"));
#endif
}

void handleIpgLaserClearPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_IPG_LASER == 1
    ipgLaserClearLog();
    res.sendJson(F("{\"success\":true,\"message\":\"IPG response log cleared.\"}"));
#else
    res.code = 503;
    res.sendJson(F("{\"success\":false,\"message\":\"IPG Laser not enabled.\"}"));
#endif
}
