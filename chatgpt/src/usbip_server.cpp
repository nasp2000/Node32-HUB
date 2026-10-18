#include "usbip_server.h"

#include "feature_flags.h"
#if ENABLE_USBIP_SERVER == 1

#include <Arduino.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>
#include <errno.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "feature_flags.h"

#include <esp_heap_caps.h>

#include "direct_log.h"
#include "usb_host_manager.h"

namespace {

static constexpr uint16_t USBIP_SERVER_PORT_DEFAULT = 3240;
static constexpr size_t kUsbIpRxBufSize = 16384;
static constexpr int USBIP_LISTEN_BACKLOG = 2;
static constexpr uint8_t USBIP_MAX_CLIENTS = 4;
static constexpr int USBIP_RECV_TIMEOUT_MS_DEFAULT = 1200;
static constexpr uint16_t USBIP_IN_WAIT_TIMEOUT_MS_DEFAULT = 220;
static constexpr int USBIP_SOCKET_REUSE = 1;
static constexpr uint16_t USBIP_VERSION_V111 = 0x0111;

static constexpr uint16_t OP_REQ_DEVLIST = 0x8005;
static constexpr uint16_t OP_REP_DEVLIST = 0x0005;
static constexpr uint16_t OP_REQ_IMPORT  = 0x8003;
static constexpr uint16_t OP_REP_IMPORT  = 0x0003;

static constexpr uint32_t USBIP_CMD_SUBMIT = 0x00000001;
static constexpr uint32_t USBIP_CMD_UNLINK = 0x00000002;
static constexpr uint32_t USBIP_RET_SUBMIT = 0x00000003;
static constexpr uint32_t USBIP_RET_UNLINK = 0x00000004;
static constexpr uint32_t USBIP_DIR_OUT = 0x00000000;
static constexpr uint32_t USBIP_DIR_IN = 0x00000001;

static constexpr int32_t USBIP_ERR_ENODEV = -19;
static constexpr int32_t USBIP_ERR_EIO = -5;
static constexpr int32_t USBIP_ERR_EAGAIN = -11;
static constexpr int32_t USBIP_ERR_ENOTSUP = -95;

static constexpr uint32_t USBIP_ST_OK = 0x00;
static constexpr uint32_t USBIP_ST_NA = 0x01;

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

struct __attribute__((packed)) UsbIpImportRequest {
    char busid[32];
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

struct __attribute__((packed)) UsbIpHeaderCmdUnlink {
    uint32_t seqnum;
    uint8_t padding[24];
};

struct __attribute__((packed)) UsbIpHeaderRetUnlink {
    int32_t status;
    uint8_t padding[24];
};

struct __attribute__((packed)) UsbIpHeaderBasic {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
};

struct __attribute__((packed)) UsbIpHeader {
    UsbIpHeaderBasic base;
    union {
        UsbIpHeaderCmdSubmit cmd_submit;
        UsbIpHeaderRetSubmit ret_submit;
        UsbIpHeaderCmdUnlink cmd_unlink;
        UsbIpHeaderRetUnlink ret_unlink;
    } u;
};

struct UsbIpExportedDevice {
    bool present;
    uint8_t slot;
    uint8_t devAddr;
    uint8_t inEpAddr;
    uint8_t outEpAddr;
    uint8_t intEpAddr;
    uint8_t numInterfaces;
    uint8_t ifaceClass[4];
    uint8_t ifaceSubClass[4];
    uint8_t ifaceProtocol[4];
    char busid[32];
    uint8_t speed;
    uint16_t vid;
    uint16_t pid;
    uint16_t bcdDevice;
    uint8_t devClass;
    uint8_t devSubClass;
    uint8_t devProto;
};

struct UsbIpRxBridge {
    bool active;
    uint8_t slot;
    uint16_t head;
    uint16_t tail;
    uint8_t* data;
    portMUX_TYPE lock;
};

static TaskHandle_t s_serverTask = nullptr;
static volatile bool s_running = false;
static int s_listenFd = -1;
static UsbIpRuntimeConfig s_runtimeCfg = {true, USBIP_SERVER_PORT_DEFAULT, USBIP_RECV_TIMEOUT_MS_DEFAULT, USBIP_IN_WAIT_TIMEOUT_MS_DEFAULT};
static UsbIpServerStats s_stats = {false, USBIP_SERVER_PORT_DEFAULT, 0, 0, 0, 0, 0, 0, false};
static UsbIpRxBridge s_rxBridge = {false, 0, 0, 0, nullptr, portMUX_INITIALIZER_UNLOCKED};
static SemaphoreHandle_t s_rxDataSem = nullptr;
static SemaphoreHandle_t s_importSessionLock = nullptr;
static uint32_t s_lastRxBytes = 0;
static uint32_t s_lastTxBytes = 0;
static uint32_t s_lastThroughputTick = 0;
static uint8_t s_lastDeviceCount = 0;

struct UsbIpClientTaskCtx {
    int clientFd;
};

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

static bool netSendAll(int fd, const void* data, size_t len) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        int sent = send(fd, p, remaining, 0);
        if (sent <= 0) return false;
        p += sent;
        remaining -= static_cast<size_t>(sent);
        s_stats.tx_bytes += static_cast<uint32_t>(sent);
    }
    return true;
}

static size_t rxBridgeAvailable() {
    size_t avail = 0;
    portENTER_CRITICAL(&s_rxBridge.lock);
    if (s_rxBridge.head >= s_rxBridge.tail) {
        avail = (size_t)(s_rxBridge.head - s_rxBridge.tail);
    } else {
        avail = (size_t)(kUsbIpRxBufSize - s_rxBridge.tail + s_rxBridge.head);
    }
    portEXIT_CRITICAL(&s_rxBridge.lock);
    return avail;
}

static size_t rxBridgePop(uint8_t* out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    portENTER_CRITICAL(&s_rxBridge.lock);
    while (n < cap && s_rxBridge.tail != s_rxBridge.head) {
        out[n++] = s_rxBridge.data[s_rxBridge.tail];
        s_rxBridge.tail = (uint16_t)((s_rxBridge.tail + 1U) % kUsbIpRxBufSize);
    }
    portEXIT_CRITICAL(&s_rxBridge.lock);
    return n;
}

static void usbHostRxMirrorCb(uint8_t slot, const uint8_t* data, size_t len, void* /*ctx*/) {
    if (!data || len == 0) return;

    portENTER_CRITICAL(&s_rxBridge.lock);
    if (!s_rxBridge.active || slot != s_rxBridge.slot) {
        portEXIT_CRITICAL(&s_rxBridge.lock);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((s_rxBridge.head + 1U) % kUsbIpRxBufSize);
        if (next == s_rxBridge.tail) {
            // Drop oldest byte to keep most recent stream data available.
            s_rxBridge.tail = (uint16_t)((s_rxBridge.tail + 1U) % kUsbIpRxBufSize);
        }
        s_rxBridge.data[s_rxBridge.head] = data[i];
        s_rxBridge.head = next;
    }
    portEXIT_CRITICAL(&s_rxBridge.lock);

    if (s_rxDataSem) {
        (void)xSemaphoreGive(s_rxDataSem);
    }
}

static bool netRecvAll(int fd, void* data, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        int got = recv(fd, p, remaining, 0);
        if (got <= 0) return false;
        p += got;
        remaining -= static_cast<size_t>(got);
        s_stats.rx_bytes += static_cast<uint32_t>(got);
    }
    return true;
}

static uint8_t snapshotConnectedDevices(UsbIpExportedDevice* out, uint8_t cap) {
    if (!out || cap == 0) return 0;
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < USB_HOST_MAX_CDC_DEVICES; slot++) {
        UsbCdcDevice dev = usbHostGetDevice(slot);
        if (!dev.connected) continue;
        if (count >= cap) break;

        UsbIpExportedDevice item = {};
        item.present = true;
        item.slot = slot;
        item.devAddr = dev.dev_addr;
        item.inEpAddr = dev.in_ep_addr;
        item.outEpAddr = dev.out_ep_addr;
        item.intEpAddr = 0;
        item.speed = dev.speed;
        item.vid = dev.vid;
        item.pid = dev.pid;
        item.bcdDevice = dev.bcd_device;
        item.devClass = dev.iface_class;
        item.devSubClass = dev.iface_subclass;
        item.devProto = dev.iface_protocol;
        item.numInterfaces = 1;
        item.ifaceClass[0] = dev.iface_class;
        item.ifaceSubClass[0] = dev.iface_subclass;
        item.ifaceProtocol[0] = dev.iface_protocol;
        const uint32_t busDev = (dev.dev_addr > 0) ? dev.dev_addr : (uint32_t)(slot + 1U);
        snprintf(item.busid, sizeof(item.busid), "1-%lu", (unsigned long)busDev);
        out[count++] = item;
    }
    return count;
}

static int findDeviceByBusId(const char* busid, UsbIpExportedDevice* found) {
    if (!busid || !found) return -1;
    UsbIpExportedDevice list[USB_HOST_MAX_CDC_DEVICES] = {};
    uint8_t n = snapshotConnectedDevices(list, USB_HOST_MAX_CDC_DEVICES);
    for (uint8_t i = 0; i < n; i++) {
        if (strncmp(busid, list[i].busid, sizeof(list[i].busid)) == 0) {
            *found = list[i];
            return i;
        }
    }
    return -1;
}

static bool findDeviceByDevNum(uint32_t devnum, UsbIpExportedDevice* found) {
    if (!found) return false;
    UsbIpExportedDevice list[USB_HOST_MAX_CDC_DEVICES] = {};
    uint8_t n = snapshotConnectedDevices(list, USB_HOST_MAX_CDC_DEVICES);
    for (uint8_t i = 0; i < n; i++) {
        const uint32_t cur = (list[i].devAddr > 0) ? list[i].devAddr : (uint32_t)(list[i].slot + 1U);
        if (cur == devnum) {
            *found = list[i];
            return true;
        }
    }
    return false;
}

static bool sendRetSubmit(int fd, const UsbIpHeader& req, int32_t status, int32_t actualLen) {
    UsbIpHeader ret = {};
    ret.base.command = USBIP_RET_SUBMIT;
    ret.base.seqnum = req.base.seqnum;
    ret.base.devid = req.base.devid;
    ret.base.direction = req.base.direction;
    ret.base.ep = req.base.ep;
    ret.u.ret_submit.status = status;
    ret.u.ret_submit.actual_length = actualLen;
    ret.u.ret_submit.start_frame = 0;
    ret.u.ret_submit.number_of_packets = 0;
    ret.u.ret_submit.error_count = 0;
    memset(ret.u.ret_submit.padding, 0, sizeof(ret.u.ret_submit.padding));
    packUsbIpHeader(ret, true);
    return netSendAll(fd, &ret, sizeof(ret));
}

static bool sendRetSubmitWithPayload(int fd, const UsbIpHeader& req, int32_t status,
                                     int32_t actualLen, const uint8_t* payload) {
    if (!sendRetSubmit(fd, req, status, actualLen)) return false;
    if (actualLen > 0 && payload) {
        return netSendAll(fd, payload, (size_t)actualLen);
    }
    return true;
}

static bool sendRetUnlink(int fd, const UsbIpHeader& req, int32_t status) {
    UsbIpHeader ret = {};
    ret.base.command = USBIP_RET_UNLINK;
    ret.base.seqnum = req.base.seqnum;
    ret.base.devid = req.base.devid;
    ret.base.direction = req.base.direction;
    ret.base.ep = req.base.ep;
    ret.u.ret_unlink.status = status;
    memset(ret.u.ret_unlink.padding, 0, sizeof(ret.u.ret_unlink.padding));
    packUsbIpHeader(ret, true);
    return netSendAll(fd, &ret, sizeof(ret));
}

static bool discardIncomingBytes(int fd, size_t len) {
    uint8_t tmp[128];
    size_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(tmp) ? sizeof(tmp) : remaining;
        if (!netRecvAll(fd, tmp, chunk)) return false;
        remaining -= chunk;
    }
    return true;
}

static bool recvAndForwardOutPayload(int fd, uint8_t slot, size_t len, int32_t* forwarded) {
    if (!forwarded) return false;
    *forwarded = 0;
    if (len == 0) return true;

    uint8_t tmp[512];
    size_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(tmp) ? sizeof(tmp) : remaining;
        if (!netRecvAll(fd, tmp, chunk)) return false;

        size_t sent = usbHostSend(slot, tmp, chunk);
        *forwarded += (int32_t)sent;
        remaining -= chunk;
        if (sent < chunk) {
            if (!discardIncomingBytes(fd, remaining)) return false;
            return true;
        }
    }
    return true;
}

static int32_t handleControlSubmitOnEp0(int fd, const UsbIpHeader& req, uint8_t slot, int32_t* actualLen) {
    if (!actualLen) return USBIP_ERR_EIO;
    *actualLen = 0;

    const uint8_t bmRequestType = req.u.cmd_submit.setup[0];
    const uint8_t bRequest = req.u.cmd_submit.setup[1];
    const uint16_t wValue = (uint16_t)req.u.cmd_submit.setup[2] |
                            ((uint16_t)req.u.cmd_submit.setup[3] << 8);
    const uint16_t wIndex = (uint16_t)req.u.cmd_submit.setup[4] |
                            ((uint16_t)req.u.cmd_submit.setup[5] << 8);
    const uint16_t wLength = (uint16_t)req.u.cmd_submit.setup[6] |
                             ((uint16_t)req.u.cmd_submit.setup[7] << 8);
    (void)wIndex;

    // USB CDC ACM SET_LINE_CODING (Host->Device, Class, Interface), 7-byte payload.
    if (bmRequestType == 0x21 && bRequest == 0x20 &&
        req.base.direction == USBIP_DIR_OUT && wLength == 7) {
        int32_t tlen = req.u.cmd_submit.transfer_buffer_length;
        if (tlen < 7) return USBIP_ERR_EIO;

        uint8_t payload[7] = {};
        if (!netRecvAll(fd, payload, sizeof(payload))) return USBIP_ERR_EIO;
        if (tlen > 7) {
            if (!discardIncomingBytes(fd, (size_t)(tlen - 7))) return USBIP_ERR_EIO;
        }

        uint32_t baud = (uint32_t)payload[0] |
                        ((uint32_t)payload[1] << 8) |
                        ((uint32_t)payload[2] << 16) |
                        ((uint32_t)payload[3] << 24);
        uint8_t stopBits = payload[4];
        uint8_t parity = payload[5];
        uint8_t dataBits = payload[6];

        uint8_t mappedStop = 1;
        if (stopBits == 2) mappedStop = 2;

        bool ok = usbHostSetLineCoding(slot, baud, dataBits, mappedStop, parity);
        if (!ok) return USBIP_ERR_EIO;
        *actualLen = 0;
        return 0;
    }

    // USB CDC ACM SET_CONTROL_LINE_STATE — forward DTR/RTS to device.
    if (bmRequestType == 0x21 && bRequest == 0x22 && req.base.direction == USBIP_DIR_OUT) {
        int32_t tlen = req.u.cmd_submit.transfer_buffer_length;
        if (tlen > 0) {
            if (!discardIncomingBytes(fd, (size_t)tlen)) return USBIP_ERR_EIO;
        }
        usbHostSetDtrRts(slot, !!(wValue & 1), !!(wValue & 2));
        *actualLen = 0;
        return 0;
    }

    // Unsupported control request: consume outgoing payload if any.
    if (req.base.direction == USBIP_DIR_OUT && req.u.cmd_submit.transfer_buffer_length > 0) {
        if (!discardIncomingBytes(fd, (size_t)req.u.cmd_submit.transfer_buffer_length)) return USBIP_ERR_EIO;
    }
    return USBIP_ERR_ENOTSUP;
}

static bool sendDevList(int fd, uint16_t clientVersion) {
    UsbIpExportedDevice list[USB_HOST_MAX_CDC_DEVICES] = {};
    const uint8_t n = snapshotConnectedDevices(list, USB_HOST_MAX_CDC_DEVICES);

    UsbIpOpCommon hdr = {clientVersion, OP_REP_DEVLIST, USBIP_ST_OK};
    packOpCommon(hdr, true);
    if (!netSendAll(fd, &hdr, sizeof(hdr))) return false;

    UsbIpDevListReply rep = {n};
    packDevListReply(rep, true);
    if (!netSendAll(fd, &rep, sizeof(rep))) return false;

    for (uint8_t i = 0; i < n; i++) {
        UsbIpUsbDevice dev = {};
        snprintf(dev.path, sizeof(dev.path), "/usb/device/%s", list[i].busid);
        strlcpy(dev.busid, list[i].busid, sizeof(dev.busid));
        dev.busnum = 1;
        dev.devnum = (list[i].devAddr > 0) ? list[i].devAddr : (uint32_t)(list[i].slot + 1U);
        dev.speed = list[i].speed ? list[i].speed : 2;
        dev.idVendor = list[i].vid;
        dev.idProduct = list[i].pid;
        dev.bcdDevice = list[i].bcdDevice;
        dev.bDeviceClass = list[i].devClass;
        dev.bDeviceSubClass = list[i].devSubClass;
        dev.bDeviceProtocol = list[i].devProto;
        dev.bConfigurationValue = 1;
        dev.bNumConfigurations = 1;
        dev.bNumInterfaces = list[i].numInterfaces;
        packUsbDevice(dev, true);
        if (!netSendAll(fd, &dev, sizeof(dev))) return false;

        for (uint8_t k = 0; k < list[i].numInterfaces && k < 4; k++) {
            UsbIpUsbInterface iface = {};
            iface.bInterfaceClass = list[i].ifaceClass[k];
            iface.bInterfaceSubClass = list[i].ifaceSubClass[k];
            iface.bInterfaceProtocol = list[i].ifaceProtocol[k];
            iface.padding = 0;
            if (!netSendAll(fd, &iface, sizeof(iface))) return false;
        }
    }

    return true;
}

static bool runImportSession(int fd, const UsbIpExportedDevice& dev, uint16_t clientVersion) {
    UsbIpOpCommon replyHdr = {clientVersion, OP_REP_IMPORT, USBIP_ST_OK};
    packOpCommon(replyHdr, true);
    if (!netSendAll(fd, &replyHdr, sizeof(replyHdr))) return false;

    UsbIpUsbDevice udev = {};
    snprintf(udev.path, sizeof(udev.path), "/usb/device/%s", dev.busid);
    strlcpy(udev.busid, dev.busid, sizeof(udev.busid));
    udev.busnum = 1;
    udev.devnum = (dev.devAddr > 0) ? dev.devAddr : (uint32_t)(dev.slot + 1U);
    udev.speed = dev.speed ? dev.speed : 2;
    udev.idVendor = dev.vid;
    udev.idProduct = dev.pid;
    udev.bcdDevice = dev.bcdDevice;
    udev.bDeviceClass = dev.devClass;
    udev.bDeviceSubClass = dev.devSubClass;
    udev.bDeviceProtocol = dev.devProto;
    udev.bConfigurationValue = 1;
    udev.bNumConfigurations = 1;
    udev.bNumInterfaces = 1;
    packUsbDevice(udev, true);
    if (!netSendAll(fd, &udev, sizeof(udev))) return false;

    const uint32_t importedDevNum = (dev.devAddr > 0) ? dev.devAddr : (uint32_t)(dev.slot + 1U);

    portENTER_CRITICAL(&s_rxBridge.lock);
    s_rxBridge.active = true;
    s_rxBridge.slot = dev.slot;
    s_rxBridge.head = 0;
    s_rxBridge.tail = 0;
    portEXIT_CRITICAL(&s_rxBridge.lock);

    // Keep connection alive and map a practical subset of URBs to the existing USB CDC host API.
    while (s_running) {
        UsbIpHeader hdr = {};
        if (!netRecvAll(fd, &hdr, sizeof(hdr))) break;
        packUsbIpHeader(hdr, false);

        if (hdr.base.command == USBIP_CMD_SUBMIT) {
            const uint32_t devnum = (hdr.base.devid & 0xFFFFu);
            if (devnum != importedDevNum) {
                int32_t tlen = hdr.u.cmd_submit.transfer_buffer_length;
                if (hdr.base.direction == USBIP_DIR_OUT && tlen > 0) {
                    if (!discardIncomingBytes(fd, (size_t)tlen)) return false;
                }
                if (!sendRetSubmit(fd, hdr, USBIP_ERR_ENODEV, 0)) return false;
                continue;
            }

            UsbIpExportedDevice curDev = {};
            if (!findDeviceByDevNum(devnum, &curDev) || curDev.slot != dev.slot) {
                int32_t tlen = hdr.u.cmd_submit.transfer_buffer_length;
                if (hdr.base.direction == USBIP_DIR_OUT && tlen > 0) {
                    if (!discardIncomingBytes(fd, (size_t)tlen)) return false;
                }
                if (!sendRetSubmit(fd, hdr, USBIP_ERR_ENODEV, 0)) return false;
                continue;
            }

            if (hdr.base.ep == 0) {
                int32_t actual = 0;
                int32_t st = handleControlSubmitOnEp0(fd, hdr, curDev.slot, &actual);
                if (!sendRetSubmit(fd, hdr, st, actual)) return false;
                continue;
            }

            const uint32_t inEpNum = (uint32_t)(curDev.inEpAddr & 0x7Fu);
            const uint32_t outEpNum = (uint32_t)(curDev.outEpAddr & 0x7Fu);
            const uint32_t intEpNum = (uint32_t)(curDev.intEpAddr & 0x7Fu);
            bool epSupported = false;
            if (hdr.base.direction == USBIP_DIR_OUT) {
                epSupported = (hdr.base.ep == outEpNum);
            } else if (hdr.base.direction == USBIP_DIR_IN) {
                epSupported = (hdr.base.ep == inEpNum);
                if (!epSupported && intEpNum != 0) {
                    epSupported = (hdr.base.ep == intEpNum);
                }
            }

            if (!epSupported) {
                int32_t tlen = hdr.u.cmd_submit.transfer_buffer_length;
                if (hdr.base.direction == USBIP_DIR_OUT && tlen > 0) {
                    if (!discardIncomingBytes(fd, (size_t)tlen)) return false;
                }
                if (!sendRetSubmit(fd, hdr, USBIP_ERR_ENOTSUP, 0)) return false;
                continue;
            }

            if (hdr.base.direction == USBIP_DIR_OUT) {
                int32_t tlen = hdr.u.cmd_submit.transfer_buffer_length;
                if (tlen < 0) {
                    if (!sendRetSubmit(fd, hdr, USBIP_ERR_EIO, 0)) return false;
                    continue;
                }

                int32_t forwarded = 0;
                if (!recvAndForwardOutPayload(fd, curDev.slot, (size_t)tlen, &forwarded)) return false;
                const int32_t st = (forwarded == tlen) ? 0 : USBIP_ERR_EIO;
                if (!sendRetSubmit(fd, hdr, st, forwarded)) return false;
                continue;
            }

            // IN path: wait briefly for RX bytes mirrored from usb_host_manager callback.
            int32_t want = hdr.u.cmd_submit.transfer_buffer_length;
            if (want <= 0) {
                if (!sendRetSubmit(fd, hdr, 0, 0)) return false;
                continue;
            }

            if (rxBridgeAvailable() == 0 && s_rxDataSem) {
                (void)xSemaphoreTake(s_rxDataSem, pdMS_TO_TICKS(s_runtimeCfg.in_wait_timeout_ms));
            }

            size_t cap = (size_t)want;
            if (cap > 1024U) cap = 1024U;
            uint8_t payload[1024];
            size_t got = rxBridgePop(payload, cap);
            if (!sendRetSubmitWithPayload(fd, hdr, 0, (int32_t)got, payload)) return false;
        } else if (hdr.base.command == USBIP_CMD_UNLINK) {
            if (!sendRetUnlink(fd, hdr, 0)) return false;
        } else {
            directLogPrintln(String("[USBIP] unknown URB command 0x") + String((unsigned long)hdr.base.command, HEX));
            break;
        }
    }

    portENTER_CRITICAL(&s_rxBridge.lock);
    s_rxBridge.active = false;
    s_rxBridge.head = 0;
    s_rxBridge.tail = 0;
    portEXIT_CRITICAL(&s_rxBridge.lock);
    return true;
}

static bool handleImport(int fd, uint16_t clientVersion) {
    UsbIpImportRequest req = {};
    if (!netRecvAll(fd, &req, sizeof(req))) return false;
    req.busid[sizeof(req.busid) - 1] = '\0';

    UsbIpExportedDevice dev = {};
    if (findDeviceByBusId(req.busid, &dev) < 0) {
        UsbIpOpCommon replyHdr = {clientVersion, OP_REP_IMPORT, USBIP_ST_NA};
        packOpCommon(replyHdr, true);
        return netSendAll(fd, &replyHdr, sizeof(replyHdr));
    }

    // Only one active IMPORT session can own the RX bridge at a time.
    if (!s_importSessionLock || xSemaphoreTake(s_importSessionLock, 0) != pdTRUE) {
        UsbIpOpCommon replyHdr = {clientVersion, OP_REP_IMPORT, USBIP_ST_NA};
        packOpCommon(replyHdr, true);
        return netSendAll(fd, &replyHdr, sizeof(replyHdr));
    }

    const bool ok = runImportSession(fd, dev, clientVersion);
    (void)xSemaphoreGive(s_importSessionLock);
    return ok;
}

static void closeSocketIfOpen(int &fd) {
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        fd = -1;
    }
}

static bool setupListenSocket() {
    s_listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_listenFd < 0) {
        directLogPrintln(String("[USBIP] socket() failed: errno=") + String(errno));
        return false;
    }

    setsockopt(s_listenFd, SOL_SOCKET, SO_REUSEADDR, &USBIP_SOCKET_REUSE, sizeof(USBIP_SOCKET_REUSE));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_runtimeCfg.listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        directLogPrintln(String("[USBIP] bind() failed: errno=") + String(errno));
        closeSocketIfOpen(s_listenFd);
        return false;
    }

    if (listen(s_listenFd, USBIP_LISTEN_BACKLOG) != 0) {
        directLogPrintln(String("[USBIP] listen() failed: errno=") + String(errno));
        closeSocketIfOpen(s_listenFd);
        return false;
    }

    directLogPrintln(String("[USBIP] listening on tcp/") + String(static_cast<unsigned>(s_runtimeCfg.listen_port)));
    return true;
}

static void handleClient(int clientFd) {
    s_stats.accepted_clients++;

    timeval tv = {};
    const uint32_t recvMs = s_runtimeCfg.recv_timeout_ms;
    tv.tv_sec = (time_t)(recvMs / 1000U);
    tv.tv_usec = (suseconds_t)((recvMs % 1000U) * 1000U);
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint16_t clientVersion = USBIP_VERSION_V111;
    while (s_running) {
        UsbIpOpCommon op = {};
        if (!netRecvAll(clientFd, &op, sizeof(op))) break;
        packOpCommon(op, false);

        if (op.version != USBIP_VERSION_V111) {
            directLogPrintln(String("[USBIP] unsupported version 0x") + String((unsigned)op.version, HEX));
            break;
        }
        clientVersion = op.version;

        if (op.code == OP_REQ_DEVLIST) {
            if (!sendDevList(clientFd, clientVersion)) break;
            continue;
        }
        if (op.code == OP_REQ_IMPORT) {
            if (!handleImport(clientFd, clientVersion)) break;
            continue;
        }

        directLogPrintln(String("[USBIP] unknown operation 0x") + String((unsigned)op.code, HEX));
        break;
    }

    closeSocketIfOpen(clientFd);
    if (s_stats.active_clients > 0) s_stats.active_clients--;
}

static void usbipClientTask(void* arg) {
    UsbIpClientTaskCtx* ctx = reinterpret_cast<UsbIpClientTaskCtx*>(arg);
    if (!ctx) {
        vTaskDelete(nullptr);
        return;
    }

    const int clientFd = ctx->clientFd;
    free(ctx);
    handleClient(clientFd);
    vTaskDelete(nullptr);
}

static void usbipServerTask(void* /*arg*/) {
    while (s_running) {
        if (s_listenFd < 0) {
            if (!setupListenSocket()) {
                vTaskDelay(pdMS_TO_TICKS(1200));
                continue;
            }
        }

        sockaddr_in clientAddr = {};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(s_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!s_running) break;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (s_stats.active_clients >= USBIP_MAX_CLIENTS) {
            directLogPrintln(String("[USBIP] max clients reached (") + String((unsigned)USBIP_MAX_CLIENTS) + ") rejecting");
            closeSocketIfOpen(clientFd);
            continue;
        }

        // Reserve a client slot before task creation to avoid accept-time races.
        s_stats.active_clients++;

        UsbIpClientTaskCtx* ctx = reinterpret_cast<UsbIpClientTaskCtx*>(malloc(sizeof(UsbIpClientTaskCtx)));
        if (!ctx) {
            directLogPrintln(F("[USBIP] failed to alloc client task context"));
            if (s_stats.active_clients > 0) s_stats.active_clients--;
            closeSocketIfOpen(clientFd);
            continue;
        }
        ctx->clientFd = clientFd;

        TaskHandle_t clientTask = nullptr;
        if (xTaskCreatePinnedToCore(usbipClientTask, "usbip_cli", 6144, ctx, 3, &clientTask, tskNO_AFFINITY) != pdPASS) {
            directLogPrintln(F("[USBIP] failed to create client task"));
            free(ctx);
            if (s_stats.active_clients > 0) s_stats.active_clients--;
            closeSocketIfOpen(clientFd);
            continue;
        }
    }

    closeSocketIfOpen(s_listenFd);
    s_stats.running = false;
    s_serverTask = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void usbipServerBegin() {
    if (s_running) return;
    if (!s_runtimeCfg.enabled) return;

    if (!s_rxBridge.data) {
        s_rxBridge.data = (uint8_t*)heap_caps_malloc(kUsbIpRxBufSize, MALLOC_CAP_SPIRAM);
        if (!s_rxBridge.data) {
            directLogPrintln(F("[USBIP] failed to alloc RX bridge buffer"));
            return;
        }
    }

    s_stats = {false, s_runtimeCfg.listen_port, 0, 0, 0, 0};
    if (!s_rxDataSem) {
        s_rxDataSem = xSemaphoreCreateBinary();
    }
    if (!s_importSessionLock) {
        s_importSessionLock = xSemaphoreCreateMutex();
    }
    usbHostSetRxHook(usbHostRxMirrorCb, nullptr);
    s_running = true;
    s_stats.running = true;

    if (xTaskCreatePinnedToCore(usbipServerTask, "usbip_srv", 6144, nullptr, 4, &s_serverTask, tskNO_AFFINITY) != pdPASS) {
        s_running = false;
        s_stats.running = false;
        usbHostSetRxHook(nullptr, nullptr);
        directLogPrintln(F("[USBIP] task creation failed"));
    }
}

void usbipServerUpdate() {
    const uint32_t now = millis();
    if (s_lastThroughputTick == 0) {
        s_lastThroughputTick = now;
        s_lastRxBytes = s_stats.rx_bytes;
        s_lastTxBytes = s_stats.tx_bytes;
        UsbIpExportedDevice list[USB_HOST_MAX_CDC_DEVICES] = {};
        s_lastDeviceCount = snapshotConnectedDevices(list, USB_HOST_MAX_CDC_DEVICES);
        return;
    }

    const uint32_t elapsed = now - s_lastThroughputTick;
    if (elapsed >= 1000) {
        const uint32_t rxDelta = s_stats.rx_bytes - s_lastRxBytes;
        const uint32_t txDelta = s_stats.tx_bytes - s_lastTxBytes;
        s_stats.rx_bytes_per_sec = (rxDelta * 1000U) / elapsed;
        s_stats.tx_bytes_per_sec = (txDelta * 1000U) / elapsed;
        s_lastRxBytes = s_stats.rx_bytes;
        s_lastTxBytes = s_stats.tx_bytes;
        s_lastThroughputTick = now;
    }

    UsbIpExportedDevice list[USB_HOST_MAX_CDC_DEVICES] = {};
    const uint8_t currentCount = snapshotConnectedDevices(list, USB_HOST_MAX_CDC_DEVICES);
    if (currentCount != s_lastDeviceCount) {
        s_stats.devices_dirty = true;
        s_lastDeviceCount = currentCount;
    }
}

void usbipServerStop() {
    if (!s_running) return;

    s_running = false;
    usbHostSetRxHook(nullptr, nullptr);
    portENTER_CRITICAL(&s_rxBridge.lock);
    s_rxBridge.active = false;
    s_rxBridge.head = 0;
    s_rxBridge.tail = 0;
    portEXIT_CRITICAL(&s_rxBridge.lock);
    if (s_rxBridge.data) {
        heap_caps_free(s_rxBridge.data);
        s_rxBridge.data = nullptr;
    }
    // Keep semaphore allocated across stop/start cycles to avoid callback/delete races.
    if (s_rxDataSem) {
        while (xSemaphoreTake(s_rxDataSem, 0) == pdTRUE) {
        }
    }
    closeSocketIfOpen(s_listenFd);
}

bool usbipServerIsRunning() {
    return s_running;
}

UsbIpServerStats usbipServerGetStats() {
    UsbIpServerStats snap = s_stats;
    s_stats.devices_dirty = false;
    return snap;
}

UsbIpRuntimeConfig usbipServerGetRuntimeConfig() {
    return s_runtimeCfg;
}

void usbipServerSetRuntimeConfig(const UsbIpRuntimeConfig& cfg) {
    UsbIpRuntimeConfig next = cfg;
    if (next.listen_port == 0) next.listen_port = USBIP_SERVER_PORT_DEFAULT;
    if (next.recv_timeout_ms < 100) next.recv_timeout_ms = USBIP_RECV_TIMEOUT_MS_DEFAULT;
    if (next.in_wait_timeout_ms < 20) next.in_wait_timeout_ms = USBIP_IN_WAIT_TIMEOUT_MS_DEFAULT;

    const bool wasRunning = s_running;
    s_runtimeCfg = next;
    s_stats.listen_port = s_runtimeCfg.listen_port;

    if (wasRunning) {
        usbipServerStop();
    }
    if (s_runtimeCfg.enabled) {
        usbipServerBegin();
    }
}

#endif
