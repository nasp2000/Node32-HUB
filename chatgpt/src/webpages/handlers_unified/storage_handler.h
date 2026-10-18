#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

void handleStorageRetrySdPost(HttpRequest& req, HttpResponse& res);
void handleSdBuffersGet(HttpRequest& req, HttpResponse& res);
void handleSdStatusGet(HttpRequest& req, HttpResponse& res);
void handleSdActionPost(HttpRequest& req, HttpResponse& res);
void handleSdResetWriteCounterPost(HttpRequest& req, HttpResponse& res);
void handleSdcardConfigGet(HttpRequest& req, HttpResponse& res);
void handleSdcardConfigSavePost(HttpRequest& req, HttpResponse& res);
