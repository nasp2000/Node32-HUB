// test_touch7_geo.cpp — TouchMeteo-7 geo helper tests (host doctest).
// Covers touch7_geo.h: URL encode/decode, geocoding search parsing,
// ip-api auto-locate parsing. Standalone: no Arduino headers required.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Compiles the PRODUCTION header directly (pure C, stdlib only).
#include "../../chatgpt/src/touch7_geo.h"

TEST_CASE("url encode keeps unreserved, encodes rest") {
    char out[64];
    touch7GeoUrlEncode("Arraiolos", out, sizeof(out));
    CHECK(!strcmp(out, "Arraiolos"));
    touch7GeoUrlEncode("Sao Joao da Madeira", out, sizeof(out));
    CHECK(!strcmp(out, "Sao%20Joao%20da%20Madeira"));
    touch7GeoUrlEncode("Vila Nova de Gaia", out, sizeof(out));
    CHECK(!strcmp(out, "Vila%20Nova%20de%20Gaia"));
    touch7GeoUrlEncode("a/b?c=d&e", out, sizeof(out));
    CHECK(!strcmp(out, "a%2Fb%3Fc%3Dd%26e"));
}

TEST_CASE("url encode truncates safely") {
    char out[8];
    touch7GeoUrlEncode("Arraiolos", out, sizeof(out));
    CHECK(out[7] == 0);
    CHECK(!strncmp(out, "Arraiol", 7));
}

TEST_CASE("url decode %XX and plus") {
    char out[64];
    touch7GeoUrlDecode("Sao%20Joao", out, sizeof(out));
    CHECK(!strcmp(out, "Sao Joao"));
    touch7GeoUrlDecode("a+b", out, sizeof(out));
    CHECK(!strcmp(out, "a b"));
    touch7GeoUrlDecode("%C3%A9vora", out, sizeof(out));
    CHECK(out[0] != 0);
}

TEST_CASE("geocoding parse two candidates") {
    const char* js =
        "{\"results\":["
        "{\"id\":1,\"name\":\"Arraiolos\",\"latitude\":38.7167,\"longitude\":-7.99,"
        "\"country_code\":\"PT\",\"country\":\"Portugal\",\"admin1\":\"Evora\"},"
        "{\"id\":2,\"name\":\"Arraiolos Novo\",\"latitude\":38.5,\"longitude\":-8.1,"
        "\"country_code\":\"PT\",\"country\":\"Portugal\",\"admin1\":\"Evora\"}]}";
    Touch7GeoCand c[TOUCH7_GEO_MAX];
    int n = touch7GeoParseSearch(js, c, TOUCH7_GEO_MAX);
    CHECK(n == 2);
    CHECK(!strcmp(c[0].name, "Arraiolos"));
    CHECK(!strcmp(c[0].country, "Portugal"));
    CHECK(!strcmp(c[0].admin1, "Evora"));
    CHECK(c[0].lat > 38.71);
    CHECK(c[0].lat < 38.72);
    CHECK(c[0].lon > -8.0);
    CHECK(c[0].lon < -7.98);
    CHECK(!strcmp(c[1].name, "Arraiolos Novo"));
}

TEST_CASE("geocoding parse empty and malformed") {
    Touch7GeoCand c[TOUCH7_GEO_MAX];
    CHECK(touch7GeoParseSearch("{\"results\":[]}", c, TOUCH7_GEO_MAX) == 0);
    CHECK(touch7GeoParseSearch("{\"error\":true}", c, TOUCH7_GEO_MAX) == 0);
    CHECK(touch7GeoParseSearch("not json", c, TOUCH7_GEO_MAX) == 0);
    CHECK(touch7GeoParseSearch(NULL, c, TOUCH7_GEO_MAX) == 0);
}

TEST_CASE("geocoding parse escaped quotes in name") {
    const char* js =
        "{\"results\":[{\"name\":\"Vila \\\"Nova\\\"\",\"latitude\":41.1,"
        "\"longitude\":-8.6,\"country\":\"Portugal\",\"admin1\":\"Porto\"}]}";
    Touch7GeoCand c[TOUCH7_GEO_MAX];
    int n = touch7GeoParseSearch(js, c, TOUCH7_GEO_MAX);
    CHECK(n == 1);
    CHECK(!strcmp(c[0].name, "Vila \"Nova\""));
    CHECK(!strcmp(c[0].admin1, "Porto"));
}

TEST_CASE("autoloc parse success") {
    const char* js =
        "{\"status\":\"success\",\"country\":\"Portugal\",\"city\":\"Lisboa\","
        "\"lat\":38.72,\"lon\":-9.14}";
    Touch7GeoAuto a;
    CHECK(touch7GeoParseAuto(js, &a));
    CHECK(!strcmp(a.status, "success"));
    CHECK(!strcmp(a.city, "Lisboa"));
    CHECK(!strcmp(a.country, "Portugal"));
    CHECK(a.lat > 38.7);
    CHECK(a.lat < 38.8);
    CHECK(a.lon > -9.2);
    CHECK(a.lon < -9.1);
}

TEST_CASE("autoloc parse failure") {
    const char* js = "{\"status\":\"fail\",\"message\":\"private range\"}";
    Touch7GeoAuto a;
    CHECK(!touch7GeoParseAuto(js, &a));
    CHECK(!strcmp(a.status, "fail"));
    CHECK(!touch7GeoParseAuto("garbage", &a));
}
