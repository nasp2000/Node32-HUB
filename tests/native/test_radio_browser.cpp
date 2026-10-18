#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

// Standalone reimplementation of the radio-browser.info search logic in
// chatgpt/src/radio_audio_runtime.cpp (radioAudioSearchStations + the URL
// builder). Production uses HTTPClient against de1/de2/nl1/at1; here we test
// the pure decision logic: query encoding, limit clamping and the response
// field simplification.

namespace {

std::string urlEncode(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string buildUrl(const std::string& base, const std::string& name,
                     const std::string& genre, const std::string& country,
                     int offset, int limit) {
    if (limit <= 0) limit = 50;
    if (limit > 50) limit = 50;
    if (offset < 0) offset = 0;
    const char* fields = "stationuuid,name,url,url_resolved,favicon,country,countrycode,tags,bitrate,codec,homepage";
    std::string url = base + "/json/stations/search?hidebroken=true&hls=0&limit=" +
                      std::to_string(limit) + "&offset=" + std::to_string(offset) +
                      "&fields=" + fields;
    if (!name.empty()) url += "&name=" + urlEncode(name);
    if (!genre.empty()) url += "&tag=" + urlEncode(genre);
    if (!country.empty()) url += "&countrycode=" + urlEncode(country);
    if (name.empty()) url += "&order=clickcount&reverse=true";
    return url;
}

struct Station {
    std::string name, url, favicon, countrycode, genre;
    int bitrate = 0;
};

// Mirrors radioAudioSearchStations simplification: url_resolved preferred,
// falling back to url. Only the columns the UI uses survive (uuid/country/
// codec/homepage are dropped, keeping a full 25-station page inside the
// 8 KB search-cache slot).
Station simplify(const std::string& name, const std::string& url,
                 const std::string& urlResolved, const std::string& favicon,
                 const std::string& countrycode, const std::string& tags,
                 int bitrate) {
    Station s;
    s.name = name;
    s.url = urlResolved.empty() ? url : urlResolved;
    s.favicon = favicon;
    s.countrycode = countrycode;
    s.genre = tags;
    s.bitrate = bitrate;
    return s;
}

// Mirrors radioAudioFetchMeta simplification for /json/tags and /json/countries.
std::string simplifyTags(const std::vector<std::pair<std::string, std::string>>& rows,
                         int maxN, bool& stopped) {
    std::string out = "[";
    int n = 0;
    stopped = false;
    for (auto& r : rows) {
        std::string name = r.first;
        std::string tag = r.second;
        std::string used = name.empty() ? tag : name;
        if (used.empty()) continue;
        if (n) out += ",";
        out += "{\"name\":\"" + used + "\",\"count\":1}";
        n++;
        if (n >= maxN) { stopped = true; break; }
    }
    out += "]";
    return out;
}

std::string simplifyCountries(const std::vector<std::pair<std::string, std::string>>& rows,
                              int maxN) {
    std::string out = "[";
    int n = 0;
    for (auto& r : rows) {
        if (r.second.empty()) continue; // no iso code -> skip
        if (n) out += ",";
        out += "{\"name\":\"" + r.first + "\",\"code\":\"" + r.second + "\"}";
        n++;
        if (n >= maxN) break;
    }
    out += "]";
    return out;
}

TEST_CASE("tags simplify: name preferred, tag fallback, empty skipped, capped") {
    bool stopped = false;
    auto out = simplifyTags({{"pop", "pop"}, {"", "rock"}, {"", ""}}, 50, stopped);
    CHECK(out.find("\"pop\"") != std::string::npos);
    CHECK(out.find("\"rock\"") != std::string::npos);
    CHECK_FALSE(stopped);
    bool stopped2 = false;
    simplifyTags({{"a","a"},{"b","b"},{"c","c"}}, 2, stopped2);
    CHECK(stopped2);
}

TEST_CASE("countries simplify: iso code required, entries mapped") {
    auto out = simplifyCountries({{"Portugal", "PT"}, {"Germany", ""}, {"USA", "US"}}, 50);
    CHECK(out.find("\"code\":\"PT\"") != std::string::npos);
    CHECK(out.find("\"code\":\"US\"") != std::string::npos);
    CHECK(out.find("\"Germany\"") == std::string::npos);
    CHECK(out.find("\"code\":\"\"") == std::string::npos);
}

// Mirrors radioAudioDefaultStationsJson name derivation: 128 kbps only,
// brand = 80s80s/90s90s, genre stripped of the leading brand, dashes -> spaces,
// trimmed and first letter capitalized.
std::string defaultName(const std::string& url) {
    if (url.find("mp3-192") != std::string::npos) return "";
    std::string brand = "80s80s";
    if (url.find("streams.90s90s.de") != std::string::npos) brand = "90s90s";
    std::string rest = url;
    rest.erase(0, rest.find("streams." + brand + ".de/") + ("streams." + brand + ".de/").size());
    std::string genre = rest.substr(0, rest.find('/'));
    if (genre.rfind("80s80s", 0) == 0) genre = genre.substr(6);
    else if (genre.rfind("90s90s", 0) == 0) genre = genre.substr(6);
    for (auto& c : genre) if (c == '-') c = ' ';
    size_t a = genre.find_first_not_of(' ');
    size_t b = genre.find_last_not_of(' ');
    genre = (a == std::string::npos) ? "" : genre.substr(a, b - a + 1);
    for (auto& c : genre) c = (char)tolower((unsigned char)c);
    if (!genre.empty()) genre[0] = (char)toupper((unsigned char)genre[0]);
    return brand + " " + genre;
}

TEST_CASE("default station name derivation: brand + genre, dedup 192") {
    CHECK(defaultName("https://streams.80s80s.de/80s80sDAB/mp3-128/homepage/") == "80s80s Dab");
    CHECK(defaultName("https://streams.80s80s.de/web/mp3-128/homepage/") == "80s80s Web");
    CHECK(defaultName("https://streams.90s90s.de/90s90s-sommerhits/mp3-128/homepage/") == "90s90s Sommerhits");
    CHECK(defaultName("https://streams.90s90s.de/grunge/mp3-128/homepage/") == "90s90s Grunge");
    CHECK(defaultName("https://streams.80s80s.de/rock/mp3-192/homepage/").empty()); // dedup: 192 skipped
}

// Mirrors radioAudioIsMusicFile: only formats with a bundled decoder are listed.
bool isMusicFile(const std::string& n) {
    std::string l = n;
    for (auto& c : l) c = (char)tolower((unsigned char)c);
    auto ends = [&](const char* s) {
        size_t ns = strlen(s);
        return l.size() >= ns && l.compare(l.size() - ns, ns, s) == 0;
    };
    return ends(".mp3") || ends(".wav") || ends(".flac") || ends(".aac") || ends(".ogg");
}

TEST_CASE("music file filter matches the decoders actually bundled") {
    CHECK(isMusicFile("song.mp3"));
    CHECK(isMusicFile("song.wav"));
    CHECK(isMusicFile("song.flac"));
    CHECK(isMusicFile("song.aac"));
    CHECK(isMusicFile("song.ogg"));
    CHECK(isMusicFile("SONG.MP3"));       // case-insensitive
    CHECK_FALSE(isMusicFile("song.m4a")); // MP4/AAC container unsupported
    CHECK_FALSE(isMusicFile("song.m4b"));
    CHECK_FALSE(isMusicFile("song.txt"));
    CHECK_FALSE(isMusicFile("song"));
}

} // namespace

TEST_CASE("search URL encodes name with spaces and UTF-8") {
    auto u = buildUrl("http://de1.api.radio-browser.info", "Radio Comercial", "", "", 0, 50);
    CHECK(u.find("name=Radio%20Comercial") != std::string::npos);
    auto u2 = buildUrl("http://de1.api.radio-browser.info", "túlio", "", "", 0, 50);
    CHECK(u2.find("name=t%C3%BAlio") != std::string::npos);
}

TEST_CASE("search URL appends genre tag and countrycode only when provided") {
    auto u = buildUrl("http://de1.api.radio-browser.info", "", "jazz", "PT", 0, 50);
    CHECK(u.find("&tag=jazz") != std::string::npos);
    CHECK(u.find("&countrycode=PT") != std::string::npos);
    CHECK(u.find("&name=") == std::string::npos);
    auto bare = buildUrl("http://de1.api.radio-browser.info", "", "", "", 0, 50);
    CHECK(bare.find("&tag=") == std::string::npos);
    CHECK(bare.find("&countrycode=") == std::string::npos);
}

TEST_CASE("browse by country or genre lists top-clicked stations (no name needed)") {
    auto byCountry = buildUrl("http://de1.api.radio-browser.info", "", "", "PT", 0, 50);
    CHECK(byCountry.find("&countrycode=PT") != std::string::npos);
    CHECK(byCountry.find("&order=clickcount&reverse=true") != std::string::npos);
    auto byGenre = buildUrl("http://de1.api.radio-browser.info", "", "jazz", "", 0, 50);
    CHECK(byGenre.find("&tag=jazz") != std::string::npos);
    CHECK(byGenre.find("&order=clickcount&reverse=true") != std::string::npos);
    auto both = buildUrl("http://de1.api.radio-browser.info", "", "rock", "GB", 0, 50);
    CHECK(both.find("&tag=rock") != std::string::npos);
    CHECK(both.find("&countrycode=GB") != std::string::npos);
    CHECK(both.find("&order=clickcount&reverse=true") != std::string::npos);
}

TEST_CASE("name searches keep natural ordering (no clickcount sort)") {
    auto u = buildUrl("http://de1.api.radio-browser.info", "comercial", "pop", "PT", 0, 50);
    CHECK(u.find("&name=comercial") != std::string::npos);
    CHECK(u.find("&order=clickcount") == std::string::npos);
}

TEST_CASE("limit is clamped to 1..50 and offset floors at 0") {
    CHECK(buildUrl("b", "x", "", "", 0, 0).find("limit=50") != std::string::npos);
    CHECK(buildUrl("b", "x", "", "", 0, 250).find("limit=50") != std::string::npos);
    CHECK(buildUrl("b", "x", "", "", 0, 25).find("limit=25") != std::string::npos);
    CHECK(buildUrl("b", "x", "", "", -3, 50).find("offset=0") != std::string::npos);
    CHECK(buildUrl("b", "x", "", "", 20, 50).find("offset=20") != std::string::npos);
}

TEST_CASE("fields param keeps the raw response small") {
    auto u = buildUrl("http://de1.api.radio-browser.info", "jazz", "", "", 0, 50);
    CHECK(u.find("fields=stationuuid,name,url,url_resolved,favicon,country,countrycode,tags,bitrate,codec,homepage") != std::string::npos);
}

TEST_CASE("url_resolved is preferred over url for playback") {
    auto s = simplify("M80", "https://a/plain", "https://cdn/real?sig=1", "", "PT", "pop", 128);
    CHECK(s.url == "https://cdn/real?sig=1");
    auto s2 = simplify("M80", "https://a/plain", "", "", "PT", "pop", 128);
    CHECK(s2.url == "https://a/plain");
}

TEST_CASE("ui fields are preserved through simplification") {
    auto s = simplify("Rádio Comercial", "https://a/stream", "https://cdn/x", "https://f/ic.png",
                      "PT", "pop,news", 128);
    CHECK(s.name == "Rádio Comercial");
    CHECK(s.favicon == "https://f/ic.png");
    CHECK(s.countrycode == "PT");
    CHECK(s.genre == "pop,news");
    CHECK(s.bitrate == 128);
}

// Mirrors radioAudioSearchStations has_more rule: a full raw page (rawCount >=
// limit) means more results probably exist, even when playability filtering
// later drops stations below the limit. This keeps the Discover Next button
// visible on pages that render fewer stations than the requested limit.
static bool hasMore(size_t rawCount, size_t limit) {
    return rawCount >= limit;
}

TEST_CASE("has_more reflects the raw page size, not the filtered count") {
    CHECK(hasMore(25, 25));   // full page -> more may exist
    CHECK(hasMore(50, 25));   // over full page -> more exist
    CHECK_FALSE(hasMore(10, 25)); // short raw page -> last page
    CHECK_FALSE(hasMore(0, 25));  // no results -> no next page
    // Regression: a page that filtered down to 2 playable stations still
    // offers Next when the upstream returned a full page of 25.
    CHECK(hasMore(25, 25));
    CHECK_FALSE(hasMore(24, 25));
}
