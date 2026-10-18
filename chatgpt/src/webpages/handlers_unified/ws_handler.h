#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "../http_abstraction.h"
#include "../../feature_flags.h"

#if ENABLE_ASYNC_WEBSOCKET == 1

#include "../../grbl_web_api.h"

static String _wsJsonEscape(const String& raw) {
    String out;
    out.reserve(raw.length() + 8);
    for (size_t i = 0; i < raw.length(); i++) {
        const char c = raw[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

static AsyncWebSocket _wsGrbl("/ws/grbl");
static unsigned long _wsLastStatusPush = 0;
static const unsigned long WS_STATUS_INTERVAL_MS = 100;

static void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    (void)server;
    (void)data;
    (void)len;
    (void)arg;
    if (type == WS_EVT_CONNECT) {
        String statusJson = grblWebBuildStatusJson("ws-client");
        client->text(statusJson);
    } else if (type == WS_EVT_DISCONNECT) {
    } else if (type == WS_EVT_PONG) {
    } else if (type == WS_EVT_ERROR) {
    }
}

inline void initWsServer(AsyncWebServer& server) {
    _wsGrbl.onEvent(_onWsEvent);
    server.addHandler(&_wsGrbl);
}

inline void wsGrblBroadcastStatus() {
    if (_wsGrbl.count() == 0) return;
    String json = grblWebBuildStatusJson("ws-client");
    _wsGrbl.textAll(json);
}

inline void wsGrblBroadcastConsole(const String& text) {
    if (_wsGrbl.count() == 0) return;
    String msg;
    msg.reserve(256 + text.length());
    msg += "{\"type\":\"console\",\"text\":\"";
    msg += _wsJsonEscape(text);
    msg += "\"}";
    _wsGrbl.textAll(msg);
}

inline void wsGrblBroadcastProduction(const char* type, const String& message) {
    if (_wsGrbl.count() == 0) return;
    String msg;
    msg.reserve(256 + strlen(type) + message.length());
    msg += "{\"type\":\"production\",\"eventType\":\"";
    msg += _wsJsonEscape(String(type));
    msg += "\",\"message\":\"";
    msg += _wsJsonEscape(message);
    msg += "\"}";
    _wsGrbl.textAll(msg);
}

inline void wsGrblLoop() {
    if (_wsGrbl.count() == 0) return;
    _wsGrbl.cleanupClients();
    unsigned long now = millis();
    if (now - _wsLastStatusPush >= WS_STATUS_INTERVAL_MS) {
        _wsLastStatusPush = now;
        wsGrblBroadcastStatus();
    }
}

#else

inline void initWsServer(AsyncWebServer&) {}
inline void wsGrblBroadcastStatus() {}
inline void wsGrblBroadcastConsole(const String&) {}
inline void wsGrblBroadcastProduction(const char*, const String&) {}
inline void wsGrblLoop() {}

#endif
