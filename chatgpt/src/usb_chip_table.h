// usb_chip_table.h
// Data-driven USB VID:PID → chip family/flags auto-detect, shared by the native
// and TinyUSB USB-host backends (via usb_host_manager.h). Pure logic, no Arduino
// or IDF dependencies — unit-tested in tests/native/test_usb_chip_table.cpp.
//
// Adding a new chip = add ONE row to s_usbChipTable (an exact VID:PID row, then
// optionally a VID-only fallback with pid 0xFFFF). No other code changes.
// Consumers key off usbChipName()/usbChipFamilyFlags()/usbChipNeedsVendorLineCoding()
// — never raw VID/PID compares.

#pragma once
#include <stdint.h>
#include <stddef.h>

enum UsbChipFlag : uint8_t {
    USB_CHIP_FLAG_NONE               = 0,
    USB_CHIP_FLAG_VENDOR_LINE_CODING = (1 << 0), // needs CH340-style vendor baud requests (0xA1/0x9A/0xA4) instead of CDC SET_LINE_CODING
};

struct UsbChipDef {
    uint16_t    vid;
    uint16_t    pid;   // 0xFFFF = any PID for this VID (VID-only fallback row).
    uint8_t     flags;
    const char* name;
};

static const UsbChipDef s_usbChipTable[] = {
    // CH340/CH341 family — vendor-specific line coding.
    { 0x1A86, 0x7523, USB_CHIP_FLAG_VENDOR_LINE_CODING, "CH340" },  // CH340G/C/B
    { 0x1A86, 0x55D4, USB_CHIP_FLAG_VENDOR_LINE_CODING, "CH340" },  // CH340 variant
    { 0x1A86, 0x5512, USB_CHIP_FLAG_VENDOR_LINE_CODING, "CH341" },  // CH341A
    { 0x1A86, 0x55D2, USB_CHIP_FLAG_VENDOR_LINE_CODING, "CH340E" },
    { 0x1A86, 0xE025, USB_CHIP_FLAG_VENDOR_LINE_CODING, "CH342" },
    { 0x1A86, 0x55D3, 0,                                "CH343" },  // standard CDC-ACM (no vendor requests)
    { 0x1A86, 0x55D5, 0,                                "CH343" },
    { 0x1A86, 0xFFFF, 0,                                "CH34x" },
    // FTDI FTxxx — NOT standard CDC: interface class 0xFF vendor-specific,
    // driven through FTDI SIO vendor requests (SIO_SET_BAUDRATE 0x03 +
    // SIO_SET_MODEM_CTRL 0x01). The native backend routes these via
    // isFtdiFamilyVidPid(), so no USB_CHIP_FLAG_VENDOR_LINE_CODING flag here
    // (that flag means CH340-style requests, which FTDI does not accept).
    { 0x0403, 0x6001, 0, "FT232R" },
    { 0x0403, 0x6015, 0, "FT231X" },
    { 0x0403, 0x6010, 0, "FT2232H" },
    { 0x0403, 0x6011, 0, "FT4232H" },
    { 0x0403, 0xFFFF, 0, "FTDI" },
    // Prolific PL2303 — standard CDC.
    { 0x067B, 0x2303, 0, "PL2303" },
    { 0x067B, 0x23A3, 0, "PL2303TA" },
    { 0x067B, 0xFFFF, 0, "PL230x" },
    // Silicon Labs CP210x — standard CDC.
    { 0x10C4, 0xEA60, 0, "CP2102/04" },
    { 0x10C4, 0xEA70, 0, "CP2105" },
    { 0x10C4, 0xEA71, 0, "CP2108" },
    { 0x10C4, 0xFFFF, 0, "CP210x" },
    // Raspberry Pi Pico (RP2040).
    { 0x2C7C, 0x6001, 0, "RP2040" },
    { 0x2C7C, 0xFFFF, 0, "RP204x" },
    // NXP (Teensy).
    { 0x1FC9, 0x0094, 0, "Teensy 4.0" },
    { 0x1FC9, 0xFFFF, 0, "NXP" },
    // VID-only entries: MKS / Arduino / Adafruit.
    { 0x1B3D, 0xFFFF, 0, "MKS" },
    { 0x2341, 0xFFFF, 0, "Arduino" },
    { 0x239A, 0xFFFF, 0, "Adafruit" },
};

// Look up a chip row: exact VID:PID match first, then VID-only (pid 0xFFFF).
// Returns nullptr when the VID/PID is unknown ("USB-UART" / no flags).
static inline const UsbChipDef* usbChipFind(uint16_t vid, uint16_t pid) {
    const size_t n = sizeof(s_usbChipTable) / sizeof(s_usbChipTable[0]);
    for (size_t i = 0; i < n; i++) {
        if (s_usbChipTable[i].vid == vid && s_usbChipTable[i].pid == pid) return &s_usbChipTable[i];
    }
    for (size_t i = 0; i < n; i++) {
        if (s_usbChipTable[i].vid == vid && s_usbChipTable[i].pid == 0xFFFF) return &s_usbChipTable[i];
    }
    return nullptr;
}

// Init-behaviour flags for a VID/PID (0 when unknown).
static inline uint8_t usbChipFamilyFlags(uint16_t vid, uint16_t pid) {
    const UsbChipDef* e = usbChipFind(vid, pid);
    return e ? e->flags : 0;
}

// True for chips that need CH340-style vendor line-coding requests instead of
// standard CDC SET_LINE_CODING (0x21/0x20).
static inline bool usbChipNeedsVendorLineCoding(uint16_t vid, uint16_t pid) {
    return (usbChipFamilyFlags(vid, pid) & USB_CHIP_FLAG_VENDOR_LINE_CODING) != 0;
}

// Map USB VID/PID to a friendly chip name (e.g. "CH340", "CP2102/04", "FT232R").
static inline const char* usbChipName(uint16_t vid, uint16_t pid) {
    const UsbChipDef* e = usbChipFind(vid, pid);
    return e ? e->name : "USB-UART";
}