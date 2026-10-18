#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

void handleEmailTestPost(HttpRequest& req, HttpResponse& res);
#if ENABLE_SMTP==1
void handleEmailResetPost(HttpRequest& req, HttpResponse& res);
#endif
