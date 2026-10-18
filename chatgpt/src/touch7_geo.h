#pragma once

// touch7_geo.h — pure place-search / auto-locate helpers for TouchMeteo-7.
// No Arduino dependencies: unit-tested by tests/native/test_touch7_geo.cpp.
// Network fetch lives in touch7_weather.cpp; HTTP handlers in
// touch7_handler.cpp. Providers (both keyless):
//   search: https://geocoding-api.open-meteo.com/v1/search?name=X&count=5
//   auto:   http://ip-api.com/json/?fields=status,message,city,country,lat,lon

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TOUCH7_GEO_MAX 5

struct Touch7GeoCand {
    char name[64];
    char country[48];
    char admin1[48];
    double lat;
    double lon;
};

struct Touch7GeoAuto {
    char status[16];
    char city[64];
    char country[48];
    double lat;
    double lon;
};

// URL-encode (RFC 3986 unreserved kept, space -> %20). Always NUL-terminates.
static inline void touch7GeoUrlEncode(const char* in, char* out, size_t n) {
    static const char* kHex = "0123456789ABCDEF";
    size_t o = 0;
    if (!in || !out || n == 0) return;
    for (size_t i = 0; in[i] != 0; i++) {
        unsigned char c = (unsigned char)in[i];
        int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (keep) {
            if (o + 1 >= n) break;
            out[o++] = (char)c;
        } else {
            if (o + 3 >= n) break;
            out[o++] = '%';
            out[o++] = kHex[(c >> 4) & 0xF];
            out[o++] = kHex[c & 0xF];
        }
    }
    out[o] = 0;
}

// URL-decode %XX and '+'. Always NUL-terminates.
static inline void touch7GeoUrlDecode(const char* in, char* out, size_t n) {
    size_t o = 0;
    if (!in || !out || n == 0) return;
    for (size_t i = 0; in[i] != 0; i++) {
        if (o + 1 >= n) break;
        if (in[i] == '+') {
            out[o++] = ' ';
        } else if (in[i] == '%' && in[i + 1] && in[i + 2]) {
            char h[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = 0;
}

// --- minimal JSON scanners (objects/arrays/strings/numbers, escape-aware) ---

static inline const char* touch7GeoSkipWs(const char* p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

// Find `"key"` at object level, return pointer just past ':' (or NULL).
static inline const char* touch7GeoFindKey(const char* obj, const char* key) {
    if (!obj || !key) return NULL;
    size_t kl = strlen(key);
    const char* p = obj;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char* c = touch7GeoSkipWs(p + 1 + kl + 1);
            if (c && *c == ':') return c + 1;
        }
        p++;
    }
    return NULL;
}

// Copy a JSON string value (escape-aware, \" \\ \/ \b \f \n \r \t, \uXXXX
// replaced by '?'). Returns pointer past closing quote (or NULL).
static inline const char* touch7GeoCopyStr(const char* p, char* out, size_t n) {
    if (!p || !out || n == 0) return NULL;
    p = touch7GeoSkipWs(p);
    if (!p || *p != '"') return NULL;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        char ch = *p;
        if (ch == '\\' && p[1]) {
            p++;
            switch (*p) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u':
                    ch = '?';
                    if (p[1] && p[2] && p[3] && p[4]) p += 4;
                    break;
                default: break;
            }
        }
        if (o + 1 < n) out[o++] = ch;
        p++;
    }
    out[o] = 0;
    return (*p == '"') ? p + 1 : NULL;
}

static inline bool touch7GeoGetStr(const char* obj, const char* key, char* out, size_t n) {
    const char* v = touch7GeoFindKey(obj, key);
    if (!v) {
        if (out && n) out[0] = 0;
        return false;
    }
    return touch7GeoCopyStr(v, out, n) != NULL;
}

static inline bool touch7GeoGetNum(const char* obj, const char* key, double* out) {
    const char* v = touch7GeoFindKey(obj, key);
    if (!v) return false;
    v = touch7GeoSkipWs(v);
    if (!v || (*v != '-' && *v != '+' && (*v < '0' || *v > '9'))) return false;
    if (out) *out = strtod(v, NULL);
    return true;
}

// Parse geocoding `{"results":[{name,country,admin1,latitude,longitude}...]}`.
// Returns candidate count (0 = none/error). Never writes past max.
static inline int touch7GeoParseSearch(const char* json, Touch7GeoCand* out, int max) {
    if (!json || !out || max <= 0) return 0;
    const char* r = touch7GeoFindKey(json, "results");
    if (!r) return 0;
    r = touch7GeoSkipWs(r);
    if (!r || *r != '[') return 0;
    r++;
    int n = 0;
    while (n < max) {
        r = touch7GeoSkipWs(r);
        if (!r || *r == ']' || *r == 0) break;
        if (*r != '{') {
            r++;
            continue;
        }
        // Find matching '}' (escape-aware).
        const char* q = r;
        int depth = 0;
        bool inStr = false;
        while (*q) {
            if (inStr) {
                if (*q == '\\' && q[1]) q++;
                else if (*q == '"') inStr = false;
            } else if (*q == '"') {
                inStr = true;
            } else if (*q == '{') {
                depth++;
            } else if (*q == '}') {
                depth--;
                if (depth == 0) break;
            }
            q++;
        }
        if (depth != 0 || *q != '}') break;
        // Extract fields from [r, q].
        size_t len = (size_t)(q - r) + 1;
        char item[768];
        if (len >= sizeof(item)) len = sizeof(item) - 1;
        memcpy(item, r, len);
        item[len] = 0;
        Touch7GeoCand* c = &out[n];
        c->name[0] = 0; c->country[0] = 0; c->admin1[0] = 0;
        c->lat = 0.0; c->lon = 0.0;
        touch7GeoGetStr(item, "name", c->name, sizeof(c->name));
        touch7GeoGetStr(item, "country", c->country, sizeof(c->country));
        touch7GeoGetStr(item, "admin1", c->admin1, sizeof(c->admin1));
        touch7GeoGetNum(item, "latitude", &c->lat);
        touch7GeoGetNum(item, "longitude", &c->lon);
        if (c->name[0]) n++;
        r = q + 1;
        r = touch7GeoSkipWs(r);
        if (*r == ',') r++;
    }
    return n;
}

// Parse ip-api `{"status","city","country","lat","lon"}`. True on success.
static inline bool touch7GeoParseAuto(const char* json, Touch7GeoAuto* out) {
    if (!json || !out) return false;
    out->status[0] = 0; out->city[0] = 0; out->country[0] = 0;
    out->lat = 0.0; out->lon = 0.0;
    touch7GeoGetStr(json, "status", out->status, sizeof(out->status));
    if (strcmp(out->status, "success") != 0) return false;
    touch7GeoGetStr(json, "city", out->city, sizeof(out->city));
    touch7GeoGetStr(json, "country", out->country, sizeof(out->country));
    touch7GeoGetNum(json, "lat", &out->lat);
    touch7GeoGetNum(json, "lon", &out->lon);
    return true;
}
