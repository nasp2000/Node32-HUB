// test_usb_chip_table.cpp
// Unit tests for the data-driven USB chip auto-detect table (usb_chip_table.h).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "usb_chip_table.h"

TEST_CASE("chip name: exact VID:PID mapping for seeded chips") {
    // CH340 family.
    CHECK(usbChipName(0x1A86, 0x7523) == std::string("CH340"));
    CHECK(usbChipName(0x1A86, 0x55D4) == std::string("CH340"));
    CHECK(usbChipName(0x1A86, 0x5512) == std::string("CH341"));
    CHECK(usbChipName(0x1A86, 0x55D2) == std::string("CH340E"));
    CHECK(usbChipName(0x1A86, 0xE025) == std::string("CH342"));
    CHECK(usbChipName(0x1A86, 0x55D3) == std::string("CH343")); // CH343 is CDC-ACM
    // Non-CH340 chips.
    CHECK(usbChipName(0x0403, 0x6001) == std::string("FT232R"));
    CHECK(usbChipName(0x067B, 0x2303) == std::string("PL2303"));
    CHECK(usbChipName(0x10C4, 0xEA60) == std::string("CP2102/04"));
    CHECK(usbChipName(0x10C4, 0xEA71) == std::string("CP2108"));
    CHECK(usbChipName(0x2C7C, 0x6001) == std::string("RP2040"));
}

TEST_CASE("chip name: VID-only fallback (pid 0xFFFF row)") {
    // Unknown PID inside a known VID family falls back to the family name.
    CHECK(usbChipName(0x1A86, 0x9999) == std::string("CH34x"));
    CHECK(usbChipName(0x0403, 0x9999) == std::string("FTDI"));
    CHECK(usbChipName(0x067B, 0x9999) == std::string("PL230x"));
    CHECK(usbChipName(0x10C4, 0x9999) == std::string("CP210x"));
    CHECK(usbChipName(0x1B3D, 0x0001) == std::string("MKS"));
    CHECK(usbChipName(0x2341, 0x0043) == std::string("Arduino"));
}

TEST_CASE("chip name: unknown VID/PID returns generic") {
    CHECK(usbChipName(0x1234, 0x5678) == std::string("USB-UART"));
    CHECK(usbChipName(0x0000, 0x0000) == std::string("USB-UART"));
}

TEST_CASE("vendor line coding flags: CH340 family only") {
    // Chips that need the CH340-style vendor baud requests.
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x7523) == true);   // CH340G/C/B
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x55D4) == true);   // CH340 variant
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x5512) == true);   // CH341A
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x55D2) == true);   // CH340E
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0xE025) == true);   // CH342
    // Standard CDC-ACM / non-CH340 chips must NOT use vendor requests.
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x55D3) == false);  // CH343
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x55D5) == false);  // CH343 dual-port
    CHECK(usbChipNeedsVendorLineCoding(0x1A86, 0x9999) == false);  // unknown PID, VID-only row has no flag
    CHECK(usbChipNeedsVendorLineCoding(0x0403, 0x6001) == false);  // FTDI FT232R
    CHECK(usbChipNeedsVendorLineCoding(0x067B, 0x2303) == false);  // PL2303
    CHECK(usbChipNeedsVendorLineCoding(0x10C4, 0xEA60) == false);  // CP210x
}

TEST_CASE("lookup precedence: exact row wins over VID-only row") {
    // 1A86:55D3 must resolve to the exact CH343 row, not the 1A86:FFFF fallback.
    const UsbChipDef* e = usbChipFind(0x1A86, 0x55D3);
    REQUIRE(e != nullptr);
    CHECK(e->pid == 0x55D3);
    CHECK(e->name == std::string("CH343"));
    CHECK((e->flags & USB_CHIP_FLAG_VENDOR_LINE_CODING) == 0);

    // 1A86:9999 resolves to the VID-only row.
    const UsbChipDef* f = usbChipFind(0x1A86, 0x9999);
    REQUIRE(f != nullptr);
    CHECK(f->pid == 0xFFFF);
    CHECK(f->name == std::string("CH34x"));
}

TEST_CASE("family flags: 0 for unknown, correct bits for CH340") {
    CHECK(usbChipFamilyFlags(0x1234, 0x5678) == 0);
    CHECK(usbChipFamilyFlags(0x0403, 0x6001) == 0);
    CHECK((usbChipFamilyFlags(0x1A86, 0x7523) & USB_CHIP_FLAG_VENDOR_LINE_CODING) != 0);
    CHECK((usbChipFamilyFlags(0x1A86, 0x55D2) & USB_CHIP_FLAG_VENDOR_LINE_CODING) != 0);
}

TEST_CASE("unknown lookup returns nullptr") {
    CHECK(usbChipFind(0x0B0B, 0x0B0B) == nullptr);
    CHECK(usbChipFind(0x0000, 0x0000) == nullptr);
}