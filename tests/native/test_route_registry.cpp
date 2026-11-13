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

// ── A1 (2026-11-13): GET routes are dispatched from the onNotFound catch-all ──
// production dispatches a registered GET from the single catch-all handler
// (dispatchRegisteredGetRoute) instead of installing one AsyncWebHandler per
// path. The decision is: HTTP_GET only, exact method+path lookup in the same
// table. Mirrored here so a regression in those two rules has a host-side test.
namespace {

struct CatchAllDispatch {
    const RouteRegistry& reg;
    explicit CatchAllDispatch(const RouteRegistry& r) : reg(r) {}
    // Returns true = the route matched and the catch-all answered.
    bool dispatch(const std::string& method, const std::string& url) const {
        if (method != "GET") return false;          // POST/PUT/DELETE stay individually registered
        return reg.find("GET", url) != 0;           // handler 0 == null == no route
    }
    size_t getCount() const {
        size_t n = 0;
        for (size_t i = 0; i < reg.count(); ++i) {
            if (reg.routes[i].method == "GET") ++n;
        }
        return n;
    }
};

}  // namespace

TEST_CASE("A1: catch-all serves a registered GET route") {
    RouteRegistry r;
    r.registerRoute("GET", "/api/status", 901);
    r.registerRoute("POST", "/api/status", 902);
    CatchAllDispatch d(r);
    CHECK(d.dispatch("GET", "/api/status"));
    CHECK(d.getCount() == 1);
}

TEST_CASE("A1: catch-all never serves non-GET, and GET never falls into a POST-only route") {
    RouteRegistry r;
    r.registerRoute("POST", "/api/config", 903);
    r.registerRoute("DELETE", "/api/saved", 904);
    r.registerRoute("GET", "/api/status", 905);
    CatchAllDispatch d(r);
    CHECK_FALSE(d.dispatch("POST", "/api/config"));   // stays on its own handler
    CHECK_FALSE(d.dispatch("DELETE", "/api/saved"));
    CHECK_FALSE(d.dispatch("GET", "/api/config"));    // GET on a POST-only path: no dispatch (404/405 path)
    CHECK(d.dispatch("GET", "/api/status"));
    CHECK(d.getCount() == 1);
}

TEST_CASE("A1: catch-all match is exact — no prefix, no trailing slash, no query") {
    RouteRegistry r;
    r.registerRoute("GET", "/api/status", 906);
    CatchAllDispatch d(r);
    CHECK_FALSE(d.dispatch("GET", "/api/stat"));       // prefix
    CHECK_FALSE(d.dispatch("GET", "/api/status/"));    // trailing slash
    CHECK_FALSE(d.dispatch("GET", "/api/status?x=1")); // url() excludes the query; a query here is a bug
    CHECK_FALSE(d.dispatch("GET", "/API/STATUS"));     // case sensitive
    CHECK(d.dispatch("GET", "/api/status"));
}

TEST_CASE("A1: getCount splits the table (the GET share of the handlers saved)") {
    RouteRegistry r;
    for (int i = 0; i < 10; ++i) r.registerRoute(i < 7 ? "GET" : "POST", "/api/r" + std::to_string(i), 1000 + i);
    CatchAllDispatch d(r);
    CHECK(d.getCount() == 7);
    CHECK(r.count() - d.getCount() == 3);   // POST/PUT/DELETE stay individually registered
}

// ── A1b (2026-11-13): page GET routes live in one table, dispatched by the same
// catch-all. Production keeps insertion order and dedupes by path (setupWebServer
// can run twice), and only GET is dispatched from here.
namespace {

struct PageGetTable {
    std::vector<std::pair<std::string, int>> entries;   // path -> handler token
    static const size_t MAX_ENTRIES = 4;                // production PAGE_GET_MAX scaled down
    bool capped = false;

    void add(const std::string& path, int handler) {
        for (const auto& e : entries) if (e.first == path) return;   // first wins (idempotent)
        if (entries.size() >= MAX_ENTRIES) { capped = true; return; }
        entries.push_back({path, handler});
    }
    int dispatch(const std::string& method, const std::string& url) const {
        if (method != "GET") return 0;
        for (const auto& e : entries) if (e.first == url) return e.second;
        return 0;
    }
    size_t count() const { return entries.size(); }
};

}  // namespace

TEST_CASE("A1b: page GET table dispatches exact paths in insertion order") {
    PageGetTable t;
    t.add("/status", 11);
    t.add("/monitor", 12);
    CHECK(t.dispatch("GET", "/status") == 11);
    CHECK(t.dispatch("GET", "/monitor") == 12);
    CHECK(t.dispatch("GET", "/status/") == 0);    // exact only
    CHECK(t.dispatch("GET", "/stat") == 0);       // no prefix
    CHECK(t.count() == 2);
}

TEST_CASE("A1b: duplicate registration keeps the first handler (setupWebServer may run twice)") {
    PageGetTable t;
    t.add("/email", 21);
    t.add("/email", 99);                    // re-registration must not override or grow
    CHECK(t.dispatch("GET", "/email") == 21);
    CHECK(t.count() == 1);
}

TEST_CASE("A1b: table is GET-only and capped without crashing") {
    PageGetTable t;
    t.add("/a", 1); t.add("/b", 2); t.add("/c", 3); t.add("/d", 4);
    CHECK_FALSE(t.capped);
    t.add("/e", 5);
    CHECK(t.capped);
    CHECK(t.count() == 4);
    CHECK(t.dispatch("POST", "/a") == 0);   // POST keeps its own handler
    CHECK(t.dispatch("GET", "/a") == 1);
}
