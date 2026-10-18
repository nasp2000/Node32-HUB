#pragma once
#include "../http_abstraction.h"

void handleRs232PersistCurrentTemplatePost(HttpRequest& req, HttpResponse& res);
void handleRs232StatusGet(HttpRequest& req, HttpResponse& res);
void handleTestRs232Post(HttpRequest& req, HttpResponse& res);
