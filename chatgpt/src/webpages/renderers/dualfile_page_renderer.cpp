#include "dualfile_page_renderer.h"

#include "../../feature_flags.h"
#include "../assets/page_dualfile_asset.h"

size_t sendDualFilePageResponse(AsyncWebServerRequest* request) {
#if ENABLE_DUALFILE==1
    const size_t len = strlen_P(PAGE_DUALFILE);
    AsyncWebServerResponse* response = request->beginResponse_P(
        200, F("text/html; charset=utf-8"),
        (const uint8_t*)PAGE_DUALFILE, len
    );
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
    return len;
#else
    request->send(404, "text/plain", "Not available");
    return 0;
#endif
}
