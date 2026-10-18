#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>

// Standalone reimplementation of pack_validator.h validation rules.
// Each checker returns empty string (pass) or error message (failure).

static std::string checkSnifferRS232(bool sniffer, bool rs232) {
    if (sniffer && rs232) return "Sniffer conflicts with RS232";
    return {};
}

static std::string checkSnifferDB25(bool sniffer, bool db25) {
    if (sniffer && db25) return "Sniffer conflicts with DB25";
    return {};
}

static std::string checkModbusRTU_DB25(bool modbus_rtu, bool db25) {
    if (modbus_rtu && db25) return "Modbus RTU conflicts with DB25";
    return {};
}

static std::string checkUsbipDep(bool usbip, bool usb_host) {
    if (usbip && !usb_host) return "USBIP requires USB_HOST";
    return {};
}

static std::string checkModbusRtuDep(bool rtu, bool modbus) {
    if (rtu && !modbus) return "MODBUS_RTU requires MODBUS";
    return {};
}

static std::string checkModbusTcpDep(bool tcp, bool modbus) {
    if (tcp && !modbus) return "MODBUS_TCP requires MODBUS";
    return {};
}

static std::string checkModbusSlaveDep(bool slave, bool modbus) {
    if (slave && !modbus) return "MODBUS_SLAVE requires MODBUS";
    return {};
}

static std::string checkRs232ChartsDep(bool charts, bool rs232) {
    if (charts && !rs232) return "RS232_CHARTS requires RS232";
    return {};
}

static std::string checkGoogleScriptDep(bool script, bool google_email) {
    if (script && !google_email) return "PAGE_GOOGLE_SCRIPT requires GOOGLE_EMAIL";
    return {};
}

static std::string checkSdcardPageDep(bool page, bool sdcard) {
    (void)sdcard;
    // T12: storage page is present in every pack, even without an SD slot
    // (LittleFS/PSRAM-only boards). No SDCARD requirement.
    (void)page;
    return {};
}

static std::string checkFtpPageDep(bool page, bool ftp) {
    if (page && !ftp) return "PAGE_FTP requires FTP_CLIENT";
    return {};
}

// ============================================================
// Hardware conflict rules
// ============================================================

TEST_CASE("pack_validator: sniffer vs RS232") {
    CHECK(checkSnifferRS232(true, true) != "");
    CHECK(checkSnifferRS232(true, false) == "");
    CHECK(checkSnifferRS232(false, true) == "");
    CHECK(checkSnifferRS232(false, false) == "");
}

TEST_CASE("pack_validator: sniffer vs DB25") {
    CHECK(checkSnifferDB25(true, true) != "");
    CHECK(checkSnifferDB25(true, false) == "");
    CHECK(checkSnifferDB25(false, true) == "");
    CHECK(checkSnifferDB25(false, false) == "");
}

TEST_CASE("pack_validator: Modbus RTU vs DB25") {
    CHECK(checkModbusRTU_DB25(true, true) != "");
    CHECK(checkModbusRTU_DB25(true, false) == "");
    CHECK(checkModbusRTU_DB25(false, true) == "");
    CHECK(checkModbusRTU_DB25(false, false) == "");
}

// ============================================================
// Dependency rules
// ============================================================

TEST_CASE("pack_validator: USBIP depends on USB_HOST") {
    CHECK(checkUsbipDep(true, false) != "");
    CHECK(checkUsbipDep(true, true) == "");
    CHECK(checkUsbipDep(false, false) == "");
    CHECK(checkUsbipDep(false, true) == "");
}

TEST_CASE("pack_validator: MODBUS_RTU depends on MODBUS") {
    CHECK(checkModbusRtuDep(true, false) != "");
    CHECK(checkModbusRtuDep(true, true) == "");
    CHECK(checkModbusRtuDep(false, false) == "");
    CHECK(checkModbusRtuDep(false, true) == "");
}

TEST_CASE("pack_validator: MODBUS_TCP depends on MODBUS") {
    CHECK(checkModbusTcpDep(true, false) != "");
    CHECK(checkModbusTcpDep(true, true) == "");
    CHECK(checkModbusTcpDep(false, false) == "");
    CHECK(checkModbusTcpDep(false, true) == "");
}

TEST_CASE("pack_validator: MODBUS_SLAVE depends on MODBUS") {
    CHECK(checkModbusSlaveDep(true, false) != "");
    CHECK(checkModbusSlaveDep(true, true) == "");
    CHECK(checkModbusSlaveDep(false, false) == "");
    CHECK(checkModbusSlaveDep(false, true) == "");
}

TEST_CASE("pack_validator: RS232_CHARTS depends on RS232") {
    CHECK(checkRs232ChartsDep(true, false) != "");
    CHECK(checkRs232ChartsDep(true, true) == "");
    CHECK(checkRs232ChartsDep(false, false) == "");
    CHECK(checkRs232ChartsDep(false, true) == "");
}

TEST_CASE("pack_validator: PAGE_GOOGLE_SCRIPT depends on GOOGLE_EMAIL") {
    CHECK(checkGoogleScriptDep(true, false) != "");
    CHECK(checkGoogleScriptDep(true, true) == "");
    CHECK(checkGoogleScriptDep(false, false) == "");
    CHECK(checkGoogleScriptDep(false, true) == "");
}

TEST_CASE("pack_validator: PAGE_SDCARD present in every pack (T12)") {
    CHECK(checkSdcardPageDep(true, false) == "");
    CHECK(checkSdcardPageDep(true, true) == "");
    CHECK(checkSdcardPageDep(false, false) == "");
    CHECK(checkSdcardPageDep(false, true) == "");
}

TEST_CASE("pack_validator: PAGE_FTP depends on FTP_CLIENT") {
    CHECK(checkFtpPageDep(true, false) != "");
    CHECK(checkFtpPageDep(true, true) == "");
    CHECK(checkFtpPageDep(false, false) == "");
    CHECK(checkFtpPageDep(false, true) == "");
}
