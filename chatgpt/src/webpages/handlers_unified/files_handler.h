#pragma once
#include "../http_abstraction.h"

void handleSdMountPost(HttpRequest& req, HttpResponse& res);
void handleSdUnmountPost(HttpRequest& req, HttpResponse& res);
void handleSdCreateFilePost(HttpRequest& req, HttpResponse& res);
void handleSdCreateDirPost(HttpRequest& req, HttpResponse& res);
void handleSdRenamePost(HttpRequest& req, HttpResponse& res);
void handleSdDeletePost(HttpRequest& req, HttpResponse& res);
