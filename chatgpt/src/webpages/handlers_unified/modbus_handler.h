#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_MODBUS==1
void handleModbusConfigGet(HttpRequest& req, HttpResponse& res);
void handleModbusConfigPost(HttpRequest& req, HttpResponse& res);
void handleModbusLiveGet(HttpRequest& req, HttpResponse& res);
void handleModbusPollNowPost(HttpRequest& req, HttpResponse& res);
void handleModbusRtuWritePost(HttpRequest& req, HttpResponse& res);
void handleModbusScanRtuPost(HttpRequest& req, HttpResponse& res);
void handleModbusScanStatusGet(HttpRequest& req, HttpResponse& res);
void handleModbusScanStopPost(HttpRequest& req, HttpResponse& res);
void handleModbusStatsGet(HttpRequest& req, HttpResponse& res);
void handleModbusResetStatsPost(HttpRequest& req, HttpResponse& res);
void handleModbusScanTcpPost(HttpRequest& req, HttpResponse& res);
void handleModbusWritePost(HttpRequest& req, HttpResponse& res);
void handleModbusChartFilesGet(HttpRequest& req, HttpResponse& res);
void handleModbusChartColsGet(HttpRequest& req, HttpResponse& res);
void handleModbusChartDataGet(HttpRequest& req, HttpResponse& res);
#endif
