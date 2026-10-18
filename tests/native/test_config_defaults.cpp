#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "config_defaults.h"

// Sentinel test for config_defaults.h — the fallback values used whenever a
// flag is NOT defined by user_config.h or a pack. A wrong default here can
// silently enable/disable modules on boards that never override it.
// Compiles the production header directly (no deps).
// NOTE: defaults here may be overridden by user_config.h/packs at build time;
// this test verifies the *fallback* file is self-consistent.

namespace {

bool isZeroOrOne(int v) { return v == 0 || v == 1; }

} // namespace

TEST_CASE("all ENABLE_* defaults are numeric 0 or 1") {
    CHECK(isZeroOrOne(ENABLE_OTA));
    CHECK(isZeroOrOne(ENABLE_CAMERA));
    CHECK(isZeroOrOne(ENABLE_MDNS));
    CHECK(isZeroOrOne(ENABLE_ASYNC_WEBSOCKET));
    CHECK(isZeroOrOne(ENABLE_FILESYSTEM));
    CHECK(isZeroOrOne(ENABLE_SMTP));
    CHECK(isZeroOrOne(ENABLE_GOOGLE_EMAIL));
    CHECK(isZeroOrOne(ENABLE_TELNET));
    CHECK(isZeroOrOne(ENABLE_TELNET_AUTH));
    CHECK(isZeroOrOne(ENABLE_RS232));
    CHECK(isZeroOrOne(ENABLE_RS232_CHARTS));
    CHECK(isZeroOrOne(ENABLE_PROXIMITY_CHARTS));
    CHECK(isZeroOrOne(ENABLE_DB25));
    CHECK(isZeroOrOne(ENABLE_LCD_SCREEN));
    CHECK(isZeroOrOne(ENABLE_HTTP_BASIC_AUTH));
    CHECK(isZeroOrOne(ENABLE_API_REFERENCE_PAGE));
    CHECK(isZeroOrOne(ENABLE_STRUCTURES_PAGE));
    CHECK(isZeroOrOne(ENABLE_WIFI));
    CHECK(isZeroOrOne(ENABLE_WIFI_WEB_SERVER));
    CHECK(isZeroOrOne(ENABLE_NTP));
    CHECK(isZeroOrOne(ENABLE_SCHEDULER));
    CHECK(isZeroOrOne(ENABLE_DEBUG_LOGS));
    CHECK(isZeroOrOne(ENABLE_HELP_TEXTS));
    CHECK(isZeroOrOne(ENABLE_RS232_XONXOFF));
    CHECK(isZeroOrOne(ENABLE_SNIFFER));
    CHECK(isZeroOrOne(ENABLE_ETHERNET));
    CHECK(isZeroOrOne(ENABLE_ETHERNET_W5500));
    CHECK(isZeroOrOne(ENABLE_AHT20_BMP280));
    CHECK(isZeroOrOne(ENABLE_SENSOR_CONFIG_PAGE));
    CHECK(isZeroOrOne(ENABLE_MQ135));
    CHECK(isZeroOrOne(ENABLE_HX711));
    CHECK(isZeroOrOne(ENABLE_INA226));
    CHECK(isZeroOrOne(ENABLE_SOUND_SENSOR));
    CHECK(isZeroOrOne(ENABLE_MAX31865));
    CHECK(isZeroOrOne(ENABLE_PROXIMITY));
    CHECK(isZeroOrOne(ENABLE_UDA1334A));
    CHECK(isZeroOrOne(ENABLE_SDCARD));
    CHECK(isZeroOrOne(ENABLE_MODBUS));
    CHECK(isZeroOrOne(ENABLE_MODBUS_RTU));
    CHECK(isZeroOrOne(ENABLE_MODBUS_TCP));
    CHECK(isZeroOrOne(ENABLE_MODBUS_SLAVE));
    CHECK(isZeroOrOne(ENABLE_BUZZER));
    CHECK(isZeroOrOne(ENABLE_UNIKON));
    CHECK(isZeroOrOne(ENABLE_BRICON));
    CHECK(isZeroOrOne(ENABLE_IPG_LASER));
    CHECK(isZeroOrOne(ENABLE_CAN));
    CHECK(isZeroOrOne(ENABLE_USBIP_SERVER));
    CHECK(isZeroOrOne(ENABLE_GRBL));
    CHECK(isZeroOrOne(ENABLE_PAGE_COMMANDS));
    CHECK(isZeroOrOne(ENABLE_PAGE_RECIPIENTS));
    CHECK(isZeroOrOne(ENABLE_PAGE_FILES));
    CHECK(isZeroOrOne(ENABLE_PAGE_SDCARD));
    CHECK(isZeroOrOne(ENABLE_PAGE_MONITOR));
    CHECK(isZeroOrOne(ENABLE_PAGE_ACCESS));
    CHECK(isZeroOrOne(ENABLE_PAGE_GOOGLE_SCRIPT));
    CHECK(isZeroOrOne(ENABLE_FTP_CLIENT));
    CHECK(isZeroOrOne(ENABLE_PAGE_FTP));
    CHECK(isZeroOrOne(ENABLE_DUALFILE));
    CHECK(isZeroOrOne(BOARD_HAS_TCA9554_DO));
    CHECK(isZeroOrOne(PROXIMITY_USE_PCNT));
    CHECK(isZeroOrOne(ENABLE_LPT_SERIAL_MONITOR));
    CHECK(isZeroOrOne(ENABLE_NANO_REBOOT_ON_BOOT));
}

TEST_CASE("numeric config defaults are within sane ranges") {
    CHECK(FILESYSTEM_PROVIDER >= 1);
    CHECK(FILESYSTEM_PROVIDER <= 3);
    CHECK(SCREEN_TYPE >= 1);
    CHECK(BUZZER_PIN >= 0);
    CHECK(PROXIMITY_CHANNELS >= 1);
    CHECK(PROXIMITY_CHANNELS <= 8);
}

TEST_CASE("core modules are on by default (fallback board)") {
    // These are the modules every board expects unless a pack says otherwise.
    CHECK(ENABLE_OTA == 1);
    CHECK(ENABLE_FILESYSTEM == 1);
    CHECK(ENABLE_SMTP == 1);
    CHECK(ENABLE_TELNET == 1);
    CHECK(ENABLE_HTTP_BASIC_AUTH == 1);
    CHECK(ENABLE_WIFI == 1);
    CHECK(ENABLE_NTP == 1);
    CHECK(ENABLE_SCHEDULER == 1);
    CHECK(ENABLE_SDCARD == 1);
    CHECK(ENABLE_DEBUG_LOGS == 1);
    CHECK(ENABLE_RS232_CHARTS == 1);
    CHECK(ENABLE_SENSOR_CONFIG_PAGE == 1);
    CHECK(ENABLE_MDNS == 1);   // software-only feature, default on for all packs
}

TEST_CASE("hardware-dependent modules are off by default (must opt-in)") {
    // Requiring an explicit opt-in prevents accidental builds on boards that
    // don't have the hardware.
    CHECK(ENABLE_CAMERA == 0);
    CHECK(ENABLE_ASYNC_WEBSOCKET == 0);
    CHECK(ENABLE_DB25 == 0);
    CHECK(ENABLE_SNIFFER == 0);
    CHECK(ENABLE_MQ135 == 0);
    CHECK(ENABLE_HX711 == 0);
    CHECK(ENABLE_INA226 == 0);
    CHECK(ENABLE_SOUND_SENSOR == 0);
    CHECK(ENABLE_MAX31865 == 0);
    CHECK(ENABLE_PROXIMITY == 0);
    CHECK(ENABLE_UDA1334A == 0);
    CHECK(ENABLE_MODBUS == 0);
    CHECK(ENABLE_BUZZER == 0);
    CHECK(ENABLE_UNIKON == 0);
    CHECK(ENABLE_BRICON == 0);
    CHECK(ENABLE_IPG_LASER == 0);
    CHECK(ENABLE_CAN == 0);
    CHECK(ENABLE_USBIP_SERVER == 0);
    CHECK(ENABLE_GRBL == 0);
    CHECK(ENABLE_DUALFILE == 0);
    CHECK(ENABLE_PAGE_GOOGLE_SCRIPT == 0);
    CHECK(ENABLE_FTP_CLIENT == 0);
    CHECK(ENABLE_PAGE_FTP == 0);
}

TEST_CASE("Modbus slave requires master module default consistency") {
    // ENABLE_MODBUS_SLAVE defaults to 0 independent of ENABLE_MODBUS — the
    // pack validation enforces MODBUS_SLAVE->MODBUS dependency at build time.
    CHECK(ENABLE_MODBUS == ENABLE_MODBUS_RTU);   // both default 0
    CHECK(ENABLE_MODBUS == ENABLE_MODBUS_TCP);   // both default 0
    CHECK(ENABLE_MODBUS == ENABLE_MODBUS_SLAVE); // both default 0
}

TEST_CASE("W5500 Ethernet defaults mirror ENABLE_ETHERNET") {
    // Ethernet default is derived: W5500 follows the main Ethernet flag.
    CHECK(ENABLE_ETHERNET_W5500 == ENABLE_ETHERNET);
    CHECK(ENABLE_WIFI_WEB_SERVER == ENABLE_WIFI);
    CHECK(ENABLE_PAGE_COMMANDS == ENABLE_SCHEDULER);
}

TEST_CASE("non-flag defaults are positive integers") {
    CHECK(BUZZER_PIN > 0);            // GPIO46
    CHECK(PROXIMITY_CHANNELS > 0);
    CHECK(FILESYSTEM_PROVIDER > 0);
    CHECK(SCREEN_TYPE > 0);
}
