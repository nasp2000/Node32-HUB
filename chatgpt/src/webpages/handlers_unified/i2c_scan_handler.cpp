#include "i2c_scan_handler.h"
#include "../../i2c_scan.h"

void handleI2cScanGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(runI2cScanJson());
}
