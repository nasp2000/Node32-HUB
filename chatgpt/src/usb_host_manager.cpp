// usb_host_manager.cpp
// USB Host CDC-ACM manager for ESP32-P4.
// Registers a CDC-ACM class client with the IDF USB Host library.
// On device connect: applies default line coding and starts receiving data.
// On RX data: calls injectDataToRXBuffer() to feed the RS232 pipeline.
// On device disconnect: marks slot as free.

#include "usb_host_manager.h"
#include "grbl_sender.h"

#if ENABLE_USB_HOST == 1
#include <string.h>

// ── Begin-stage markers (crash forensics) ────────────────────────────────────
// usbHostBegin() can wedge the chip (INT_WDT on S3); serial output dies with
// it. Each stage is stored in RTC slow memory *before* it runs — RTC survives
// watchdog/software resets (not power loss), needs no NVS, no flash wear.
// After the reboot, /api/grbl/status "usb_begin_stage" shows exactly where
// bring-up died (same place the boot-log CRASH_INT_WDT entries come from).
// Verified 2026-10-11 on n16r8 ELF: symbol lands at 0x50000200 (.rtc.data).
// .rtc.data is PROGBITS: startup re-copies the initializer on EVERY boot,
// including WDT resets -> stage was wiped -> CRASH_STAGE always empty.
// RTC_NOINIT_ATTR (".rtc_noinit", NOBITS) is never touched by startup, so the
// stage written before the wedge survives TG1WDT_SYS_RST. No initializer here
// (meaningless for noinit); power-on garbage is rejected by
// usbHostStageIsKnown()/BootMark. S3_CNC-only; other packs keep RTC_DATA_ATTR.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
static RTC_NOINIT_ATTR char s_beginStageRtc[24];
#else
static RTC_DATA_ATTR char s_beginStageRtc[24] = "";
#endif
static char s_beginStageRam[24] = "";

// Public (header): also used once at end of setup() to drop a "boot" marker
// that validates the RTC mechanism even if no begin ever runs.
void usbHostBeginStageMark(const char* stage) {
    if (stage) {
        strncpy(s_beginStageRam, stage, sizeof(s_beginStageRam) - 1);
        s_beginStageRam[sizeof(s_beginStageRam) - 1] = '\0';
        strncpy(s_beginStageRtc, stage, sizeof(s_beginStageRtc) - 1);
        s_beginStageRtc[sizeof(s_beginStageRtc) - 1] = '\0';
    }
}

const char* usbHostBeginStage() {
    if (s_beginStageRam[0]) return s_beginStageRam;
    return s_beginStageRtc;
}

// Setup-time marker: records "boot" ONLY when RTC holds no known stage.
// Power-on garbage or an untouched RTC gets "boot"; a crash point (or "off")
// left by the previous boot is preserved for post-mortem reads.
static bool usbHostStageIsKnown(const char* v) {
    static const char* const known[] = {
        "boot", "off", "task", "enter", "sync", "slots", "gpiosample", "gpio",
        "install_pre",
        "install_ok", "pwr", "hprt", "client", "tasks", "done", nullptr
    };
    if (!v || !v[0]) return false;
    for (const char* const* k = known; *k; k++) {
        if (strcmp(v, *k) == 0) return true;
    }
    return false;
}

void usbHostBeginStageBootMark() {
    if (!usbHostStageIsKnown(s_beginStageRtc)) {
        usbHostBeginStageMark("boot");
    } else if (!s_beginStageRam[0]) {
        strncpy(s_beginStageRam, s_beginStageRtc, sizeof(s_beginStageRam) - 1);
        s_beginStageRam[sizeof(s_beginStageRam) - 1] = '\0';
    }
}
#endif

// CH340 baud-rate divisor encoding (matches Linux kernel ch341.c).
// Internal clock = 48 MHz, prescaler (ps) ∈ [0,3], factor (fact) ∈ [0,1].
// clk_div = 1 << (12 - 3*ps - fact)
// div = 48000000 / (clk_div * baud), clamped to [2, 256].
// Returns encoded value: (0x100 - div) << 8 | fact << 2 | ps.
static uint16_t ch340CalcDivisor(unsigned long baud) {
    // For 115200 with CH340G (0x1A86:7523): ps=3, fact=0, div=52 → 0xCC03.
    // For   9600 with CH340G: ps=2, fact=0, div=78 → 0xB202.
    // These match MacOS/Windows CH340 drivers as well.
    //
    // ch341_min_rates[ps] for CH340G @ 48 MHz:
    //   ps=0: ~46,  ps=1: ~366,  ps=2: ~2930,  ps=3: ~23438

    static const uint32_t minRate[4] = { 46, 366, 2930, 23438 };

    int ps;
    for (ps = 3; ps >= 0; ps--) {
        if (baud > minRate[ps]) break;
    }
    if (ps < 0) ps = 0;

    uint32_t clkDiv = (uint32_t)(1u << (12 - 3 * ps - 1));
    uint32_t d = 48000000UL / (clkDiv * baud);

    if (d < 2 || d > 256) {
        d = 48000000UL / ((clkDiv * 2) * baud);
        return (uint16_t)(((0x100 - d) << 8) | (0 << 2) | (uint32_t)ps);
    }

    uint64_t left  = 16ULL * 48000000UL / (clkDiv * d) - 16ULL * baud;
    uint64_t right = 16ULL * baud - 16ULL * 48000000UL / (clkDiv * (d + 1));
    if (right < left) d++;

    if (d % 2 == 0) {
        d /= 2;
        return (uint16_t)(((0x100 - d) << 8) | (0 << 2) | (uint32_t)ps);
    }

    return (uint16_t)(((0x100 - d) << 8) | (1 << 2) | (uint32_t)ps);
}

#include "feature_flags.h"
#if ENABLE_USB_HOST == 1 && USB_HOST_NATIVE_AVAILABLE == 1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <string.h>

// IDF USB Host library headers.
#include <usb/usb_host.h>
#include <driver/gpio.h>

#if defined(__has_include)
#if __has_include(<hal/gpio_ll.h>)
#include <hal/gpio_ll.h>          // IO_MUX struct (driver/gpio.h doesn't pull it on P4)
#endif
#if __has_include(<soc/soc_caps.h>)
#include <soc/soc_caps.h>
#endif
#if __has_include(<hal/usb_wrap_ll.h>)
#include <hal/usb_wrap_ll.h>
#endif
// USB PHY API: supports OTG11 FS FSLS host mode on ESP32-P4 (USB_PHY_SUPPORTS_P4_OTG11=1).
#if __has_include(<esp_private/usb_phy.h>)
#include <esp_private/usb_phy.h>
#define USB_HOST_HAVE_PHY_API 1
#endif
// DWC2 register structure for direct HPRT0 access (P4 HS peripheral diagnostic).
#if __has_include(<soc/usb_dwc_struct.h>)
#include <soc/usb_dwc_struct.h>
#define USB_HOST_HPRT0_AVAILABLE 1
#endif
#endif

// Direct HPRT0 PPWR toggle for P4, bypassing the HCD API when event_pending
// prevents usb_host_lib_set_root_port_power() from succeeding.
// Toggles both HS (UTMI, external Type-A) and FS (FSLS, GPIO26/27) ports
// since we can't know at compile time which one has the CH334 hub connected.
#if !defined(USB_HOST_P4_ROOT_POWER_CYCLE)
#define USB_HOST_P4_ROOT_POWER_CYCLE 1 // 1 = install+post-task PPWR cycles (P4 workaround)
#endif
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(USB_HOST_HPRT0_AVAILABLE)
static void dwcHprtSetPortPower(volatile uint32_t* hprtReg, bool on) {
    uint32_t const w1c_mask = (1U << 1) | (1U << 2) | (1U << 3) | (1U << 5);
    uint32_t val = *hprtReg;
    if (on) {
        val |=  (1U << 12);
    } else {
        val &= ~(1U << 12);
    }
    val &= ~w1c_mask;
    *hprtReg = val;
}
static void dwcHprtSetPower(bool on) {
    // Only touch the controllers selected by the active map. On ESP32-P4 the
    // USB_DWC_HS/FS extern pointers can resolve to non-USB addresses when the
    // corresponding peripheral is not enabled in the build (FS-only / HS-only
    // test maps), so reading them would panic with a load access fault.
#if (USB_HOST_P4_DUAL_TEST_MAP & 1U)
    dwcHprtSetPortPower(&USB_DWC_HS.hprt_reg.val, on);
#endif
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && (USB_HOST_P4_DUAL_TEST_MAP & 2U)
    dwcHprtSetPortPower(&USB_DWC_FS.hprt_reg.val, on);
#endif
}
#endif

#include "direct_log.h"
#include "commands.h"

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// injectDataToRXBuffer is defined in sensor_helpers_impl.h (included from chatgpt.ino).
// Declare here so usb_host_manager can feed CDC data to the monitor pipeline.
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

#if ENABLE_GRBL == 1
#include "grbl_sender.h"     // grblOnRxData()
#endif

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static bool s_running = false;
static const char* s_backendName = "native";
static String s_lastError;
static UsbHostRxHook s_rxHook = nullptr;
static void* s_rxHookCtx = nullptr;

// Device slot array — one entry per concurrent CDC device.
static UsbCdcDevice s_devices[USB_HOST_MAX_CDC_DEVICES];

// Track non-CDC device addresses (hubs, etc.) to avoid re-processing every scan cycle.
#define KNOWN_NON_CDC_ADDRS_MAX 8
static uint8_t s_nonCdcAddrs[KNOWN_NON_CDC_ADDRS_MAX];
static int s_nonCdcAddrCount = 0;

static bool isNonCdcAddr(uint8_t addr) {
    for (int i = 0; i < s_nonCdcAddrCount; i++) {
        if (s_nonCdcAddrs[i] == addr) return true;
    }
    return false;
}
static void addNonCdcAddr(uint8_t addr) {
    if (s_nonCdcAddrCount < KNOWN_NON_CDC_ADDRS_MAX) {
        s_nonCdcAddrs[s_nonCdcAddrCount++] = addr;
    }
}

// Per-address open cooldown (ms). A CH340 whose EP 0 is wedged stays in the
// host lib address list after its slot is closed, so without throttling the
// scan would call usb_host_device_open() and log ESP_ERR_INVALID_STATE every
// 500 ms forever. This also leaves room for the recovery power-cycle (which
// re-enumerates on a fresh address or after a short gap) to succeed.
static uint32_t s_lastOpenAttemptMs[128] = {0};
#define USB_HOST_OPEN_RETRY_MS 5000

static SemaphoreHandle_t s_mutex = nullptr;

static UsbHostStats s_stats = {};
static UsbHostDiag s_diag = {};

// OTG11 FS FSLS PHY handle: set when USB_HOST_P4_OTG11_FS_HOST=1 and usb_new_phy() succeeds.
// Must be deleted via usb_del_phy() in usbHostStop() to properly release the FS PHY.
#if defined(USB_HOST_HAVE_PHY_API)
static usb_phy_handle_t s_p4OtgFsPhyHandle = nullptr;
// OTG20 HS external UTMI PHY handle: also taken explicitly when OTG11 FS is enabled, so
// that both root ports (FS + HS) coexist with skip_phy_setup=true.
static usb_phy_handle_t s_p4OtgHsPhyHandle = nullptr;
#endif

// FreeRTOS task handles.
static TaskHandle_t s_daemonTask  = nullptr;
static TaskHandle_t s_clientTask  = nullptr;

static usb_host_client_handle_t s_clientHandle = nullptr;

#define CDC_IN_XFER_SIZE   1024
#define CDC_OUT_XFER_SIZE   512
#define CDC_CTRL_XFER_SIZE (sizeof(usb_setup_packet_t) + 16)

#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 0)
#ifndef USB_HOST_NATIVE_PERIPHERAL_MAP
// Default to OTG20 HS only (BIT0 = 1U) — matches what ESP-IDF uses when peripheral_map is not
// set explicitly (zero-init). Reference projects that work on P4 (e.g. usbip-esp32p4) all
// leave peripheral_map = 0 / 1U. Setting it to 3 (HS+FS simultaneously) causes the HS
// controller's PCSTS to stay 0 even when a device is present.
// Override with -DUSB_HOST_NATIVE_PERIPHERAL_MAP=3 in build flags to enable both controllers.
#define USB_HOST_NATIVE_PERIPHERAL_MAP 1U
#endif
#else
#ifndef USB_HOST_NATIVE_PERIPHERAL_MAP
#define USB_HOST_NATIVE_PERIPHERAL_MAP 0U
#endif
#endif

// Test knob for P4 dual-controller experiments (EN-20260905-FSDUAL). Defined by the pack
// (pack_p4_cnc.h / pack_p4_usb_server.h); fallback = native map keeps other builds unchanged.
#ifndef USB_HOST_P4_DUAL_TEST_MAP
#define USB_HOST_P4_DUAL_TEST_MAP USB_HOST_NATIVE_PERIPHERAL_MAP
#endif

#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1)
#ifndef USB_HOST_P4_PHY_ROUTE_DEFAULT
// 0 => USB_WRAP mapped to PHY0, 1 => USB_WRAP mapped to PHY1
// PHY 1 is required for the Type-A ports (behind CH334 hub) because the
// hub's upstream port uses FSLS initial handshake on the PHY1 pads. PHY 0
// is the USB-C route used for device mode. Default to 1 so the hub is
// detected immediately on boot without waiting for the 20s fallback.
#define USB_HOST_P4_PHY_ROUTE_DEFAULT 1U
#endif
#ifndef USB_HOST_P4_PHY_FALLBACK_SCAN_THRESHOLD
#define USB_HOST_P4_PHY_FALLBACK_SCAN_THRESHOLD 5U   // ~2.5s first PHY route switch
#endif
#ifndef USB_HOST_P4_PIN_EXCHG_FALLBACK_SCAN_THRESHOLD
#define USB_HOST_P4_PIN_EXCHG_FALLBACK_SCAN_THRESHOLD 15U  // ~7.5s pin exchange
#endif
#ifndef USB_HOST_P4_PHY_ROUTE_WITH_PIN_EXCHG_SCAN_THRESHOLD
#define USB_HOST_P4_PHY_ROUTE_WITH_PIN_EXCHG_SCAN_THRESHOLD 25U  // ~12.5s final combo

#ifndef USB_HOST_P4_AUTO_RESTART_SCAN_THRESHOLD
#define USB_HOST_P4_AUTO_RESTART_SCAN_THRESHOLD 40U  // ~20s auto-restart if all fallbacks exhausted
#endif
#endif
#endif

struct NativeCdcDev {
    usb_device_handle_t dev_hdl = nullptr;
    uint8_t iface_num = 0xFF;
    uint8_t iface_alt = 0;
    uint8_t in_ep_addr = 0;
    uint8_t out_ep_addr = 0;
    usb_transfer_t* in_xfer = nullptr;
    usb_transfer_t* out_xfer = nullptr;
    usb_transfer_t* ctrl_xfer = nullptr;
    SemaphoreHandle_t out_sem = nullptr;
    SemaphoreHandle_t ctrl_sem = nullptr;
    SemaphoreHandle_t out_mux = nullptr;
    int slot = -1;
    bool active = false;
    bool broken = false; // true when CH340 EP 0 is permanently stalled
};

static NativeCdcDev s_cdcDevs[USB_HOST_MAX_CDC_DEVICES];

// Per-slot timestamp of last CH340 vendor reinit (for stale-RX recovery).
static uint32_t s_lastCh340ReinitMs[USB_HOST_MAX_CDC_DEVICES] = {0};
// Consecutive reinit failure counter; after CH340_REINIT_MAX_FAILS, trigger port power-cycle.
static uint8_t s_ch340ReinitFails[USB_HOST_MAX_CDC_DEVICES] = {0};
#define CH340_REINIT_MAX_FAILS 3
// Session guard: the CH340 recovery may power-cycle the root port at most once
// per boot. A genuinely dead chip then stays marked broken instead of looping.
static bool s_ch340RecoveryRestartDone = false;

// VID/PID that identify CH340/CH341-family chips requiring vendor-specific
// baud-rate requests instead of standard CDC SET_LINE_CODING (0x21/0x20).
static bool isCh340FamilyVidPid(uint16_t vid, uint16_t pid) {
    if (vid != 0x1A86) return false;
    switch (pid) {
        case 0x7523: // CH340G/C/B
        case 0x55D4: // CH340 variant
        case 0x5512: // CH341A
        case 0x55D2: // CH340E
        case 0xE025: // CH342
            return true;
        case 0x55D3: // CH343 — uses standard CDC-ACM, NOT vendor-specific
        case 0x55D5: // CH343 dual-port
        default:
            return false;
    }
}

// VID/PID that identify FTDI SIO-family chips (FT232R, FT231X, ...). These
// enumerate with interface class 0xFF and implement NOTHING from the standard
// CDC-ACM spec — they are driven purely through vendor requests (SIO):
//   SIO_SET_BAUDRATE  (0x03)
//   SIO_SET_MODEM_CTRL (0x01)
// Sending them a standard SET_LINE_CODING (0x21/0x20) makes them STALL EP 0,
// which the P4 DWC2 HCD never reclaims — every later submit then fails with
// ESP_ERR_NOT_FINISHED and the UART stays at the default 9600.
static bool isFtdiFamilyVidPid(uint16_t vid, uint16_t pid) {
    if (vid != 0x0403) return false;
    switch (pid) {
        case 0x6001: // FT232R
        case 0x6015: // FT231X
        case 0x6010: // FT2232H
        case 0x6011: // FT4232H
            return true;
        default:
            return false;
    }
}

#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
static uint8_t s_wrapPhyRouteIdx = (uint8_t)USB_HOST_P4_PHY_ROUTE_DEFAULT;
static bool s_wrapPhyFallbackAttempted = false;
static bool s_wrapPinExchgEnabled = false;
static bool s_wrapPinExchgFallbackAttempted = false;
static bool s_wrapPhyWithPinExchgFallbackAttempted = false;
static bool s_wrapAutoRestartAttempted = false;
#endif

const char* usbChipName(uint16_t vid, uint16_t pid) {
    switch (vid) {
        case 0x10C4:
            switch (pid) {
                case 0xEA60: return "CP2102/04";
                case 0xEA70: return "CP2105";
                case 0xEA71: return "CP2108";
                default: return "CP210x";
            }
        case 0x1A86:
            switch (pid) {
                case 0x7523: return "CH340";
                case 0x55D4: return "CH340";
                case 0x5512: return "CH341";
                case 0x55D2: return "CH340E";
                case 0x55D3: return "CH343";
                case 0x55D5: return "CH343";
                case 0xE025: return "CH342";
                default: return "CH34x";
            }
        case 0x0403:
            switch (pid) {
                case 0x6001: return "FT232R";
                case 0x6015: return "FT231X";
                case 0x6010: return "FT2232H";
                case 0x6011: return "FT4232H";
                default: return "FTDI";
            }
        case 0x067B:
            switch (pid) {
                case 0x2303: return "PL2303";
                case 0x23A3: return "PL2303TA";
                default: return "PL230x";
            }
        case 0x2C7C:
            switch (pid) {
                case 0x6001: return "RP2040";
                default: return "RP204x";
            }
        case 0x1FC9:
            switch (pid) {
                case 0x0094: return "Teensy 4.0";
                default: return "NXP";
            }
        case 0x1B3D: return "MKS";
        case 0x2341: return "Arduino";
        case 0x239A: return "Adafruit";
        default: return "USB-UART";
    }
}

// ---------------------------------------------------------------------------
// Helper: find free slot / slot by handle
// ---------------------------------------------------------------------------

static int findFreeSlot() {
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (!s_devices[i].connected) return i;
    }
    return -1;
}

static int findSlotByDeviceHandle(usb_device_handle_t hdl) {
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (s_cdcDevs[i].active && s_cdcDevs[i].dev_hdl == hdl) return i;
    }
    return -1;
}

static int findSlotByDeviceAddress(uint8_t devAddr) {
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (s_devices[i].connected && s_devices[i].dev_addr == devAddr) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Descriptor parsing / transfer helpers
// ---------------------------------------------------------------------------

static void resetNativeDevRuntime(int slot) {
    if (slot < 0 || slot >= USB_HOST_MAX_CDC_DEVICES) return;
    NativeCdcDev* dev = &s_cdcDevs[slot];
    dev->dev_hdl = nullptr;
    dev->iface_num = 0xFF;
    dev->iface_alt = 0;
    dev->in_ep_addr = 0;
    dev->out_ep_addr = 0;
    dev->in_xfer = nullptr;
    dev->out_xfer = nullptr;
    dev->ctrl_xfer = nullptr;
    dev->slot = slot;
    dev->active = false;
    dev->broken = false;
}

static bool findBulkInterface(usb_device_handle_t dev_hdl, uint8_t* iface_num_out,
                              uint8_t* iface_alt_out, uint8_t* in_ep_out,
                              uint8_t* out_ep_out, uint8_t* iface_class_out,
                              uint8_t* iface_subclass_out, uint8_t* iface_protocol_out) {
    if (!iface_num_out || !iface_alt_out || !in_ep_out || !out_ep_out ||
        !iface_class_out || !iface_subclass_out || !iface_protocol_out) return false;

    const usb_config_desc_t* cfg = nullptr;
    if (usb_host_get_active_config_descriptor(dev_hdl, &cfg) != ESP_OK || !cfg) {
        return false;
    }

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(cfg);
    const uint8_t* end = ptr + cfg->wTotalLength;
    ptr += cfg->bLength;

    uint8_t cur_iface = 0xFF;
    uint8_t cur_alt = 0;
    uint8_t cur_class = 0;
    uint8_t cur_subclass = 0;
    uint8_t cur_protocol = 0;
    uint8_t found_in = 0;
    uint8_t found_out = 0;

    while (ptr + sizeof(usb_standard_desc_t) <= end) {
        const usb_standard_desc_t* d = reinterpret_cast<const usb_standard_desc_t*>(ptr);
        if (d->bLength < sizeof(usb_standard_desc_t) || ptr + d->bLength > end) {
            break;
        }

        if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t* intf = reinterpret_cast<const usb_intf_desc_t*>(d);
            cur_iface = intf->bInterfaceNumber;
            cur_alt = intf->bAlternateSetting;
            cur_class = intf->bInterfaceClass;
            cur_subclass = intf->bInterfaceSubClass;
            cur_protocol = intf->bInterfaceProtocol;
            found_in = 0;
            found_out = 0;
        } else if (d->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && cur_iface != 0xFF) {
            const usb_ep_desc_t* ep = reinterpret_cast<const usb_ep_desc_t*>(d);
            const uint8_t epType = (uint8_t)(ep->bmAttributes & 0x03);
            if (epType == USB_TRANSFER_TYPE_BULK) {
                if ((ep->bEndpointAddress & 0x80) != 0) found_in = ep->bEndpointAddress;
                else found_out = ep->bEndpointAddress;

                if (found_in != 0 && found_out != 0) {
                    *iface_num_out = cur_iface;
                    *iface_alt_out = cur_alt;
                    *in_ep_out = found_in;
                    *out_ep_out = found_out;
                    *iface_class_out = cur_class;
                    *iface_subclass_out = cur_subclass;
                    *iface_protocol_out = cur_protocol;
                    return true;
                }
            }
        }

        ptr += d->bLength;
    }

    return false;
}

static esp_err_t submitInTransfer(NativeCdcDev* dev) {
    if (!dev || !dev->active || !dev->in_xfer) return ESP_ERR_INVALID_STATE;
    dev->in_xfer->num_bytes = (int)dev->in_xfer->data_buffer_size;
    return usb_host_transfer_submit(dev->in_xfer);
}

static void nativeInXferCb(usb_transfer_t* transfer) {
    NativeCdcDev* dev = reinterpret_cast<NativeCdcDev*>(transfer ? transfer->context : nullptr);
    if (!dev || dev->slot < 0 || dev->slot >= USB_HOST_MAX_CDC_DEVICES) return;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
        const size_t got = (size_t)transfer->actual_num_bytes;
        if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_devices[dev->slot].rx_bytes += (uint32_t)got;
            s_stats.total_rx_bytes += (uint32_t)got;
            xSemaphoreGive(s_mutex);
        }

        if (s_rxHook) {
            s_rxHook((uint8_t)dev->slot, transfer->data_buffer, got, s_rxHookCtx);
        }

#if ENABLE_GRBL == 1
        // Only feed the GRBL parser when the runtime transport is USB Host.
        // In UART mode (S3 default) a plugged OTG adapter would otherwise
        // deliver a duplicate copy of every GRBL response (the controller's
        // USB-UART chip mirrors its TX), corrupting the line parser.
        if (grblGetTransportMode() == GrblTransportMode::USB) {
            grblOnRxData(transfer->data_buffer, got, (uint8_t)dev->slot);
        }
#else
        injectDataToRXBuffer(String(reinterpret_cast<const char*>(transfer->data_buffer), got),
                             String(F("USB-CDC")));
#endif
    }

    if (!s_running || !dev->active || transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        transfer->status == USB_TRANSFER_STATUS_CANCELED) {
        return;
    }

    esp_err_t err = submitInTransfer(dev);
    if (err != ESP_OK) {
        directLogPrintf("[USB-HOST] IN resubmit failed slot %d: %s\n",
                        dev->slot, esp_err_to_name(err));
    }
}

static void nativeOutXferCb(usb_transfer_t* transfer) {
    NativeCdcDev* dev = reinterpret_cast<NativeCdcDev*>(transfer ? transfer->context : nullptr);
    if (!dev || !dev->out_sem) return;
    xSemaphoreGive(dev->out_sem);
}

static void nativeCtrlXferCb(usb_transfer_t* transfer) {
    NativeCdcDev* dev = reinterpret_cast<NativeCdcDev*>(transfer ? transfer->context : nullptr);
    if (!dev || !dev->ctrl_sem) return;
    xSemaphoreGive(dev->ctrl_sem);
}

// Submit a control transfer, retrying a few times when the HCD reports
// ESP_ERR_NOT_FINISHED. That error means the previous control transfer is
// still being reclaimed (e.g. after an EP0 STALL or a device that stopped
// answering a request); ESP-IDF serializes control transfers on one pipe, so
// simply waiting a short time and re-submitting succeeds once the HCD
// completes its internal bookkeeping. Unrelated to dup-check retries.
static esp_err_t submitControlWithRetry(NativeCdcDev* dev) {
    esp_err_t err;
    for (int attempt = 0; attempt < 5; attempt++) {
        err = usb_host_transfer_submit_control(s_clientHandle, dev->ctrl_xfer);
        if (err != ESP_ERR_NOT_FINISHED) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static bool nativeSetLineCoding(NativeCdcDev* dev, uint32_t baud,
                                uint8_t data_bits, uint8_t stop_bits,
                                uint8_t parity) {
    if (!dev || !dev->active || !dev->ctrl_xfer || !s_clientHandle) return false;

    usb_setup_packet_t* req = reinterpret_cast<usb_setup_packet_t*>(dev->ctrl_xfer->data_buffer);
    uint8_t* payload = dev->ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);

    req->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                         USB_BM_REQUEST_TYPE_TYPE_CLASS |
                         USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    req->bRequest = 0x20; // SET_LINE_CODING
    req->wValue = 0;
    req->wIndex = dev->iface_num;
    req->wLength = 7;

    payload[0] = (uint8_t)(baud & 0xFF);
    payload[1] = (uint8_t)((baud >> 8) & 0xFF);
    payload[2] = (uint8_t)((baud >> 16) & 0xFF);
    payload[3] = (uint8_t)((baud >> 24) & 0xFF);
    payload[4] = stop_bits;
    payload[5] = parity;
    payload[6] = data_bits;

    dev->ctrl_xfer->device_handle = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback = nativeCtrlXferCb;
    dev->ctrl_xfer->context = dev;
    dev->ctrl_xfer->timeout_ms = 500;
    dev->ctrl_xfer->num_bytes = (int)(sizeof(usb_setup_packet_t) + 7);

    if (dev->ctrl_sem) {
        while (xSemaphoreTake(dev->ctrl_sem, 0) == pdTRUE) {
        }
    }

    esp_err_t err = submitControlWithRetry(dev);
    if (err != ESP_OK) {
        directLogPrintf("[USB-CDC] line coding submit failed slot %d: %s\n",
                        dev->slot, esp_err_to_name(err));
        s_lastError = "line_coding_submit_failed";
        return false;
    }

    if (!dev->ctrl_sem || xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(700)) != pdTRUE) {
        directLogPrintf("[USB-CDC] line coding timeout slot %d\n", dev->slot);
        s_lastError = "line_coding_timeout";
        return false;
    }

    if (dev->ctrl_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        s_lastError = "line_coding_transfer_failed";
        return false;
    }

    return true;
}

// Send a CH340/CH341 vendor write-register request (0x9A).
// bmRequestType=0x40, bRequest=0x9A, wValue=reg, wIndex=value, wLength=0.
// Matching Linux kernel ch341.c convention: register address in wValue.
static bool ch340NativeWriteReg(NativeCdcDev* dev, uint16_t reg, uint16_t value) {
    if (!dev || !dev->active || !dev->ctrl_xfer || !s_clientHandle) return false;

    usb_setup_packet_t* req = reinterpret_cast<usb_setup_packet_t*>(dev->ctrl_xfer->data_buffer);
    req->bmRequestType = 0x40;
    req->bRequest = 0x9A;
    req->wValue = reg;    // register address (e.g., 0x1312, 0x2518)
    req->wIndex = value;  // register value (divisor, LCR, etc.)
    req->wLength = 0;

    dev->ctrl_xfer->device_handle    = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback         = nativeCtrlXferCb;
    dev->ctrl_xfer->context          = dev;
    dev->ctrl_xfer->timeout_ms       = 500;
    dev->ctrl_xfer->num_bytes        = sizeof(usb_setup_packet_t);

    if (dev->ctrl_sem) {
        while (xSemaphoreTake(dev->ctrl_sem, 0) == pdTRUE) {}
    }

    esp_err_t err = submitControlWithRetry(dev);
    if (err != ESP_OK) {
        directLogPrintf("[CH340] write_reg submit failed slot %d reg=0x%04X: %s\n",
                        dev->slot, reg, esp_err_to_name(err));
        return false;
    }

    if (!dev->ctrl_sem || xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(700)) != pdTRUE) {
        directLogPrintf("[CH340] write_reg timeout slot %d reg=0x%04X\n", dev->slot, reg);
        return false;
    }

    if (dev->ctrl_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        return false;
    }

    return true;
}

// Send a CH340 vendor control request (0xA1) — serial init.
// bmRequestType=0x40, bRequest=0xA1, wValue=0, wIndex=0, wLength=0.
static bool ch340NativeSerialInit(NativeCdcDev* dev) {
    if (!dev || !dev->active || !dev->ctrl_xfer || !s_clientHandle) return false;

    usb_setup_packet_t* req = reinterpret_cast<usb_setup_packet_t*>(dev->ctrl_xfer->data_buffer);
    memset(req, 0, sizeof(usb_setup_packet_t));
    req->bmRequestType = 0x40;
    req->bRequest = 0xA1;
    req->wLength = 0;

    dev->ctrl_xfer->device_handle    = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback         = nativeCtrlXferCb;
    dev->ctrl_xfer->context          = dev;
    dev->ctrl_xfer->timeout_ms       = 500;
    dev->ctrl_xfer->num_bytes        = sizeof(usb_setup_packet_t);

    if (dev->ctrl_sem) {
        while (xSemaphoreTake(dev->ctrl_sem, 0) == pdTRUE) {}
    }

    esp_err_t err = submitControlWithRetry(dev);
    if (err != ESP_OK) {
        directLogPrintf("[CH340] serial_init submit failed slot %d: %s\n",
                        dev->slot, esp_err_to_name(err));
        return false;
    }

    // Wait strictly longer than transfer->timeout_ms (500) so a device that
    // never answers 0xA1 is always reclaimed by the HCD (TIMED_OUT callback
    // re-releases the control pipe). Returning while the transfer is still
    // in flight would make every later vendor submit fail with
    // ESP_ERR_NOT_FINISHED, permanently wedging EP 0 (seen on CH340 clones).
    if (!dev->ctrl_sem || xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(900)) != pdTRUE) {
        directLogPrintf("[CH340] serial_init timeout slot %d\n", dev->slot);
        return false;
    }

    return dev->ctrl_xfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

// Send a CH340 MODEM_CTRL request (0xA4) — assert/de-assert DTR/RTS.
// bmRequestType=0x40, bRequest=0xA4, wValue=~control, wIndex=0.
// control bits: BIT6=RTS, BIT5=DTR.
static bool ch340NativeSetHandshake(NativeCdcDev* dev, uint8_t control) {
    if (!dev || !dev->active || !dev->ctrl_xfer || !s_clientHandle) return false;

    usb_setup_packet_t* req = reinterpret_cast<usb_setup_packet_t*>(dev->ctrl_xfer->data_buffer);
    memset(req, 0, sizeof(usb_setup_packet_t));
    req->bmRequestType = 0x40;
    req->bRequest = 0xA4;
    req->wValue = (uint16_t)(~control) & 0xFFFF;
    req->wLength = 0;

    dev->ctrl_xfer->device_handle    = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback         = nativeCtrlXferCb;
    dev->ctrl_xfer->context          = dev;
    dev->ctrl_xfer->timeout_ms       = 500;
    dev->ctrl_xfer->num_bytes        = sizeof(usb_setup_packet_t);

    if (dev->ctrl_sem) {
        while (xSemaphoreTake(dev->ctrl_sem, 0) == pdTRUE) {}
    }

    esp_err_t err = submitControlWithRetry(dev);
    if (err != ESP_OK) {
        directLogPrintf("[CH340] handshake submit failed slot %d: %s\n",
                        dev->slot, esp_err_to_name(err));
        return false;
    }

    // Wait longer than transfer->timeout_ms (500) — see ch340NativeSerialInit.
    if (!dev->ctrl_sem || xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(900)) != pdTRUE) {
        directLogPrintf("[CH340] handshake timeout slot %d\n", dev->slot);
        return false;
    }

    return dev->ctrl_xfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

// CH340/CH341 vendor-specific baud-rate and line-control setup.
// Registers: 0x1312 = baud divisor + prescaler, 0x2518 = LCR (line control).
static bool ch340NativeSetLineCoding(NativeCdcDev* dev, uint32_t baud) {
    if (!dev || !dev->active || !dev->ctrl_xfer || !s_clientHandle) return false;

    // Compute divisor with proper prescaler/fact/div encoding.
    uint16_t divisor = ch340CalcDivisor(baud);

    // Write baud divisor + prescaler register pair (0x1312).
    bool ok = ch340NativeWriteReg(dev, 0x1312, divisor);
    if (!ok) {
        directLogPrintf("[CH340] baud write failed slot %d baud=%lu\n", dev->slot, (unsigned long)baud);
        return false;
    }

    // Write LCR (0x2518): RX enable (0x80) | TX enable (0x40) | 8N1 (0x03) = 0xC3.
    ok = ch340NativeWriteReg(dev, 0x2518, 0xC3);
    if (!ok) {
        directLogPrintf("[CH340] LCR write failed slot %d\n", dev->slot);
    }

    return true;
}

static void closeSlotDevice(int slot, bool countDisconnectEvent) {
    if (slot < 0 || slot >= USB_HOST_MAX_CDC_DEVICES) return;
    NativeCdcDev* dev = &s_cdcDevs[slot];
    const bool wasConnected = s_devices[slot].connected;

    dev->active = false;

    if (dev->dev_hdl != nullptr) {
        if (dev->in_ep_addr != 0) {
            (void)usb_host_endpoint_halt(dev->dev_hdl, dev->in_ep_addr);
            (void)usb_host_endpoint_flush(dev->dev_hdl, dev->in_ep_addr);
            (void)usb_host_endpoint_clear(dev->dev_hdl, dev->in_ep_addr);
        }
        if (dev->out_ep_addr != 0) {
            (void)usb_host_endpoint_halt(dev->dev_hdl, dev->out_ep_addr);
            (void)usb_host_endpoint_flush(dev->dev_hdl, dev->out_ep_addr);
            (void)usb_host_endpoint_clear(dev->dev_hdl, dev->out_ep_addr);
        }
    }

    if (dev->in_xfer) {
        (void)usb_host_transfer_free(dev->in_xfer);
        dev->in_xfer = nullptr;
    }
    if (dev->out_xfer) {
        (void)usb_host_transfer_free(dev->out_xfer);
        dev->out_xfer = nullptr;
    }
    if (dev->ctrl_xfer) {
        (void)usb_host_transfer_free(dev->ctrl_xfer);
        dev->ctrl_xfer = nullptr;
    }

    if (dev->dev_hdl != nullptr && s_clientHandle && dev->iface_num != 0xFF) {
        (void)usb_host_interface_release(s_clientHandle, dev->dev_hdl, dev->iface_num);
    }
    if (dev->dev_hdl != nullptr && s_clientHandle) {
        (void)usb_host_device_close(s_clientHandle, dev->dev_hdl);
    }

    resetNativeDevRuntime(slot);

    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_devices[slot].connected = false;
        s_devices[slot].dev_addr = 0;
        s_devices[slot].rx_bytes = 0;
        s_devices[slot].tx_bytes = 0;
        s_devices[slot].speed = 0;
        s_devices[slot].vid = 0;
        s_devices[slot].pid = 0;
        s_devices[slot].bcd_device = 0;
        s_devices[slot].iface_class = 0;
        s_devices[slot].iface_subclass = 0;
        s_devices[slot].iface_protocol = 0;
        s_devices[slot].in_ep_addr = 0;
        s_devices[slot].out_ep_addr = 0;
        s_devices[slot].description[0] = '\0';
        if (wasConnected && s_stats.devices_connected > 0) s_stats.devices_connected--;
        if (wasConnected && countDisconnectEvent) s_stats.disconnect_events++;
        xSemaphoreGive(s_mutex);
    }

    if (wasConnected) {
        directLogPrintf("[USB-CDC] device disconnected from slot %d\n", slot);
        sendVirtualMessage(("🔌 USB-RS232 disconnected (slot " +
                       String(slot) + ")").c_str(), WARN, false, false);
    }
}

// ---------------------------------------------------------------------------
// Device connect / disconnect (called from the client task context)
// ---------------------------------------------------------------------------

static void onDeviceConnect(uint8_t devAddr) {
    const int slot = findFreeSlot();
    if (slot < 0) {
        directLogPrintf("[USB-CDC] no free slot for new device (addr=%u)\n", devAddr);
        s_lastError = "no_free_slot";
        return;
    }

    usb_device_handle_t dev_hdl = nullptr;
    esp_err_t err = usb_host_device_open(s_clientHandle, devAddr, &dev_hdl);
    if (err != ESP_OK || !dev_hdl) {
        directLogPrintf("[USB-CDC] device_open failed (addr=%u): %s\n",
                        devAddr, esp_err_to_name(err));
        s_lastError = String("device_open_failed:") + esp_err_to_name(err);
        return;
    }

    uint16_t dev_vid = 0;
    uint16_t dev_pid = 0;
    uint16_t dev_bcd = 0;
    const usb_device_desc_t* dev_desc = nullptr;
    if (usb_host_get_device_descriptor(dev_hdl, &dev_desc) == ESP_OK && dev_desc) {
        dev_vid = dev_desc->idVendor;
        dev_pid = dev_desc->idProduct;
        dev_bcd = dev_desc->bcdDevice;
    }

    // FTDI SIO-family (FT232R/FT231X/...): NOT supported on the ESP32-P4
    // native DWC2 host. Genuine chips answer SIO vendor requests, but the
    // cheap FT232RL clones on many CNC boards enumerate with a fake 0403:6001,
    // STALL CLASS requests and NEVER answer VENDOR requests. On the P4 an
    // unanswered/stalled control transfer is never reclaimed by the HCD,
    // permanently wedging EP0 and later panicking in hcd_urb_dequeue().
    // Refuse the device up front (clean close, no vendor request submitted)
    // instead of trying to init it.
    if (isFtdiFamilyVidPid(dev_vid, dev_pid)) {
        directLogPrintf("[USB-CDC] FTDI-family %04X:%04X addr=%u — not supported on P4 DWC2 host, disabling\n",
                        (unsigned)dev_vid, (unsigned)dev_pid, devAddr);
        sendVirtualMessage(String("[USB] FTDI USB-UART (") + String(usbChipName(dev_vid, dev_pid)) +
                              String(") not supported on this board — use a CH340 adapter instead"), WARN, false, false);
        addNonCdcAddr(devAddr);
        s_lastError = "ftdi_not_supported";
        (void)usb_host_device_close(s_clientHandle, dev_hdl);
        return;
    }

    uint8_t iface_num = 0xFF;
    uint8_t iface_alt = 0;
    uint8_t in_ep = 0;
    uint8_t out_ep = 0;
    uint8_t iface_class = 0;
    uint8_t iface_subclass = 0;
    uint8_t iface_protocol = 0;
    if (!findBulkInterface(dev_hdl, &iface_num, &iface_alt, &in_ep, &out_ep,
                           &iface_class, &iface_subclass, &iface_protocol)) {
        directLogPrintf("[USB-CDC] no bulk IN/OUT interface (addr=%u) — likely a HUB\n", devAddr);
        if (dev_vid || dev_pid) {
            sendVirtualMessage(String("[USB] addr=") + String(devAddr) + " not CDC — no bulk interface (hub/UVC/etc.), VID=" +
                               String(dev_vid, HEX) + ":" + String(dev_pid, HEX) + " — left free (UVC can claim)", INFO, false, false);
        } else {
            sendVirtualMessage(String("[USB] addr=") + String(devAddr) + " not CDC (hub/skip)", INFO, false, false);
        }
        addNonCdcAddr(devAddr);
        s_lastError = "no_bulk_interface";
        (void)usb_host_device_close(s_clientHandle, dev_hdl);
        return;
    }

    // Class gate: a bulk IN+OUT pair alone does NOT mean CDC. Mass Storage
    // (0x08, e.g. USB pen drives), printers (0x07) and others also use bulk
    // endpoints — claiming them as RS232 creates phantom serial slots (the
    // Kingston pen showed up as "USB-RS232 slot 0"). Only CDC-DATA (0x0A)
    // carries serial traffic; CDC-ACM-Control (0x02) is accepted too and the
    // DATA interface is resolved below. Anything else is left free (UVC/MSC).
    // Carve-out (2026-10-06, v1.115 regression): the CH340 family presents
    // vendor class 0xFF but speaks CDC-like bulk after vendor init (see the
    // CH340 init path below) — it is the most common GRBL adapter chip and
    // must not be refused. FTDI stays refused (see above).
    if (iface_class != 0x0Au && iface_class != 0x02u &&
        !isCh340FamilyVidPid(dev_vid, dev_pid)) {
        directLogPrintf("[USB-CDC] bulk interface is class 0x%02X (addr=%u) — not CDC, left free\n",
                        iface_class, devAddr);
        sendVirtualMessage(String("[USB] addr=") + String(devAddr) + " class 0x" +
                           String(iface_class, HEX) + " (not CDC), VID=" +
                           String(dev_vid, HEX) + ":" + String(dev_pid, HEX) +
                           " — left free (UVC/MSC can claim)", INFO, false, false);
        addNonCdcAddr(devAddr);
        s_lastError = "non_cdc_bulk_class";
        (void)usb_host_device_close(s_clientHandle, dev_hdl);
        return;
    }

    err = usb_host_interface_claim(s_clientHandle, dev_hdl, iface_num, iface_alt);
    if (err != ESP_OK) {
        directLogPrintf("[USB-CDC] interface_claim failed (addr=%u iface=%u alt=%u): %s\n",
                        devAddr, iface_num, iface_alt, esp_err_to_name(err));
        s_lastError = String("interface_claim_failed:") + esp_err_to_name(err);
        (void)usb_host_device_close(s_clientHandle, dev_hdl);
        return;
    }

    NativeCdcDev* dev = &s_cdcDevs[slot];
    resetNativeDevRuntime(slot);
    dev->dev_hdl = dev_hdl;
    dev->iface_num = iface_num;
    dev->iface_alt = iface_alt;
    dev->in_ep_addr = in_ep;
    dev->out_ep_addr = out_ep;
    dev->slot = slot;
    dev->active = true;

    err = usb_host_transfer_alloc(CDC_IN_XFER_SIZE, 0, &dev->in_xfer);
    if (err == ESP_OK) err = usb_host_transfer_alloc(CDC_OUT_XFER_SIZE, 0, &dev->out_xfer);
    if (err == ESP_OK) err = usb_host_transfer_alloc(CDC_CTRL_XFER_SIZE, 0, &dev->ctrl_xfer);
    if (err != ESP_OK || !dev->in_xfer || !dev->out_xfer || !dev->ctrl_xfer) {
        directLogPrintf("[USB-CDC] transfer_alloc failed slot %d: %s\n",
                        slot, esp_err_to_name(err));
        s_lastError = String("transfer_alloc_failed:") + esp_err_to_name(err);
        closeSlotDevice(slot, false);
        return;
    }

    dev->in_xfer->device_handle = dev_hdl;
    dev->in_xfer->bEndpointAddress = in_ep;
    dev->in_xfer->callback = nativeInXferCb;
    dev->in_xfer->context = dev;
    dev->in_xfer->timeout_ms = 0;

    dev->out_xfer->device_handle = dev_hdl;
    dev->out_xfer->bEndpointAddress = out_ep;
    dev->out_xfer->callback = nativeOutXferCb;
    dev->out_xfer->context = dev;
    dev->out_xfer->timeout_ms = 700;

    dev->ctrl_xfer->device_handle = dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback = nativeCtrlXferCb;
    dev->ctrl_xfer->context = dev;
    dev->ctrl_xfer->timeout_ms = 700;

    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_devices[slot].connected = true;
        s_devices[slot].dev_addr = devAddr;
        s_devices[slot].rx_bytes = 0;
        s_devices[slot].tx_bytes = 0;
        s_devices[slot].baud = USB_HOST_DEFAULT_BAUD;
        s_devices[slot].speed = 2;
        s_devices[slot].vid = dev_vid;
        s_devices[slot].pid = dev_pid;
        s_devices[slot].bcd_device = dev_bcd;
        s_devices[slot].iface_class = iface_class;
        s_devices[slot].iface_subclass = iface_subclass;
        s_devices[slot].iface_protocol = iface_protocol;
        s_devices[slot].in_ep_addr = in_ep;
        s_devices[slot].out_ep_addr = out_ep;
        snprintf(s_devices[slot].description, sizeof(s_devices[slot].description),
             "slot %d %s (addr %u, %04X:%04X)",
             slot,
             usbChipName(dev_vid, dev_pid),
             (unsigned)devAddr,
             (unsigned)dev_vid,
             (unsigned)dev_pid);
        s_stats.devices_connected++;
        s_stats.connect_events++;
        xSemaphoreGive(s_mutex);
    }

    err = submitInTransfer(dev);
    if (err != ESP_OK) {
        directLogPrintf("[USB-CDC] initial IN submit failed slot %d: %s\n",
                        slot, esp_err_to_name(err));
        s_lastError = String("initial_in_submit_failed:") + esp_err_to_name(err);
        closeSlotDevice(slot, true);
        return;
    }

    if (isCh340FamilyVidPid(dev_vid, dev_pid)) {
        // CH340: skip standard CDC SET_LINE_CODING — some CH340 variants STALL
        // on EP 0 for this request, locking the control endpoint forever.
        // Vendor config matching Linux kernel ch341.c, which NEVER sends the
        // optional 0xA1 serial-init probe: some CH340 clones ignore 0xA1, and
        // on the ESP32-P4 DWC2 the HCD never reclaims an un-answered control
        // transfer, so submitting 0xA1 first wedges the shared control pipe —
        // every later submit (0x9A baud, 0xA4 handshake) then fails with
        // ESP_ERR_NOT_FINISHED and the UART never reaches 115200.
        // 1. Vendor baud divisor + LCR (0x9A writes to 0x1312 + 0x2518)
        bool ok2 = ch340NativeSetLineCoding(dev, USB_HOST_DEFAULT_BAUD);
        // 2. Assert DTR+RTS via MODEM_CTRL (REQ 0xA4, mcr=0x60)
        bool ok3 = ch340NativeSetHandshake(dev, 0x60); // DTR|RTS
        bool ok1 = true;
        if (!ok2 || !ok3) {
            // Both failed and the pipe is free (0xA1 was never submitted), so
            // try the legacy serial-init probe once as a last resort, then
            // retry baud + handshake once more.
            ok1 = ch340NativeSerialInit(dev);
            if (ok1) {
                ok2 = ch340NativeSetLineCoding(dev, USB_HOST_DEFAULT_BAUD);
                ok3 = ch340NativeSetHandshake(dev, 0x60);
            }
        }
        if (!ok1 && !ok2 && !ok3) {
            directLogPrintf("[CH340] EP 0 stalled slot %d — all vendor commands failed\n", slot);
            s_lastError = "ch340_ep0_stalled";
        } else if (!ok1 || !ok2 || !ok3) {
            directLogPrintf("[CH340] init partial failure slot %d: serial=%d baud=%d handshake=%d\n",
                            slot, (int)ok1, (int)ok2, (int)ok3);
        } else {
            directLogPrintf("[CH340] init OK slot %d\n", slot);
        }
    } else {
        // Standard CDC SET_LINE_CODING for non-CH340 devices.
        directLogPrintf("[USB-CDC] chip %s (%04X:%04X) class=%u subclass=%u proto=%u\n",
                        usbChipName(dev_vid, dev_pid), dev_vid, dev_pid,
                        iface_class, iface_subclass, iface_protocol);
        bool lineOk = nativeSetLineCoding(dev,
                                          USB_HOST_DEFAULT_BAUD,
                                          USB_HOST_DEFAULT_DATA_BITS,
                                          USB_HOST_DEFAULT_STOP_BITS,
                                          USB_HOST_DEFAULT_PARITY);
        if (!lineOk) {
            directLogPrintf("[USB-CDC] default line coding failed slot %d\n", slot);
            s_lastError = "default_line_coding_failed";
            // Some CH340/CH34x clones report a non-listed VID/PID (or none at
            // all) and STALL the standard CDC SET_LINE_CODING on EP 0, which
            // wedges the control pipe. Those chips still accept the vendor
            // baud + handshake requests (Linux ch341.c drives them this way).
            // Try the vendor fallback so the UART actually reaches 115200 —
            // genuine non-CH340 devices simply STALL it harmlessly.
            directLogPrintf("[USB-CDC] line coding failed — trying CH340 vendor fallback slot %d\n", slot);
            bool vOk2 = ch340NativeSetLineCoding(dev, USB_HOST_DEFAULT_BAUD);
            bool vOk3 = ch340NativeSetHandshake(dev, 0x60); // DTR|RTS
            if (vOk2 && vOk3) {
                directLogPrintf("[USB-CDC] line coding OK via CH340 vendor fallback slot %d\n", slot);
                s_lastError = "";
            } else {
                directLogPrintf("[USB-CDC] line coding failed (std=%d vendorW=%d vendorH=%d) slot %d\n",
                                (int)lineOk, (int)vOk2, (int)vOk3, slot);
                s_lastError = "line_coding_failed_and_vendor_fallback_failed";
            }
        }
    }

    if (dev->broken) {
        directLogPrintf("[USB-CDC] slot %d open but broken — no reinit will be attempted\n", slot);
    } else {
        directLogPrintf("[USB-CDC] device opened slot %d addr=%u iface=%u in=0x%02X out=0x%02X\n",
                        slot, devAddr, iface_num, in_ep, out_ep);
        s_lastError = "";
        sendVirtualMessage(("✅ USB-RS232 connected (slot " + String(slot) +
                               ", " + String(USB_HOST_DEFAULT_BAUD) + " baud)").c_str(), INFO, false, false);
        grblClearBlockReason();
    }
}

static void onDeviceDisconnect(usb_device_handle_t dev_hdl) {
    const int slot = findSlotByDeviceHandle(dev_hdl);
    if (slot < 0) return;
    closeSlotDevice(slot, true);
}

static void usbClientEventCallback(const usb_host_client_event_msg_t* event_msg, void* /*arg*/) {
    if (!event_msg) return;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        s_diag.new_dev_events++;
        directLogPrintf("[USB-HOST] NEW_DEV addr=%u\n", (unsigned)event_msg->new_dev.address);
        onDeviceConnect(event_msg->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        s_diag.dev_gone_events++;
        directLogPrintf("[USB-HOST] DEV_GONE hdl=%p\n", event_msg->dev_gone.dev_hdl);
        onDeviceDisconnect(event_msg->dev_gone.dev_hdl);
        break;
    default:
        break;
    }
}

// Retry CH340 vendor init sequence for a slot that has received no data.
// Called from the scan loop when a CH340 device is connected but rx_bytes==0.
// Returns true on success, false on failure.
static bool ch340ReinitSlot(int slot) {
    if (slot < 0 || slot >= USB_HOST_MAX_CDC_DEVICES) return false;
    NativeCdcDev* dev = &s_cdcDevs[slot];
    if (!dev->active || !dev->ctrl_xfer) return false;
    directLogPrintf("[CH340] reinit slot %d (rx stuck at 0)\n", slot);
    // Same ordering as onDeviceConnect (Linux ch341.c): baud + handshake first
    // WITHOUT the optional 0xA1 serial-init. On CH340 clones that ignore 0xA1
    // the HCD never reclaims that control transfer, wedging EP 0 and making
    // every later submit fail with ESP_ERR_NOT_FINISHED.
    bool ok = true;
    if (!ch340NativeSetLineCoding(dev, USB_HOST_DEFAULT_BAUD)) {
        directLogPrintf("[CH340] reinit set_line_coding FAILED slot %d\n", slot);
        ok = false;
    }
    if (!ch340NativeSetHandshake(dev, 0x60)) {
        directLogPrintf("[CH340] reinit set_handshake FAILED slot %d\n", slot);
        ok = false;
    }
    if (!ok) {
        // Last resort: legacy serial-init probe, then one retry of baud + handshake.
        if (ch340NativeSerialInit(dev) &&
            ch340NativeSetLineCoding(dev, USB_HOST_DEFAULT_BAUD) &&
            ch340NativeSetHandshake(dev, 0x60)) {
            ok = true;
        }
    }
    if (ok) {
        directLogPrintf("[CH340] reinit OK slot %d\n", slot);
    } else {
        sendVirtualMessage(String("[CH340] reinit FAILED slot ") + String(slot), WARN, false, false);
    }
    return ok;
}

static void scanAndAttachKnownDevices() {
    if (!s_clientHandle || !s_running) return;

    s_diag.scan_cycles++;

    // Refresh HPRT0 register state on every scan for real-time diagnostics.
    // PPWR (bit 12) must be 1; PCSTS (bit 0) shows physical device presence.
    // Also read OTG11 FS HPRT0 — if FS PCSTS=1, the Type-A is on GPIO26/27 (FS FSLS pads).
#if defined(USB_HOST_HPRT0_AVAILABLE)
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1)
    // Sample BOTH controllers on every scan regardless of the active map.
    // PCSTS (bit0) shows physical presence on each pad pair. On CAM-P4 the
    // camera sits behind the CH334 hub on the FSLS bus (USB1), so USB_DWC_FS
    // is the one that matters — it was previously only read in the dual-test
    // maps (DUAL_TEST_MAP & 2), leaving the FS pad invisible in production.
    s_diag.hprt0_raw    = USB_DWC_HS.hprt_reg.val;
    s_diag.hprt0_fs_raw = USB_DWC_FS.hprt_reg.val;
#if defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
    {
        // Root-port state watch: log every PCSTS/PENA/PRST transition at INFO
        // so the web monitor shows whether the hub driver ever attempts to
        // enable/reset the port. PENA (bit2) only pulses while a port reset
        // clock runs — a clean always-0 proves the hub driver NEVER got its
        // connect event, not that the device failed enumeration. Sticky
        // everPENA/everPRST survive the whole session. With OTG11 FS host
        // (map=2) the ACTIVE controller is USB_DWC_FS, so both are watched.
        static uint8_t s_lastHsPcsts = 0xFF, s_lastHsPena = 0xFF, s_lastHsPrst = 0xFF;
        static uint8_t s_lastFsPcsts = 0xFF, s_lastFsPena = 0xFF, s_lastFsPrst = 0xFF;
        static bool s_everPena = false, s_everPrst = false;
        const uint8_t hsPcsts = (uint8_t)(s_diag.hprt0_raw & 1U);
        const uint8_t hsPena  = (uint8_t)((s_diag.hprt0_raw >> 2) & 1U);
        const uint8_t hsPrst  = (uint8_t)((s_diag.hprt0_raw >> 4) & 1U);
        const uint8_t fsPcsts = (uint8_t)(s_diag.hprt0_fs_raw & 1U);
        const uint8_t fsPena  = (uint8_t)((s_diag.hprt0_fs_raw >> 2) & 1U);
        const uint8_t fsPrst  = (uint8_t)((s_diag.hprt0_fs_raw >> 4) & 1U);
        const uint8_t pena = fsPena | hsPena;
        const uint8_t prst = fsPrst | hsPrst;
        const uint8_t hsSpd = (uint8_t)((s_diag.hprt0_raw >> 17) & 3U);  // 0=HS 1=FS 2=LS
        if (pena) s_everPena = true;
        if (prst) s_everPrst = true;
        if (hsPcsts != s_lastHsPcsts || hsPena != s_lastHsPena || hsPrst != s_lastHsPrst ||
            fsPcsts != s_lastFsPcsts || fsPena != s_lastFsPena || fsPrst != s_lastFsPrst) {
            sendVirtualMessage(String("[USB] HPRT: HS(PC=") + String(hsPcsts) + " E=" + String(hsPena) + " R=" + String(hsPrst) + " SPD=" + String(hsSpd) +
                               ") FS(PC=" + String(fsPcsts) + " E=" + String(fsPena) + " R=" + String(fsPrst) + ")" +
                               " otg11=" + String(USB_HOST_P4_OTG11_FS_HOST) +
                               " route=" + String(s_wrapPhyRouteIdx) +
                               " pin=" + String(s_wrapPinExchgEnabled ? 1 : 0) +
                               " everPENA=" + String(s_everPena ? 1 : 0) +
                               " everPRST=" + String(s_everPrst ? 1 : 0), INFO, false, true);
            s_lastHsPcsts = hsPcsts; s_lastHsPena = hsPena; s_lastHsPrst = hsPrst;
            s_lastFsPcsts = fsPcsts; s_lastFsPena = fsPena; s_lastFsPrst = fsPrst;
        }
    }
#endif
#else
    s_diag.hprt0_raw = USB_DWC.hprt_reg.val;
#endif
#endif

    uint8_t addrList[16] = {};
    int numDevices = 0;
    esp_err_t err = usb_host_device_addr_list_fill((int)(sizeof(addrList)), addrList, &numDevices);
    if (err != ESP_OK) {
        s_diag.scan_addr_failures++;
        s_lastError = String("addr_list_failed:") + esp_err_to_name(err);
        directLogPrintf("[USB-HOST] device_addr_list_fill failed: %s\n", esp_err_to_name(err));
        return;
    }

    s_diag.last_addr_count = (numDevices > 255) ? 255 : (uint8_t)((numDevices < 0) ? 0 : numDevices);

    usb_host_lib_info_t info = {};
    if (usb_host_lib_info(&info) == ESP_OK) {
        s_diag.lib_num_devices = info.num_devices;
        s_diag.lib_num_clients = info.num_clients;
    }

    if (numDevices <= 0) {
        s_diag.scan_no_device_cycles++;
        if (s_stats.devices_connected == 0 && s_lastError.length() == 0) {
            s_lastError = "no_devices_on_bus";
        }
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1)
        if ((s_diag.scan_no_device_cycles % 20) == 1) {
            sendVirtualMessage(String("[USB] no-dev #") + String((int)s_diag.scan_no_device_cycles) +
                               " HS(PC=" + String(s_diag.hprt0_raw & 1U) + " E=" + String((s_diag.hprt0_raw >> 2) & 1U) +
                               " SPD=" + String((s_diag.hprt0_raw >> 17) & 3U) +
                               ") FS(PC=" + String(s_diag.hprt0_fs_raw & 1U) + " E=" + String((s_diag.hprt0_fs_raw >> 2) & 1U) +
                               ") otg11=" + String(USB_HOST_P4_OTG11_FS_HOST == 1 ? 1 : 0), INFO, false, true);
        }
#else
        if ((s_diag.scan_no_device_cycles % 20) == 1) {
            sendVirtualMessage(String("[USB] no-dev #") + String((int)s_diag.scan_no_device_cycles) +
                               " PCSTS=" + String(s_diag.hprt0_raw & 1U), INFO, false, true);
        }
#endif
        return;
    }

    for (int i = 0; i < numDevices; i++) {
        const uint8_t addr = addrList[i];
        if (addr == 0) continue;
        if (findSlotByDeviceAddress(addr) >= 0) continue;
        if (isNonCdcAddr(addr)) continue;
        // Throttle re-open attempts after a failed/closed device: the address
        // lingers in the host lib list even when its control pipe is wedged.
        const uint32_t nowMs = millis();
        const uint32_t lastOpen = s_lastOpenAttemptMs[addr & 0x7F];
        if ((uint32_t)(nowMs - lastOpen) < USB_HOST_OPEN_RETRY_MS) continue;
        s_lastOpenAttemptMs[addr & 0x7F] = nowMs;
        sendVirtualMessage(String("[USB] new device addr=") + String(addr), INFO, false, false);
        onDeviceConnect(addr);
    }

    // Stale-RX recovery: if a connected CH340 has received no data for
    // 5+ s, re-run the vendor init sequence. This recovers from chips whose
    // serial init failed temporarily during onDeviceConnect.
    // Slots marked broken (EP 0 permanently stalled) are skipped entirely.
    // After CH340_REINIT_MAX_FAILS consecutive failures, the device is marked
    // broken and no further attempts are made — the system continues running
    // without this device.
    const uint32_t now = millis();
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (!s_devices[i].connected) continue;
        const bool isCh340 = isCh340FamilyVidPid(s_devices[i].vid, s_devices[i].pid);
        if (!isCh340) continue;
        if (s_cdcDevs[i].broken) continue;
        if (s_devices[i].rx_bytes > 0) {
            s_lastCh340ReinitMs[i] = now;
            s_ch340ReinitFails[i] = 0;
            continue;
        }
        if (!s_cdcDevs[i].active || !s_cdcDevs[i].ctrl_xfer) continue;
        if ((now - s_lastCh340ReinitMs[i]) <= 5000) continue;

        s_lastCh340ReinitMs[i] = now;
        bool reinitOk = ch340ReinitSlot(i);
        if (reinitOk) {
            s_ch340ReinitFails[i] = 0;
        } else {
            s_ch340ReinitFails[i]++;
            if (s_ch340ReinitFails[i] >= CH340_REINIT_MAX_FAILS) {
                directLogPrintf("[CH340] %d consecutive reinit failures slot %d — marking broken, closing slot\n",
                                (int)CH340_REINIT_MAX_FAILS, i);
                sendVirtualMessage(String("[CH340] EP 0 permanently stalled slot ") + String(i) +
                                      String(" — device unusable on this board"), WARN, false, false);
                s_cdcDevs[i].broken = true;
                s_ch340ReinitFails[i] = 0;
                closeSlotDevice(i, true);
                // Power-cycle the root port (DWC register on P4) so the wedged
                // CH340 re-enumerates with a fresh EP 0. Some CH340/C clones
                // recover a permanently-stalled control endpoint this way.
                // Guarded: at most one recovery restart per boot.
                if (!s_ch340RecoveryRestartDone) {
                    s_ch340RecoveryRestartDone = true;
                    usbHostRequestRestart();
                }
            }
        }
    }
}

#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
static void applyUsbWrapPhyRoute(uint8_t phyIdx) {
    const uint8_t route = (uint8_t)(phyIdx & 0x01U);
    usb_wrap_ll_phy_select(&USB_WRAP, route);
    s_wrapPhyRouteIdx = route;
    s_diag.phy_route_idx = route;
}

static void applyUsbWrapPinExchange(bool enable) {
    usb_wrap_ll_phy_enable_pin_exchg(&USB_WRAP, enable);
    s_wrapPinExchgEnabled = enable;
    s_diag.pin_exchg_enabled = enable;
}

static void tryUsbWrapPhyFallbackIfNeeded() {
    // USB_WRAP PHY route switching only applies to OTG20 HS (peripheral_map BIT0).
    // OTG11 FS (BIT1) uses the GPIO-based internal FSLS PHY via usb_new_phy() and is
    // not affected by usb_wrap_ll_phy_select. Skip entirely when using OTG11 FS.
    if ((s_diag.peripheral_map & 1U) == 0U) return;
    if (s_diag.new_dev_events != 0 || s_diag.last_addr_count != 0) return;
    // A device is physically attached (HPRT PCSTS=1): never switch the PHY
    // route / power-cycle the port mid-attach — that aborts an in-flight
    // enumeration. The fallback ladder targets "no attach detected" only.
    if (s_diag.hprt0_raw & 1U) return;
    if (!s_wrapPhyFallbackAttempted &&
        s_diag.scan_no_device_cycles >= (uint32_t)USB_HOST_P4_PHY_FALLBACK_SCAN_THRESHOLD) {
        const uint8_t newRoute = (uint8_t)((s_wrapPhyRouteIdx == 0U) ? 1U : 0U);
        applyUsbWrapPhyRoute(newRoute);
        s_wrapPhyFallbackAttempted = true;
        s_diag.phy_fallback_attempted = true;
        s_diag.phy_route_switches++;

        // Re-arm attach detection after switching route.
        esp_err_t err = usb_host_lib_set_root_port_power(false);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] fallback root_port_power(false) failed: %s\n", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        err = usb_host_lib_set_root_port_power(true);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] fallback root_port_power(true) failed: %s\n", esp_err_to_name(err));
        }

        s_lastError = "no_attach_switched_phy_route";
        directLogPrintf("[USB-HOST] no attach detected, switched USB_WRAP PHY route to %u\n", (unsigned)newRoute);
        sendVirtualMessage(String("[USB] fallback: switched PHY route to ") + String(newRoute), INFO, false, true);
        return;
    }

    if (!s_wrapPinExchgFallbackAttempted &&
        s_diag.scan_no_device_cycles >= (uint32_t)USB_HOST_P4_PIN_EXCHG_FALLBACK_SCAN_THRESHOLD) {
        applyUsbWrapPinExchange(true);
        s_wrapPinExchgFallbackAttempted = true;
        s_diag.pin_exchg_fallback_attempted = true;

        esp_err_t err = usb_host_lib_set_root_port_power(false);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] pinexchg root_port_power(false) failed: %s\n", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        err = usb_host_lib_set_root_port_power(true);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] pinexchg root_port_power(true) failed: %s\n", esp_err_to_name(err));
        }

        s_lastError = "no_attach_enabled_pin_exchange";
        directLogPrintf("[USB-HOST] no attach detected, enabled USB_WRAP D+/D- pin exchange override\n");
        sendVirtualMessage("[USB] fallback: enabled D+/D- pin exchange", INFO, false, true);
        return;
    }

    // Final matrix pass: keep pin-exchange enabled and switch PHY route again.
    // This covers the remaining D+/D- + PHY routing combination on ESP32-P4.
    if (!s_wrapPhyWithPinExchgFallbackAttempted && s_wrapPinExchgEnabled &&
        s_diag.scan_no_device_cycles >= (uint32_t)USB_HOST_P4_PHY_ROUTE_WITH_PIN_EXCHG_SCAN_THRESHOLD) {
        const uint8_t newRoute = (uint8_t)((s_wrapPhyRouteIdx == 0U) ? 1U : 0U);
        applyUsbWrapPhyRoute(newRoute);
        s_wrapPhyWithPinExchgFallbackAttempted = true;
        s_diag.phy_route_switches++;

        esp_err_t err = usb_host_lib_set_root_port_power(false);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] final fallback root_port_power(false) failed: %s\n", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        err = usb_host_lib_set_root_port_power(true);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] final fallback root_port_power(true) failed: %s\n", esp_err_to_name(err));
        }

        s_lastError = "no_attach_switched_phy_route_with_pin_exchange";
        directLogPrintf("[USB-HOST] no attach detected, switched USB_WRAP PHY route to %u with pin exchange enabled\n",
                        (unsigned)newRoute);
        sendVirtualMessage(String("[USB] fallback: route ") + String(newRoute) + " + pin exchange ON", INFO, false, true);
        return;
    }

    // 4th fallback: auto-restart the USB host stack to retry with fresh state.
    // This helps when the CH334F hub or downstream device fails to enumerate
    // due to timing, power sequencing, or PHY initialization edge cases.
    if (!s_wrapAutoRestartAttempted &&
        s_diag.scan_no_device_cycles >= (uint32_t)USB_HOST_P4_AUTO_RESTART_SCAN_THRESHOLD) {
        s_wrapAutoRestartAttempted = true;
        s_diag.auto_restart_attempted = true;
        s_lastError = "no_attach_scheduled_restart";
        directLogPrintf("[USB-HOST] no attach detected after %u scans, scheduling USB host restart\n",
                        (unsigned)s_diag.scan_no_device_cycles);
        sendVirtualMessage(String("[USB] fallback: USB host restart after ") + String(s_diag.scan_no_device_cycles) + " empty scans", INFO, false, true);
        usbHostRequestRestart();
    }
}
#endif

// ---------------------------------------------------------------------------
// Host daemon / client tasks
// ---------------------------------------------------------------------------

static void daemonTask(void* /*arg*/) {
    while (s_running) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(200), &flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            directLogPrintf("[USB-HOST] lib_handle_events error: %s\n", esp_err_to_name(err));
        }
    }
    s_daemonTask = nullptr;
    vTaskDelete(nullptr);
}

static void clientTask(void* /*arg*/) {
    TickType_t lastScanTick = 0;
    while (s_running) {
        if (!s_clientHandle) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        esp_err_t err = usb_host_client_handle_events(s_clientHandle, pdMS_TO_TICKS(200));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            directLogPrintf("[USB-HOST] client_handle_events error: %s\n", esp_err_to_name(err));
        }

        const TickType_t now = xTaskGetTickCount();
        if ((now - lastScanTick) >= pdMS_TO_TICKS(500)) {
            scanAndAttachKnownDevices();
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
            tryUsbWrapPhyFallbackIfNeeded();
#endif
            lastScanTick = now;
        }
    }
    s_clientTask = nullptr;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK)
// Enumeration filter: accept every device, but log its descriptor to the Web
// Monitor. In the arduino-esp32 3.3.12 core this feature is enabled, and the
// IDF host calls this callback during enumeration. If it is never called, the
// enumeration failed before the first descriptor read (address 0 control
// transfer) — which distinguishes "device rejects/blocks enum" from "no device".
static bool usbHostEnumFilterLog(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue) {
    if (bConfigurationValue) *bConfigurationValue = 1;  // first configuration
    if (dev_desc) {
        sendVirtualMessage(String("[USB] enum filter: VID=0x") + String(dev_desc->idVendor, HEX) +
                           " PID=0x" + String(dev_desc->idProduct, HEX) +
                           " USB=0x" + String(dev_desc->bcdUSB, HEX) +
                           " class=0x" + String(dev_desc->bDeviceClass, HEX),
                           INFO, false, true);
    }
    return true;  // accept all devices
}
#endif

void usbHostBegin() {
    if (s_running) return;
    usbHostBeginStageMark("enter");
    s_lastError = "";

    memset(s_devices, 0, sizeof(s_devices));
    memset(s_nonCdcAddrs, 0, sizeof(s_nonCdcAddrs));
    s_nonCdcAddrCount = 0;
    memset(s_lastOpenAttemptMs, 0, sizeof(s_lastOpenAttemptMs));
    s_ch340RecoveryRestartDone = false;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.lib_num_devices = -1;
    s_diag.lib_num_clients = -1;
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1)
    sendVirtualMessage(String("[USB] PHY_ROUTE=") + String(USB_HOST_P4_PHY_ROUTE_DEFAULT) + " MAP=" +
#if defined(USB_HOST_P4_OTG11_FS_HOST) && (USB_HOST_P4_OTG11_FS_HOST == 1)
        String(USB_HOST_P4_DUAL_TEST_MAP) +
#else
        String(USB_HOST_NATIVE_PERIPHERAL_MAP) +
#endif
        " OTG11=" +
#if defined(USB_HOST_P4_OTG11_FS_HOST)
        String(USB_HOST_P4_OTG11_FS_HOST) +
#else
        String("undef") +
#endif
        " HUBS=" +
#if defined(CONFIG_USB_HOST_HUBS_SUPPORTED) || defined(CONFIG_USB_HOST_ENABLE_HUBS)
        String("y"),
#else
        String("n"),
#endif
        INFO, false, false);
#endif
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
    s_wrapPhyRouteIdx = (uint8_t)(USB_HOST_P4_PHY_ROUTE_DEFAULT & 0x01U);
    s_wrapPhyFallbackAttempted = false;
    s_wrapPinExchgEnabled = false;
    s_wrapPinExchgFallbackAttempted = false;
    s_wrapPhyWithPinExchgFallbackAttempted = false;
    s_wrapAutoRestartAttempted = false;
    s_diag.phy_route_idx = s_wrapPhyRouteIdx;
    s_diag.phy_fallback_attempted = false;
    s_diag.phy_route_switches = 0;
    s_diag.pin_exchg_enabled = false;
    s_diag.pin_exchg_fallback_attempted = false;
    applyUsbWrapPinExchange(false);
    applyUsbWrapPhyRoute(s_wrapPhyRouteIdx);
#endif
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        resetNativeDevRuntime(i);
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        directLogPrintf("[USB-HOST] failed to create mutex\n");
        s_lastError = "mutex_create_failed";
        return;
    }
    usbHostBeginStageMark("sync");

    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        s_cdcDevs[i].out_sem = xSemaphoreCreateBinary();
        s_cdcDevs[i].ctrl_sem = xSemaphoreCreateBinary();
        s_cdcDevs[i].out_mux = xSemaphoreCreateMutex();
        if (!s_cdcDevs[i].out_sem || !s_cdcDevs[i].ctrl_sem || !s_cdcDevs[i].out_mux) {
            s_lastError = "slot_sync_create_failed";
            directLogPrintf("[USB-HOST] failed to create slot sync objects\n");
            for (int j = 0; j <= i; j++) {
                if (s_cdcDevs[j].out_sem) vSemaphoreDelete(s_cdcDevs[j].out_sem);
                if (s_cdcDevs[j].ctrl_sem) vSemaphoreDelete(s_cdcDevs[j].ctrl_sem);
                if (s_cdcDevs[j].out_mux) vSemaphoreDelete(s_cdcDevs[j].out_mux);
                s_cdcDevs[j].out_sem = nullptr;
                s_cdcDevs[j].ctrl_sem = nullptr;
                s_cdcDevs[j].out_mux = nullptr;
            }
            vSemaphoreDelete(s_mutex);
            s_mutex = nullptr;
            return;
        }
    }
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT bisect (2026-10-11): proves CDC-slot sync objects done.
    // Literals + flush only. Revert with the sync->gpio fix.
    usbHostBeginStageMark("slots");
    Serial.printf("STAGE: slots\n"); Serial.flush();
#endif

    // Sample GPIO27/26 BEFORE usb_new_phy takes ownership of those pads.
    // After usb_new_phy() muxes them as USB function, gpio_get_level() always returns 0
    // regardless of external signal (GPIO matrix is disconnected from the USB pad).
    // NOTE: On ESP32-P4, GPIO26/GPIO27 are NOT FSLS PHY pins — the FSLS PHY uses
    // dedicated USB1P1_P/N pins. This GPIO sampling only applies to S3/S2 targets.
    s_diag.gpio27_level = -1;
    s_diag.gpio26_level = -1;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT trial (2026-10-11): SKIP pre-PHY GPIO26/27 sampling. Proven
    // wedge on empty S3 port (CRASH_STAGE stuck at "slots" 3/3, then silent
    // TG1WDT): gpio_config()/get_level below never returns here. The levels
    // are diagnostics-only (web-UI pre-PHY readout); report N/A like P4 — the
    // PHY takes the pads at usb_host_install() below. Revert: delete this
    // branch (keep the #elif below).
    s_diag.gpio27_level = -2;
    s_diag.gpio26_level = -2;
#elif !defined(CONFIG_IDF_TARGET_ESP32P4)
    {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << 27) | (1ULL << 26);
        io_conf.mode         = GPIO_MODE_INPUT;
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type    = GPIO_INTR_DISABLE;
        if (gpio_config(&io_conf) == ESP_OK) {
            s_diag.gpio27_level = (int8_t)gpio_get_level((gpio_num_t)27);
            s_diag.gpio26_level = (int8_t)gpio_get_level((gpio_num_t)26);
        }
    }
    // gpio_config() above calls gpio_ll_func_sel() which:
    //   1. Sets IO_MUX.gpio[26/27].mcu_sel = PIN_FUNC_GPIO (1), routing the pads to the
    //      GPIO matrix instead of the USB FSLS PHY1.
    //   2. Disables USB pads via USB_WRAP.otg_conf.usb_pad_enable = 0.
    // Restore both: mcu_sel=0 (PHY function) and usb_pad_enable=1.
    // Do NOT touch pad_pull_override or dp_pullup: those must be left to the USB_WRAP
    // hardware for normal FSLS bus signaling (pullup on connect, pulldown on disconnect).
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
    IO_MUX.gpio[26].mcu_sel = 0;
    IO_MUX.gpio[27].mcu_sel = 0;
#endif
#else
    s_diag.gpio27_level = -2;  // N/A on P4 (GPIO26/27 are not FSLS PHY pins)
    s_diag.gpio26_level = -2;
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT bisect (2026-10-11): proves GPIO sampling done. If this
    // prints and "gpio" never lands, the wedge is the deferred directLogPrintf.
    usbHostBeginStageMark("gpiosample");
    Serial.printf("STAGE: gpiosample\n"); Serial.flush();
#endif
    directLogPrintf("[USB-HOST] pre-PHY GPIO27(FS-D+)=%d GPIO26(FS-D-)=%d (on P4: -2=N/A)\n",
                    (int)s_diag.gpio27_level, (int)s_diag.gpio26_level);
    usbHostBeginStageMark("gpio");

    usb_host_config_t host_cfg = {};
    host_cfg.skip_phy_setup = false;
    host_cfg.root_port_unpowered = false;
    host_cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    host_cfg.peripheral_map = USB_HOST_NATIVE_PERIPHERAL_MAP;
    s_diag.peripheral_map = host_cfg.peripheral_map;
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK)
    host_cfg.enum_filter_cb = usbHostEnumFilterLog;
#endif

#if defined(USB_HOST_P4_OTG11_FS_HOST) && (USB_HOST_P4_OTG11_FS_HOST == 1) && defined(USB_HOST_HAVE_PHY_API)
    // OTG11 FS FSLS host mode: initialize a PHY for the second host controller
    // (USB1 / OTG11, dedicated USB1P1_DM/DP pads) when USB_HOST_P4_OTG11_FS_HOST=1.
    // usb_host_install ONLY supports UTMI (OTG20 HS) when skip_phy_setup=false on P4.
    // By calling usb_new_phy with USB_PHY_TARGET_INT + HOST mode, we pre-init OTG11 FS.
    // peripheral_map bits: BIT0 = USB0 OTG20 HS (UTMI), BIT1 = USB1 OTG11 FS (FSLS).
    // USB_HOST_P4_DUAL_TEST_MAP selects the test map: 3 = HS+FS dual, 2 = FS-only,
    // 1 = HS-only (fallback). Only the PHYs for selected bits are pre-initialised;
    // skip_phy_setup=true is set when we pre-init at least one PHY.
    {
#if (USB_HOST_P4_DUAL_TEST_MAP & 1U) || (USB_HOST_P4_DUAL_TEST_MAP & 2U)
        // ---------------- FS (USB1 / OTG11, FSLS internal PHY) ----------------
        usb_phy_config_t phy_conf = {};
        phy_conf.controller = USB_PHY_CTRL_OTG;
        phy_conf.target     = USB_PHY_TARGET_INT;   // FSLS internal PHY = OTG11 FS
        phy_conf.otg_mode   = USB_OTG_MODE_HOST;
        phy_conf.otg_speed  = USB_PHY_SPEED_FULL;   // FS device (CH340G is 12 Mbit/s FS)
        esp_err_t phy_err = ESP_ERR_INVALID_ARG;
        if (USB_HOST_P4_DUAL_TEST_MAP & 2U) {
            phy_err = usb_new_phy(&phy_conf, &s_p4OtgFsPhyHandle);
        }
        if (USB_HOST_P4_DUAL_TEST_MAP & 2U && phy_err != ESP_OK) {
            s_lastError = String("otg11_phy_init_failed:") + esp_err_to_name(phy_err);
            directLogPrintf("[USB-HOST] OTG11 FS PHY init failed: %s\n", esp_err_to_name(phy_err));
            usbHostStop();
            return;
        }
        // ---------------- HS (USB0 / OTG20, external UTMI PHY) ----------------
        usb_phy_config_t hs_conf = {};
        hs_conf.controller = USB_PHY_CTRL_OTG;
        hs_conf.target     = USB_PHY_TARGET_UTMI;
        hs_conf.otg_mode   = USB_OTG_MODE_HOST;
        hs_conf.otg_speed  = USB_PHY_SPEED_UNDEFINED;
        esp_err_t hs_err = ESP_ERR_INVALID_ARG;
        if (USB_HOST_P4_DUAL_TEST_MAP & 1U) {
            hs_err = usb_new_phy(&hs_conf, &s_p4OtgHsPhyHandle);
        }
        if (USB_HOST_P4_DUAL_TEST_MAP & 1U && hs_err != ESP_OK) {
            s_lastError = String("otg20_phy_init_failed:") + esp_err_to_name(hs_err);
            directLogPrintf("[USB-HOST] OTG20 HS UTMI PHY init failed: %s\n", esp_err_to_name(hs_err));
            usbHostStop();
            return;
        }
        host_cfg.peripheral_map = (uint32_t)USB_HOST_P4_DUAL_TEST_MAP;
        s_diag.peripheral_map = host_cfg.peripheral_map;
        directLogPrintf("[USB-HOST] P4 pre-init PHYs ready peripheral_map=0x%lx (test map)\n",
                        (unsigned long)host_cfg.peripheral_map);
#else
        (void)0;  // map without pre-init: keep default skip_phy_setup=false path
#endif
    }
#if (USB_HOST_P4_DUAL_TEST_MAP & 1U) || (USB_HOST_P4_DUAL_TEST_MAP & 2U)
    host_cfg.skip_phy_setup = true;  // PHYs already initialised by usb_new_phy above.
#endif
#endif

    directLogPrintf("[USB-HOST] install cfg: otg_periph_num=%u peripheral_map=0x%08lx skip_phy=%d\n",
#if defined(SOC_USB_OTG_PERIPH_NUM)
                    (unsigned)SOC_USB_OTG_PERIPH_NUM,
#else
                    0U,
#endif
                    (unsigned long)host_cfg.peripheral_map,
                    (int)host_cfg.skip_phy_setup);
    sendVirtualMessage(String("[USB] install peripheral_map=") + String(host_cfg.peripheral_map, HEX) + " skip_phy=" + String(host_cfg.skip_phy_setup), DEBUG, false, false);

    usbHostBeginStageMark("install_pre");
    esp_err_t err = usb_host_install(&host_cfg);
    usbHostBeginStageMark("install_ok");
    if (err != ESP_OK) {
        s_lastError = String("usb_host_install_failed:") + esp_err_to_name(err);
        directLogPrintf("[USB-HOST] usb_host_install failed: %s\n", esp_err_to_name(err));
        sendVirtualMessage(String("[USB] install FAILED: ") + esp_err_to_name(err), ERROR, false, false);
        usbHostStop();
        return;
    }
    sendVirtualMessage("[USB] install OK", DEBUG, false, false);

    // Power-cycle root port to give the CH334 hub a clean VBUS reset on its upstream port.
    // This ensures the hub's D+ pullup is properly re-asserted after a fresh power-on.
    // If the hub was previously powered (always-on 5V rail), cycling upstream VBUS forces
    // a clean re-enumeration sequence. The P4's root_port_unpowered=false during install
    // already has PPWR on, but a explicit off→on gives the hub a known startup edge.
    //
    // NOTE: On ESP32-P4 with UTMI PHY, hub_root_start() (POWER_ON) inside usb_host_install()
    // triggers an immediate port status change interrupt that sets HCD event_pending.
    // hub_root_stop() (POWER_OFF) then fails because hcd_port_command sees event_pending and
    // returns ESP_ERR_INVALID_STATE, triggering assert(ret==ESP_OK) at hub.c:666.
    // The event hasn't been consumed yet (event loop hasn't started).
    // On P4, use direct DWC2 register write to bypass the HCD check entirely.
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(USB_HOST_HPRT0_AVAILABLE) && (USB_HOST_P4_ROOT_POWER_CYCLE == 1)
    {
        directLogPrintf("[USB-HOST] P4: direct register root_port_power cycle\n");
        dwcHprtSetPower(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        dwcHprtSetPower(true);
        directLogPrintf("[USB-HOST] P4: direct root_port_power cycled OK\n");
        s_diag.root_pwr_on_err = (int32_t)ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
#elif defined(CONFIG_IDF_TARGET_ESP32P4) && (USB_HOST_P4_ROOT_POWER_CYCLE == 1)
    directLogPrintf("[USB-HOST] P4: HPRT0 register not available, skipping root_port_power cycle\n");
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
    // USB_HOST_P4_ROOT_POWER_CYCLE=0: no install-time power cycle (v1.06-style
    // clean host start). Diagnostics only.
    directLogPrintf("[USB-HOST] P4: root_port_power cycle disabled by pack (mode 0)\n");
#else
    // S3 root-port power cycle is DEFERRED until after the daemon/client tasks
    // are created below. usb_host_lib_set_root_port_power() blocks until the
    // hub driver processes the request inside usb_host_lib_handle_events() —
    // with no task pumping events yet, the caller deadlocks forever (this
    // wedged an HTTP handler task on the S3 CNC lazy-start path).
    // See the "deferred S3 power cycle" block after task creation.
    s_diag.root_pwr_on_err = (int32_t)ESP_OK;
#endif

    // Capture initial register state for diagnostics.
    // HPRT0 bit 12 (PPWR) must be 1 for connect detection to work.
    // HPRT0 bit 0 (PCSTS) = 1 means a device is physically present on the bus.
    // GHWCFG2 bits 7:6 (hsphytype) = 1 means UTMI+ HS PHY is compiled in.
    // hprt0_fs_raw: if PCSTS=1 here with OTG11 FS, the Type-A is on GPIO26/27 (FS pads).
#if defined(USB_HOST_HPRT0_AVAILABLE)
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1)
    if (USB_HOST_P4_DUAL_TEST_MAP & 1U) {
        s_diag.hprt0_raw    = USB_DWC_HS.hprt_reg.val;
    }
    if (USB_HOST_P4_DUAL_TEST_MAP & 1U) {
        s_diag.ghwcfg2_raw  = USB_DWC_HS.ghwcfg2_reg.val;
    }
    if (USB_HOST_P4_DUAL_TEST_MAP & 2U) {
        s_diag.hprt0_fs_raw = USB_DWC_FS.hprt_reg.val;
    }
#else
    s_diag.hprt0_raw    = USB_DWC.hprt_reg.val;
    s_diag.ghwcfg2_raw  = USB_DWC.ghwcfg2_reg.val;
#endif
    directLogPrintf("[USB-HOST] HPRT0=0x%08lx PPWR=%lu PCSTS=%lu PENA=%lu GHWCFG2=0x%08lx hsphytype=%lu FS-HPRT0=0x%08lx FS-PCSTS=%lu\n",
                    (unsigned long)s_diag.hprt0_raw,
                    (unsigned long)((s_diag.hprt0_raw >> 12) & 1U),
                    (unsigned long)(s_diag.hprt0_raw & 1U),
                    (unsigned long)((s_diag.hprt0_raw >> 2) & 1U),
                    (unsigned long)s_diag.ghwcfg2_raw,
                    (unsigned long)((s_diag.ghwcfg2_raw >> 6) & 0x3U),
                    (unsigned long)s_diag.hprt0_fs_raw,
                    (unsigned long)(s_diag.hprt0_fs_raw & 1U));
    sendVirtualMessage(String("[USB] HPRT0=") + String(s_diag.hprt0_raw, HEX) + " PPWR=" + String((s_diag.hprt0_raw >> 12) & 1U) + " PCSTS=" + String(s_diag.hprt0_raw & 1U), DEBUG, false, false);
    usbHostBeginStageMark("hprt");

    // GPIO27/26 were already sampled before usb_new_phy (moved above the host_cfg block).
    // The values in s_diag.gpio27_level / s_diag.gpio26_level are set there.
#endif

    usb_host_client_config_t client_cfg = {};
    client_cfg.is_synchronous = false;
    client_cfg.max_num_event_msg = 8;
    client_cfg.async.client_event_callback = usbClientEventCallback;
    client_cfg.async.callback_arg = nullptr;

    err = usb_host_client_register(&client_cfg, &s_clientHandle);
    if (err != ESP_OK) {
        s_lastError = String("client_register_failed:") + esp_err_to_name(err);
        directLogPrintf("[USB-HOST] client_register failed: %s\n", esp_err_to_name(err));
        usbHostStop();
        return;
    }
    usbHostBeginStageMark("client");

    s_lastError = "";

    s_running = true;
    if (xTaskCreatePinnedToCore(daemonTask, "usb_daemon", 6144, nullptr, 19, &s_daemonTask, 0) != pdPASS) {
        s_lastError = "daemon_task_create_failed";
        directLogPrintf("[USB-HOST] daemon task create failed\n");
        usbHostStop();
        return;
    }
    if (xTaskCreatePinnedToCore(clientTask, "usb_client", 6144, nullptr, 18, &s_clientTask, 0) != pdPASS) {
        s_lastError = "client_task_create_failed";
        directLogPrintf("[USB-HOST] client task create failed\n");
        usbHostStop();
        return;
    }
    usbHostBeginStageMark("tasks");

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
    // Deferred S3 root-port power cycle (see note at the install site above).
    // The daemon task is now alive and pumping usb_host_lib_handle_events(),
    // so the blocking set_root_port_power() calls complete instead of
    // deadlocking the caller. The delay lets the daemon start its first pump.
    vTaskDelay(pdMS_TO_TICKS(500));
    {
        esp_err_t pwr_off = usb_host_lib_set_root_port_power(false);
        vTaskDelay(pdMS_TO_TICKS(80));
        esp_err_t pwr_on = usb_host_lib_set_root_port_power(true);
        s_diag.root_pwr_on_err = (int32_t)pwr_on;
        if (pwr_off != ESP_OK) {
            directLogPrintf("[USB-HOST] root_port_power(false) = %s (may already be off)\n", esp_err_to_name(pwr_off));
        }
        if (pwr_on != ESP_OK && pwr_on != ESP_ERR_INVALID_STATE) {
            s_lastError = String("root_port_pwr_on_failed:") + esp_err_to_name(pwr_on);
            directLogPrintf("[USB-HOST] root_port_power(true) failed: %s\n", esp_err_to_name(pwr_on));
            sendVirtualMessage(String("[USB] root_port_power FAILED: ") + esp_err_to_name(pwr_on), ERROR, false, false);
        }
        if (pwr_on == ESP_ERR_INVALID_STATE) {
            directLogPrintf("[USB-HOST] root_port_power(true) INVALID_STATE (port already in connect state)\n");
        }
        if (pwr_on == ESP_OK) {
            directLogPrintf("[USB-HOST] root_port_power cycled OK\n");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // settle time after power-on
#endif
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(USB_HOST_HPRT0_AVAILABLE) && (USB_HOST_P4_ROOT_POWER_CYCLE == 1 || USB_HOST_P4_ROOT_POWER_CYCLE == 2)
    // P4: SECOND root-port power cycle AFTER both tasks are pumping events.
    // The install-time cycle (see the hub.c:666 note above) runs before any
    // usb_host_lib_handle_events() call exists: its port-change interrupt
    // latches event_pending with nobody consuming it, starving the hub
    // driver's first hcd_port_command() (ESP_ERR_INVALID_STATE) so port
    // enable / enumeration never starts even though PCSTS=1 (device present).
    // Cycling again with the event loop live lets the hub driver see a clean
    // CONNECT and begin debounce/reset/enum in the default PHY state.
#if USB_HOST_P4_ROOT_POWER_CYCLE == 1
    directLogPrintf("[USB-HOST] P4: post-task root_port_power cycle (direct register)\n");
    dwcHprtSetPower(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    dwcHprtSetPower(true);
    vTaskDelay(pdMS_TO_TICKS(150));
#else
    // Mode 2: coherent cycle through the HCD API (same pattern as the proven
    // S3 deferred power cycle). The daemon task is already pumping events, so
    // the blocking calls complete and the port state machine sees POWER_OFF →
    // POWER_ON instead of an unannounced register write.
    directLogPrintf("[USB-HOST] P4: post-task root_port_power cycle (HCD API)\n");
    esp_err_t pwr_off = usb_host_lib_set_root_port_power(false);
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t pwr_on = usb_host_lib_set_root_port_power(true);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (pwr_off != ESP_OK) {
        directLogPrintf("[USB-HOST] root_port_power(false) = %s (may already be off)\n", esp_err_to_name(pwr_off));
    }
    if (pwr_on != ESP_OK) {
        directLogPrintf("[USB-HOST] root_port_power(true) = %s\n", esp_err_to_name(pwr_on));
        sendVirtualMessage(String("[USB] root_port_power API %s") + esp_err_to_name(pwr_on), WARN, false, false);
    }
#endif
    s_diag.root_pwr_on_err = (int32_t)ESP_OK;
    sendVirtualMessage("[USB] P4: post-task power cycle done (hub re-connect)", INFO, false, true);
#endif
    usbHostBeginStageMark("pwr");

    directLogPrintf("[USB-HOST] started (native backend)\n");
    sendVirtualMessage(F("✅ USB Host started — native backend active"), INFO, false, false);
    usbHostBeginStageMark("done");
}

const char* usbHostBackendName() {
    return s_backendName;
}

const char* usbHostLastError() {
    return s_lastError.c_str();
}

void usbHostStop() {
    const bool wasRunning = s_running;
    s_running = false;

    if (s_clientHandle) {
        (void)usb_host_client_unblock(s_clientHandle);
    }
    (void)usb_host_lib_unblock();

    if (wasRunning) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        closeSlotDevice(i, false);
    }
    memset(s_nonCdcAddrs, 0, sizeof(s_nonCdcAddrs));
    s_nonCdcAddrCount = 0;
    memset(s_lastOpenAttemptMs, 0, sizeof(s_lastOpenAttemptMs));
    s_ch340RecoveryRestartDone = false;

    if (s_clientHandle) {
        (void)usb_host_client_deregister(s_clientHandle);
        s_clientHandle = nullptr;
    }

    (void)usb_host_device_free_all();

    // Drain events until ALL_FREE or timeout (~2s).
    {
        bool allFree = false;
        for (int i = 0; i < 100; i++) {
            uint32_t flags = 0;
            esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
            if (err == ESP_ERR_INVALID_STATE) { allFree = true; break; }
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) break;
            if ((flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0) { allFree = true; break; }
        }
        if (!allFree) {
            directLogPrintf("[USB-HOST] ALL_FREE not reached, trying harder...\n");
        }
    }

#if defined(USB_HOST_P4_OTG11_FS_HOST) && (USB_HOST_P4_OTG11_FS_HOST == 1) && defined(USB_HOST_HAVE_PHY_API)
    // Delete the OTG11 FS PHY after uninstalling the host library.
    if (s_p4OtgFsPhyHandle) {
        usb_del_phy(s_p4OtgFsPhyHandle);
        s_p4OtgFsPhyHandle = nullptr;
    }
    // Delete the OTG20 HS UTMI PHY (taken explicitly only in dual-controller mode).
    if (s_p4OtgHsPhyHandle) {
        usb_del_phy(s_p4OtgHsPhyHandle);
        s_p4OtgHsPhyHandle = nullptr;
    }
#endif

    // Retry uninstall with event draining between attempts.
    {
        esp_err_t uninst_err = ESP_FAIL;
        for (int attempt = 0; attempt < 5; attempt++) {
            uninst_err = usb_host_uninstall();
            if (uninst_err == ESP_OK) break;
            directLogPrintf("[USB-HOST] uninstall attempt %d: %s\n", attempt + 1, esp_err_to_name(uninst_err));
            uint32_t flags = 0;
            (void)usb_host_lib_handle_events(pdMS_TO_TICKS(50), &flags);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (uninst_err != ESP_OK) {
            directLogPrintf("[USB-HOST] uninstall FAILED after retries: %s\n", esp_err_to_name(uninst_err));
        }
    }

    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (s_cdcDevs[i].out_sem) {
            vSemaphoreDelete(s_cdcDevs[i].out_sem);
            s_cdcDevs[i].out_sem = nullptr;
        }
        if (s_cdcDevs[i].ctrl_sem) {
            vSemaphoreDelete(s_cdcDevs[i].ctrl_sem);
            s_cdcDevs[i].ctrl_sem = nullptr;
        }
        if (s_cdcDevs[i].out_mux) {
            vSemaphoreDelete(s_cdcDevs[i].out_mux);
            s_cdcDevs[i].out_mux = nullptr;
        }
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
    }

    directLogPrintf("[USB-HOST] stopped\n");
}

// ── Async begin (never block HTTP handlers on PHY sequencing) ──────────────
// usbHostBegin() can block for seconds (root-port power sequencing waits on
// the hub driver). Callers on server tasks must use usbHostBeginAsync().
// NOTE: check-and-set below is not SMP-atomic; the only caller is the GRBL
// transport endpoint, so concurrent begins do not occur in practice.
static bool s_beginInProgress = false;
static uint32_t s_beginStartMs = 0;
// Stale-begin horizon: a wedged attempt must not block retries forever.
static const uint32_t kBeginStaleMs = 60000;
// Live counters (RAM): calls = async launches, runs = task-body entries.
// Distinguishes "task never scheduled" (calls>runs) without relying on RTC.
static uint32_t s_beginCalls = 0;
static uint32_t s_beginRuns = 0;
// Cooperative cancel: set by the OFF path while a begin is in flight; the
// begin task tears the stack back down right after bringing it up, so the
// transport mode and the host state can never disagree.
static bool s_beginCancelled = false;

static void beginTaskFn(void* /*arg*/) {
    // Mark FIRST, print second: if Serial.print ever blocks (JTAG FIFO full
    // with no USB reader attached), the stage still proves the task ran.
    s_beginRuns++;
    usbHostBeginStageMark("task");
    directLogPrintf("[USB-HOST] begin task running\n");
    usbHostBegin();
    if (s_beginCancelled) {
        s_beginCancelled = false;
        if (usbHostIsRunning()) usbHostStop();
    }
    s_beginInProgress = false;
    vTaskDelete(nullptr);
}

int usbHostBeginAsync() {
    if (usbHostIsRunning()) return 1;
    const uint32_t now = millis();
    if (s_beginInProgress && (now - s_beginStartMs) < kBeginStaleMs) return -1;
    s_beginInProgress = true;
    s_beginStartMs = now;
    s_beginCalls++;
    // 8 KB stack: usbHostBegin() nests install + logging + String ops; a
    // 4 KB task risked overflowing before printing anything (silent death).
    if (xTaskCreate(beginTaskFn, "usb_begin", 8192, nullptr, 5, nullptr) != pdPASS) {
        s_beginInProgress = false;
        return -1;
    }
    return 0;
}

bool usbHostBeginInProgress() {
    return s_beginInProgress && !usbHostIsRunning();
}

uint32_t usbHostBeginCalls() {
    return s_beginCalls;
}

uint32_t usbHostBeginRuns() {
    return s_beginRuns;
}

// Mark a pending background begin as cancelled (OFF pressed mid-start).
// The begin task stops the stack right after it comes up. No-op otherwise.
void usbHostCancelPendingBegin() {
    s_beginCancelled = true;
}

// Clear a stale cancel (fresh ON press after an OFF).
void usbHostClearBeginCancelled() {
    s_beginCancelled = false;
}

bool usbHostIsRunning() {
    return s_running;
}

// One-shot task: stop the host, wait briefly, then restart it.
// Runs outside the daemon/client tasks to avoid deadlocking on task join.
// Note: on P4, the HCD API fails because usb_host_lib_set_root_port_power()
// returns ESP_ERR_INVALID_STATE when event_pending is set (port status change
// interrupt not yet consumed). Direct DWC2 register write is used instead.
// RestartTask is used on non-P4 targets (stop+start the full stack).
// On P4 without HPRT0 access (no register struct), fall back to the same
// stop+start approach since direct power-cycle isn't available.
#if !defined(CONFIG_IDF_TARGET_ESP32P4) || !defined(USB_HOST_HPRT0_AVAILABLE)
static void restartTask(void* /*arg*/) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    usbHostStop();
    vTaskDelay(pdMS_TO_TICKS(50));
    directLogPrintf("[USB-HOST] restart: HPRT0 not available, skip POWER_OFF\n");
#else
    // Non-P4: POWER_OFF before stop (HCD API requires library installed).
    esp_err_t pwr_err = usb_host_lib_set_root_port_power(false);
    if (pwr_err != ESP_OK && pwr_err != ESP_ERR_INVALID_STATE) {
        directLogPrintf("[USB-HOST] restart root_port_power(false) failed: %s\n", esp_err_to_name(pwr_err));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    usbHostStop();
#endif
    vTaskDelay(pdMS_TO_TICKS(200));
    usbHostBegin();
    vTaskDelete(nullptr);
}
#endif

// P4 reconnect task: does NOT stop/uninstall the USB host library (which
// leaves the DWC2 HCD in an unrecoverable state on P4). Instead:
//   1. Close active slots (client resources freed)
//   2. Restore the KNOWN-GOOD initial PHY wrap state (default route, pin
//      exchange OFF) — the fallback ladder may have left a mutated combo
//      (route flip / D+/D- pin exchange) that is wrong for this board; the
//      boot state is the one that saw the device (PCSTS=1). Power-cycling
//      under a wrong combo keeps it dead. Fallback flags stay set so the
//      ladder does NOT re-mutate the restored state afterwards.
//   3. Yield to let the client/daemon tasks process DEV_GONE
//   4. Power-cycle the root port via direct register
//   5. Wait for the hub/device to re-enumerate
// The USB host library stays running throughout — usbh_devs_open will
// find a clean device entry after reconnection.
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(USB_HOST_HPRT0_AVAILABLE)
static void reconnectTask(void* /*arg*/) {
    directLogPrintf("[USB-HOST] reconnect: close active slots\n");
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (s_devices[i].connected) {
            closeSlotDevice(i, true);
        }
    }
    // Yield so the client task processes DEV_GONE while device is still on.
    vTaskDelay(pdMS_TO_TICKS(200));
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && defined(USB_WRAP_LL_SELECT_PHY_SUPPORTED)
    s_wrapPhyRouteIdx = (uint8_t)(USB_HOST_P4_PHY_ROUTE_DEFAULT & 0x01U);
    applyUsbWrapPhyRoute(s_wrapPhyRouteIdx);
    applyUsbWrapPinExchange(false);
    sendVirtualMessage(String("[USB] reconnect: PHY restored route=") + String(s_wrapPhyRouteIdx) +
                       " pinExchg=off (ladder stays disabled)", INFO, false, true);
#endif
    directLogPrintf("[USB-HOST] reconnect: direct register POWER_OFF\n");
    dwcHprtSetPower(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    directLogPrintf("[USB-HOST] reconnect: direct register POWER_ON\n");
    dwcHprtSetPower(true);
    // Wait for hub + CH340 to fully enumerate (~1.5s typical).
    vTaskDelay(pdMS_TO_TICKS(2000));
#if defined(SOC_USB_OTG_PERIPH_NUM) && (SOC_USB_OTG_PERIPH_NUM > 1) && USB_HOST_P4_OTG11_FS_HOST==1
    s_diag.hprt0_fs_raw = 0;
    if (USB_HOST_P4_DUAL_TEST_MAP & 2U) {
        s_diag.hprt0_fs_raw = USB_DWC_FS.hprt_reg.val;
    }
    directLogPrintf("[USB-HOST] reconnect done FS-HPRT0=0x%08lx FS-PCSTS=%lu\n",
                    (unsigned long)s_diag.hprt0_fs_raw,
                    (unsigned long)(s_diag.hprt0_fs_raw & 1U));
#else
    directLogPrintf("[USB-HOST] reconnect done\n");
#endif
    vTaskDelete(nullptr);
}
#endif

void usbHostRequestRestart() {
    directLogPrintf("[USB-HOST] restart requested\n");
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(USB_HOST_HPRT0_AVAILABLE)
    xTaskCreatePinnedToCore(reconnectTask, "usb_reconnect", 4096, nullptr, 4, nullptr, 0);
#else
    xTaskCreatePinnedToCore(restartTask, "usb_restart", 4096, nullptr, 4, nullptr, 0);
#endif
}

void usbHostSetRxHook(UsbHostRxHook hook, void* ctx) {
    s_rxHook = hook;
    s_rxHookCtx = ctx;
}

UsbCdcDevice usbHostGetDevice(uint8_t slot) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return {};
    UsbCdcDevice copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_devices[slot];
        xSemaphoreGive(s_mutex);
    }
    return copy;
}

UsbHostStats usbHostGetStats() {
    UsbHostStats copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_stats;
        xSemaphoreGive(s_mutex);
    }
    return copy;
}

UsbHostDiag usbHostGetDiag() {
    UsbHostDiag copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_diag;
        xSemaphoreGive(s_mutex);
        return copy;
    }
    return s_diag;
}

size_t usbHostSend(uint8_t slot, const uint8_t* data, size_t len) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES || !data || len == 0) return 0;
    if (!s_running) return 0;

    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        const bool connected = s_devices[slot].connected;
        xSemaphoreGive(s_mutex);
        if (!connected) return 0;
    }

    NativeCdcDev* dev = &s_cdcDevs[slot];
    if (!dev->active || !dev->out_xfer || !dev->out_mux || !dev->out_sem) return 0;
    if (xSemaphoreTake(dev->out_mux, pdMS_TO_TICKS(40)) != pdTRUE) return 0;

    size_t totalWritten = 0;
    while (totalWritten < len && dev->active) {
        size_t chunk = len - totalWritten;
        if (chunk > (size_t)dev->out_xfer->data_buffer_size) {
            chunk = (size_t)dev->out_xfer->data_buffer_size;
        }

        memcpy(dev->out_xfer->data_buffer, data + totalWritten, chunk);
        dev->out_xfer->num_bytes = (int)chunk;

        while (xSemaphoreTake(dev->out_sem, 0) == pdTRUE) {
        }

        esp_err_t err = usb_host_transfer_submit(dev->out_xfer);
        if (err != ESP_OK) {
            directLogPrintf("[USB-CDC] TX submit failed slot %d: %s\n",
                            slot, esp_err_to_name(err));
            break;
        }

        if (xSemaphoreTake(dev->out_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            directLogPrintf("[USB-CDC] TX timeout slot %d\n", slot);
            break;
        }

        // The device may have been closed while we waited (closeSlotDevice
        // set active=false and freed out_xfer) — re-check before touching it.
        if (!dev->active || !dev->out_xfer) break;

        if (dev->out_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            if (dev->out_xfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
                break;
            }
            directLogPrintf("[USB-CDC] TX failed slot %d status=%d\n",
                            slot, (int)dev->out_xfer->status);
            break;
        }

        size_t written = (size_t)dev->out_xfer->actual_num_bytes;
        if (written == 0) break;
        totalWritten += written;
        if (written < chunk) break;
    }

    xSemaphoreGive(dev->out_mux);

    if (totalWritten > 0 && s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_devices[slot].tx_bytes += (uint32_t)totalWritten;
        s_stats.total_tx_bytes += (uint32_t)totalWritten;
        xSemaphoreGive(s_mutex);
    }

    return totalWritten;
}

size_t usbHostSendStr(uint8_t slot, const char* str) {
    if (!str) return 0;
    return usbHostSend(slot, reinterpret_cast<const uint8_t*>(str), strlen(str));
}

bool usbHostSetLineCoding(uint8_t slot, uint32_t baud, uint8_t data_bits,
                          uint8_t stop_bits, uint8_t parity) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) {
        s_lastError = "usb_slot_invalid";
        return false;
    }
    if (!s_running) {
        s_lastError = "usb_not_running";
        return false;
    }

    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        const bool connected = s_devices[slot].connected;
        xSemaphoreGive(s_mutex);
        if (!connected) {
            s_lastError = "usb_slot_not_connected";
            return false;
        }
    }

    NativeCdcDev* dev = &s_cdcDevs[slot];
    if (!dev->active) {
        s_lastError = "line_coding_device_inactive";
        return false;
    }
    if (!dev->out_mux) {
        s_lastError = "line_coding_lock_missing";
        return false;
    }
    if (xSemaphoreTake(dev->out_mux, pdMS_TO_TICKS(60)) != pdTRUE) {
        s_lastError = "line_coding_lock_timeout";
        return false;
    }

    uint32_t useBaud = baud;
    if (useBaud == 0) {
        useBaud = s_devices[slot].baud > 0 ? s_devices[slot].baud : USB_HOST_DEFAULT_BAUD;
    }
    const uint8_t useDataBits = data_bits > 0 ? data_bits : USB_HOST_DEFAULT_DATA_BITS;
    const uint8_t useStopBits = stop_bits > 0 ? stop_bits : USB_HOST_DEFAULT_STOP_BITS;

    // Always try standard CDC SET_LINE_CODING first (sets data format).
    // For CH340-family, also send vendor-specific baud request.
    const bool isCh340 = isCh340FamilyVidPid(s_devices[slot].vid, s_devices[slot].pid);

    bool ok = nativeSetLineCoding(dev, useBaud, useDataBits, useStopBits, parity);
    if (!ok && dev->active) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ok = nativeSetLineCoding(dev, useBaud, useDataBits, useStopBits, parity);
    }
    if (isCh340) {
        ok = ch340NativeSetLineCoding(dev, useBaud);
    }
    if (!ok && dev->active && isCh340) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ok = ch340NativeSetLineCoding(dev, useBaud);
    }
    if (ok && isCh340FamilyVidPid(s_devices[slot].vid, s_devices[slot].pid)) {
        directLogPrintf("[CH340] baud set to %lu via vendor request slot=%d\n",
                        (unsigned long)useBaud, slot);
    }

    // Re-assert DTR+RTS after baud change via CH340 MODEM_CTRL (0xA4).
    if (isCh340) {
        ch340NativeSetHandshake(dev, 0x60);
    }

    xSemaphoreGive(dev->out_mux);

    if (ok && s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_devices[slot].baud = useBaud;
        xSemaphoreGive(s_mutex);
        s_lastError = "";
    }
    return ok;
}

bool usbHostSetDtrRts(uint8_t slot, bool dtr, bool rts) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return false;
    NativeCdcDev* dev = &s_cdcDevs[slot];
    if (!dev->active) return false;

    const uint16_t vid = s_devices[slot].vid;
    const uint16_t pid = s_devices[slot].pid;

    if (isCh340FamilyVidPid(vid, pid)) {
        uint8_t mcr = 0;
        if (dtr) mcr |= 0x20; // CH340_BIT_DTR
        if (rts) mcr |= 0x40; // CH340_BIT_RTS
        return ch340NativeSetHandshake(dev, mcr);
    }

    // Non-CH340: send standard CDC SET_CONTROL_LINE_STATE.
    if (!dev->ctrl_xfer || !s_clientHandle) return false;
    usb_setup_packet_t* req = reinterpret_cast<usb_setup_packet_t*>(dev->ctrl_xfer->data_buffer);
    memset(req, 0, sizeof(usb_setup_packet_t));
    req->bmRequestType = 0x21;
    req->bRequest      = 0x22;
    req->wValue        = (uint16_t)((dtr ? 1 : 0) | (rts ? 2 : 0));
    req->wIndex        = dev->iface_num;
    req->wLength       = 0;

    dev->ctrl_xfer->device_handle    = dev->dev_hdl;
    dev->ctrl_xfer->bEndpointAddress = 0;
    dev->ctrl_xfer->callback         = nativeCtrlXferCb;
    dev->ctrl_xfer->context          = dev;
    dev->ctrl_xfer->timeout_ms       = 500;
    dev->ctrl_xfer->num_bytes        = sizeof(usb_setup_packet_t);

    if (dev->ctrl_sem) {
        while (xSemaphoreTake(dev->ctrl_sem, 0) == pdTRUE) {}
    }

    esp_err_t err = submitControlWithRetry(dev);
    if (err != ESP_OK) return false;
    // Wait longer than transfer->timeout_ms (500) — see ch340NativeSerialInit.
    if (!dev->ctrl_sem || xSemaphoreTake(dev->ctrl_sem, pdMS_TO_TICKS(900)) != pdTRUE) return false;
    return dev->ctrl_xfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

#elif ENABLE_USB_HOST == 1 && USB_HOST_TINYUSB_AVAILABLE == 1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdarg.h>

#if defined(__has_include)
#if __has_include(<tusb.h>) && __has_include(<class/cdc/cdc_host.h>)
#include <tusb.h>
#include <class/cdc/cdc_host.h>
#else
#include <arduino_tinyusb/tinyusb/src/tusb.h>
#include <arduino_tinyusb/tinyusb/src/class/cdc/cdc_host.h>
#endif
#else
#include <tusb.h>
#include <class/cdc/cdc_host.h>
#endif

#include "direct_log.h"
#include "commands.h"

// CH340/CH341-family VID/PID check — same logic as the native backend.
static bool isCh340FamilyVidPid(uint16_t vid, uint16_t pid) {
    if (vid != 0x1A86) return false;
    switch (pid) {
        case 0x7523:
        case 0x55D4:
        case 0x5512:
        case 0x55D2:
        case 0xE025:
            return true;
        case 0x55D3:
        case 0x55D5:
        default:
            return false;
    }
}

const char* usbChipName(uint16_t vid, uint16_t pid) {
    switch (vid) {
        case 0x10C4:
            switch (pid) {
                case 0xEA60: return "CP2102/04";
                case 0xEA70: return "CP2105";
                case 0xEA71: return "CP2108";
                default: return "CP210x";
            }
        case 0x1A86:
            switch (pid) {
                case 0x7523: return "CH340";
                case 0x55D4: return "CH340";
                case 0x5512: return "CH341";
                case 0x55D2: return "CH340E";
                case 0x55D3: return "CH343";
                case 0x55D5: return "CH343";
                case 0xE025: return "CH342";
                default: return "CH34x";
            }
        case 0x0403:
            switch (pid) {
                case 0x6001: return "FT232R";
                case 0x6015: return "FT231X";
                case 0x6010: return "FT2232H";
                case 0x6011: return "FT4232H";
                default: return "FTDI";
            }
        case 0x067B:
            switch (pid) {
                case 0x2303: return "PL2303";
                case 0x23A3: return "PL2303TA";
                default: return "PL230x";
            }
        case 0x2C7C:
            switch (pid) {
                case 0x6001: return "RP2040";
                default: return "RP204x";
            }
        case 0x1FC9:
            switch (pid) {
                case 0x0094: return "Teensy 4.0";
                default: return "NXP";
            }
        case 0x1B3D: return "MKS";
        case 0x2341: return "Arduino";
        case 0x239A: return "Adafruit";
        default: return "USB-UART";
    }
}

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

#if ENABLE_GRBL == 1
#include "grbl_sender.h"
#endif

static bool s_running = false;
static const char* s_backendName = "tinyusb";
static String s_lastError;
static UsbHostRxHook s_rxHook = nullptr;
static void* s_rxHookCtx = nullptr;
static UsbCdcDevice s_devices[USB_HOST_MAX_CDC_DEVICES];
static SemaphoreHandle_t s_mutex = nullptr;
static UsbHostStats s_stats = {};
static UsbHostDiag s_diag = {};
static TaskHandle_t s_tuhTask = nullptr;
// rhport=0 = USB-OTG20 (HS DWC2, USB-A host connector on P4, base ~0x50040000)
// rhport=1 = USB-OTG11 (FS/JTAG, base 0x50000000, limited DWC2 register set — NOT for host use)
static uint8_t s_tinyUsbRhPort = 0;
static SemaphoreHandle_t s_ioMutex = nullptr;

extern "C" bool tuh_vid_pid_get(uint8_t daddr, uint16_t* vid, uint16_t* pid) __attribute__((weak));
extern "C" uint8_t tuh_speed_get(uint8_t daddr) __attribute__((weak));

static const uint8_t kTinyUsbMaxIfaces = 16;
static uint8_t s_slotByIdx[kTinyUsbMaxIfaces];
static uint8_t s_idxBySlot[USB_HOST_MAX_CDC_DEVICES];

static uint8_t tinyUsbMapSpeedToUsbIp(uint8_t tinyusbSpeed) {
    switch (tinyusbSpeed) {
        case TUSB_SPEED_LOW:
            return 1;
        case TUSB_SPEED_HIGH:
            return 3;
#ifdef TUSB_SPEED_SUPER
        case TUSB_SPEED_SUPER:
            return 5;
#endif
        default:
            return 2;
    }
}

static int findFreeSlot() {
    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        if (!s_devices[i].connected) return i;
    }
    return -1;
}

static int findSlotByIdx(uint8_t idx) {
    if (idx >= kTinyUsbMaxIfaces) return -1;
    const uint8_t slot = s_slotByIdx[idx];
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return -1;
    if (!s_devices[slot].connected) return -1;
    return (int)slot;
}

static void tinyUsbTask(void* /*arg*/) {
    // Only initialise TinyUSB if the Arduino framework has not already done so.
    // On ESP32-P4 with ARDUINO_USB_MODE=0 the framework may have called tusb_init()
    // before our task runs.  Double-init causes undefined behaviour; skip safely.
    if (!tusb_inited()) {
        const tusb_rhport_init_t rhInit = {
            .role  = TUSB_ROLE_HOST,
            // Full-speed is enough for USB-UART adapters and avoids hitting
            // HS-specific DWC2 paths that are fragile on P4 ES silicon.
            .speed = TUSB_SPEED_FULL,
        };
        const bool ok = tusb_init(s_tinyUsbRhPort, &rhInit);
        if (!ok) {
            directLogPrintf("[USB-TUH] tusb_init(host) failed rhport=%u\n", (unsigned)s_tinyUsbRhPort);
            s_lastError = "tusb_init_failed";
            s_running = false;
            vTaskDelete(nullptr);
            return;
        }
        directLogPrintf("[USB-TUH] tusb_init done rhport=%u\n", (unsigned)s_tinyUsbRhPort);
    } else {
        directLogPrintf("[USB-TUH] TinyUSB already inited (framework), skipping tusb_init rhport=%u\n",
                        (unsigned)s_tinyUsbRhPort);
    }

    directLogPrintf("[USB-TUH] started - CDC host ready rhport=%u\n", (unsigned)s_tinyUsbRhPort);
    sendVirtualMessage(F("✅ USB Host started — awaiting USB-RS232 adapters"), INFO, false, false);

    while (s_running) {
        tuh_task_ext(20, false);
    }

    (void)tusb_deinit(s_tinyUsbRhPort);
    vTaskDelete(nullptr);
}

extern "C" void tuh_mount_cb(uint8_t /*daddr*/) {
    // Interface-specific mount callback handles slot assignment.
}

extern "C" void tuh_umount_cb(uint8_t /*daddr*/) {
    // Interface-specific unmount callback handles slot cleanup.
}

extern "C" void tuh_cdc_mount_cb(uint8_t idx) {
    if (idx >= kTinyUsbMaxIfaces) return;
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    int slot = findSlotByIdx(idx);
    if (slot < 0) slot = findFreeSlot();
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[USB-TUH] no free slot for interface %u\n", (unsigned)idx);
        return;
    }

    tuh_itf_info_t info = {};
    tuh_cdc_itf_get_info(idx, &info);

    uint16_t vid = 0;
    uint16_t pid = 0;
    if (tuh_vid_pid_get) {
        (void)tuh_vid_pid_get(info.daddr, &vid, &pid);
    }

    uint8_t usbipSpeed = 2;
    if (tuh_speed_get) {
        usbipSpeed = tinyUsbMapSpeedToUsbIp(tuh_speed_get(info.daddr));
    }

    s_slotByIdx[idx] = (uint8_t)slot;
    s_idxBySlot[slot] = idx;
    s_devices[slot].connected = true;
    s_devices[slot].dev_addr = info.daddr;
    s_devices[slot].rx_bytes = 0;
    s_devices[slot].tx_bytes = 0;
    s_devices[slot].baud = USB_HOST_DEFAULT_BAUD;
    s_devices[slot].speed = usbipSpeed;
    s_devices[slot].vid = vid;
    s_devices[slot].pid = pid;
    s_devices[slot].bcd_device = 0;
    s_devices[slot].iface_class = info.desc.bInterfaceClass;
    s_devices[slot].iface_subclass = info.desc.bInterfaceSubClass;
    s_devices[slot].iface_protocol = info.desc.bInterfaceProtocol;
    s_devices[slot].in_ep_addr = 0;
    s_devices[slot].out_ep_addr = 0;
    snprintf(s_devices[slot].description,
             sizeof(s_devices[slot].description),
             "slot %d %s (dev %u, %04X:%04X @ %lu baud)",
             slot,
             usbChipName(vid, pid),
             (unsigned)info.daddr,
             (unsigned)vid,
             (unsigned)pid,
             (unsigned long)USB_HOST_DEFAULT_BAUD);

    s_stats.devices_connected++;
    s_stats.connect_events++;
    s_diag.new_dev_events++;
    xSemaphoreGive(s_mutex);

    (void)tuh_cdc_connect_sync(idx);

    // Apply default baud rate using standard CDC SET_LINE_CODING.
    // If it fails and device is CH340-family, send vendor-specific request instead.
    if (!tuh_cdc_set_baudrate_sync(idx, USB_HOST_DEFAULT_BAUD) && isCh340FamilyVidPid(vid, pid)) {
        unsigned long factor = 6000000UL / USB_HOST_DEFAULT_BAUD;
        if (factor < 1) factor = 1;
        if (factor > 0xFFFF) factor = 0xFFFF;
        tuh_xfer_t cxfer = {
            .rhport   = s_tinyUsbRhPort,
            .dev_addr = info.daddr,
            .ep_addr  = 0,
            .bmRequestType = 0x40,
            .bRequest = 0x9A,
            .wValue   = (uint16_t)factor,
            .wIndex   = 0x1312,
            .wLength  = 0,
            .data     = nullptr,
            .timeout_ms = 500,
            .complete_cb = nullptr,
            .user_data = nullptr,
        };
        if (tuh_control_xfer(&cxfer)) {
            directLogPrintf("[USB-TUH] CH340 baud set via vendor request slot=%d\n", slot);
        } else {
            directLogPrintf("[USB-TUH] CH340 vendor baud request failed slot=%d\n", slot);
        }
    }

    directLogPrintf("[USB-TUH] CDC mounted idx=%u slot=%d\n", (unsigned)idx, slot);
    sendVirtualMessage(("✅ USB-RS232 connected (slot " + String(slot) +
                           ", " + String(USB_HOST_DEFAULT_BAUD) + " baud)").c_str(), INFO, false, false);
    grblClearBlockReason();
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx) {
    if (idx >= kTinyUsbMaxIfaces) return;
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    const int slot = findSlotByIdx(idx);
    if (slot >= 0) {
        s_devices[slot].connected = false;
        s_idxBySlot[slot] = 0xFF;
        if (s_stats.devices_connected > 0) s_stats.devices_connected--;
        s_stats.disconnect_events++;
        s_diag.dev_gone_events++;
        xSemaphoreGive(s_mutex);
        s_slotByIdx[idx] = 0xFF;
        directLogPrintf("[USB-TUH] CDC unmounted idx=%u slot=%d\n", (unsigned)idx, slot);
        sendVirtualMessage(("🔌 USB-RS232 disconnected (slot " +
                       String(slot) + ")").c_str(), WARN, false, false);
        return;
    }
    xSemaphoreGive(s_mutex);
    s_slotByIdx[idx] = 0xFF;
}

extern "C" void tuh_cdc_rx_cb(uint8_t idx) {
    const int slot = findSlotByIdx(idx);
    if (slot < 0) return;

    uint8_t buf[256];
    while (true) {
        const uint32_t avail = tuh_cdc_read_available(idx);
        if (avail == 0) break;

        const uint32_t chunk = (avail > sizeof(buf)) ? (uint32_t)sizeof(buf) : avail;
        const uint32_t got = tuh_cdc_read(idx, buf, chunk);
        if (got == 0) break;

        if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_devices[slot].rx_bytes += got;
            s_stats.total_rx_bytes += got;
            xSemaphoreGive(s_mutex);
        }

        if (s_rxHook) {
            s_rxHook((uint8_t)slot, buf, got, s_rxHookCtx);
        }

#if ENABLE_GRBL == 1
        if (grblGetTransportMode() == GrblTransportMode::USB) {
            grblOnRxData(buf, got, (uint8_t)slot);
        }
#else
        injectDataToRXBuffer(String(reinterpret_cast<const char*>(buf), got),
                             String(F("USB-CDC")));
#endif
    }
}

extern "C" void tuh_cdc_tx_complete_cb(uint8_t /*idx*/) {
}

void usbHostBegin() {
    if (s_running) return;
    s_lastError = "";

    memset(s_devices, 0, sizeof(s_devices));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.lib_num_devices = -1;
    s_diag.lib_num_clients = -1;
    s_diag.phy_route_idx = 0;
    s_diag.phy_fallback_attempted = false;
    s_diag.phy_route_switches = 0;
    s_diag.pin_exchg_enabled = false;
    s_diag.pin_exchg_fallback_attempted = false;
    memset(s_slotByIdx, 0xFF, sizeof(s_slotByIdx));
    memset(s_idxBySlot, 0xFF, sizeof(s_idxBySlot));

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        directLogPrintf("[USB-TUH] failed to create mutex\n");
        s_lastError = "mutex_create_failed";
        return;
    }

    s_ioMutex = xSemaphoreCreateMutex();
    if (!s_ioMutex) {
        directLogPrintf("[USB-TUH] failed to create io mutex\n");
        s_lastError = "io_mutex_create_failed";
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
        return;
    }

    // TinyUSB host must be explicitly initialised on the host RHPort.
    // In this TinyUSB variant, one-arg tusb_init(...) defaults to RHPort0
    // (device mode). Use 2-arg form to select RHPort1 host mode.
#ifdef BOARD_TUH_RHPORT
    s_tinyUsbRhPort = (uint8_t)BOARD_TUH_RHPORT;
#else
    s_tinyUsbRhPort = 1;
#endif
    s_diag.peripheral_map = (uint32_t)s_tinyUsbRhPort;
    s_running = true;
    xTaskCreatePinnedToCore(tinyUsbTask, "usb_tuh", 6144, nullptr, 5, &s_tuhTask, 0);
}

const char* usbHostBackendName() {
    return s_backendName;
}

const char* usbHostLastError() {
    return s_lastError.c_str();
}

void usbHostStop() {
    if (!s_running) return;
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(150));

    for (int i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        s_devices[i].connected = false;
        s_idxBySlot[i] = 0xFF;
    }
    memset(s_slotByIdx, 0xFF, sizeof(s_slotByIdx));

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
    }
    if (s_ioMutex) {
        vSemaphoreDelete(s_ioMutex);
        s_ioMutex = nullptr;
    }

    directLogPrintf("[USB-TUH] stopped\n");
}

bool usbHostIsRunning() {
    return s_running;
}

// One-shot task: stop the host, wait briefly, then restart it.
// Runs outside the daemon/client tasks to avoid deadlocking on task join.
static void restartTask(void* /*arg*/) {
    vTaskDelay(pdMS_TO_TICKS(50));
    usbHostStop();
    vTaskDelay(pdMS_TO_TICKS(500));
    usbHostBegin();
    vTaskDelete(nullptr);
}

void usbHostRequestRestart() {
    directLogPrintf("[USB-TUH] restart requested\n");
    xTaskCreatePinnedToCore(restartTask, "usb_restart", 4096, nullptr, 4, nullptr, 0);
}

void usbHostSetRxHook(UsbHostRxHook hook, void* ctx) {
    s_rxHook = hook;
    s_rxHookCtx = ctx;
}
UsbCdcDevice usbHostGetDevice(uint8_t slot) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return {};
    UsbCdcDevice copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_devices[slot];
        xSemaphoreGive(s_mutex);
    }
    return copy;
}

UsbHostStats usbHostGetStats() {
    UsbHostStats copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_stats;
        xSemaphoreGive(s_mutex);
    }
    return copy;
}

UsbHostDiag usbHostGetDiag() {
    UsbHostDiag copy = {};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        copy = s_diag;
        xSemaphoreGive(s_mutex);
        return copy;
    }
    return s_diag;
}

size_t usbHostSend(uint8_t slot, const uint8_t* data, size_t len) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return 0;
    if (!data || len == 0) return 0;
    if (!s_devices[slot].connected) return 0;

    const uint8_t idx = s_idxBySlot[slot];
    if (idx == 0xFF || !tuh_cdc_mounted(idx)) return 0;

    if (!s_ioMutex || xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(20)) != pdTRUE) return 0;

    // Stream all data through TinyUSB FIFO — single flush at the end.
    // Old code flushed every 64 bytes, causing an unnecessary USB transfer
    // per chunk. For GRBL streaming (~14 byte lines at 1000+ lines/sec),
    // this added ~1ms USB frame overhead per flush. Batching eliminates it.
    const uint32_t written = tuh_cdc_write(idx, data, (uint32_t)len);
    if (written > 0) {
        (void)tuh_cdc_write_flush(idx);
    }

    xSemaphoreGive(s_ioMutex);

    if (written > 0 && s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_devices[slot].tx_bytes += written;
        s_stats.total_tx_bytes += written;
        xSemaphoreGive(s_mutex);
    }
    return (size_t)written;
}

size_t usbHostSendStr(uint8_t slot, const char* str) {
    if (!str) return 0;
    return usbHostSend(slot, reinterpret_cast<const uint8_t*>(str), strlen(str));
}

bool usbHostSetLineCoding(uint8_t slot, uint32_t baud, uint8_t data_bits,
                          uint8_t stop_bits, uint8_t parity) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) {
        s_lastError = "usb_slot_invalid";
        return false;
    }
    if (!s_devices[slot].connected) {
        s_lastError = "usb_slot_not_connected";
        return false;
    }

    const uint8_t idx = s_idxBySlot[slot];
    if (idx == 0xFF || !tuh_cdc_mounted(idx)) {
        s_lastError = "line_coding_device_unmounted";
        return false;
    }
    if (!s_ioMutex) {
        s_lastError = "line_coding_lock_missing";
        return false;
    }
    if (xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        s_lastError = "line_coding_lock_timeout";
        return false;
    }

    cdc_line_coding_t lc = {};
    const bool hasLocal = tuh_cdc_get_line_coding_local(idx, &lc);
    if (!hasLocal) {
        lc.bit_rate = USB_HOST_DEFAULT_BAUD;
        lc.data_bits = USB_HOST_DEFAULT_DATA_BITS;
        lc.stop_bits = USB_HOST_DEFAULT_STOP_BITS;
        lc.parity = USB_HOST_DEFAULT_PARITY;
    }

    if (baud > 0) lc.bit_rate = baud;
    if (data_bits > 0) lc.data_bits = data_bits;
    if (stop_bits > 0) lc.stop_bits = stop_bits;
    if (parity > 0 || parity == 0) lc.parity = parity;

    tusb_xfer_result_t r = tuh_cdc_set_line_coding_sync(idx, &lc);
    if (r != XFER_RESULT_SUCCESS && tuh_cdc_mounted(idx)) {
        vTaskDelay(pdMS_TO_TICKS(30));
        r = tuh_cdc_set_line_coding_sync(idx, &lc);
    }

    // CH340/CH341 do not support standard SET_LINE_CODING. If the standard
    // request failed and the device is CH340-family, send the vendor-specific
    // requests directly via the TinyUSB control API.
    bool ch340Ok = false;
    if (r != XFER_RESULT_SUCCESS) {
        const uint16_t vid = s_devices[slot].vid;
        const uint16_t pid = s_devices[slot].pid;
        if (isCh340FamilyVidPid(vid, pid)) {
            const uint8_t daddr = s_devices[slot].dev_addr;

            // SERIAL_INIT (0xA1) — required before other vendor requests.
            {
                tuh_xfer_t xfer = {
                    .rhport   = s_tinyUsbRhPort,
                    .dev_addr = daddr,
                    .ep_addr  = 0,
                    .bmRequestType = 0x40,
                    .bRequest = 0xA1,
                    .wValue   = 0,
                    .wIndex   = 0,
                    .wLength  = 0,
                    .data     = nullptr,
                    .timeout_ms = 500,
                    .complete_cb = nullptr,
                    .user_data = nullptr,
                };
                tuh_control_xfer(&xfer);
            }

            // Write baud divisor + prescaler (reg 0x1312) with proper encoding.
            uint16_t divisor = ch340CalcDivisor(lc.bit_rate);
            {
                tuh_xfer_t xfer = {
                    .rhport   = s_tinyUsbRhPort,
                    .dev_addr = daddr,
                    .ep_addr  = 0,
                    .bmRequestType = 0x40,
                    .bRequest = 0x9A,
                    .wValue   = 0x1312,
                    .wIndex   = divisor,
                    .wLength  = 0,
                    .data     = nullptr,
                    .timeout_ms = 500,
                    .complete_cb = nullptr,
                    .user_data = nullptr,
                };
                if (tuh_control_xfer(&xfer)) {
                    ch340Ok = true;
                }
            }

            // Write LCR (reg 0x2518, value 0xC3 = RX|TX|8N1).
            {
                tuh_xfer_t xfer = {
                    .rhport   = s_tinyUsbRhPort,
                    .dev_addr = daddr,
                    .ep_addr  = 0,
                    .bmRequestType = 0x40,
                    .bRequest = 0x9A,
                    .wValue   = 0x2518,
                    .wIndex   = 0xC3,
                    .wLength  = 0,
                    .data     = nullptr,
                    .timeout_ms = 500,
                    .complete_cb = nullptr,
                    .user_data = nullptr,
                };
                tuh_control_xfer(&xfer);
            }

            // MODEM_CTRL (0xA4) — assert DTR+RTS.
            {
                tuh_xfer_t xfer = {
                    .rhport   = s_tinyUsbRhPort,
                    .dev_addr = daddr,
                    .ep_addr  = 0,
                    .bmRequestType = 0x40,
                    .bRequest = 0xA4,
                    .wValue   = (uint16_t)(~0x60) & 0xFFFF,
                    .wIndex   = 0,
                    .wLength  = 0,
                    .data     = nullptr,
                    .timeout_ms = 500,
                    .complete_cb = nullptr,
                    .user_data = nullptr,
                };
                tuh_control_xfer(&xfer);
            }
        }
    }

    xSemaphoreGive(s_ioMutex);
    if (r == XFER_RESULT_SUCCESS || ch340Ok) {
        if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_devices[slot].baud = lc.bit_rate;
            xSemaphoreGive(s_mutex);
        }
        s_lastError = "";
        return true;
    }
    s_lastError = "line_coding_sync_failed";
    return false;
}

bool usbHostSetDtrRts(uint8_t slot, bool dtr, bool rts) {
    if (slot >= USB_HOST_MAX_CDC_DEVICES) return false;
    if (!s_devices[slot].connected) return false;

    const uint8_t idx = s_idxBySlot[slot];
    if (idx == 0xFF || !tuh_cdc_mounted(idx)) return false;

    const uint16_t vid = s_devices[slot].vid;
    const uint16_t pid = s_devices[slot].pid;
    const uint8_t daddr = s_devices[slot].dev_addr;

    if (isCh340FamilyVidPid(vid, pid)) {
        // CH340: MODEM_CTRL (0xA4) with DTR/RTS bits.
        uint8_t mcr = (dtr ? 0x20 : 0) | (rts ? 0x40 : 0);
        tuh_xfer_t xfer = {
            .rhport   = s_tinyUsbRhPort,
            .dev_addr = daddr,
            .ep_addr  = 0,
            .bmRequestType = 0x40,
            .bRequest = 0xA4,
            .wValue   = (uint16_t)(~mcr) & 0xFFFF,
            .wIndex   = 0,
            .wLength  = 0,
            .data     = nullptr,
            .timeout_ms = 500,
            .complete_cb = nullptr,
            .user_data = nullptr,
        };
        return tuh_control_xfer(&xfer);
    }

    // Non-CH340: standard CDC SET_CONTROL_LINE_STATE.
    tuh_xfer_t xfer = {
        .rhport   = s_tinyUsbRhPort,
        .dev_addr = daddr,
        .ep_addr  = 0,
        .bmRequestType = 0x21,
        .bRequest = 0x22,
        .wValue   = (uint16_t)((dtr ? 1 : 0) | (rts ? 2 : 0)),
        .wIndex   = s_devices[slot].iface_num,
        .wLength  = 0,
        .data     = nullptr,
        .timeout_ms = 500,
        .complete_cb = nullptr,
        .user_data = nullptr,
    };
    return tuh_control_xfer(&xfer);
}

#endif // ENABLE_USB_HOST backend
