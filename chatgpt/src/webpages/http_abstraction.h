#pragma once
#include <Arduino.h>

struct HttpRequest {
    String method;
    String path;
    String query;
    String body;
    String contentType;
};

struct HttpResponse {
    int code = 200;
    String contentType = F("text/plain");
    String body;
    bool isBinary = false;
    const uint8_t* binaryData = nullptr;
    size_t binaryLen = 0;
    void (*freeFn)(const uint8_t*) = nullptr;
    bool isRedirect = false;
    String redirectUrl;
    bool isNotModified = false;
    String frameHash;

    void send(int c, const String& ct, const String& b) {
        code = c; contentType = ct; body = b;
        isBinary = false; isRedirect = false; isNotModified = false;
        freeFn = nullptr; binaryData = nullptr; binaryLen = 0;
        frameHash = "";
    }

    void sendJson(const String& json) {
        send(200, F("application/json"), json);
    }

    void sendError(int c) {
        if (c == 503) send(c, F("text/plain"), F("Service Unavailable"));
        else if (c == 500) send(c, F("text/plain"), F("Internal Server Error"));
        else if (c == 404) send(c, F("text/plain"), F("Not Found"));
        else if (c == 400) send(c, F("text/plain"), F("Bad Request"));
        else send(c, F("text/plain"), F("Error"));
    }

    void redirect(const String& url) {
        isRedirect = true; redirectUrl = url;
    }

    void sendNotModified() {
        isNotModified = true;
    }
};

inline String httpStatusToString(int code) {
    switch (code) {
        case 200: return F("200 OK");
        case 302: return F("302 Found");
        case 304: return F("304 Not Modified");
        case 400: return F("400 Bad Request");
        case 401: return F("401 Unauthorized");
        case 404: return F("404 Not Found");
        case 429: return F("429 Too Many Requests");
        case 500: return F("500 Internal Server Error");
        case 503: return F("503 Service Unavailable");
        default: return String(code);
    }
}
