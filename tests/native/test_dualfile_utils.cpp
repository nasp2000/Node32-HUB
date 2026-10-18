#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>

// Standalone reimplementation of SDSecurity helpers
// Tests the same path normalization and validation logic used by dualfile_handler.cpp

static std::string normalizePath(const std::string& path) {
    std::string p = path;
    if (p.empty() || p[0] != '/') p = "/" + p;
    // Remove trailing slash first (matches production sd_security.h order)
    if (p.length() > 1 && p.back() == '/') p.pop_back();
    // Then collapse multiple slashes
    size_t pos;
    while ((pos = p.find("//")) != std::string::npos)
        p.replace(pos, 2, "/");
    return p;
}

static bool isPathSafe(const std::string& path) {
    if (path.find("..") != std::string::npos) return false;
    if (path.find('\\') != std::string::npos) return false;
    if (path.empty()) return false;
    if (path[0] != '/') return false;
    if (path.find("//") != std::string::npos) return false;
    if (path.length() > 256) return false;
    return true;
}

TEST_CASE("normalizePath basic") {
    CHECK(normalizePath("/") == "/");
    CHECK(normalizePath("foo") == "/foo");
    CHECK(normalizePath("/foo/") == "/foo");
    CHECK(normalizePath("/a/b/c/") == "/a/b/c");
    CHECK(normalizePath("") == "/");
    CHECK(normalizePath("/dir/") == "/dir");
}

TEST_CASE("normalizePath multi-slash") {
    // Production order: remove ONE trailing / (not all), THEN collapse //
    // So "//foo//bar//": remove last / → "//foo//bar/" → collapse → "/foo/bar/"
    CHECK(normalizePath("//foo//bar//") == "/foo/bar/");
    // "///foo///": remove last / → "///foo//" → collapse → "//foo/" → "/foo/"
    CHECK(normalizePath("///foo///") == "/foo/");
    // "/a////b": no trailing / → collapse → "/a/b"
    CHECK(normalizePath("/a////b") == "/a/b");
    // "//": remove last / → "/" → collapse → "/"
    CHECK(normalizePath("//") == "/");
}

TEST_CASE("isPathSafe valid") {
    CHECK(isPathSafe("/"));
    CHECK(isPathSafe("/foo"));
    CHECK(isPathSafe("/foo/bar/baz.gcode"));
    CHECK(isPathSafe("/my-file.txt"));
    CHECK(isPathSafe("/.hidden"));
    CHECK(isPathSafe("/foo_bar"));
    CHECK(isPathSafe("/foo/bar."));

    std::string maxLen(256, 'a');
    maxLen[0] = '/';
    CHECK(isPathSafe(maxLen));
}

TEST_CASE("isPathSafe rejects traversal") {
    CHECK_FALSE(isPathSafe("/foo/../bar"));
    CHECK_FALSE(isPathSafe("/.."));
}

TEST_CASE("isPathSafe rejects bad patterns") {
    CHECK_FALSE(isPathSafe("\\foo"));
    CHECK_FALSE(isPathSafe(""));
    CHECK_FALSE(isPathSafe("foo/bar"));
    CHECK_FALSE(isPathSafe("//foo"));
    CHECK_FALSE(isPathSafe("/foo/..bar"));

    std::string tooLong(257, 'a');
    tooLong[0] = '/';
    CHECK_FALSE(isPathSafe(tooLong));
}
