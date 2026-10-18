#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

// TouchMeteo-7 unified handlers (BH1750 + touch7 config/peers + falas).
// Whole unit compiles only for PACK_S3_TOUCH7; empty decls elsewhere.

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#if ENABLE_BH1750==1
void handleBh1750ConfigGet(HttpRequest& req, HttpResponse& res);
void handleBh1750ConfigPost(HttpRequest& req, HttpResponse& res);
void handleBh1750MeasurePost(HttpRequest& req, HttpResponse& res);
#endif // ENABLE_BH1750==1

void handleTouch7ConfigGet(HttpRequest& req, HttpResponse& res);
void handleTouch7ConfigPost(HttpRequest& req, HttpResponse& res);
void handleTouch7PeerRefreshPost(HttpRequest& req, HttpResponse& res);
void handleTouch7WeatherRefreshPost(HttpRequest& req, HttpResponse& res);
void handleTouch7ForecastGet(HttpRequest& req, HttpResponse& res);
void handleTouch7GeoGet(HttpRequest& req, HttpResponse& res);
void handleTouch7AutoLocGet(HttpRequest& req, HttpResponse& res);
void handleTouch7TapPost(HttpRequest& req, HttpResponse& res);
void handleTouch7FrameGet(HttpRequest& req, HttpResponse& res);
void handleTouch7PclkGet(HttpRequest& req, HttpResponse& res);

void handleFalasListGet(HttpRequest& req, HttpResponse& res);
void handleFalasPlayPost(HttpRequest& req, HttpResponse& res);
void handleFalasDeletePost(HttpRequest& req, HttpResponse& res);

#endif
