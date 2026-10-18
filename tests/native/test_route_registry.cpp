#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the API route registry semantics used by
// route_registry_api.cpp: a fixed-capacity table (MAX_API_ROUTES), exact
// method+path lookup, and method-agnostic path existence (405 vs 404).
// Mirrors production behavior: no duplicate detection, full table silently
// drops new registrations, unknown methods are stored but never match a known
// HTTP verb in findApiRouteHandler (production maps unknown -> HTTP_GET).
namespace {

typedef int Handler;   // handler token (0 = null). Standalone tests don't call handlers.

struct RouteEntry {
    std::string method;
    std::string path;
    Handler handler;
};

static const int MAX_API_ROUTES = 256;

struct RouteRegistry {
    std::vector<RouteEntry> routes;

    bool registerRoute(const std::string& method, const std::string& path, Handler h) {
        if (routes.size() >= (size_t)MAX_API_ROUTES) return false;  // silently ignored in prod
        RouteEntry e;
        e.method = method;
        e.path = path;
        e.handler = h;
        routes.push_back(e);
        return true;
    }

    size_t count() const { return routes.size(); }

    // Exact method+path match (mirrors findApiRouteHandler).
    Handler find(const std::string& method, const std::string& path) const {
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].method == method && routes[i].path == path) return routes[i].handler;
        }
        return 0;
    }

    // Method-agnostic path existence (mirrors isKnownApiPath).
    bool isKnownPath(const std::string& path) const {
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].path == path) return true;
        }
        return false;
    }

    // Same method+path already present? (NOT enforced by production today.)
    bool hasDuplicate(const std::string& method, const std::string& path) const {
        int n = 0;
        for (size_t i = 0; i < routes.size(); ++i) {
            if (routes[i].method == method && routes[i].path == path) ++n;
        }
        return n > 1;
    }
};

} // namespace

TEST_CASE("register route stores method+path and find returns it") {
    RouteRegistry r;
    Handler h = 101;
    REQUIRE(r.registerRoute("GET", "/api/status", h));
    CHECK(r.count() == 1);
    CHECK(r.find("GET", "/api/status") == h);
    // Wrong method or wrong path -> not found
    CHECK(r.find("POST", "/api/status") == 0);
    CHECK(r.find("GET", "/api/other") == 0);
}

TEST_CASE("multiple routes with different methods on same path are distinct") {
    RouteRegistry r;
    Handler hGet = 201;
    Handler hPost = 202;
    r.registerRoute("GET", "/api/config", hGet);
    r.registerRoute("POST", "/api/config", hPost);
    CHECK(r.count() == 2);
    CHECK(r.find("GET", "/api/config") == hGet);
    CHECK(r.find("POST", "/api/config") == hPost);
    CHECK(r.find("DELETE", "/api/config") == 0);
}

TEST_CASE("duplicate registration is currently allowed (first one wins on find)") {
    // Documents current production behavior: registerApiRoute() has no
    // duplicate guard, and findApiRouteHandler() scans from index 0, so the
    // FIRST registered handler for a method+path is the one dispatched.
    RouteRegistry r;
    Handler h1 = 301;
    Handler h2 = 302;
    r.registerRoute("GET", "/api/dup", h1);
    r.registerRoute("GET", "/api/dup", h2);
    CHECK(r.hasDuplicate("GET", "/api/dup"));
    CHECK(r.find("GET", "/api/dup") == h1);
}

TEST_CASE("MAX_API_ROUTES full table rejects new registrations without crash") {
    RouteRegistry r;
    for (int i = 0; i < MAX_API_ROUTES; ++i) {
        CHECK(r.registerRoute("GET", "/api/r" + std::to_string(i), 400 + i));
    }
    CHECK(r.count() == (size_t)MAX_API_ROUTES);
    // Overflow registration is silently dropped (production returns void)
    CHECK_FALSE(r.registerRoute("GET", "/api/overflow", 999));
    CHECK(r.count() == (size_t)MAX_API_ROUTES);
    CHECK(r.find("GET", "/api/overflow") == 0);
    // Existing routes still resolvable
    CHECK(r.find("GET", "/api/r0") != 0);
    CHECK(r.find("GET", "/api/r255") != 0);
}

TEST_CASE("path without leading slash is stored verbatim (not auto-corrected)") {
    // Production does not normalize paths. A route registered without a leading
    // slash will never be reachable via an HTTP request path.
    RouteRegistry r;
    r.registerRoute("GET", "api/no-slash", 501);
    CHECK(r.find("GET", "api/no-slash") != 0);
    CHECK(r.find("GET", "/api/no-slash") == 0);
    CHECK_FALSE(r.isKnownPath("/api/no-slash"));
    CHECK(r.isKnownPath("api/no-slash"));
}

TEST_CASE("unknown method is stored but does not shadow known methods") {
    RouteRegistry r;
    r.registerRoute("GET", "/api/x", 601);
    r.registerRoute("FOO", "/api/x", 602);
    CHECK(r.find("GET", "/api/x") == 601);
    CHECK(r.find("FOO", "/api/x") == 602);
    CHECK(r.find("PUT", "/api/x") == 0);
}

TEST_CASE("null handler registration is accepted by table (no crash on lookup)") {
    RouteRegistry r;
    r.registerRoute("GET", "/api/null", 0);
    CHECK(r.count() == 1);
    CHECK(r.find("GET", "/api/null") == 0);
    CHECK(r.isKnownPath("/api/null"));
}

TEST_CASE("isKnownPath is method-agnostic (405 vs 404 distinction)") {
    RouteRegistry r;
    r.registerRoute("GET", "/api/known", 701);
    r.registerRoute("POST", "/api/known", 702);
    // Path known even for a method never registered
    CHECK(r.isKnownPath("/api/known"));
    // Unknown path
    CHECK_FALSE(r.isKnownPath("/api/unknown"));
    // Empty path is not known
    CHECK_FALSE(r.isKnownPath(""));
}

TEST_CASE("enumerate all routes returns complete non-failing listing") {
    RouteRegistry r;
    const int N = 64;
    for (int i = 0; i < N; ++i) {
        r.registerRoute(i % 2 == 0 ? "GET" : "POST", "/api/e" + std::to_string(i), 800 + i);
    }
    size_t found = 0;
    for (size_t i = 0; i < r.count(); ++i) {
        CHECK_FALSE(r.routes[i].path.empty());
        CHECK_FALSE(r.routes[i].method.empty());
        ++found;
    }
    CHECK(found == (size_t)N);
}
