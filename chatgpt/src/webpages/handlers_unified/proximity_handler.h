#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_PROXIMITY==1
void handleProximityGet(HttpRequest& req, HttpResponse& res);
void handleProximityResetPost(HttpRequest& req, HttpResponse& res);
void handleProximityResetAllPost(HttpRequest& req, HttpResponse& res);
void handleProximityResetTotalPost(HttpRequest& req, HttpResponse& res);
void handleProximityConfigGet(HttpRequest& req, HttpResponse& res);
void handleProximityConfigPost(HttpRequest& req, HttpResponse& res);
void handleProximityShiftConfigGet(HttpRequest& req, HttpResponse& res);
void handleProximityShiftConfigPost(HttpRequest& req, HttpResponse& res);
void handleProximityShiftdayEmailsGet(HttpRequest& req, HttpResponse& res);
void handleProximityShiftdayEmailsPost(HttpRequest& req, HttpResponse& res);
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
void handleProximityTca9554Get(HttpRequest& req, HttpResponse& res);
void handleProximityTca9554Post(HttpRequest& req, HttpResponse& res);
#endif
void handleProximitySaveAllPost(HttpRequest& req, HttpResponse& res);
void handleProximityChartFilesGet(HttpRequest& req, HttpResponse& res);
void handleProximityChartColsGet(HttpRequest& req, HttpResponse& res);
void handleProximityChartDataGet(HttpRequest& req, HttpResponse& res);
void handleProximityEventsGet(HttpRequest& req, HttpResponse& res);
void handleProximityOeeGet(HttpRequest& req, HttpResponse& res);
void handleProximityOeeResetPost(HttpRequest& req, HttpResponse& res);
void handleProximityShiftHistoryGet(HttpRequest& req, HttpResponse& res);
void handleProximityTestEmailPost(HttpRequest& req, HttpResponse& res);
#endif
