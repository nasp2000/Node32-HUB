#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

struct HttpRequest {
    String method;
    String path;
    String query;
    String body;
    String contentType;
    String authHeader;
};

struct HttpResponse {
    int code = 200;
    String contentType = "text/plain";
    String body;
    bool isBinary = false;
    const uint8_t* binaryData = nullptr;
    size_t binaryLen = 0;
    void (*freeFn)(const uint8_t*) = nullptr;
    bool isRedirect = false;
    String redirectUrl;

    void sendJson(const String& json) { body = json; contentType = "application/json"; }
    void sendText(const String& text) { body = text; contentType = "text/plain; charset=utf-8"; }
    void sendHtml(const String& html) { body = html; contentType = "text/html; charset=utf-8"; }
    void send(int code_, const String& ct, const String& b) { code = code_; contentType = ct; body = b; }
    void redirect(const String& url, int code_ = 302) { code = code_; isRedirect = true; redirectUrl = url; }
};

using HttpHandler = void(*)(HttpRequest&, HttpResponse&);
using HttpBodyHandler = void(*)(HttpRequest&, HttpResponse&, const uint8_t* data, size_t len, size_t index, size_t total);

struct RouteEntry {
    String method;
    String path;
    HttpHandler handler;
};

class IWebServer {
public:
    virtual ~IWebServer() = default;

    virtual bool init(uint16_t port = 80) = 0;
    virtual void loop() = 0;

    virtual void addRoute(const String& method, const String& path, HttpHandler handler) = 0;
    virtual void addStaticRoute(const String& path, const String& contentType, const char* data, size_t len) = 0;
    virtual void addBodyRoute(const String& method, const String& path, HttpBodyHandler handler) = 0;

    virtual void serveStatic(const String& path, const String& contentType, const String& content) = 0;
    virtual void serveFile(const String& path, const String& localPath, const String& contentType) = 0;

    virtual bool authenticate(HttpRequest& req, HttpResponse& res) = 0;
    virtual void setAuthCallback(std::function<bool(const String& user, const String& pass)> cb) = 0;

    virtual size_t activeSessions() const = 0;
    virtual void broadcast(const String& event, const String& data) = 0;
};
