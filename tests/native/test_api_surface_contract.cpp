// Native contract tests for three crawl findings that share one shape:
// a page/API pair where one side was never wired to the other.
//
//   LPT-1  the LPT/DB25 page reads printer + e-mail + timeout settings from
//          GET /api/config?section=system, which never returned them.
//   STR-1  the structures page reads `structures` *and* the RS232 summary from
//          the same section (it also read `saved_configs`).
//   GS-1   the Google Script page reads `google_email.google_script_url` from
//          GET /api/config?section=email, which only had smtp + email.
//   DO-1   GET /api/do/status|config were WiFi-only.
//   HX-2   only POST /api/hx711-config existed; GET config, measure and tare
//          were missing.
//
// The page/API side is checked as source contracts: the keys each page reads
// must be produced by the handler, and every route must go through
// registerApiRoute() — the shared registry that the Ethernet dispatcher also
// resolves (ethernet_bootstrap_routes.h → findApiRouteHandler), which is what
// makes a route transport-independent.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string readSrc(const char *rel) {
    std::string path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot read " << path);
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Payload body of handleConfigGet for one section. Anchored on the first field
// the section writes (`doc["system_name"]` / `doc["smtp"]`) because the
// `if (section == ...)` marker also appears in the capacity pre-computation.
std::string sectionPayload(const std::string &handler, const char *anchor) {
    const size_t start = handler.find(anchor);
    REQUIRE_MESSAGE(start != std::string::npos, "payload anchor not found: " << anchor);
    const size_t end = handler.find("\n    if (section ==", start);
    REQUIRE_MESSAGE(end != std::string::npos, "end of payload block not found after " << anchor);
    return handler.substr(start, end - start);
}

bool hasAll(const std::string &hay, const std::vector<const char *> &needles) {
    for (const char *n : needles) {
        INFO("missing: " << n);
        if (hay.find(n) == std::string::npos) return false;
    }
    return true;
}

} // namespace

TEST_CASE("LPT-1/STR-1: section=system returns every key those pages read") {
    const std::string handler = readSrc("webpages/handlers_unified/config_handler.cpp");
    const std::string system = sectionPayload(handler, "doc[\"system_name\"]");

    // Keys the LPT page assigns from `d.*`.
    CHECK(hasAll(system, {"printer_emulation_mode", "charset_mode",
                          "autoEmailEnabledTimeout", "autoEmailDisabledTimeout",
                          "auto_send", "max_lines", "min_content_length"}));
    // Structures block read by the structures page.
    CHECK(hasAll(system, {"structures", "auto_detect", "amada_isq20_mtf1",
                          "amada_isq20_awse", "amada_filter_headers", "amada_header_every"}));
    // ... and the RS232 summary it reads from `d.saved_configs`.
    CHECK(hasAll(system, {"saved_configs", "rs232_baudrate_index", "rs232_data_bits",
                          "rs232_parity", "rs232_stop_bits", "rs232_enabled"}));

    // The pages still read exactly those names (so the test fails if either
    // side drifts).
    const std::string lpt = readSrc("webpages/assets/page_lpt_db25_asset.cpp");
    CHECK(hasAll(lpt, {"section=system", "printer_emulation_mode", "charset_mode",
                       "email.auto_send", "email.max_lines", "autoEmailEnabledTimeout"}));
    const std::string str = readSrc("webpages/assets/page_structures_asset.cpp");
    CHECK(hasAll(str, {"section=system", "d.structures", "saved_configs", "rs232_baudrate_index"}));

    // The system payload is no longer sized for the old 4-field body.
    CHECK(handler.find("outCapacity = 2560;") != std::string::npos);
}

TEST_CASE("GS-1: section=email exposes the saved Google Script URL") {
    const std::string handler = readSrc("webpages/handlers_unified/config_handler.cpp");
    const std::string email = sectionPayload(handler, "doc[\"smtp\"] = full[\"smtp\"]");
    CHECK(hasAll(email, {"google_email", "google_script_url", "getGoogleScriptUrl()"}));

    const std::string page = readSrc("webpages/assets/page_google_script_asset.cpp");
    CHECK(hasAll(page, {"section=email", "google_email", "google_script_url"}));
}

TEST_CASE("DO-1/HX-2: the DO and HX711 endpoints are in the shared registry") {
    const std::string doRoutes = readSrc("webpages/routes/web_routes_do.cpp");
    CHECK(hasAll(doRoutes, {"registerApiRoute(\"GET\", \"/api/do/status\"",
                            "registerApiRoute(\"GET\", \"/api/do/config\""}));

    const std::string configRoutes = readSrc("webpages/routes/web_routes_config.cpp");
    CHECK(hasAll(configRoutes, {"registerApiRoute(\"GET\",  \"/api/hx711-config\"",
                                "registerApiRoute(\"POST\", \"/api/hx711-measure\"",
                                "registerApiRoute(\"POST\", \"/api/hx711-tare\""}));
}

TEST_CASE("a registered API route is reachable over Ethernet (shared registry)") {
    // The Ethernet dispatcher resolves the same table the WiFi server fills, so
    // "registered once" means "served on both transports" — the invariant this
    // closes DO-1/HX-2 for good.
    const std::string eth = readSrc("ethernet_bootstrap_routes.h");
    CHECK(eth.find("findApiRouteHandler(method, path)") != std::string::npos);

    // The DO page also POSTs JSON over Ethernet; that path keeps its dedicated
    // bootstrap handlers (body accumulation), so they must still be present.
    CHECK(hasAll(eth, {"path == F(\"/api/do/set\")", "path == F(\"/api/do/config\")"}));
}
