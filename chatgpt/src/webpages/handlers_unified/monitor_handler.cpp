#include "monitor_handler.h"
#include "../../web_shared_logic.h"

// MON-2: these are acknowledgements only. The old session counter fed an
// `override` flag that nothing gated (the single reader ignored it and the
// counter was never read), so the dead state was removed.
void handleMonitorOpenPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(F("{\"status\":\"ok\"}"));
}

void handleMonitorClosePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(F("{\"status\":\"ok\"}"));
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
