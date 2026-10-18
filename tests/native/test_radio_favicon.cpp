#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>

// Compiles the production first-party station-favicon derivation header.
#include "radio_favicon.h"

static std::string derive(const char* url) {
    char buf[192];
    uint32_t n = radio_favicon::deriveForStreamUrl(url, buf, sizeof(buf));
    return std::string(buf, n);
}

TEST_CASE("favicon derive: plain https stream host") {
    CHECK(derive("https://streams.80s80s.de/80s80sDAB/mp3-128/homepage/")
          == "https://streams.80s80s.de/favicon.ico");
    CHECK(derive("https://streams.90s90s.de/web/mp3-192/streams.90s90s.de/")
          == "https://streams.90s90s.de/favicon.ico");
}

TEST_CASE("favicon derive: http, port, no path") {
    CHECK(derive("http://195.23.85.126:8455/stream")
          == "https://195.23.85.126/favicon.ico");
    CHECK(derive("http://stream-icy.bauermedia.pt/comercial.mp3")
          == "https://stream-icy.bauermedia.pt/favicon.ico");
    CHECK(derive("https://host.example")
          == "https://host.example/favicon.ico");
}

TEST_CASE("favicon derive: non-http or empty returns nothing") {
    CHECK(derive("sd:/music/track.mp3").empty());
    CHECK(derive("").empty());
    CHECK(derive(nullptr).empty());
    CHECK(derive("ftp://host/x").empty());
    CHECK(derive("https:///no-host").empty());
}

TEST_CASE("favicon derive: query-only URLs still resolve to the host") {
    // A URL with no path (just query) must still yield the host favicon.
    CHECK(derive("https://icecast.example.org/stream?mount=1")
          == "https://icecast.example.org/favicon.ico");
}

TEST_CASE("favicon derive: output buffer bounds") {
    char tiny[8];
    CHECK(radio_favicon::deriveForStreamUrl("https://a.example/x", tiny, sizeof(tiny)) == 0);
    char exact[256];
    uint32_t n = radio_favicon::deriveForStreamUrl("https://streams.80s80s.de/a", exact, sizeof(exact));
    CHECK(n > 0);
    CHECK(strlen(exact) == n);
}
