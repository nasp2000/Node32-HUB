#pragma once
#include "../http_abstraction.h"

void handleTdkLambdaAvailableGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaStatusGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaCommandsGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaLogGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaSendPost(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaClearPost(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaConfigGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaConfigPost(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaCsvFlushPost(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaSelfTestPost(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaSamplesGet(HttpRequest& req, HttpResponse& res);
void handleTdkLambdaCsvGet(HttpRequest& req, HttpResponse& res);
