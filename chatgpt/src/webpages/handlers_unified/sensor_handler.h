#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

void handleSensorFlagsGet(HttpRequest& req, HttpResponse& res);

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
void handleSensorConfigGet(HttpRequest& req, HttpResponse& res);
void handleSensorConfigPost(HttpRequest& req, HttpResponse& res);
void handleSensorMeasurePost(HttpRequest& req, HttpResponse& res);
void handleSensorResetStatsPost(HttpRequest& req, HttpResponse& res);
void handleSensorSlotsGet(HttpRequest& req, HttpResponse& res);
void handleSensorFlushPost(HttpRequest& req, HttpResponse& res);
#endif

void handleChartCapsGet(HttpRequest& req, HttpResponse& res);

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
void handleSensorChartsSessionPost(HttpRequest& req, HttpResponse& res);
void handleSensorChartsHeartbeatPost(HttpRequest& req, HttpResponse& res);
void handleSensorChartsSessionDelete(HttpRequest& req, HttpResponse& res);
void handleSensorChartsSessionEndPost(HttpRequest& req, HttpResponse& res);
#endif

#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
void handleSensorHistoryGet(HttpRequest& req, HttpResponse& res);
void handleSensorHistoryFilesGet(HttpRequest& req, HttpResponse& res);
void handleSensorHistoryFileGet(HttpRequest& req, HttpResponse& res);
#endif

#if ENABLE_MQ135==1
void handleMq135ConfigGet(HttpRequest& req, HttpResponse& res);
void handleMq135ConfigPost(HttpRequest& req, HttpResponse& res);
void handleMq135MeasurePost(HttpRequest& req, HttpResponse& res);
void handleMq135CalibrateR0Post(HttpRequest& req, HttpResponse& res);
#endif

#if ENABLE_HX711==1
void handleHx711ConfigGet(HttpRequest& req, HttpResponse& res);
void handleHx711ConfigPost(HttpRequest& req, HttpResponse& res);
void handleHx711MeasurePost(HttpRequest& req, HttpResponse& res);
void handleHx711TarePost(HttpRequest& req, HttpResponse& res);
#endif

#if ENABLE_INA226==1
void handleIna226ConfigGet(HttpRequest& req, HttpResponse& res);
void handleIna226ConfigPost(HttpRequest& req, HttpResponse& res);
void handleIna226MeasurePost(HttpRequest& req, HttpResponse& res);
#endif

#if ENABLE_MAX31865==1
void handleMax31865ConfigGet(HttpRequest& req, HttpResponse& res);
void handleMax31865ConfigPost(HttpRequest& req, HttpResponse& res);
void handleMax31865MeasurePost(HttpRequest& req, HttpResponse& res);
#endif

#if ENABLE_SOUND_SENSOR==1
void handleSoundConfigGet(HttpRequest& req, HttpResponse& res);
void handleSoundConfigPost(HttpRequest& req, HttpResponse& res);
void handleSoundMeasurePost(HttpRequest& req, HttpResponse& res);
#endif
