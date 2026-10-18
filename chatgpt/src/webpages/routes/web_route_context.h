#pragma once

#include <ESPAsyncWebServer.h>

struct WebRouteContext {
    bool (*ensureBasicAuth)(AsyncWebServerRequest* request);
    bool (*allowHtmlClient)(AsyncWebServerRequest* request);
    void (*decrementActiveHtmlSessions)();
    void (*logServed)(AsyncWebServerRequest* request, const char* url, size_t bytes, bool fromPgm);
};

// Declared in chatgpt.ino — firmware version string used as ETag value.
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;

inline String normalizeEtagToken(String token) {
    token.trim();
    if (token.startsWith("W/")) token = token.substring(2);
    token.trim();
    if (token.length() >= 2 && token.startsWith("\"") && token.endsWith("\"")) {
        token = token.substring(1, token.length() - 1);
    }
    token.trim();
    return token;
}

inline bool ifNoneMatchContainsEtag(const String& ifNoneMatch, const char* expectedEtag) {
    if (!expectedEtag || !expectedEtag[0]) return false;
    String raw = ifNoneMatch;
    raw.trim();
    if (raw.length() == 0) return false;
    if (raw == "*") return true;

    int start = 0;
    while (start < raw.length()) {
        int comma = raw.indexOf(',', start);
        String token = (comma < 0) ? raw.substring(start) : raw.substring(start, comma);
        token = normalizeEtagToken(token);
        if (token == expectedEtag) return true;
        if (comma < 0) break;
        start = comma + 1;
    }
    return false;
}

// Sends a 304 Not Modified if the client already has the current firmware's page cached.
// Call AFTER ensureBasicAuth, BEFORE allowHtmlClient so a cache hit consumes no session slot.
// Returns true when a 304 was sent (caller must return immediately).
inline bool sendNotModifiedIfCached(AsyncWebServerRequest* request) {
    if (request->hasHeader("If-None-Match") &&
        (ifNoneMatchContainsEtag(request->header("If-None-Match"), FIRMWARE_BUILD) ||
         ifNoneMatchContainsEtag(request->header("If-None-Match"), FIRMWARE_VERSION))) {
        AsyncWebServerResponse* r = request->beginResponse(304);
        r->addHeader("ETag", FIRMWARE_BUILD);
        r->addHeader("Cache-Control", "private, max-age=15, must-revalidate");
        request->send(r);
        return true;
    }
    return false;
}