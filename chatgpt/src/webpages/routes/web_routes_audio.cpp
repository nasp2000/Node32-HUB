// src/webpages/routes/web_routes_audio.cpp
// Route registration for audio DAC (UDA1334A)

#include "web_routes_audio.h"

#include <ESPAsyncWebServer.h>
#include "route_registry_api.h"
#include "web_route_context.h"
#include "../http_abstraction.h"
#include "../handlers_unified/audio_handler.h"
#include "../../radio_audio_runtime.h"
#include "../renderers/page_audio_dac_uda1334a_renderer.h"
#include "../renderers/radio_ui_page_renderer.h"
#include "../renderers/page_render_common.h"
#include "../assets/page_radio_ui_asset.h"
#include "../../feature_flags.h"

#if ENABLE_UDA1334A==1

// ---- Body accumulators for POST handlers ----
namespace {
struct AudioBodyAccum {
    static const size_t UDA1334A_CONFIG_MAX_PAYLOAD = 1024;
    static const size_t RADIO_QUEUE_MAX_PAYLOAD = 12288;
    static const size_t RADIO_SCHEDULE_MAX_PAYLOAD = 16384;

    static void udaConfigPost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            if (total == 0 || total > UDA1334A_CONFIG_MAX_PAYLOAD) {
                request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large\"}");
                return;
            }
            String* body = new String();
            if (!body) {
                request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
                return;
            }
            body->reserve(total);
            request->_tempObject = body;
        }
        String* bodyBuffer = reinterpret_cast<String*>(request->_tempObject);
        if (!bodyBuffer) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Request buffer missing\"}");
            return;
        }
        if (bodyBuffer->length() + len > UDA1334A_CONFIG_MAX_PAYLOAD) {
            delete bodyBuffer;
            request->_tempObject = nullptr;
            request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large\"}");
            return;
        }
        bodyBuffer->concat(reinterpret_cast<const char*>(data), len);
        if (index + len < total) return;
        HttpRequest hreq;
        hreq.body = *bodyBuffer;
        hreq.contentType = F("application/json");
        delete bodyBuffer;
        request->_tempObject = nullptr;
        HttpResponse hresp;
        handleUda1334aConfigPost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }

    static void radioQueuePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            if (total == 0 || total > RADIO_QUEUE_MAX_PAYLOAD) {
                request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large\"}");
                return;
            }
            String* body = new String();
            if (!body) {
                request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
                return;
            }
            body->reserve(total);
            request->_tempObject = body;
        }
        String* bodyBuffer = reinterpret_cast<String*>(request->_tempObject);
        if (!bodyBuffer) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Request buffer missing\"}");
            return;
        }
        bodyBuffer->concat(reinterpret_cast<const char*>(data), len);
        if (index + len < total) return;
        HttpRequest hreq;
        hreq.body = *bodyBuffer;
        hreq.contentType = F("application/json");
        delete bodyBuffer;
        request->_tempObject = nullptr;
        HttpResponse hresp;
        handleRadioQueuePost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }

    static void radioSchedulePost(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            if (total == 0 || total > RADIO_SCHEDULE_MAX_PAYLOAD) {
                request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large\"}");
                return;
            }
            String* body = new String();
            if (!body) {
                request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
                return;
            }
            body->reserve(total);
            request->_tempObject = body;
        }
        String* bodyBuffer = reinterpret_cast<String*>(request->_tempObject);
        if (!bodyBuffer) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Request buffer missing\"}");
            return;
        }
        bodyBuffer->concat(reinterpret_cast<const char*>(data), len);
        if (index + len < total) return;
        HttpRequest hreq;
        hreq.body = *bodyBuffer;
        hreq.contentType = F("application/json");
        delete bodyBuffer;
        request->_tempObject = nullptr;
        HttpResponse hresp;
        handleRadioSchedulePost(hreq, hresp);
        request->send(hresp.code, hresp.contentType, hresp.body);
    }
};
} // namespace

#endif // ENABLE_UDA1334A==1

static void registerUda1334aApiRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UDA1334A==1

    registerApiRoute("GET",  "/api/uda1334a-config",     handleUda1334aConfigGet);
    registerApiRoute("POST", "/api/uda1334a-test",       handleUda1334aTestPost);
    registerApiRoute("POST", "/api/uda1334a-timing",     handleUda1334aTimingPost);
    registerApiRoute("POST", "/api/radio/i2s-poc",       handleI2sPocPost);
    registerApiRoute("POST", "/api/radio/mp3-test",      handleMp3TestPost);
    registerApiRoute("GET",  "/api/radio/status",        handleRadioStatusGet);
    registerApiRoute("GET",  "/api/radio/queue",         handleRadioQueueGet);
    registerApiRoute("GET",  "/api/radio/favorites",     handleRadioFavoritesGet);
    registerApiRoute("POST", "/api/radio/stop",          handleRadioStopPost);
    registerApiRoute("POST", "/api/radio/toggle",        handleRadioTogglePost);
    registerApiRoute("POST", "/api/radio/next",          handleRadioNextPost);
    registerApiRoute("POST", "/api/radio/random",        handleRadioRandomPost);
    registerApiRoute("POST", "/api/radio/volume",        handleRadioVolumePost);
    registerApiRoute("POST", "/api/radio/seek",          handleRadioSeekPost);
    registerApiRoute("POST", "/api/radio/play-url",      handleRadioPlayUrlPost);
    registerApiRoute("POST", "/api/radio/play-sd",       handleRadioPlaySdPost);
    registerApiRoute("POST", "/api/radio/favorites/add",  handleRadioFavoritesAddPost);
    registerApiRoute("POST", "/api/radio/favorites/remove",  handleRadioFavoritesRemovePost);
    registerApiRoute("POST", "/api/radio/favorites/play",    handleRadioFavoritesPlayPost);
    registerApiRoute("GET",  "/api/radio/artwork",       handleRadioArtworkGet);
    registerApiRoute("GET",  "/api/radio/search",        handleRadioSearchGet);
    registerApiRoute("GET",  "/api/radio/stations/search", handleRadioStationsSearchGet);
    registerApiRoute("GET",  "/api/radio/stations/tags",     handleRadioStationsTagsGet);
    registerApiRoute("GET",  "/api/radio/stations/countries", handleRadioStationsCountriesGet);
    registerApiRoute("GET",  "/api/radio/stations/default",   handleRadioStationsDefaultGet);
    registerApiRoute("GET",  "/api/radio/library",         handleRadioLibraryGet);    registerApiRoute("GET",  "/api/radio/library/art",     handleRadioLibraryArtGet);    registerApiRoute("GET",  "/api/radio/playlists",     handleRadioPlaylistsGet);    registerApiRoute("POST", "/api/radio/playlist/save",  handleRadioPlaylistSavePost);
    registerApiRoute("POST", "/api/radio/playlist/load",  handleRadioPlaylistLoadPost);
    registerApiRoute("POST", "/api/radio/playlist/delete", handleRadioPlaylistDeletePost);
    registerApiRoute("POST", "/api/radio/shuffle",        handleRadioShufflePost);
    registerApiRoute("POST", "/api/radio/repeat",         handleRadioRepeatPost);
    registerApiRoute("GET",  "/api/radio/resume",         handleRadioResumeGet);
    registerApiRoute("POST", "/api/radio/resume",         handleRadioResumePost);
    registerApiRoute("POST", "/api/radio/sleep",         handleRadioSleepPost);
    registerApiRoute("GET",  "/api/radio/eq",            handleRadioEqGet);
    registerApiRoute("GET",  "/api/radio/schedule",      handleRadioScheduleGet);

    // UPnP / DLNA client
    registerApiRoute("GET",  "/api/radio/upnp/servers",  handleUpnpServersGet);
    registerApiRoute("GET",  "/api/radio/upnp/browse",   handleUpnpBrowseGet);
    registerApiRoute("POST", "/api/radio/upnp/discover", handleUpnpDiscoverPost);

    // POST /api/uda1334a-config (body accum)
    server.on("/api/uda1334a-config", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            AudioBodyAccum::udaConfigPost(request, data, len, index, total);
        });

    // POST /api/radio/queue (body accum)
    server.on("/api/radio/queue", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            AudioBodyAccum::radioQueuePost(request, data, len, index, total);
        });

    // POST /api/radio/eq — body accumulator (JSON with gains array)
    server.on("/api/radio/eq", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            if (index == 0) {
                if (total == 0 || total > 1024) {
                    request->send(413, "application/json", "{\"success\":false,\"message\":\"Payload too large\"}");
                    return;
                }
                String* body = new String();
                if (!body) {
                    request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
                    return;
                }
                body->reserve(total);
                request->_tempObject = body;
            }
            String* bodyBuffer = reinterpret_cast<String*>(request->_tempObject);
            if (!bodyBuffer) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Request buffer missing\"}");
                return;
            }
            bodyBuffer->concat(reinterpret_cast<const char*>(data), len);
            if (index + len < total) return;
            HttpRequest hreq;
            hreq.body = *bodyBuffer;
            hreq.contentType = F("application/json");
            delete bodyBuffer;
            request->_tempObject = nullptr;
            HttpResponse hresp;
            handleRadioEqPost(hreq, hresp);
            request->send(hresp.code, hresp.contentType, hresp.body);
        });

    // POST /api/radio/schedule — body accumulator (JSON slots array)
    server.on("/api/radio/schedule", HTTP_POST,
        [](AsyncWebServerRequest* request) { (void)request; },
        nullptr,
        [&context](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!context.ensureBasicAuth(request)) return;
            AudioBodyAccum::radioSchedulePost(request, data, len, index, total);
        });

#else
    (void)server;
    (void)context;
#endif // ENABLE_UDA1334A==1
}

void registerAudioRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_UDA1334A == 1

    registerUda1334aApiRoutes(server, context);

    // HTML page
    server.on("/uda1334a", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
        if (!response) {
            context.decrementActiveHtmlSessions();
            request->send(500, "text/plain", "Internal Server Error");
            return;
        }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        const size_t bytes = streamUDA1334APageToPrint(*response);
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/uda1334a", bytes, true);
#endif
    });

    server.on("/radio-ui", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (sendNotModifiedIfCached(request)) return;
        if (!context.allowHtmlClient(request)) return;

        static bool _init = false;
        static String _pre, _suf;
        static size_t _aLen = 0;
        if (!_init) {
            buildPageFrame(_pre, _suf, "\xF0\x9F\x8E\xB5 Harmonica Box", true);
            // Radio is a full-screen player with its own 5-button bottom navigation —
            // keep the shared mobile Home/Back bar (auto.js .bottom-nav) off this page.
            _pre += F("<script>window._noBottomNav=true;window._localPagesNeverExpire=true;</script>");
            _aLen = strlen_P(PAGE_RADIO_UI);
            _init = true;
        }
        const size_t pLen = _pre.length(), aLen = _aLen, sLen = _suf.length();
        const char* pPtr = _pre.c_str(), *sPtr = _suf.c_str();

        AsyncWebServerResponse* response = request->beginChunkedResponse(
            "text/html; charset=utf-8",
            [pLen, aLen, sLen, pPtr, sPtr](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                return serveThreeSegments(buf, maxLen, index, pPtr, pLen, PAGE_RADIO_UI, aLen, sPtr, sLen);
            });
        if (!response) { context.decrementActiveHtmlSessions(); request->send(500, "text/plain", "Internal Server Error"); return; }
        response->addHeader("ETag", FIRMWARE_BUILD);
        response->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(response);
#ifndef QUIET_HTTP_LOGS
        context.logServed(request, "/radio-ui", pLen + aLen + sLen, true);
#endif
    });

#else
    (void)server;
    (void)context;
#endif // ENABLE_UDA1334A
}

