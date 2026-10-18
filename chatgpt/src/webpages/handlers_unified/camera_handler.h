#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_CAMERA==1
void handleCameraStatusGet(HttpRequest& req, HttpResponse& res);
void handleCameraStartPost(HttpRequest& req, HttpResponse& res);
void handleCameraStopPost(HttpRequest& req, HttpResponse& res);
void handleCameraSnapshotGet(HttpRequest& req, HttpResponse& res);
void handleCameraConfigGet(HttpRequest& req, HttpResponse& res);
void handleCameraConfigPost(HttpRequest& req, HttpResponse& res);
void handleCameraPhotoPost(HttpRequest& req, HttpResponse& res);
void handleCameraLastPhotoGet(HttpRequest& req, HttpResponse& res);
void handleCameraRecordingStatusGet(HttpRequest& req, HttpResponse& res);
void handleCameraRecordingStartPost(HttpRequest& req, HttpResponse& res);
void handleCameraRecordingStopPost(HttpRequest& req, HttpResponse& res);
void handleCameraMotionDiffGet(HttpRequest& req, HttpResponse& res);
void handleCameraMotionResetPost(HttpRequest& req, HttpResponse& res);
void handleCameraVisionBurstPost(HttpRequest& req, HttpResponse& res);
void handleCameraVisionResetPost(HttpRequest& req, HttpResponse& res);
void handleCameraFilesGet(HttpRequest& req, HttpResponse& res);
void handleCameraDeletePost(HttpRequest& req, HttpResponse& res);
#endif
