#pragma once
#include "../http_abstraction.h"

void handleScheduledDatetimeSlotsGet(HttpRequest& req, HttpResponse& res);
void handleScheduledDatetimeSlotGet(HttpRequest& req, HttpResponse& res);
void handleScheduledDatetimeSlotPost(HttpRequest& req, HttpResponse& res);
void handleDailyRebootGet(HttpRequest& req, HttpResponse& res);
void handleDailyRebootPost(HttpRequest& req, HttpResponse& res);
void handleScheduledCommandSlotsGet(HttpRequest& req, HttpResponse& res);
void handleScheduledCommandSlotGet(HttpRequest& req, HttpResponse& res);
void handleScheduledCommandSlotPost(HttpRequest& req, HttpResponse& res);
