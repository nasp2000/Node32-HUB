// Native test for the Access page gate (BACKLOG ACC-4).
//
// ENABLE_PAGE_ACCESS is a COMPILE-TIME flag, so the property to guard is in the
// route sources, not in a runtime function: with the flag off, the /access page
// must not be registered (WiFi or Ethernet) and the home button must not be
// emitted, exactly like /sdcard with ENABLE_PAGE_SDCARD. The APIs stay
// available in every build (scripts/diagnostics/self-test).
//
// This test reads the sources and pins those guards. It is the only way to
// check a preprocessor gate off-device without a second firmware build; it
// fails if someone removes an #if, moves the page route outside it, or gates
// the APIs by accident.
//
// SRC_DIR_PATH is passed by the Makefile rule (absolute path); the fallback
// keeps a manual `cd tests/native && ./test_access_page_gate` working.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string src(const char* relative) {
    std::string text;
    REQUIRE_MESSAGE(readFile(std::string(SRC_DIR_PATH) + "/" + relative, text),
                    "cannot read " << relative);
    return text;
}

// Position just after the first occurrence of `needle`, or npos.
size_t after(const std::string& hay, const std::string& needle) {
    size_t p = hay.find(needle);
    return (p == std::string::npos) ? p : p + needle.size();
}

} // namespace

TEST_CASE("the WiFi /access page route is inside #if ENABLE_PAGE_ACCESS") {
    const std::string file = src("webpages/routes/web_routes_access.cpp");
    const size_t fn = after(file, "void registerAccessRoutes(");
    REQUIRE(fn != std::string::npos);
    const std::string body = file.substr(fn);

    const size_t guard = body.find("#if ENABLE_PAGE_ACCESS==1");
    const size_t route = body.find("registerPageGet(\"/access\"");
    const size_t end = body.find("#endif // ENABLE_PAGE_ACCESS");
    REQUIRE(guard != std::string::npos);
    REQUIRE(route != std::string::npos);
    REQUIRE(end != std::string::npos);
    CHECK(guard < route);          // the guard opens before the page route
    CHECK(route < end);            // and closes after it
    // With the flag off there is no fallback registration of the page.
    CHECK(body.substr(end).find("registerPageGet(\"/access\"") == std::string::npos);
}

TEST_CASE("the Access APIs stay registered in every build") {
    const std::string file = src("webpages/routes/web_routes_access.cpp");
    const size_t apiStart = file.find("static void registerAccessApiRoutes(");
    const size_t apiEnd = file.find("void registerAccessRoutes(");
    REQUIRE(apiStart != std::string::npos);
    REQUIRE(apiEnd != std::string::npos);
    const std::string apiBody = file.substr(apiStart, apiEnd - apiStart);
    // No page gate may wrap the API registrations.
    CHECK(apiBody.find("ENABLE_PAGE_ACCESS") == std::string::npos);
    CHECK(apiBody.find("registerApiRoute(\"GET\",  \"/api/access-config\"") != std::string::npos);
    CHECK(apiBody.find("registerApiRoute(\"GET\",  \"/api/session-history\"") != std::string::npos);
    CHECK(apiBody.find("registerApiRoute(\"POST\", \"/api/access-kick-all\"") != std::string::npos);
}

TEST_CASE("the Ethernet /access page answers 404 when the page is off") {
    const std::string file = src("ethernet_bootstrap_routes.h");
    const size_t at = file.find("path == F(\"/access\")");
    REQUIRE(at != std::string::npos);
    const size_t next = file.find("} else if", at);
    REQUIRE(next != std::string::npos);
    const std::string branch = file.substr(at, next - at);
    CHECK(branch.find("#if ENABLE_PAGE_ACCESS==1") != std::string::npos);
    CHECK(branch.find("streamSharedAccessPage") != std::string::npos);
    CHECK(branch.find("#else") != std::string::npos);
    CHECK(branch.find("404 Not Found") != std::string::npos);
    // The guard must close inside the same branch.
    CHECK(branch.find("#endif") != std::string::npos);
}

TEST_CASE("the home button for /access follows the same flag") {
    const std::string file = src("webpages/renderers/home_page_renderer.h");
    const size_t btn = file.find("SYSTEM_SECTION_ACCESS_BUTTON));");
    REQUIRE(btn != std::string::npos);
    // The nearest preceding #if within the same block must be the page flag.
    const size_t guard = file.rfind("#if ENABLE_PAGE_ACCESS==1", btn);
    REQUIRE(guard != std::string::npos);
    const size_t closing = file.find("#endif", guard);
    REQUIRE(closing != std::string::npos);
    CHECK(closing > btn);          // the button is inside the guarded block
}

TEST_CASE("the gate mirrors the /sdcard page") {
    // Same scheme as the reference implementation: the page registration sits
    // inside #if ENABLE_PAGE_SDCARD, the storage APIs do not.
    const std::string file = src("webpages/routes/web_routes_storage.cpp");
    const size_t guard = file.find("#if ENABLE_PAGE_SDCARD==1");
    const size_t page = file.find("registerPageGet(\"/sdcard\"");
    REQUIRE(guard != std::string::npos);
    REQUIRE(page != std::string::npos);
    CHECK(guard < page);
    CHECK(file.find("#endif // ENABLE_PAGE_SDCARD", page) != std::string::npos);
}
