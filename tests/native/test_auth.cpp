#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <cstring>

// Standalone reimplementation of the HTTP Basic Auth decision logic used in
// webpage_state.cpp::ensureBasicAuth(). Mirrors production:
//   1. admin user/pass checked first -> any method allowed
//   2. viewer (read-only) user checked if enabled AND viewer has a password
//      -> only GET/HEAD/OPTIONS allowed; writes get 405 "Viewer is read-only"
//   3. otherwise -> 401 (requestAuthentication)
// ENABLE_HTTP_BASIC_AUTH=1 is assumed (when 0, auth always passes).
namespace {

enum AuthResult { AUTH_OK, AUTH_401, AUTH_405 };

struct Request {
    std::string authorization;  // raw Authorization header
    std::string method;         // "GET", "POST", ...
    int  lastStatus = 0;        // 0 = none sent
    std::string lastBody;
};

// ---- Base64 decode (standard table, mirrors ESPAsyncWebServer) ----
static bool base64Decode(const std::string& in, std::string& out) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, bits = 0;
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') break;                       // padding: stop decoding
        const char* p = strchr(T, c);
        if (p == 0) return false;                  // invalid base64 char
        val = (val << 6) | (int)(p - T);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((val >> bits) & 0xFF));
        }
    }
    return true;
}

// Splits "user:pass" on the first colon. Mirrors production behaviour where an
// empty username/password pair is simply compared against configured values.
static bool parseCredentials(const std::string& raw, std::string& user, std::string& pass) {
    size_t colon = raw.find(':');
    if (colon == std::string::npos) {
        user = raw;
        pass.clear();
        return true;
    }
    user = raw.substr(0, colon);
    pass = raw.substr(colon + 1);
    return true;
}

struct Config {
    std::string webUser;
    std::string webPass;
    bool authEnabled = true;   // persisted master switch (web.auth_enabled)
    bool viewerEnabled = false;
    std::string viewerUser;
    std::string viewerPass;
};

static bool isReadMethod(const std::string& m) {
    return m == "GET" || m == "HEAD" || m == "OPTIONS";
}

static AuthResult auth(const Config& cfg, Request& req) {
    // 0) Master switch: when auth is disabled nothing is checked.
    if (!cfg.authEnabled) {
        req.lastStatus = 200;
        return AUTH_OK;
    }
    // 0b) No credentials configured -> open (avoids lockout).
    if (cfg.webUser.empty() && cfg.webPass.empty()) {
        req.lastStatus = 200;
        return AUTH_OK;
    }
    // Mirrors request->authenticate(user, pass): decode "Basic <base64>".
    if (req.authorization.compare(0, 6, "Basic ") != 0) {
        req.lastStatus = 401;
        return AUTH_401;
    }
    std::string decoded;
    if (!base64Decode(req.authorization.substr(6), decoded)) {
        req.lastStatus = 401;
        return AUTH_401;
    }
    std::string u, p;
    parseCredentials(decoded, u, p);

    // 1) Admin user first.
    if (u == cfg.webUser && p == cfg.webPass) {
        req.lastStatus = 200;
        return AUTH_OK;
    }
    // 2) Viewer user (only if enabled AND has a password).
    if (cfg.viewerEnabled && cfg.viewerPass.length() > 0 &&
        u == cfg.viewerUser && p == cfg.viewerPass) {
        if (!isReadMethod(req.method)) {
            req.lastStatus = 405;
            req.lastBody = "Viewer is read-only";
            return AUTH_405;
        }
        req.lastStatus = 200;
        return AUTH_OK;
    }
    // 3) Deny.
    req.lastStatus = 401;
    return AUTH_401;
}

static std::string base64Encode(const std::string& in) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        val = (val << 8) | (unsigned char)in[i];
        bits += 8;
        while (bits >= 6) {
            out.push_back(T[(val >> (bits - 6)) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > 0) {
        out.push_back(T[(val << (6 - bits)) & 0x3F]);
    }
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

} // namespace

TEST_CASE("correct admin credentials authenticate with any method") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "POST";
    req.authorization = "Basic " + base64Encode("admin:secret");
    CHECK(auth(cfg, req) == AUTH_OK);
    CHECK(req.lastStatus == 200);
}

TEST_CASE("auth disabled bypasses all checks (no header, no credentials)") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.authEnabled = false;
    Request req; req.method = "POST";
    req.authorization = "";  // no Authorization header at all
    CHECK(auth(cfg, req) == AUTH_OK);
    CHECK(req.lastStatus == 200);
}

TEST_CASE("auth disabled bypasses even wrong credentials") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.authEnabled = false;
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("admin:wrong");
    CHECK(auth(cfg, req) == AUTH_OK);
}

TEST_CASE("auth enabled with no credentials configured is open (no lockout)") {
    Config cfg; cfg.webUser = ""; cfg.webPass = "";
    cfg.authEnabled = true;
    Request req; req.method = "GET";
    req.authorization = "";
    CHECK(auth(cfg, req) == AUTH_OK);
    CHECK(req.lastStatus == 200);
}

TEST_CASE("auth enabled with empty username but set password still enforces") {
    Config cfg; cfg.webUser = ""; cfg.webPass = "secret";
    cfg.authEnabled = true;
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode(":secret");
    CHECK(auth(cfg, req) == AUTH_OK);
    Request bad; bad.method = "GET";
    bad.authorization = "Basic " + base64Encode(":wrong");
    CHECK(auth(cfg, bad) == AUTH_401);
}

TEST_CASE("wrong password is denied with 401") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("admin:wrong");
    CHECK(auth(cfg, req) == AUTH_401);
    CHECK(req.lastStatus == 401);
}

TEST_CASE("missing Authorization header is denied with 401") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "GET";
    req.authorization = "";  // no header at all
    CHECK(auth(cfg, req) == AUTH_401);
    CHECK(req.lastStatus == 401);
}

TEST_CASE("malformed header (no Basic scheme) is denied with 401") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "GET";
    req.authorization = "Bearer abc123";  // wrong scheme
    CHECK(auth(cfg, req) == AUTH_401);
}

TEST_CASE("invalid base64 payload is denied without crash") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "GET";
    req.authorization = "Basic @@!!!invalid@@";  // chars outside base64 alphabet
    CHECK(auth(cfg, req) == AUTH_401);
    CHECK(req.lastStatus == 401);
}

TEST_CASE("empty username is rejected") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode(":secret");  // empty user
    CHECK(auth(cfg, req) == AUTH_401);
}

TEST_CASE("admin with empty password authenticates if configured empty") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("admin:");  // empty pass
    CHECK(auth(cfg, req) == AUTH_OK);
}

TEST_CASE("viewer credentials work for read methods when enabled with password") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.viewerEnabled = true; cfg.viewerUser = "view"; cfg.viewerPass = "ro";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("view:ro");
    CHECK(auth(cfg, req) == AUTH_OK);
    CHECK(req.lastStatus == 200);
}

TEST_CASE("viewer is blocked (405) on write methods") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.viewerEnabled = true; cfg.viewerUser = "view"; cfg.viewerPass = "ro";
    for (std::string m : {"POST", "PUT", "DELETE", "PATCH"}) {
        Request req; req.method = m;
        req.authorization = "Basic " + base64Encode("view:ro");
        CHECK(auth(cfg, req) == AUTH_405);
        CHECK(req.lastStatus == 405);
        CHECK(req.lastBody == "Viewer is read-only");
    }
}

TEST_CASE("viewer with no password set is never accepted") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.viewerEnabled = true; cfg.viewerUser = "view"; cfg.viewerPass = "";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("view:");
    CHECK(auth(cfg, req) == AUTH_401);
}

TEST_CASE("viewer disabled is never accepted") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    cfg.viewerEnabled = false; cfg.viewerUser = "view"; cfg.viewerPass = "ro";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("view:ro");
    CHECK(auth(cfg, req) == AUTH_401);
}

TEST_CASE("multiple failed attempts do not lock out (401 each, no lockout state)") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "secret";
    for (int i = 0; i < 3; ++i) {
        Request req; req.method = "GET";
        req.authorization = "Basic " + base64Encode("admin:bad");
        CHECK(auth(cfg, req) == AUTH_401);
    }
    // Correct credentials still work after failures.
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("admin:secret");
    CHECK(auth(cfg, req) == AUTH_OK);
}

TEST_CASE("password containing colon is parsed correctly (split on first colon)") {
    Config cfg; cfg.webUser = "admin"; cfg.webPass = "a:b:c";
    Request req; req.method = "GET";
    req.authorization = "Basic " + base64Encode("admin:a:b:c");
    CHECK(auth(cfg, req) == AUTH_OK);
}
