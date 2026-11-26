#include "web_routes_tdk_lambda.h"

#include "../../feature_flags.h"
#include "../assets/page_tdk_lambda_asset.h"
#include "../handlers_unified/tdk_lambda_handler.h"
#include "../renderers/page_render_common.h"
#include "route_registry_api.h"

void registerTdkLambdaRoutes(AsyncWebServer& server, const WebRouteContext& context) {
    (void)server;
    // Registered once: served by WiFi and by Ethernet (findApiRouteHandler).
    registerApiRoute("GET",  "/api/tdk-lambda-available",  handleTdkLambdaAvailableGet);
    registerApiRoute("GET",  "/api/tdk-lambda-status",     handleTdkLambdaStatusGet);
    registerApiRoute("GET",  "/api/tdk-lambda-commands",   handleTdkLambdaCommandsGet);
    registerApiRoute("GET",  "/api/tdk-lambda-log",        handleTdkLambdaLogGet);
    registerApiRoute("GET",  "/api/tdk-lambda-config",     handleTdkLambdaConfigGet);
    registerApiRoute("GET",  "/api/tdk-lambda-samples",    handleTdkLambdaSamplesGet);
    registerApiRoute("GET",  "/api/tdk-lambda-csv",        handleTdkLambdaCsvGet);
    registerApiRoute("POST", "/api/tdk-lambda-send",       handleTdkLambdaSendPost);
    registerApiRoute("POST", "/api/tdk-lambda-clear",      handleTdkLambdaClearPost);
    registerApiRoute("POST", "/api/tdk-lambda-config",     handleTdkLambdaConfigPost);
    registerApiRoute("POST", "/api/tdk-lambda-csv-flush",  handleTdkLambdaCsvFlushPost);

#if ENABLE_TDK_LAMBDA == 1
    registerPageGet("/tdk-lambda", [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool   _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xE2\x9A\xA1 TDK-Lambda / Genesys", true, nullptr);
            _aLen = strlen_P(PAGE_TDK_LAMBDA);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char*  pPtr = _pre.c_str(),  *sPtr = _suf.c_str();

        // Long page: a busy browser may ACK slowly (>1 s) and the
        // request timeout would cut the chunked stream.
        allowSlowClientAck(request);
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index,
                    pPtr, pLen, PAGE_TDK_LAMBDA, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
    });
#endif
}
