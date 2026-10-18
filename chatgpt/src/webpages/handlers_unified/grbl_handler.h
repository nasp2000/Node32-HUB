#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_GRBL == 1

// GET /api/grbl/status
void handleGrblStatusGet(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/debug
void handleGrblDebugGet(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/usb-restart
void handleGrblUsbRestartPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/transport
void handleGrblTransportPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/usb-line-coding
void handleGrblUsbLineCodingPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/auto-detect-baud
void handleGrblAutoDetectBaudPost(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/control-token
void handleGrblControlTokenGet(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/events
void handleGrblEventsGet(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/send
void handleGrblSendPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/override
void handleGrblOverridePost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/stream
void handleGrblStreamPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/queue-start
void handleGrblQueueStartPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/queue-stop
void handleGrblQueueStopPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/pause
void handleGrblPausePost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/resume
void handleGrblResumePost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/resume-alarm
void handleGrblResumeAlarmPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/cancel
void handleGrblCancelPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/poll-interval
void handleGrblPollIntervalPost(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/list-gcode
void handleGrblListGcodeGet(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/read-gcode-text
void handleGrblReadGcodeTextGet(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/frame-bbox
void handleGrblFrameBboxGet(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/create-dir
void handleGrblCreateDirPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/delete-path
void handleGrblDeletePathPost(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/psram-file
void handleGrblPsramFileGet(HttpRequest& req, HttpResponse& res);
// GET /api/grbl/psram-read-text
void handleGrblPsramReadTextGet(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/psram-upload (body in req.body)
void handleGrblPsramUploadPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/psram-stream
void handleGrblPsramStreamPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/psram-clear
void handleGrblPsramClearPost(HttpRequest& req, HttpResponse& res);
// POST /api/grbl/upload-gcode-stream (body in req.body)
void handleGrblUploadGcodeStreamPost(HttpRequest& req, HttpResponse& res);

// Probe grid / height map
void handleGrblProbeGridGet(HttpRequest& req, HttpResponse& res);
void handleGrblProbeGridPost(HttpRequest& req, HttpResponse& res);
void handleGrblProbeGridCancelPost(HttpRequest& req, HttpResponse& res);
void handleGrblHeightMapGet(HttpRequest& req, HttpResponse& res);
void handleGrblHeightMapEnablePost(HttpRequest& req, HttpResponse& res);
void handleGrblHeightMapClearPost(HttpRequest& req, HttpResponse& res);
#endif // ENABLE_GRBL
