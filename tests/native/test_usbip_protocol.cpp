#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

static inline uint16_t swap16(uint16_t v, bool pack) {
    return pack ? htons(v) : ntohs(v);
}
static inline uint32_t swap32(uint32_t v, bool pack) {
    return pack ? htonl(v) : ntohl(v);
}
static inline int32_t swap32s(int32_t v, bool pack) {
    uint32_t u = 0;
    memcpy(&u, &v, sizeof(u));
    u = swap32(u, pack);
    int32_t r = 0;
    memcpy(&r, &u, sizeof(r));
    return r;
}

struct __attribute__((packed)) UsbIpOpCommon {
    uint16_t version;
    uint16_t code;
    uint32_t status;
};

struct __attribute__((packed)) UsbIpUsbDevice {
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
};

struct __attribute__((packed)) UsbIpUsbInterface {
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t padding;
};

struct __attribute__((packed)) UsbIpDevListReply {
    uint32_t ndev;
};

struct __attribute__((packed)) UsbIpHeaderBasic {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
};

struct __attribute__((packed)) UsbIpHeaderCmdSubmit {
    uint32_t transfer_flags;
    int32_t transfer_buffer_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t interval;
    uint8_t setup[8];
};

struct __attribute__((packed)) UsbIpHeaderRetSubmit {
    int32_t status;
    int32_t actual_length;
    int32_t start_frame;
    int32_t number_of_packets;
    int32_t error_count;
    uint8_t padding[8];
};

struct __attribute__((packed)) UsbIpHeader {
    UsbIpHeaderBasic base;
    union {
        UsbIpHeaderCmdSubmit cmd_submit;
        UsbIpHeaderRetSubmit ret_submit;
    } u;
};

static void packOpCommon(UsbIpOpCommon &op, bool pack) {
    op.version = swap16(op.version, pack);
    op.code = swap16(op.code, pack);
    op.status = swap32(op.status, pack);
}
static void packUsbDevice(UsbIpUsbDevice &dev, bool pack) {
    dev.busnum = swap32(dev.busnum, pack);
    dev.devnum = swap32(dev.devnum, pack);
    dev.speed = swap32(dev.speed, pack);
    dev.idVendor = swap16(dev.idVendor, pack);
    dev.idProduct = swap16(dev.idProduct, pack);
    dev.bcdDevice = swap16(dev.bcdDevice, pack);
}
static void packDevListReply(UsbIpDevListReply &rep, bool pack) {
    rep.ndev = swap32(rep.ndev, pack);
}
static void packUsbIpHeader(UsbIpHeader &hdr, bool pack) {
    hdr.base.command = swap32(hdr.base.command, pack);
    hdr.base.seqnum = swap32(hdr.base.seqnum, pack);
    hdr.base.devid = swap32(hdr.base.devid, pack);
    hdr.base.direction = swap32(hdr.base.direction, pack);
    hdr.base.ep = swap32(hdr.base.ep, pack);
    uint32_t* words = reinterpret_cast<uint32_t*>(&hdr.u);
    for (int i = 0; i < 5; i++) {
        words[i] = swap32(words[i], pack);
    }
}

TEST_CASE("USB/IP OpCommon pack/unpack round-trip") {
    UsbIpOpCommon op = {0x0111, 0x8005, 0};
    packOpCommon(op, true);
    CHECK(op.version == htons(0x0111));
    CHECK(op.code == htons(0x8005));
    CHECK(op.status == 0);
    packOpCommon(op, false);
    CHECK(op.version == 0x0111);
    CHECK(op.code == 0x8005);
    CHECK(op.status == 0);
}

TEST_CASE("USB/IP OpCommon network byte order") {
    UsbIpOpCommon op = {0x0111, 0x0003, 1};
    packOpCommon(op, true);
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&op);
    CHECK(raw[0] == 0x01);
    CHECK(raw[1] == 0x11);
    CHECK(raw[2] == 0x00);
    CHECK(raw[3] == 0x03);
    CHECK(raw[4] == 0x00);
    CHECK(raw[5] == 0x00);
    CHECK(raw[6] == 0x00);
    CHECK(raw[7] == 0x01);
}

TEST_CASE("USB/IP UsbDevice pack/unpack") {
    UsbIpUsbDevice dev = {};
    strncpy(dev.path, "/usb/device/1-2", sizeof(dev.path) - 1);
    strncpy(dev.busid, "1-2", sizeof(dev.busid) - 1);
    dev.busnum = 1;
    dev.devnum = 2;
    dev.speed = 3;
    dev.idVendor = 0x1A86;
    dev.idProduct = 0x7523;
    dev.bcdDevice = 0x0260;
    dev.bDeviceClass = 0x02;
    dev.bNumInterfaces = 1;
    packUsbDevice(dev, true);
    CHECK(dev.busnum == htonl(1));
    CHECK(dev.devnum == htonl(2));
    CHECK(dev.speed == htonl(3));
    CHECK(dev.idVendor == htons(0x1A86));
    CHECK(dev.idProduct == htons(0x7523));
    CHECK(dev.bcdDevice == htons(0x0260));
    packUsbDevice(dev, false);
    CHECK(dev.busnum == 1);
    CHECK(dev.devnum == 2);
    CHECK(dev.speed == 3);
    CHECK(dev.idVendor == 0x1A86);
    CHECK(dev.idProduct == 0x7523);
    CHECK(dev.bcdDevice == 0x0260);
}

TEST_CASE("USB/IP UsbDevice size is 312 bytes") {
    CHECK(sizeof(UsbIpUsbDevice) == 312);
}

TEST_CASE("USB/IP UsbInterface size is 4 bytes") {
    CHECK(sizeof(UsbIpUsbInterface) == 4);
}

TEST_CASE("USB/IP DevListReply pack/unpack") {
    UsbIpDevListReply rep = {2};
    packDevListReply(rep, true);
    CHECK(rep.ndev == htonl(2));
    packDevListReply(rep, false);
    CHECK(rep.ndev == 2);
}

TEST_CASE("USB/IP Header cmd_submit pack/unpack round-trip") {
    UsbIpHeader hdr = {};
    hdr.base.command = 0x00000001;
    hdr.base.seqnum = 42;
    hdr.base.devid = 0x00020001;
    hdr.base.direction = 0x00000001;
    hdr.base.ep = 0x81;
    hdr.u.cmd_submit.transfer_flags = 0;
    hdr.u.cmd_submit.transfer_buffer_length = 64;
    hdr.u.cmd_submit.start_frame = 0;
    hdr.u.cmd_submit.number_of_packets = 0;
    hdr.u.cmd_submit.interval = 0;
    memset(hdr.u.cmd_submit.setup, 0, sizeof(hdr.u.cmd_submit.setup));
    hdr.u.cmd_submit.setup[0] = 0x21;
    hdr.u.cmd_submit.setup[1] = 0x20;
    hdr.u.cmd_submit.setup[2] = 0x00;
    hdr.u.cmd_submit.setup[3] = 0x00;
    packUsbIpHeader(hdr, true);
    CHECK(hdr.base.command == htonl(0x00000001));
    CHECK(hdr.base.seqnum == htonl(42));
    CHECK(hdr.base.devid == htonl(0x00020001));
    CHECK(hdr.base.direction == htonl(0x00000001));
    CHECK(hdr.base.ep == htonl(0x81));
    CHECK(hdr.u.cmd_submit.transfer_buffer_length == htonl(64));
    packUsbIpHeader(hdr, false);
    CHECK(hdr.base.command == 0x00000001);
    CHECK(hdr.base.seqnum == 42);
    CHECK(hdr.base.devid == 0x00020001);
    CHECK(hdr.base.direction == 0x00000001);
    CHECK(hdr.base.ep == 0x81);
    CHECK(hdr.u.cmd_submit.transfer_buffer_length == 64);
    CHECK(hdr.u.cmd_submit.setup[0] == 0x21);
    CHECK(hdr.u.cmd_submit.setup[1] == 0x20);
}

TEST_CASE("USB/IP Header ret_submit pack/unpack") {
    UsbIpHeader hdr = {};
    hdr.base.command = 0x00000003;
    hdr.base.seqnum = 99;
    hdr.base.devid = 0x00020001;
    hdr.base.direction = 0x00000001;
    hdr.base.ep = 0x81;
    hdr.u.ret_submit.status = 0;
    hdr.u.ret_submit.actual_length = 1024;
    hdr.u.ret_submit.start_frame = 0;
    hdr.u.ret_submit.number_of_packets = 0;
    hdr.u.ret_submit.error_count = 0;
    memset(hdr.u.ret_submit.padding, 0, sizeof(hdr.u.ret_submit.padding));
    packUsbIpHeader(hdr, true);
    CHECK(hdr.base.command == htonl(0x00000003));
    CHECK(hdr.base.seqnum == htonl(99));
    CHECK(hdr.u.ret_submit.actual_length == htonl(1024));
    packUsbIpHeader(hdr, false);
    CHECK(hdr.base.command == 0x00000003);
    CHECK(hdr.base.seqnum == 99);
    CHECK(hdr.u.ret_submit.actual_length == 1024);
    CHECK(hdr.u.ret_submit.status == 0);
}

TEST_CASE("USB/IP swap32s signed round-trip") {
    int32_t orig = -19;
    int32_t packed = swap32s(orig, true);
    CHECK(packed != orig);
    int32_t unpacked = swap32s(packed, false);
    CHECK(unpacked == orig);
}

TEST_CASE("USB/IP header size is 48 bytes") {
    CHECK(sizeof(UsbIpHeader) == 48);
}

TEST_CASE("USB/IP cmd_submit setup byte parsing") {
    UsbIpHeader hdr = {};
    hdr.u.cmd_submit.setup[0] = 0x21;
    hdr.u.cmd_submit.setup[1] = 0x20;
    hdr.u.cmd_submit.setup[2] = 0xC2;
    hdr.u.cmd_submit.setup[3] = 0x01;
    hdr.u.cmd_submit.setup[4] = 0x00;
    hdr.u.cmd_submit.setup[5] = 0x00;
    hdr.u.cmd_submit.setup[6] = 0x07;
    hdr.u.cmd_submit.setup[7] = 0x00;
    hdr.base.direction = 0x00000000;
    hdr.u.cmd_submit.transfer_buffer_length = 7;
    const uint8_t bmRequestType = hdr.u.cmd_submit.setup[0];
    const uint8_t bRequest = hdr.u.cmd_submit.setup[1];
    const uint16_t wValue = (uint16_t)hdr.u.cmd_submit.setup[2] |
                            ((uint16_t)hdr.u.cmd_submit.setup[3] << 8);
    const uint16_t wLength = (uint16_t)hdr.u.cmd_submit.setup[6] |
                             ((uint16_t)hdr.u.cmd_submit.setup[7] << 8);
    CHECK(bmRequestType == 0x21);
    CHECK(bRequest == 0x20);
    CHECK(wValue == 0x01C2);
    CHECK(wLength == 7);
}
