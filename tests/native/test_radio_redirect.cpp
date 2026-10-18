#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the redirect-walk decision logic used by
// radioAudioResolveStreamUrl() in chatgpt/src/radio_audio_runtime.cpp.
// Production walks the chain with HTTPClient (3xx -> follow Location, up to 6
// hops, only absolute http(s) targets, non-200 fails). The radio stations in
// the default queue (80s80s/90s90s) answer with 302 to a signed CDN URL, and
// the HTTP source does not follow redirects on ESP32, so this resolver runs
// before the stream is opened.

namespace {

struct Hop {
    int code;             // HTTP status code returned by the server
    std::string location; // Location header (used when code is 3xx)
};

struct ResolveResult {
    bool ok;
    std::string url;
    std::string err;
};

bool isHttpUrl(const std::string& u) {
    return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

// Mirrors radioAudioResolveStreamUrl(): walk hops, following absolute 3xx
// locations, stop on 200 (keep URL), fail on non-200 or bad target.
ResolveResult resolveChain(const std::string& inUrl, const std::vector<Hop>& hops) {
    std::string url = inUrl;
    for (size_t i = 0; i < hops.size() && i < 6; i++) {
        const Hop& h = hops[i];
        if (h.code >= 300 && h.code < 400) {
            if (!isHttpUrl(h.location)) {
                return {false, "", "Bad redirect target"};
            }
            url = h.location;
            continue;
        }
        if (h.code != 200) {
            return {false, "", "HTTP " + std::to_string(h.code)};
        }
        break;
    }
    return {true, url, ""};
}

} // namespace

TEST_CASE("direct 200 stream is passed through untouched") {
    auto r = resolveChain("https://a.example/stream", {{200, ""}});
    CHECK(r.ok);
    CHECK(r.url == "https://a.example/stream");
    CHECK(r.err.empty());
}

TEST_CASE("single 302 redirect is resolved to the final URL") {
    // 80s80s pattern: 302 -> signed CDN URL -> 200
    auto r = resolveChain(
        "https://streams.80s80s.de/web/mp3-128/homepage/",
        {{302, "https://cdn.streamabc.example/regc-mp3-128-7712602?sABC=6n721r0q%230&skey=1785863693"},
         {200, ""}});
    CHECK(r.ok);
    CHECK(r.url == "https://cdn.streamabc.example/regc-mp3-128-7712602?sABC=6n721r0q%230&skey=1785863693");
}

TEST_CASE("multi-hop chain is walked in order") {
    auto r = resolveChain("https://a/1",
                          {{301, "https://b/2"}, {307, "https://c/3"}, {200, ""}});
    CHECK(r.ok);
    CHECK(r.url == "https://c/3");
}

TEST_CASE("relative redirect target is rejected") {
    auto r = resolveChain("https://a/1", {{302, "/relative/path"}, {200, ""}});
    CHECK_FALSE(r.ok);
    CHECK(r.err == "Bad redirect target");
}

TEST_CASE("empty Location on a 3xx is rejected") {
    auto r = resolveChain("https://a/1", {{302, ""}});
    CHECK_FALSE(r.ok);
    CHECK(r.err == "Bad redirect target");
}

TEST_CASE("non-HTTP status (404) fails with the status message") {
    auto r = resolveChain("https://a/1", {{404, ""}});
    CHECK_FALSE(r.ok);
    CHECK(r.err == "HTTP 404");
}

TEST_CASE("query string of the resolved URL is preserved verbatim") {
    auto r = resolveChain("https://a/1",
                          {{302, "https://cdn.x/stream?sABC=abc%23x&skey=1785863693"}, {200, ""}});
    CHECK(r.ok);
    CHECK(r.url == "https://cdn.x/stream?sABC=abc%23x&skey=1785863693");
}

TEST_CASE("http and https targets are both accepted") {
    CHECK(resolveChain("http://a/1", {{301, "http://b/2"}, {200, ""}}).ok);
    CHECK(resolveChain("https://a/1", {{301, "https://b/2"}, {200, ""}}).ok);
}

TEST_CASE("redirect limit is capped at 6 hops") {
    // 6 loop iterations process the first 6 responses (all 302); the final 200
    // is never reached, so the last resolved URL wins.
    auto r = resolveChain("https://a/1",
                          {{302, "https://a/2"}, {302, "https://a/3"}, {302, "https://a/4"},
                           {302, "https://a/5"}, {302, "https://a/6"}, {302, "https://a/7"},
                           {200, ""}});
    CHECK(r.ok); // stops at the cap with the last resolved URL (never reaches 200)
    CHECK(r.url == "https://a/7");
}
