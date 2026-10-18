#pragma once
#include "../http_abstraction.h"

void handleStatusGet(HttpRequest& req, HttpResponse& res);
void handleNvsStatsGet(HttpRequest& req, HttpResponse& res);
void handleRestartPost(HttpRequest& req, HttpResponse& res);
void handleTestPost(HttpRequest& req, HttpResponse& res);
void handleDb25InjectGet(HttpRequest& req, HttpResponse& res);
void handleFactoryResetPost(HttpRequest& req, HttpResponse& res);
void performRestartActions();
void performFactoryResetActions();
void handleHeapSnapshotGet(HttpRequest& req, HttpResponse& res);
