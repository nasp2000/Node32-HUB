#pragma once
#include "../http_abstraction.h"

void handleFtpStatusGet(HttpRequest& req, HttpResponse& res);
void handleFtpTestPost(HttpRequest& req, HttpResponse& res);
void handleFtpUploadNowPost(HttpRequest& req, HttpResponse& res);
void handleFtpFoldersGet(HttpRequest& req, HttpResponse& res);
