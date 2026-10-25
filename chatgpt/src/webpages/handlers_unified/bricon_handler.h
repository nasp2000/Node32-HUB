#pragma once
#include "../http_abstraction.h"

void handleBriconStatusGet(HttpRequest& req, HttpResponse& res);
void handleBriconArrivalsGet(HttpRequest& req, HttpResponse& res);
void handleBriconRegistrationsGet(HttpRequest& req, HttpResponse& res);
void handleBriconContestsGet(HttpRequest& req, HttpResponse& res);
void handleBriconPombalGet(HttpRequest& req, HttpResponse& res);
void handleBriconConfigPost(HttpRequest& req, HttpResponse& res);
void handleBriconPollPost(HttpRequest& req, HttpResponse& res);
void handleBriconSyncPombalPost(HttpRequest& req, HttpResponse& res);
void handleBriconSyncCalendarPost(HttpRequest& req, HttpResponse& res);
void handleBriconResetPost(HttpRequest& req, HttpResponse& res);
