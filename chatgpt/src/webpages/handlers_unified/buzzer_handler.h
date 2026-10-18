#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_BUZZER==1
void handleBuzzerConfigGet(HttpRequest& req, HttpResponse& res);
void handleBuzzerConfigPost(HttpRequest& req, HttpResponse& res);
void handleBuzzerTestPost(HttpRequest& req, HttpResponse& res);
void handleBuzzerStopPost(HttpRequest& req, HttpResponse& res);
#endif
