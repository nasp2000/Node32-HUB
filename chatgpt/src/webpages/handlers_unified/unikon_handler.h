#pragma once
#include "../http_abstraction.h"

void handleUnikonStatusGet(HttpRequest& req, HttpResponse& res);
void handleUnikonArrivalsGet(HttpRequest& req, HttpResponse& res);
void handleUnikonRegistrationsGet(HttpRequest& req, HttpResponse& res);
void handleUnikonContestsGet(HttpRequest& req, HttpResponse& res);
void handleUnikonPombalGet(HttpRequest& req, HttpResponse& res);
void handleUnikonSyncPombalPost(HttpRequest& req, HttpResponse& res);
void handleUnikonEmailPombalPost(HttpRequest& req, HttpResponse& res);
void handleUnikonPollPost(HttpRequest& req, HttpResponse& res);
void handleUnikonSyncZpPost(HttpRequest& req, HttpResponse& res);
void handleUnikonPollWPost(HttpRequest& req, HttpResponse& res);
void handleUnikonSyncCalendarPost(HttpRequest& req, HttpResponse& res);
void handleUnikonResetPost(HttpRequest& req, HttpResponse& res);
void handleUnikonSendTrainingPost(HttpRequest& req, HttpResponse& res);
void handleUnikonClearTrainingPost(HttpRequest& req, HttpResponse& res);
