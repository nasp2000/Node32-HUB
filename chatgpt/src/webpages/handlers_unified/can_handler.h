#pragma once
#include "../http_abstraction.h"

void handleCanStatusGet(HttpRequest& req, HttpResponse& res);
void handleCanFramesGet(HttpRequest& req, HttpResponse& res);
void handleCanConfigGet(HttpRequest& req, HttpResponse& res);
void handleCanClearPost(HttpRequest& req, HttpResponse& res);
void handleCanRestartPost(HttpRequest& req, HttpResponse& res);
