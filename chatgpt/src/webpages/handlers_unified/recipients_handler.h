#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

void handleRecipientSlotGet(HttpRequest& req, HttpResponse& res);
void handleRecipientSlotPost(HttpRequest& req, HttpResponse& res);
#if ENABLE_PAGE_ALERTS==1
void handleInactivityAlertConfigPost(HttpRequest& req, HttpResponse& res);
void handleInactivityAlertConfigGet(HttpRequest& req, HttpResponse& res);
void handleInactivityAlertTestPost(HttpRequest& req, HttpResponse& res);
#if ENABLE_PROXIMITY==1
void handleOeeAlertConfigPost(HttpRequest& req, HttpResponse& res);
void handleOeeAlertConfigGet(HttpRequest& req, HttpResponse& res);
#endif
#endif
void handleEmaildayExtraEmailsGet(HttpRequest& req, HttpResponse& res);
void handleEmaildayExtraEmailsPost(HttpRequest& req, HttpResponse& res);
void handleSensordayExtraEmailsGet(HttpRequest& req, HttpResponse& res);
void handleSensordayExtraEmailsPost(HttpRequest& req, HttpResponse& res);
void handleLogdayExtraEmailsGet(HttpRequest& req, HttpResponse& res);
void handleLogdayExtraEmailsPost(HttpRequest& req, HttpResponse& res);
