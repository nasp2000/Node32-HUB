#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

// GET /api/config?section=wifi|email|rs232|system|recipients|scheduler
void handleConfigGet(HttpRequest& req, HttpResponse& res);
// POST /api/config (form-urlencoded or JSON, requires section= in body)
void handleConfigPost(HttpRequest& req, HttpResponse& res);
// GET /api/configs — snapshot of saved config values
void handleConfigsGet(HttpRequest& req, HttpResponse& res);
// GET /api/access-config — lightweight web/access settings
void handleAccessConfigGet(HttpRequest& req, HttpResponse& res);
// POST /api/access-config — save web/access settings
void handleAccessConfigPost(HttpRequest& req, HttpResponse& res);
// GET /api/session-history
void handleSessionHistoryGet(HttpRequest& req, HttpResponse& res);
// POST /api/access-kick-all
void handleAccessKickAllPost(HttpRequest& req, HttpResponse& res);
