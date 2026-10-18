#include "monitor_handler.h"
#include "../../monitor_session_state.h"
#include "../../web_shared_logic.h"

void handleMonitorOpenPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    monitorOpenIncrement();
    res.sendJson(F("{\"status\":\"ok\",\"override\":true}"));
}

void handleMonitorClosePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    monitorOpenDecrement();
    res.sendJson(F("{\"status\":\"ok\",\"override\":false}"));
}

void handleClearBufferPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String responseBody;
    if (!sharedWebClearReceivedBuffer(responseBody)) {
        res.send(503, "application/json", F("{\"status\":\"busy\"}"));
        return;
    }
    res.send(200, "application/json", responseBody);
}
