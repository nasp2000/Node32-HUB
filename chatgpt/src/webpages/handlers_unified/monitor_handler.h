#pragma once
#include "../http_abstraction.h"

void handleMonitorOpenPost(HttpRequest& req, HttpResponse& res);
void handleMonitorClosePost(HttpRequest& req, HttpResponse& res);
void handleClearBufferPost(HttpRequest& req, HttpResponse& res);
