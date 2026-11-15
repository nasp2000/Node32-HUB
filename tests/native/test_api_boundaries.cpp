// Native contract tests for three "API boundary" findings.
//
//   MODBUS-2  POST /api/modbus/config accepted unbounded devices/registers while
//             the runtime keeps MODBUS_MAX_DEVICES x MODBUS_MAX_REGISTERS and
//             dropped the extras silently — a save that was never polled.
//   ETH-1     /api/wifi-disable had two divergent implementations: the WiFi
//             adapter stopped the radio from a timer (no mode guard) and the
//             Ethernet adapter did it inline (no mDNS stop).
//   RS232-1   the /rs232 page (and the home page) sent `Content-Language: pt`
//             regardless of the active language; the RS232 API routes must also
//             stay behind ENABLE_RS232.
//
// The handlers need the ESP HTTP stack, so these pin the source contract: the
// guard exists, it uses the same limits as the runtime, both transports call the
// same action, and no page hardcodes the language again.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <string>

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

} // namespace

TEST_CASE("MODBUS-2: the POST refuses more devices/registers than the runtime keeps") {
    const std::string handler = readSrc("webpages/handlers_unified/modbus_handler.cpp");

    const size_t guard = handler.find("too many devices (max ");
    REQUIRE(guard != std::string::npos);
    const size_t regsGuard = handler.find("too many registers on one device (max ");
    REQUIRE(regsGuard != std::string::npos);

    // The limits are the runtime's own macros, not a second hardcoded number.
    CHECK(handler.find("MODBUS_MAX_DEVICES") != std::string::npos);
    CHECK(handler.find("MODBUS_MAX_REGISTERS") != std::string::npos);

    // The guard runs before anything is written into the section.
    const size_t write = handler.find("mbCfg.remove(\"devices\");");
    REQUIRE(write != std::string::npos);
    CHECK(guard < write);
    CHECK(regsGuard < write);

    // The runtime still clamps (defence in depth) — the contract is "refuse
    // loudly at the edge, never drop silently in the middle".
    const std::string runtime = readSrc("modbus_runtime.cpp");
    CHECK(runtime.find("MODBUS_MAX_DEVICES") != std::string::npos);
    CHECK(runtime.find("MODBUS_MAX_REGISTERS") != std::string::npos);
}

TEST_CASE("ETH-1: both transports stop the radio through one shared action") {
    const std::string action = readSrc("webpages/handlers_unified/network_handler.cpp");
    const size_t fn = action.find("void networkStopWifiRadio()");
    REQUIRE(fn != std::string::npos);
    const std::string body = action.substr(fn, 400);
    CHECK(body.find("stopMdns()") != std::string::npos);
    CHECK(body.find("WiFi.getMode() != WIFI_OFF") != std::string::npos);
    CHECK(body.find("WiFi.disconnect(true)") != std::string::npos);
    CHECK(body.find("WiFi.mode(WIFI_OFF)") != std::string::npos);
    CHECK(body.find("esp_wifi_stop()") != std::string::npos);
    CHECK(body.find("TARGET_BOARD_P4") != std::string::npos);

    // WiFi adapter: deferred call to the shared action, no local copy.
    const std::string wifi = readSrc("webpages/routes/web_routes_network.cpp");
    CHECK(wifi.find("networkStopWifiRadio()") != std::string::npos);
    CHECK(wifi.find("WiFi.mode(WIFI_OFF);") == std::string::npos);
    CHECK(wifi.find("stopMdns();") == std::string::npos);

    // Ethernet adapter: inline call to the same action, no local copy either.
    const std::string eth = readSrc("ethernet_bootstrap_handlers.h");
    CHECK(eth.find("networkStopWifiRadio()") != std::string::npos);
    CHECK(eth.find("WiFi.mode(WIFI_OFF);") == std::string::npos);
}

TEST_CASE("RS232-1: Content-Language follows the active language") {
    const std::string rs232Page = readSrc("webpages/routes/web_routes_config.cpp");
    CHECK(rs232Page.find("addHeader(\"Content-Language\", \"pt\")") == std::string::npos);
    CHECK(rs232Page.find("addHeader(\"Content-Language\", activeLangCode)") != std::string::npos);

    const std::string home = readSrc("webpages/routes/web_routes_core.cpp");
    CHECK(home.find("addHeader(\"Content-Language\", \"pt\")") == std::string::npos);
}

TEST_CASE("RS232-1: the RS232 API routes stay behind ENABLE_RS232") {
    const std::string routes = readSrc("webpages/routes/web_routes_config.cpp");
    const size_t gate = routes.find("registerRs232TemplateRoutes(server, context);");
    REQUIRE(gate != std::string::npos);
    // The call sits inside an ENABLE_RS232 block: the nearest preceding
    // conditional must be the #if ENABLE_RS232==1 (not a page/sensor flag).
    const size_t open = routes.rfind("#if ENABLE_RS232==1", gate);
    REQUIRE(open != std::string::npos);
    CHECK(routes.find("#endif", open) > gate);
}
