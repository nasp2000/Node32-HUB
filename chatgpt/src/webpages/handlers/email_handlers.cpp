#include "email_handlers.h"
#include "../../feature_flags.h"

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1

#include "../assets/google_apps_script_asset.h"

void registerEmailApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    // Serve Google Apps Script template as a downloadable file
    server.on("/GoogleAppsScript_EmailHandler.js", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "application/javascript",
            [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                const size_t totalSize = strlen_P(GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS);
                if (index >= totalSize) return 0;
                size_t remaining = totalSize - index;
                size_t chunkSize = (remaining < maxLen) ? remaining : maxLen;
                memcpy_P(buffer, GOOGLE_APPS_SCRIPT_EMAIL_HANDLER_JS + index, chunkSize);
                return chunkSize;
            });
        response->addHeader("Content-Disposition",
            "attachment; filename=\"GoogleAppsScript_EmailHandler.js\"");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    });
}

#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL