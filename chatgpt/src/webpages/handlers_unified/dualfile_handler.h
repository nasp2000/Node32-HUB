#pragma once
#include "../http_abstraction.h"
#include <ESPAsyncWebServer.h>

void handleDualFileListGet(HttpRequest& req, HttpResponse& res);
void handleDualFileCopyPost(HttpRequest& req, HttpResponse& res);
void handleDualFileViewGet(HttpRequest& req, HttpResponse& res);
void handleDualFileWritePost(HttpRequest& req, HttpResponse& res);
void handleDualFileUploadPost(AsyncWebServerRequest* request);
void handleDualFileUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
