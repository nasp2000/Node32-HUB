// camera_p4_backend.cpp — ESP32-P4 capture via Arduino ESP_Video (MIPI CSI)
// or USB-UVC (any MJPEG webcam).
//
// Hardware: Waveshare ESP32-P4-Module-DEV-KIT; MIPI = OV5647 on the CSI
// connector (shared I2C SCCB bus), USB = Type-A OTG port in host mode.
// Same stacks as Waveshare's Camera_Preview sketch (ESPVideo) and the
// prebuilt espressif__usb_host_uvc driver.

#include "camera_p4_backend.h"

#include "feature_flags.h"
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_vfs.h>            // esp_vfs_dump_registered_paths (diagnostic)
#include <driver/jpeg_encode.h>     // HW JPEG encoder (UYVY -> JPEG, no VFS slot needed)
#include <esp_vfs_console.h>    // esp_vfs_console_register (restore /dev/console on camera end)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>        // struct timeval (DQBUF timeout, CAM-62)
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM_P4)
#include <linux/videodev2.h>   // v4l2_fmtdesc + VIDIOC_ENUM_FMT (format diagnostic; CAM_P4 only: header exists solely on the 3.3.12 P4 core, all uses sit behind P4_HAVE_ESP_VIDEO)
#endif
#include <cerrno>
#include <cstring>
#include "sdkconfig.h"
#include "camera_pins.h"  // CAM_PIN_SIOD/SIOC/XCLK on the 40-pin header
#include "camera_p4_h264.h"  // Fase 4: H.264 fps ceiling (pure header)
#include "camera_motion_uyvy.h"  // motion luma tap (pure header, no deps)
#include "psram_helpers.h"  // ps_malloc() for the USB frame slot
#include "log.h"            // LogLevel (INFO/WARN/ERROR)

// All camera init diagnostics must reach the Web Serial Monitor (AGENTS rule).
extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static inline void p4CamLog(const String &msg, int level = INFO) {
    sendVirtualMessage(msg, level, false, true);
}

// ESP_Video is an Arduino-ESP32 P4 library (3.3.11+). SCOPE: CAM_P4 pack only
// ([env:esp32p4cam]). Other P4 packs build on 3.3.5 where the header is
// absent — and they have no camera anyway, so the stub fallbacks below apply.
// NOTE: the include must sit behind plain -D checks — NOT __has_include —
// because the PlatformIO LDF only evaluates -D defines when scanning
// dependencies; an __has_include guard makes LDF skip the lib and linking fails.
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM_P4)
#include <ESP_Video.h>
#include <Wire.h>
#define P4_HAVE_ESP_VIDEO 1
#else
#define P4_HAVE_ESP_VIDEO 0
#endif

static bool s_ready = false;
// Shared SCCB/I2C0 arbitration (CAM_P4 only): the OV5647 SCCB bus (SDA7/SCL8)
// is touched by TWO IDF/Arduino users on different tasks — the idle-time
// presence probe (Arduino Wire, from the /api/camera/status handler) and the
// ESP_Video sensor init (IDF esp_sccb, from the start path). A status poll
// landing mid-begin yanks I2C0 out from under esp_video and every sensor
// transaction fails with ESP_ERR_INVALID_STATE (ret 259). These flags
// serialize them: detection never runs mid-begin and begin waits (bounded)
// for an in-flight probe to finish.
static volatile bool s_beginning = false;
static volatile bool s_detecting = false;
struct P4BeginGuard {
    P4BeginGuard() { s_beginning = true; }
    ~P4BeginGuard() { s_beginning = false; }
};
// Live resolution switches also own I2C0 (sensor register rewrites) and must
// never overlap: rapid clicks otherwise interleave two begins and wedge the
// sensor until a power cycle (same class as the begin/probe race above).
static volatile bool s_resSwitching = false;
struct P4SwitchGuard {
    P4SwitchGuard() { s_resSwitching = true; }
    ~P4SwitchGuard() { s_resSwitching = false; }
};
static uint16_t s_w = 0, s_h = 0;
static uint8_t s_quality = 12;
static uint8_t s_source = CAM_P4_SRC_MIPI;
// OV5647 MIPI sensor modes with local register profiles. These are candidates;
// a successful format switch + frame capture is the runtime support test.
// Fase 3 (Tarefa 5): 2592x1944 leads as a STILLS-ONLY candidate (photo path;
// video/stream/record/H.264 stay <=1920 wide). Its register profile is still
// HW homework — the kP4SensorFmts entry carries null regs, which
// p4SetSensorFormat() refuses cleanly (revert, never a half-programmed
// sensor). Zero GPL/ArduCAM bytes: only scalar facts + our own structure.
struct P4CsiMode { uint16_t w, h; };
static const P4CsiMode kP4CsiModes[] = {
    {2592, 1944}, {1920, 1080}, {1280, 960}, {1280, 720}, {800, 1280}, {800, 800}, {800, 640},
};
#define P4_CSI_MODE_COUNT (sizeof(kP4CsiModes) / sizeof(kP4CsiModes[0]))
static uint8_t s_csiModeAck = 0;  // bitmask into kP4CsiModes (bit i = S_FMT ok)

#if P4_HAVE_ESP_VIDEO
static ESPVideoClass s_video;  // stock CSI|ISP init (pins + lifecycle managed by begin/end)
static ESPVideoCaptureDevClass s_cap;
static bool s_capOpen = false;
// HW JPEG encoder state: video0 yields UYVY only (no JPEG on this stack —
// the esp_video JPEG_ENC device is compiled out of the Arduino P4 core), so
// every captured RAW frame is HW-encoded to JPEG via esp_driver_jpeg (driver
// API, zero VFS slots). Single reusable PSRAM output slot; all consumers
// (prime check, stream, photo) consume synchronously inside cameraP4Capture.
static jpeg_encoder_handle_t s_jpgEnc = nullptr;
static uint8_t* s_jpgSlot = nullptr;
// 1080p JPEGs can exceed 1 MB at high quality; 2 MB covers the worst case
// (PSRAM-backed on the P4, allocated once).
// Fase 3 (Tarefa 5): 5 MB — 5MP stills (2592x1944 JPEG, idle direct capture)
// need headroom the 2 MB slot cannot give; video paths never exceed 1080p.
#define P4_JPG_SLOT_SIZE (5UL * 1024UL * 1024UL)
#endif

// ---- Capture-stall guard state (CAM-62) ----------------------------------
// VIDIOC_DQBUF blocks forever when the sensor stops delivering: a stalled
// 720p mode wedged the HTTP task inside a snapshot and rebooted the board
// via TASK_WDT. cameraP4Capture() fast-fails while the dequeue heartbeat is
// stale (bounded retry for recovery); cameraP4StallTick() re-syncs the
// session from the loop task. See camera_capture_guard.h.
static volatile uint32_t s_lastDequeueOkMs = 0;  // last good dequeue (any task)
static volatile uint32_t s_lastCapTryMs = 0;     // last capture attempt
static volatile uint32_t s_lastCapRestartMs = 0; // last session re-sync
static volatile uint8_t s_capStallFails = 0;     // consecutive bounded failures
static bool s_dqbufTimeoutOn = false;            // driver-level bound active?

// ---- USB-UVC source state (CAM_P4 pack only: needs the ESP_Video-era
// uvc_host headers and ENABLE_USB_HOST=1; other P4 packs have no camera) ----
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM_P4)
#include <usb/uvc_host.h>
#include <usb/usb_host.h>      // usb_host_device_addr_list_fill (find the UVC dev_addr)
#define P4_HAVE_UVC 1
#else
#define P4_HAVE_UVC 0
#endif

#if P4_HAVE_UVC
static bool s_uvcInstalled = false;
static uvc_host_stream_hdl_t s_uvcStream = nullptr;
static volatile bool s_usbGone = false;
// Cancel flag: cameraStop() sets it so a runaway USB begin (multi-minute
// candidate/sweep probing) aborts promptly instead of colliding with the next
// start ("EP already allocated"). Cleared on every begin entry.
static volatile bool s_usbCancel = false;
void cameraP4CancelUsb() { s_usbCancel = true; }
// Last UVC device seen by the driver event callback (for get_frame_list;
// 0 = none seen yet this install).
static volatile uint8_t s_uvcDevAddr = 0;
static volatile uint8_t s_uvcStreamIdx = 0;
// Bitmask of stream indices that expose VS frame descriptors (discovered).
static volatile uint8_t s_uvcStreamMask = 0;
static uint32_t s_usbXferErr = 0;
// Latest MJPEG frame slot (PSRAM): written by the UVC driver task,
// consumed by capture(). Mutex-protected; callback never blocks.
static uint8_t* s_usbSlot = nullptr;
static size_t s_usbLen = 0;
static uint32_t s_usbGen = 0;
static uint32_t s_usbFrameCount = 0;
static SemaphoreHandle_t s_usbMux = nullptr;
// Last negotiated USB geometry (0 = never negotiated this boot). The UVC
// driver only reveals device caps at stream_open time, so the page adapts
// to these after the first successful USB start.
static uint16_t s_usbNegW = 0, s_usbNegH = 0;
static uint8_t s_usbNegFps = 0;
// Pre-Start presence cache (see cameraP4DetectSources): MIPI = SCCB ACK,
// USB = UVC VideoStreaming interface seen on the bus + its VID/PID.
static bool s_detDone = false;
static bool s_detMipi = false;
static bool s_detUsb = false;
static uint16_t s_detUsbVid = 0, s_detUsbPid = 0;
static uint32_t s_detAt = 0;
#define P4_USB_SLOT_MAX (1024UL * 1024UL)

static bool p4UvcFrameCb(const uvc_host_frame_t* frame, void* user_ctx) {
    (void)user_ctx;
    if (!frame || !frame->data || frame->data_len == 0) return true;
    if (s_usbFrameCount < 5) {
        p4CamLog(String("[CAM-P4/USB] frame #") + String(s_usbFrameCount) +
                 " fmt=" + String((int)frame->vs_format.format) +
                 " len=" + String((unsigned)frame->data_len));
        s_usbFrameCount++;
    }
    if (frame->vs_format.format != UVC_VS_FORMAT_MJPEG) return true;  // JPEG only
    if (frame->data_len > P4_USB_SLOT_MAX) return true;
    if (s_usbMux && xSemaphoreTake(s_usbMux, 0) == pdTRUE) {
        if (!s_usbSlot) {
            s_usbSlot = (uint8_t*)ps_malloc(P4_USB_SLOT_MAX);
        }
        if (s_usbSlot) {
            memcpy(s_usbSlot, frame->data, frame->data_len);
            s_usbLen = frame->data_len;
            s_usbGen++;
        }
        xSemaphoreGive(s_usbMux);
    }
    return true;  // processed: ownership returns to the driver immediately
}

static void p4UvcEventCb(const uvc_host_stream_event_data_t* event, void* user_ctx) {
    (void)user_ctx;
    if (!event) return;
    switch (event->type) {
    case UVC_HOST_TRANSFER_ERROR:
        s_usbXferErr++;
        p4CamLog(String("[CAM-P4/USB] transfer error (count=") + String(s_usbXferErr) + ")", WARN);
        break;
    case UVC_HOST_DEVICE_DISCONNECTED:
        s_usbGone = true;
        p4CamLog("[CAM-P4/USB] device disconnected", WARN);
        break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW:
        p4CamLog("[CAM-P4/USB] frame buffer overflow (frame larger than frame_size)", WARN);
        break;
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
        p4CamLog("[CAM-P4/USB] frame buffer underflow (no free buffer)", WARN);
        break;
    default:
        break;
    }
}

static void p4UvcDriverEventCb(const uvc_host_driver_event_data_t* event, void* user_ctx) {
    (void)user_ctx;
    if (!event) return;
    // Remember the last connected device: stream_open(any) binds implicitly,
    // but uvc_host_get_frame_list() needs the real address to dump what the
    // webcam actually offers (diagnostics for quirky descriptors).
    if (event->type == UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED) {
        s_uvcDevAddr = event->device_connected.dev_addr;
        s_uvcStreamIdx = event->device_connected.uvc_stream_index;
    }
    // Hot-plug while running is a documented v1 limitation (re-Start).
}

// ---- USB-UVC robustness (2026-09-24): some webcams accept probe/commit
// yet never deliver frames at the requested size (quirky descriptors /
// wrong alt-setting picked by the host driver). The IDF uvc_host API offers
// no forced alt-setting select, so instead we: (a) dump what the device
// REALLY offers (frame list) to the Web Serial Monitor, (b) iterate MJPEG
// size candidates incl. the device's own entries, (c) probe YUY2 as a
// data-path diagnostic (pump serves MJPEG only; YUY2 needs a SW JPEG
// encode path = future work), (d) retry stream_open (stale claim after
// stop/start gives transient ESP_ERR_NOT_FOUND), (e) wait on the frame
// counters directly (works regardless of s_ready, unlike Capture).
#define P4_USB_OPEN_RETRIES 3
#define P4_USB_OPEN_RETRY_MS 1200
#define P4_USB_CAND_WAIT_MS 4000
#define P4_USB_MAX_CANDS 6
#define P4_USB_LIST_MAX 32
#define P4_USB_MAX_STREAMS 3
// Raw isocronous probe (claim + hand-made UVC probe/commit + own transfers).
// Diagnostic only: it proves whether the CAMERA transmits at all, bypassing the
// bundled UVC driver. Off by default (it claims interfaces and adds ~3 s to the
// start path). Set to 1 to reproduce the CAM-58 investigation.
#define P4_USB_RAW_ISOC_PROBE 1

static const char* p4UvcFmtName(enum uvc_host_stream_format f) {
    switch (f) {
    case UVC_VS_FORMAT_MJPEG: return "MJPEG";
    case UVC_VS_FORMAT_YUY2:  return "YUY2";
    case UVC_VS_FORMAT_H264:  return "H264";
    case UVC_VS_FORMAT_H265:  return "H265";
    case UVC_VS_FORMAT_NV12:  return "NV12";
    default:                  return "?";
    }
}

// MJPEG sizes harvested per stream index (read BEFORE the stream opens: once
// the UVC component claims the device its frame-list query fails).
static uint16_t s_uvcMjpeg[P4_USB_MAX_STREAMS][P4_USB_MAX_CANDS][2];
static uint8_t s_uvcMjpegN[P4_USB_MAX_STREAMS];
static int p4UsbLogFrameList(uint8_t addr, uint8_t idx, uint16_t out[][2], int cap);

// No-op event callback: the probe client only needs to hold the device open
// long enough to read its descriptors (async clients must provide one).
static void p4UsbProbeEventCb(const usb_host_client_event_msg_t* event, void* arg) {
    (void)event; (void)arg;
}
#if P4_USB_RAW_ISOC_PROBE == 1

// ---- Raw isochronous probe (ground truth for the data path) -------------
// The bundled UVC driver hides its isoc handling, so "0 frames" could mean
// (a) the camera never transmits, (b) the driver picked a bad alternate
// setting, or (c) the host cannot do isoch at all. This probe answers that
// independently of the driver: it claims the streaming interface on the
// highest-bandwidth alternate, does the UVC probe/commit itself and then
// reads the isoc endpoint with its own transfers, reporting exactly what
// came back (packet status, byte count, first payload bytes).
struct P4IsocProbe {
    volatile int done;
    volatile int packets;        // packets delivered by the host
    volatile int nonEmpty;       // packets carrying at least 1 byte
    volatile int totalBytes;
    volatile int statusSeen;     // first non-COMPLETED status
    volatile int hdrOk;          // packets starting with a valid UVC header
    volatile int sof;            // payload headers with frame-ID
    volatile int eof;            // payload headers with EoF
    volatile int soi;            // payloads starting with FF D8 (JPEG SOI)
    uint8_t first[16];
};

// UVC payload header: bHeaderLength, bmHeaderInfo (bit0 EOF, bit1 PTS, bit2
// still, bit3 error, bit4 EOF still), bFrameId, ts/...
static bool p4IsocPayloadOk(const uint8_t* p, int len) {
    return len >= 2 && p[0] >= 2 && p[0] <= len;
}

static void p4IsocXferCb(usb_transfer_t* t) {
    auto* st = (P4IsocProbe*)t->context;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        const uint8_t* base = t->data_buffer;
        for (int i = 0; i < t->num_isoc_packets; i++) {
            auto& d = t->isoc_packet_desc[i];
            st->packets += 1;
            if (d.status != USB_TRANSFER_STATUS_COMPLETED && !st->statusSeen) st->statusSeen = d.status;
            const int n = d.actual_num_bytes;
            if (n <= 0) continue;
            st->nonEmpty += 1;
            st->totalBytes += (unsigned)n;
            const uint8_t* p = base + (size_t)i * d.num_bytes;
            if (!p4IsocPayloadOk(p, n)) continue;
            st->hdrOk += 1;
            if (p[1] & 0x01) st->eof += 1;
            if (p[1] & 0x08) st->sof += 1;
            const int off = p[0];
            if (n >= off + 2 && p[off] == 0xFF && p[off + 1] == 0xD8) st->soi += 1;
            if (!st->first[1] && n >= 8) memcpy(st->first, p, 8);
        }
    } else if (!st->statusSeen) {
        st->statusSeen = t->status;
    }
    st->done = 1;
}

// EP0 control transfer helper for the raw probe (GET_CUR / SET_CUR). Uses the
// same semaphore pattern as the bundled UVC driver: the transfer completes on
// the USB host task, so the callback only signals and we wait here.
static SemaphoreHandle_t s_p4CtrlSem = nullptr;

static void p4UsbCtrlCb(usb_transfer_t* t) { xSemaphoreGive(s_p4CtrlSem); }

static esp_err_t p4UsbCtrl(usb_host_client_handle_t ch, usb_device_handle_t dh, usb_transfer_t* xfer,
                          uint8_t bmReq, uint8_t bReq, uint16_t wValue, uint16_t wIndex,
                          uint8_t* data, uint16_t len, int timeoutMs) {
    if (!s_p4CtrlSem) s_p4CtrlSem = xSemaphoreCreateBinary();
    if (!s_p4CtrlSem) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_p4CtrlSem, 0);
    xfer->device_handle = dh;
    xfer->bEndpointAddress = 0;
    xfer->context = (void*)s_p4CtrlSem;
    xfer->callback = p4UsbCtrlCb;
    xfer->timeout_ms = pdMS_TO_TICKS(timeoutMs);
    xfer->num_bytes = sizeof(usb_setup_packet_t) + len;
    usb_setup_packet_t* s = (usb_setup_packet_t*)xfer->data_buffer;
    s->bmRequestType = bmReq; s->bRequest = bReq; s->wValue = wValue; s->wIndex = wIndex; s->wLength = len;
    if (!(bmReq & 0x80) && data) memcpy((uint8_t*)xfer->data_buffer + sizeof(usb_setup_packet_t), data, len);
    esp_err_t e = usb_host_transfer_submit_control(ch, xfer);
    if (e != ESP_OK) return e;
    // Transfer callbacks are dispatched from the CLIENT event queue: without
    // pumping usb_host_client_handle_events() the completion never reaches us
    // (the UVC driver has its own task for this, our probe client does not).
    uint32_t t0 = millis();
    bool got = false;
    while ((int)(millis() - t0) < timeoutMs) {
        usb_host_client_handle_events(ch, pdMS_TO_TICKS(1));
        if (xSemaphoreTake(s_p4CtrlSem, 0) == pdTRUE) { got = true; break; }
    }
    if (!got) return ESP_ERR_TIMEOUT;
    if (data && (bmReq & 0x80) && xfer->actual_num_bytes > (int)sizeof(usb_setup_packet_t)) {
        memcpy(data, (uint8_t*)xfer->data_buffer + sizeof(usb_setup_packet_t),
               xfer->actual_num_bytes - (int)sizeof(usb_setup_packet_t));
    }
    return xfer->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

// Claim the streaming interface, negotiate UVC by hand and read the isoch
// endpoint directly. Pure diagnostic: it proves whether the camera transmits
// and whether this host can receive isoch data at all.
static void p4UsbRawIsocProbe(usb_host_client_handle_t ch, usb_device_handle_t dh, uint8_t iface,
                               uint8_t alt, uint8_t epAddr, uint16_t bwPerUs, uint16_t bcdUVC) {
    // Linux order: negotiate on alt 0, commit, THEN select the streaming
    // alternate. Claiming the streaming alt first (old behavior) leaves some
    // non-compliant cameras (Sunplus 1b3f:22xx) permanently silent.
    // NOTE: no claim here — EP0 control works unclaimed, and claiming twice
    // (alt 0 then alt N) leaks the endpoint in this IDF version, breaking the
    // later stream_open with "EP already allocated". Single claim post-commit.
    usb_transfer_t* ctrl = nullptr;
    if (usb_host_transfer_alloc(64, 0, &ctrl) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] raw isoc: no ctrl transfer", WARN);
        usb_host_interface_release(ch, dh, iface);
        return;
    }
    // UVC 1.1 uses one-byte selectors, 1.5+ uses 0x0200/0x0201 and a 34-byte
    // probe structure. Ask for both lengths and keep what the device returns.
    const bool uv15 = (bcdUVC >= 0x0150);
    const uint16_t wProbe = uv15 ? 0x0200 : 0x0101;
    const uint16_t wCommit = uv15 ? 0x0201 : 0x0102;
    // Request EXACTLY the structure the camera's UVC revision defines. Asking
    // a UVC 1.1/1.4 device for the 34-byte 1.5 probe leaves EP0 short and the
    // host transfer never completes (observed: control transfer timeouts).
    const uint16_t probeLen = uv15 ? 34 : 26;
    uint8_t buf[40] = {};
    esp_err_t e = p4UsbCtrl(ch, dh, ctrl, 0xA1, 0x81, wProbe, iface, buf, probeLen, 2000);
    const int got = ctrl->actual_num_bytes >= (int)sizeof(usb_setup_packet_t)
                        ? ctrl->actual_num_bytes - (int)sizeof(usb_setup_packet_t) : 0;
    p4CamLog(String("[CAM-P4/USB] raw isoc: bcdUVC=0x") + String(bcdUVC, HEX) +
             " GET_CUR probe " + (e == ESP_OK ? String("ok") : String(esp_err_to_name(e))) +
             " len=" + String(got));
    if (e != ESP_OK && got < 12) {
        p4CamLog("[CAM-P4/USB] raw isoc: probe unreadable, no commit (camera stays idle)", WARN);
    } else {
        if (got >= 12) {
            String hex = String();
            for (int i = 0; i < (got > 26 ? 26 : got); i++) hex += String(buf[i], HEX) + " ";
            p4CamLog(String("[CAM-P4/USB] raw isoc: probe bytes: ") + hex);
        }
        // Keep the device's own format/frame/interval. UVC probe offsets
        // (26-byte 1.x layout): bmHint[0..1], fmt[2], frm[3], interval[4..7],
        // maxFrameSize[18..21], payload[22..25]. Linux commits bmHint=0x0001
        // (interval valid only) with the descriptor maxFrameSize — NOT 0x02.
        uint32_t maxFrame = 0, payload = 0;
        memcpy(&maxFrame, &buf[18], 4);
        memcpy(&payload, &buf[22], 4);
        buf[0] = 0x01; buf[1] = 0x00;  // bmHint: dwFrameInterval valid (Linux dialect)
        p4CamLog(String("[CAM-P4/USB] raw isoc: probe state fmt=") + String(buf[2]) +
                 " frm=" + String(buf[3]) + " maxFrame=" + String(maxFrame) +
                 " devPayload=" + String(payload) + "B (alt gives " + String(bwPerUs) + "B/us)");
        esp_err_t pe = p4UsbCtrl(ch, dh, ctrl, 0x21, 0x01, wProbe, iface, buf, (uint16_t)got, 2000);
        esp_err_t ce = p4UsbCtrl(ch, dh, ctrl, 0x21, 0x01, wCommit, iface, buf, (uint16_t)got, 2000);
        p4CamLog(String("[CAM-P4/USB] raw isoc: probe=") + (pe == ESP_OK ? String("ok") : String(esp_err_to_name(pe))) +
                 " commit=" + (ce == ESP_OK ? String("ok") : String(esp_err_to_name(ce))));
    }
    // Read the endpoint: urb of 4 packets, resubmitted until we time out.
    const uint16_t perUs = bwPerUs;   // bytes per microframe on this alt
    usb_transfer_t* isoc = nullptr;
    const int numPkt = 4;
    if (usb_host_transfer_alloc((size_t)perUs * numPkt, numPkt, &isoc) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] raw isoc: no isoc transfer", WARN);
        usb_host_transfer_free(ctrl);
        usb_host_interface_release(ch, dh, iface);
        return;
    }
    static P4IsocProbe st;
    memset(&st, 0, sizeof(st));
    isoc->device_handle = dh;
    isoc->context = &st;
    isoc->timeout_ms = pdMS_TO_TICKS(1000);
    isoc->bEndpointAddress = epAddr;
    isoc->callback = p4IsocXferCb;
    isoc->num_bytes = (size_t)perUs * numPkt;
    for (int i = 0; i < numPkt; i++) isoc->isoc_packet_desc[i].num_bytes = perUs;
    p4CamLog(String("[CAM-P4/USB] raw isoc: reading ep 0x") + String(epAddr, HEX) +
             " alt=" + String(alt) + " mps/us=" + String(perUs) + " for 800ms");
    uint32_t t0 = millis();
    esp_err_t subErr = ESP_OK;
    int loops = 0;
    while ((millis() - t0) < 800) {
        st.done = 0;
        loops++;
        subErr = usb_host_transfer_submit(isoc);
        if (subErr != ESP_OK) { delay(10); continue; }
        while (!st.done && (millis() - t0) < 800) {
            usb_host_client_handle_events(ch, pdMS_TO_TICKS(1));   // dispatch completion
        }
        if (!st.done) break;   // transfer never completed: endpoint idle
    }
    // Format/frame/payload sweep in the Linux dialect (bmHint=0x0001 always,
    // payload at offset 22). First DATA wins and stops the sweep.
    if (got >= 12) {
        uint32_t devPayload = 0;
        memcpy(&devPayload, &buf[22], 4);
        const uint32_t payVars[3] = { devPayload, (uint32_t)bwPerUs, (uint32_t)bwPerUs * 8u };
        bool altSet = false;
        uint8_t vbuf[40] = {};
        for (int pv = 0; pv < 3 && !st.nonEmpty && !s_usbCancel; pv++) {
            memcpy(&buf[22], &payVars[pv], 4);
            buf[0] = 0x01; buf[1] = 0x00;  // Linux dialect, every commit
            // MJPEG (fmt 1) has 9 frame descriptors on this camera (1080p x2,
            // 720p, 800x480, 640x480, 640x360, 320x240, 176x144, 800x600);
            // YUY2 (fmt 2) has 5+. Cover them all: Linux uses fmt1/frm4.
            for (int fmt = 1; fmt <= 2 && !st.nonEmpty; fmt++) {
                int frmMax = (fmt == 1) ? 9 : 5;
                for (int frm = 1; frm <= frmMax && !st.nonEmpty; frm++) {
                    buf[2] = (uint8_t)fmt;
                    buf[3] = (uint8_t)frm;
                    if (p4UsbCtrl(ch, dh, ctrl, 0x21, 0x01, wProbe, iface, buf, (uint16_t)got, 1000) != ESP_OK) {
                        continue;  // SET probe rejected
                    }
                    // Verify step (Linux negotiates: read back what the
                    // device accepted and commit THOSE bytes).
                    memset(vbuf, 0, sizeof(vbuf));
                    esp_err_t ve = p4UsbCtrl(ch, dh, ctrl, 0xA1, 0x81, wProbe, iface, vbuf, (uint16_t)got, 1000);
                    int gotV = ctrl->actual_num_bytes >= (int)sizeof(usb_setup_packet_t)
                               ? ctrl->actual_num_bytes - (int)sizeof(usb_setup_packet_t) : 0;
                    if (ve == ESP_OK && gotV >= 12) {
                        int n = gotV < got ? gotV : got;
                        memcpy(buf, vbuf, (size_t)n);
                        buf[0] = 0x01; buf[1] = 0x00;  // keep Linux dialect after verify
                    }
                    if (p4UsbCtrl(ch, dh, ctrl, 0x21, 0x01, wCommit, iface, buf, (uint16_t)got, 1000) != ESP_OK) {
                        continue;  // commit rejected
                    }
                    if (!altSet) {
                        // Streaming alt only AFTER the first commit.
                        esp_err_t cle = usb_host_interface_claim(ch, dh, iface, alt);
                        p4CamLog(String("[CAM-P4/USB] raw isoc: claim alt ") + String(alt) +
                                 " -> " + String(esp_err_to_name(cle)), WARN);
                        if (cle != ESP_OK) {
                            usb_host_transfer_free(isoc);
                            usb_host_transfer_free(ctrl);
                            usb_host_interface_release(ch, dh, iface);
                            return;
                        }
                        altSet = true;
                    }
                    memset(&st, 0, sizeof(st));
                    uint32_t sw = millis();
                    while ((millis() - sw) < 250) {
                        st.done = 0;
                        if (usb_host_transfer_submit(isoc) != ESP_OK) { delay(5); continue; }
                        while (!st.done && (millis() - sw) < 250) usb_host_client_handle_events(ch, pdMS_TO_TICKS(1));
                        if (!st.done) break;
                    }
                    if (st.nonEmpty) {
                        p4CamLog(String("[CAM-P4/USB] raw isoc FLOW: pay=") + String(payVars[pv]) +
                                 " fmt=" + String(buf[2]) + " frm=" + String(buf[3]) +
                                 " bytes=" + String(st.totalBytes) + " jpegSoi=" + String(st.soi), ERROR);
                    }
                }
            }
            p4CamLog(String("[CAM-P4/USB] raw isoc sweep pay=") + String(payVars[pv]) +
                     ": nonEmpty=" + String(st.nonEmpty) + " bytes=" + String(st.totalBytes), WARN);
        }
    }
    p4CamLog(String("[CAM-P4/USB] raw isoc RESULT: submits=") + String(loops) +
             " submitErr=" + (subErr == ESP_OK ? String("-") : String(esp_err_to_name(subErr))) +
             " xferStatus=" + String((int)isoc->status) +
             " packets=" + String(st.packets) + " nonEmpty=" + String(st.nonEmpty) +
             " bytes=" + String(st.totalBytes) + " hdrOk=" + String(st.hdrOk) +
             " sof=" + String(st.sof) + " eof=" + String(st.eof) + " jpegSoi=" + String(st.soi) +
             " errStatus=" + String(st.statusSeen));
    if (st.first[1]) {
        String hex = String();
        for (int i = 0; i < 8; i++) hex += String(st.first[i], HEX) + " ";
        p4CamLog(String("[CAM-P4/USB] raw isoc first bytes: ") + hex);
    }
    // Drain: the sweep often exits with one isoc submit still in flight (last
    // 250 ms window). Freeing/releasing around a pending transfer leaves the
    // endpoint allocated ("EP already allocated" on the next stream_open).
    // Wait bounded for its completion first.
    {
        uint32_t dw = millis();
        while (!st.done && (uint32_t)(millis() - dw) < 500) {
            usb_host_client_handle_events(ch, pdMS_TO_TICKS(1));
        }
    }
    usb_host_transfer_free(isoc);
    usb_host_transfer_free(ctrl);
    esp_err_t rle = usb_host_interface_release(ch, dh, iface);
    p4CamLog(String("[CAM-P4/USB] raw isoc: release iface ") + String(iface) +
             " -> " + String(esp_err_to_name(rle)), WARN);
}

#endif  // P4_USB_RAW_ISOC_PROBE

// Walk the live configuration descriptor through a temporary usb_host client
// (the UVC component only opens the device during stream_open, so this is
// safe here). Logs every interface/alternate setting and endpoint: isoch vs
// bulk, wMaxPacketSize and bInterval are what decide whether frames can flow.
// Runs BEFORE uvc_host_install: the USB host heap is freshest then.
// Best VS-isoch alt accumulates in *best (caller sweeps ALL addresses: with a
// hub in the chain, addrs[0] is the hub itself, not the camera).
struct P4UsbBestAlt {
    uint32_t bw = 0;
    uint8_t addr = 0, iface = 0, alt = 0, ep = 0;
    uint16_t perUs = 0, bcd = 0;
};
static void p4UsbDumpConfig(uint8_t addr, P4UsbBestAlt *best) {
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 4;
    cc.async.client_event_callback = p4UsbProbeEventCb;
    usb_host_client_handle_t ch = nullptr;
    esp_err_t ce = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(300);
        ce = usb_host_client_register(&cc, &ch);
        if (ce == ESP_OK) break;
    }
    if (ce != ESP_OK) {
        p4CamLog(String("[CAM-P4/USB] descriptor probe: client_register failed err=") +
                 String(esp_err_to_name(ce)), WARN);
        return;
    }
    usb_device_handle_t dh = nullptr;
    if (usb_host_device_open(ch, addr, &dh) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] descriptor probe: device busy (UVC already claimed it)", WARN);
        usb_host_client_deregister(ch);
        return;
    }
    usb_device_info_t info = {};
    if (usb_host_device_info(dh, &info) == ESP_OK) {
        p4CamLog(String("[CAM-P4/USB] device addr=") + String(info.dev_addr) +
                 " speed=" + String(info.speed == USB_SPEED_HIGH ? "HIGH" :
                                     info.speed == USB_SPEED_FULL ? "FULL" : "LOW") +
                 " bMaxPacketSize0=" + String(info.bMaxPacketSize0));
    }
    const usb_device_desc_t *dd = nullptr;
    if (usb_host_get_device_descriptor(dh, &dd) == ESP_OK && dd) {
        p4CamLog(String("[CAM-P4/USB] VID=") + String(dd->idVendor, HEX) + " PID=" + String(dd->idProduct, HEX) +
                 " bcdUSB=" + String(dd->bcdUSB, HEX));
        // Remember for the pre-Start source list (status page).
        s_detUsbVid = dd->idVendor;
        s_detUsbPid = dd->idProduct;
    }
    const usb_config_desc_t *cfg = nullptr;
    if (usb_host_get_active_config_descriptor(dh, &cfg) == ESP_OK && cfg) {
        const uint8_t *raw = reinterpret_cast<const uint8_t*>(cfg);
        uint16_t total = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
        if (total < 9) total = 9;
        p4CamLog(String("[CAM-P4/USB] config: interfaces=") + String(raw[4]) + " total=" + String(total) + "B");
        // VideoStreaming interface with the widest isoch endpoint: the
        // alternate setting the camera actually needs for its formats.
        uint16_t bcdUVC = 0;
        uint8_t curIface = 0, curAlt = 0, curClass = 0, curSub = 0;
        uint32_t bestBw = 0;
        uint8_t bestIface = 0, bestAlt = 0, bestEp = 0;
        uint16_t bestPerUs = 0;
        const uint8_t *p = raw + 9;
        const uint8_t *end = raw + total;
        while (p + 2 <= end) {
            uint8_t len = p[0], type = p[1];
            if (len < 2 || p + len > end) break;
            if (type == 0x04 && len >= 9) {
                curIface = p[2];
                curAlt = p[3];
                curClass = p[5];
                curSub = p[6];
                p4CamLog(String("  iface ") + String(p[2]) + " alt " + String(p[3]) +
                         " class=" + String(p[5], HEX) + " subclass=" + String(p[6], HEX) +
                         " protocol=" + String(p[7], HEX) + " eps=" + String(p[4]));
            } else if (type == 0x24 && len >= 5 && p[2] == 0x01) {
                bcdUVC = (uint16_t)(p[3] | ((uint16_t)p[4] << 8));   // VC header bcdUVC
            } else if (type == 0x05 && len >= 7) {
                // Endpoint descriptor: p[2]=address, p[3]=bmAttributes,
                // p[4..5]=wMaxPacketSize, p[6]=bInterval. On high speed the
                // top 2 bits of wMaxPacketSize carry "additional transactions
                // per microframe" (N = value+1).
                uint8_t addrEp = p[2];
                uint8_t attr = p[3];
                const char *kind = (attr & 3) == 1 ? "ISO" : (attr & 3) == 2 ? "BULK" : "INTR";
                uint16_t wmps = (uint16_t)(p[4] | ((uint16_t)p[5] << 8));
                uint16_t packets = (uint16_t)(wmps & 0x7FF);
                uint8_t perUs = (uint8_t)(((wmps >> 11) & 3) + 1);
                p4CamLog(String("    ep 0x") + String(addrEp, HEX) + " " + kind +
                         " wMaxPacket=" + String(packets) + "x" + String(perUs) +
                         " bytes/us=" + String((unsigned)packets * perUs) +
                         " interval=" + String(p[6]) +
                         " raw=" + String(wmps, HEX) + "/" + String(p[3], HEX));
                if ((attr & 3) == 1 && (addrEp & 0x80) && curClass == 0x0E && curSub == 0x02) {
                    const uint32_t bw = (uint32_t)packets * perUs;
                    if (bw > bestBw) {
                        bestBw = bw; bestIface = curIface; bestAlt = curAlt;
                        bestEp = addrEp; bestPerUs = (uint16_t)bw;
                    }
                }
            } else {
                // Any other descriptor (class-specific format/frame payloads):
                // printed as type/length so a vendor-specific wrapper between
                // an interface and its endpoints is visible.
                p4CamLog(String("    d 0x") + String(type, HEX) + "/" + String(len) +
                         (len >= 3 ? (String(" [") + String(p[2], HEX) + " " +
                                      String(p[3], HEX) + (len >= 5 ? (" " + String(p[4], HEX)) : "") + "]")
                                   : String("")));
            }
            p += len;
        }
        if (bestBw) {
            p4CamLog(String("[CAM-P4/USB] widest isoch alt: iface ") + String(bestIface) +
                     " alt " + String(bestAlt) + " ep 0x" + String(bestEp, HEX) +
                     " " + String(bestPerUs) + "B/us (bcdUVC=0x" + String(bcdUVC, HEX) + ")");
            if (best && bestBw > best->bw) {
                best->bw = bestBw; best->addr = addr;
                best->iface = bestIface; best->alt = bestAlt;
                best->ep = bestEp; best->perUs = bestPerUs; best->bcd = bcdUVC;
            }
        } else {
            p4CamLog("[CAM-P4/USB] no isochronous VideoStreaming endpoint found", WARN);
        }
    } else {
        p4CamLog("[CAM-P4/USB] descriptor probe: config descriptor unavailable", WARN);
    }
    usb_host_device_close(ch, dh);
    usb_host_client_deregister(ch);
}

// Raw-isoch probe on the winning address (own temp client). Separated from
// the dump so the sweep can cover every bus address first.
static void p4UsbRunRawProbe(const P4UsbBestAlt *best) {
#if P4_USB_RAW_ISOC_PROBE != 1
    (void)best;
#else
    if (!best || !best->bw) return;
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 4;
    cc.async.client_event_callback = p4UsbProbeEventCb;
    usb_host_client_handle_t ch = nullptr;
    if (usb_host_client_register(&cc, &ch) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] raw probe: client_register failed", WARN);
        return;
    }
    usb_device_handle_t dh = nullptr;
    if (usb_host_device_open(ch, best->addr, &dh) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] raw probe: device busy/gone", WARN);
        usb_host_client_deregister(ch);
        return;
    }
    p4CamLog(String("[CAM-P4/USB] raw probe on addr=") + String(best->addr) +
             " iface=" + String(best->iface) + " alt=" + String(best->alt));
    p4UsbRawIsocProbe(ch, dh, best->iface, best->alt, best->ep, best->perUs, best->bcd);
    usb_host_device_close(ch, dh);
    usb_host_client_deregister(ch);
#endif
}

// List the bus and pick the UVC candidate address. Runs before the UVC driver
// is installed so the temporary descriptor-probe client has room; the frame
// list harvest (p4UsbHarvestFrames) validates the address afterwards.
static void p4UsbScanBus() {
    uint8_t addrs[8] = {};
    int n = 0;
    if (usb_host_device_addr_list_fill(8, addrs, &n) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] cannot list USB addresses", WARN);
        return;
    }
    p4CamLog(String("[CAM-P4/USB] ") + String(n) + " USB device(s) on the bus:");
    P4UsbBestAlt best;
    for (int i = 0; i < n; i++) {
        p4CamLog(String("  addr ") + String(addrs[i]));
        p4UsbDumpConfig(addrs[i], &best);
    }
    if (best.bw) {
        p4UsbRunRawProbe(&best);
    }
}

// Harvest the frame descriptors of every stream index the device exposes
// (needs the UVC driver installed, and only works while the device is not yet
// claimed by an open stream). Multi-stream webcams often leave the first VS
// function idle, so every index is kept in a mask.
static void p4UsbHarvestFrames() {
    uint8_t addrs[8] = {};
    int n = 0;
    if (usb_host_device_addr_list_fill(8, addrs, &n) != ESP_OK) return;
    s_uvcStreamMask = 0;
    s_uvcDevAddr = 0;
    for (int i = 0; i < n; i++) {
        for (uint8_t idx = 0; idx < P4_USB_MAX_STREAMS; idx++) {
            size_t cnt = 0;
            if (uvc_host_get_frame_list(addrs[i], idx, nullptr, &cnt) == ESP_OK && cnt > 0) {
                p4CamLog(String("[CAM-P4/USB] addr=") + String(addrs[i]) +
                         " stream_index=" + String(idx) + " frames=" + String((unsigned)cnt));
                s_uvcStreamMask |= (uint8_t)(1u << idx);
                if (!s_uvcDevAddr) { s_uvcDevAddr = addrs[i]; s_uvcStreamIdx = idx; }
                s_uvcMjpegN[idx] = (uint8_t)p4UsbLogFrameList(addrs[i], idx, s_uvcMjpeg[idx], P4_USB_MAX_CANDS);
            }
        }
    }
    if (!s_uvcDevAddr) {
        p4CamLog("[CAM-P4/USB] no UVC streaming interface found on the bus", WARN);
        s_uvcDevAddr = 0;
    }
}

// Dump one stream index's offered VS frames to the monitor. Returns MJPEG
// entries found (up to cap) in out[][2] as (w,h). Never fails the begin.
static int p4UsbLogFrameList(uint8_t addr, uint8_t idx, uint16_t out[][2], int cap) {
    int nMjpeg = 0;
    size_t n = 0;
    if (uvc_host_get_frame_list(addr, idx, nullptr, &n) != ESP_OK || n == 0) {
        p4CamLog(String("[CAM-P4/USB] stream ") + String(idx) + ": frame list query failed/empty", WARN);
        return 0;
    }
    // Allocate the FULL reported count: the component rejects the fill when
    // the capacity is smaller than the number of descriptors.
    auto *buf = (uvc_host_frame_info_t*)malloc(n * sizeof(uvc_host_frame_info_t));
    if (!buf) return 0;
    size_t capn = n;
    auto listp = reinterpret_cast<uvc_host_frame_info_t(*)[]>(buf);
    esp_err_t e = uvc_host_get_frame_list(addr, idx, listp, &capn);
    if (e != ESP_OK) {
        p4CamLog(String("[CAM-P4/USB] stream ") + String(idx) + ": frame list read failed err=" +
                 String(esp_err_to_name(e)), WARN);
        free(buf);
        return 0;
    }
    p4CamLog(String("[CAM-P4/USB] stream ") + String(idx) + " offers " + String((unsigned)capn) + " frames:");
    for (size_t i = 0; i < capn; i++) {
        const auto &fi = buf[i];
        String fps = fi.default_interval ? String(1000000.0f / (float)fi.default_interval) : String("?");
        p4CamLog(String("  #") + String((unsigned)i) + " " + p4UvcFmtName(fi.format) +
                 " " + String(fi.h_res) + "x" + String(fi.v_res) + " @" + fps + "fps");
        if (fi.format == UVC_VS_FORMAT_MJPEG && fi.h_res > 0 && fi.v_res > 0) {
            // Keep the list sorted by pixel count (smallest first), keeping the
            // cap on the SMALLEST formats: the streaming interface picks its
            // alternate setting from the negotiated format, and a small format
            // is the only one that can land on a low-bandwidth alternate
            // setting (a big one needs a high-bandwidth isoch endpoint this
            // host cannot feed).
            int at = nMjpeg < cap ? nMjpeg : cap - 1;
            for (int k = 0; k < cap; k++) {
                if ((uint32_t)fi.h_res * fi.v_res < (uint32_t)out[k][0] * out[k][1]) { at = k; break; }
                if (k >= nMjpeg) { at = k; break; }
            }
            if (at >= 0 && at < cap) {
                for (int k = (nMjpeg < cap ? nMjpeg : cap - 1); k > at; k--) {
                    out[k][0] = out[k - 1][0];
                    out[k][1] = out[k - 1][1];
                }
                out[at][0] = (uint16_t)fi.h_res;
                out[at][1] = (uint16_t)fi.v_res;
                if (nMjpeg < cap) nMjpeg++;
            }
        }
    }
    free(buf);
    return nMjpeg;
}

// ---- Presence detection (no capture) --------------------------------------
// The page must list attached cameras BEFORE any Start: MIPI via an SCCB
// ACK at 0x36, USB via a quiet bus walk for a UVC VideoStreaming interface
// (class 0x0E / subclass 0x02 — a CDC adapter must NOT show up as a camera).
// Runs only while idle (never touches a live pipeline) and at most every
// 10 s; the status page reads the cache. Logs only on change (the status
// poll would otherwise spam the Web Serial Monitor).
#define P4_DETECT_MS 10000u

static bool p4SccbProbeQuiet() {
#if !P4_HAVE_ESP_VIDEO
    return false;
#else
    if (!Wire.begin(CAM_PIN_SIOD, CAM_PIN_SIOC)) return false;
    Wire.beginTransmission(0x36);
    uint8_t err = Wire.endTransmission();
    Wire.end();
    return err == 0;
#endif
}

// True when addr exposes a UVC VideoStreaming interface; fills vid/pid.
// Quiet: no monitor output (called from the status path every few seconds).
static bool p4UsbProbeUvc(uint8_t addr, uint16_t &vid, uint16_t &pid) {
#if !P4_HAVE_UVC
    (void)addr; (void)vid; (void)pid;
    return false;
#else
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 4;
    cc.async.client_event_callback = p4UsbProbeEventCb;
    usb_host_client_handle_t ch = nullptr;
    if (usb_host_client_register(&cc, &ch) != ESP_OK) return false;
    bool uvc = false;
    usb_device_handle_t dh = nullptr;
    if (usb_host_device_open(ch, addr, &dh) == ESP_OK) {
        const usb_device_desc_t *dd = nullptr;
        if (usb_host_get_device_descriptor(dh, &dd) == ESP_OK && dd) {
            vid = dd->idVendor;
            pid = dd->idProduct;
        }
        const usb_config_desc_t *cfg = nullptr;
        if (usb_host_get_active_config_descriptor(dh, &cfg) == ESP_OK && cfg) {
            const uint8_t *raw = reinterpret_cast<const uint8_t*>(cfg);
            uint16_t total = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
            if (total < 9) total = 9;
            uint8_t cls = 0, sub = 0;
            for (const uint8_t *p = raw + 9, *end = raw + total; p + 2 <= end; ) {
                uint8_t len = p[0], type = p[1];
                if (len < 2 || p + len > end) break;
                if (type == 0x04 && len >= 9) { cls = p[5]; sub = p[6]; }
                if (cls == 0x0E && sub == 0x02) { uvc = true; break; }
                p += len;
            }
        }
        usb_host_device_close(ch, dh);
    }
    usb_host_client_deregister(ch);
    return uvc;
#endif
}

void cameraP4DetectSources() {
#if !P4_HAVE_ESP_VIDEO
    return;
#else
    if (s_ready) return;  // never disturb a live pipeline; status uses cache
    if (s_beginning) return;  // a begin owns I2C0 right now; status uses cache
    uint32_t now = millis();
    if (s_detDone && (now - s_detAt) < P4_DETECT_MS) return;
    s_detecting = true;  // begin spins (bounded) on this, never inits mid-probe
    bool mipi = p4SccbProbeQuiet();
    bool usb = false;
    uint16_t vid = 0, pid = 0;
#if P4_HAVE_UVC
    uint8_t addrs[8] = {};
    int n = 0;
    if (usb_host_device_addr_list_fill(8, addrs, &n) == ESP_OK) {
        for (int i = 0; i < n && !usb; i++) {
            uint16_t v = 0, p = 0;
            if (p4UsbProbeUvc(addrs[i], v, p)) { usb = true; vid = v; pid = p; }
        }
    }
#endif
    if (!s_detDone || mipi != s_detMipi || usb != s_detUsb ||
            vid != s_detUsbVid || pid != s_detUsbPid) {
        p4CamLog(String("[CAM-P4] detected: MIPI=") + (mipi ? "OV5647" : "-") +
                 " USB=" + (usb ? String("UVC ") + String(vid, HEX) + ":" + String(pid, HEX)
                                : String("-")));
    }
    s_detMipi = mipi;
    s_detUsb = usb;
    s_detUsbVid = vid;
    s_detUsbPid = pid;
    s_detAt = now;
    s_detDone = true;
    s_detecting = false;
#endif
}

bool cameraP4MipiPresent() {
#if !P4_HAVE_ESP_VIDEO
    return false;
#else
    return s_detMipi;
#endif
}

bool cameraP4UsbPresent() {
#if !P4_HAVE_ESP_VIDEO
    return false;
#else
    return s_detUsb;
#endif
}

uint16_t cameraP4UsbVid() {
#if !P4_HAVE_ESP_VIDEO
    return 0;
#else
    return s_detUsbVid;
#endif
}

uint16_t cameraP4UsbPid() {
#if !P4_HAVE_ESP_VIDEO
    return 0;
#else
    return s_detUsbPid;
#endif
}

// Wait for frames: 1 = MJPEG in slot, 2 = non-MJPEG counted (YUY2/etc.),
// 0 = nothing. Polls counters directly (no s_ready dependency).
static int p4UsbWaitFrames(uint32_t timeoutMs) {
    uint32_t gen0 = s_usbGen, cnt0 = s_usbFrameCount;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        if (s_usbCancel) return 0;
        if (s_usbGone) return 0;
        if (s_usbGen != gen0 && s_usbLen > 0) return 1;
        if (s_usbFrameCount != cnt0) return 2;
        delay(10);
    }
    return (s_usbGen != gen0 && s_usbLen > 0) ? 1 : (s_usbFrameCount != cnt0 ? 2 : 0);
}

static bool p4UsbBegin(uint16_t w, uint16_t h) {
    // USB Host lib must already be up (usb_host_manager, ENABLE_USB_HOST=1).
    // Bus scan + descriptor dump first: the UVC driver is not installed yet,
    // so the temporary probe client has the whole USB heap to itself.
    s_usbCancel = false;  // fresh begin clears a previous stop's cancel
    p4UsbScanBus();

    uvc_host_driver_config_t drv = {};
    drv.driver_task_stack_size = 4096;
    drv.driver_task_priority = 5;
    drv.xCoreID = 1;
    drv.create_background_task = true;
    drv.event_cb = p4UvcDriverEventCb;
    if (uvc_host_install(&drv) != ESP_OK) {
        p4CamLog("[CAM-P4/USB] uvc_host_install failed", ERROR);
        return false;
    }
    s_uvcInstalled = true;

    if (!s_usbMux) s_usbMux = xSemaphoreCreateMutex();

    // Resolve the UVC address/function and harvest the offered frames now
    // (the frame-list query only works while no stream holds the device).
    p4UsbHarvestFrames();

    uvc_host_stream_config_t sc = {};
    sc.event_cb = p4UvcEventCb;
    sc.frame_cb = p4UvcFrameCb;
    sc.usb.dev_addr = s_uvcDevAddr;  // 0 = any (unknown)
    sc.usb.vid = 0;
    sc.usb.pid = 0;
    sc.usb.uvc_stream_index = s_uvcStreamIdx;
    sc.vs_format.h_res = w;
    sc.vs_format.v_res = h;
    sc.vs_format.fps = 0;  // device default
    sc.vs_format.format = UVC_VS_FORMAT_MJPEG;
    sc.advanced.number_of_frame_buffers = 2;
    // Explicit frame size: some UVC descriptors report a bogus/oversized
    // dwMaxVideoFrameSize, and frame_size=0 (use negotiation) then makes the
    // driver try to allocate huge buffers → uvc_host_stream_open returns
    // ESP_ERR_NO_MEM. 2 MB is comfortably above any 720p/1080p MJPEG frame.
    sc.advanced.frame_size = 2UL * 1024UL * 1024UL;
    sc.advanced.frame_heap_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    // The 4x-MPS default URB (e.g. 3x4 KB) cannot saturate a HS isoch pipe,
    // but big URB sets eat the internal DMA heap (URBs are not PSRAM-capable
    // here) and trip the heap-pressure watchdog. 4x8 KB = 32 KB is the
    // balance: ~2 MB/s of isoch headroom, modest DRAM cost.
    sc.advanced.number_of_urbs = 4;
    sc.advanced.urb_size = 8UL * 1024UL;
    sc.advanced.user_frame_buffers = nullptr;

    uvc_host_stream_hdl_t hdl = nullptr;
    uvc_host_stream_format_t cur = {};
    s_usbNegW = 0; s_usbNegH = 0; s_usbNegFps = 0;
    bool streaming = false;
    // Iterate every stream index the device exposes: some webcams leave the
    // first VS function idle and only feed the next one. Without discovery
    // (no address) there is nothing to iterate: only the default index.
    uint8_t idxMax = s_uvcStreamMask ? P4_USB_MAX_STREAMS : 1;
    for (uint8_t idx = 0; idx < idxMax && !streaming && !s_usbCancel; idx++) {
        if (s_uvcStreamMask && !(s_uvcStreamMask & (1u << idx))) continue;
        sc.usb.uvc_stream_index = idx;
        esp_err_t se = ESP_FAIL;
        // Retry: a stale claim right after stop/start can give transient NOT_FOUND.
        for (int attempt = 0; attempt < P4_USB_OPEN_RETRIES; attempt++) {
            if (attempt > 0) delay(P4_USB_OPEN_RETRY_MS);
            se = uvc_host_stream_open(&sc, 5000 / portTICK_PERIOD_MS, &hdl);
            if (se == ESP_OK) break;
            p4CamLog(String("[CAM-P4/USB] stream_open (idx ") + String(idx) + ") try " +
                     String(attempt + 1) + " failed err=" + String(esp_err_to_name(se)), WARN);
        }
        if (se != ESP_OK) continue;  // next index
        // Candidate sizes: requested first, then the device's own MJPEG
        // entries (harvested during discovery, before the claim), then VGA
        // as a universal last resort.
        uint16_t cands[P4_USB_MAX_CANDS][2] = {};
        int nCands = 0;
        cands[nCands][0] = w; cands[nCands][1] = h; nCands++;
        for (int i = 0; i < s_uvcMjpegN[idx] && nCands < P4_USB_MAX_CANDS; i++) {
            bool dup = false;
            for (int j = 0; j < nCands; j++) {
                if (cands[j][0] == s_uvcMjpeg[idx][i][0] && cands[j][1] == s_uvcMjpeg[idx][i][1]) { dup = true; break; }
            }
            if (!dup) {
                cands[nCands][0] = s_uvcMjpeg[idx][i][0];
                cands[nCands][1] = s_uvcMjpeg[idx][i][1];
                nCands++;
            }
        }
        if (nCands < P4_USB_MAX_CANDS) {
            bool hasVga = false;
            for (int j = 0; j < nCands; j++) {
                if (cands[j][0] == 640 && cands[j][1] == 480) { hasVga = true; break; }
            }
            if (!hasVga) { cands[nCands][0] = 640; cands[nCands][1] = 480; nCands++; }
        }
        for (int c = 0; c < nCands && !s_usbCancel; c++) {
            uvc_host_stream_format_t want = {};
            want.h_res = cands[c][0]; want.v_res = cands[c][1];
            want.fps = 0;  // device default
            want.format = UVC_VS_FORMAT_MJPEG;
            if (uvc_host_stream_format_select(hdl, &want) != ESP_OK) {
                p4CamLog(String("[CAM-P4/USB] candidate ") + String(cands[c][0]) + "x" + String(cands[c][1]) + " rejected by device", WARN);
                continue;
            }
            s_usbFrameCount = 0;
            if (uvc_host_stream_start(hdl) != ESP_OK) {
                p4CamLog(String("[CAM-P4/USB] stream_start failed @") + String(cands[c][0]) + "x" + String(cands[c][1]), ERROR);
                continue;
            }
            p4CamLog(String("[CAM-P4/USB] [idx ") + String(idx) + "] trying " + String(cands[c][0]) +
                     "x" + String(cands[c][1]) + " MJPEG...");
            int got = p4UsbWaitFrames(P4_USB_CAND_WAIT_MS);
            if (got == 1) {
                streaming = true;
                s_uvcStreamIdx = idx;
                break;
            }
            if (got == 2) {
                p4CamLog(String("[CAM-P4/USB] non-MJPEG data @") + String(cands[c][0]) + "x" + String(cands[c][1]) + " (pump serves MJPEG only)", WARN);
            } else {
                p4CamLog(String("[CAM-P4/USB] no data @") + String(cands[c][0]) + "x" + String(cands[c][1]), WARN);
            }
            uvc_host_stream_format_t after = {};
            if (uvc_host_stream_format_get(hdl, &after) == ESP_OK) {
                p4CamLog(String("[CAM-P4/USB] after start the device holds ") + String(after.h_res) + "x" +
                         String(after.v_res) + " fmt=" + String((int)after.format));
            }
            uvc_host_stream_stop(hdl);
        }
        if (!streaming) {
            uvc_host_stream_close(hdl);
            hdl = nullptr;
        }
    }
    if (s_usbCancel) {
        p4CamLog("[CAM-P4/USB] begin cancelled by stop", WARN);
        if (hdl) { uvc_host_stream_close(hdl); hdl = nullptr; }
        if (s_uvcInstalled) { uvc_host_uninstall(); s_uvcInstalled = false; }
        return false;
    }
    if (!streaming) {
        // YUY2 data-path probe (diagnostic): proves dead-device vs wrong-format.
        p4CamLog("[CAM-P4/USB] probing YUY2 640x480 (data-path diagnostic)", WARN);
        uvc_host_stream_config_t sc2 = sc;
        sc2.usb.uvc_stream_index = s_uvcStreamIdx;
        sc2.vs_format.h_res = 640; sc2.vs_format.v_res = 480;
        sc2.vs_format.fps = 0;
        sc2.vs_format.format = UVC_VS_FORMAT_YUY2;
        uvc_host_stream_hdl_t hdl2 = nullptr;
        esp_err_t e2 = uvc_host_stream_open(&sc2, 5000 / portTICK_PERIOD_MS, &hdl2);
        if (e2 == ESP_OK) {
            // Full descriptor dump (endpoints, alternate settings, frame
            // descriptors) to the console/UART - decisive if the data-path
            // probes stay silent.
            p4CamLog("[CAM-P4/USB] full USB descriptors on the console:", WARN);
            uvc_host_desc_print(hdl2);
            s_usbFrameCount = 0;
            if (uvc_host_stream_start(hdl2) == ESP_OK) {
                int got = p4UsbWaitFrames(P4_USB_CAND_WAIT_MS);
                if (got != 0) {
                    p4CamLog("[CAM-P4/USB] webcam streams YUY2-only (MJPEG delivers no data); needs SW JPEG encode path (future work)", ERROR);
                } else {
                    p4CamLog("[CAM-P4/USB] YUY2 probe: no data either (device is not sending on this interface)", WARN);
                }
                uvc_host_stream_stop(hdl2);
            } else {
                p4CamLog("[CAM-P4/USB] YUY2 probe: stream_start failed", WARN);
            }
            uvc_host_stream_close(hdl2);
        } else {
            p4CamLog(String("[CAM-P4/USB] YUY2 probe: stream_open rejected err=") + String(esp_err_to_name(e2)), WARN);
        }
        p4CamLog("[CAM-P4/USB] no MJPEG candidate delivered frames", ERROR);
        uvc_host_uninstall();
        s_uvcInstalled = false;
        return false;
    }
    if (uvc_host_stream_format_get(hdl, &cur) == ESP_OK && cur.format == UVC_VS_FORMAT_MJPEG) {
        // Publish the NEGOTIATED geometry as the live size (page + recorder
        // must follow the device, not the request).
        s_usbNegW = cur.h_res;
        s_usbNegH = cur.v_res;
        s_usbNegFps = cur.fps > 0.0f ? (uint8_t)cur.fps : 30;
        s_w = cur.h_res;
        s_h = cur.v_res;
    } else {
        s_w = w; s_h = h;
    }
    s_uvcStream = hdl;
    s_usbGone = false;
    p4CamLog(String("[CAM-P4/USB] streaming ") + String(s_w) + "x" + String(s_h) + " MJPEG");
    return true;
}

static void p4UsbEnd() {
    if (s_uvcStream) {
        uvc_host_stream_stop(s_uvcStream);
        uvc_host_stream_close(s_uvcStream);
        s_uvcStream = nullptr;
    }
    if (s_uvcInstalled) {
        uvc_host_uninstall();
        s_uvcInstalled = false;
    }
}
#endif // P4_HAVE_UVC

const char* cameraP4ProbeCaps() {
#if !P4_HAVE_ESP_VIDEO
    return "ESP_Video.h absent (need Arduino-ESP32 >= 3.3.11)";
#else
    static char caps[128];
#if CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
    const char* mipi = "MIPI=on";
#else
    const char* mipi = "MIPI=off";
#endif
#if CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE
    const char* dvp = "DVP=on";
#else
    const char* dvp = "DVP=off";
#endif
    snprintf(caps, sizeof(caps), "ESP_Video present, %s %s IDF=%s", mipi, dvp,
             esp_get_idf_version());
    return caps;
#endif
}

#if P4_HAVE_ESP_VIDEO
// ---- MIPI-CSI bring-up diagnostics (reach the Web Serial Monitor) ----------
// The IDF/Arduino error logs inside ESP_Video are compiled out (CORE_DEBUG_LEVEL=0),
// so the exact reason a capture device fails to open is otherwise invisible.
// These helpers surface it: which /dev/video* nodes esp_video_init registered
// (none => sensor not detected on SCCB), and the exact errno when opening the
// MIPI-CSI device.
static String p4ListVideoNodes() {
    String list;
    DIR* d = opendir("/dev");
    if (!d) return String("(no /dev)");
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (strncmp(e->d_name, "video", 5) == 0) {
            if (list.length()) list += ",";
            list += e->d_name;
        }
    }
    closedir(d);
    return list.length() ? list : String("(none)");
}

// Probe the OV5647 SCCB address (0x36) BEFORE esp_video_init claims the bus.
// Wire.end() releases port 0 so ESP_Video can create its own I2C master after.
static void p4ProbeSccb() {
    if (!Wire.begin(CAM_PIN_SIOD, CAM_PIN_SIOC)) {
        p4CamLog("[CAM-P4/CSI] SCCB probe: Wire.begin failed", WARN);
        return;
    }
    Wire.beginTransmission(0x36);
    uint8_t err = Wire.endTransmission();
    Wire.end();
    p4CamLog(String("[CAM-P4/CSI] SCCB 0x36 (OV5647) -> ") + (err == 0 ? "ACK" : "no ACK")
             + " code=" + String(err), err == 0 ? INFO : WARN);
}

// One-shot-per-begin diagnostic: list every pixelformat the given video node
// offers. Tells us exactly where JPEG lives (CSI video0 vs ISP video1).
// Frame-size enumeration diagnostic: for a given (node, fourcc), list every
// size the driver accepts. Decisive for JPEG: if any size supports it, S_FMT
// with that size+JPEG can work even though ENUM_FMT hides it.
static void p4EnumFrameSizes(const char *path, uint32_t fourcc, const char *name) {
    int fd = ::open(path, O_RDWR);
    if (fd < 0) return;
    for (uint32_t i = 0; i < 32; i++) {
        struct v4l2_frmsizeenum f = {};
        f.index = i;
        f.pixel_format = fourcc;
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &f) != 0) {
            if (i == 0) {
                p4CamLog(String("[CAM-P4/FRM] ") + path + " '" + name + "': no sizes (errno=" + String(errno) + ")", INFO);
            }
            break;
        }
        if (f.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            p4CamLog(String("[CAM-P4/FRM] ") + path + " '" + name + "' [" + String(i) + "] "
                     + String(f.discrete.width) + "x" + String(f.discrete.height), INFO);
        } else {
            p4CamLog(String("[CAM-P4/FRM] ") + path + " '" + name + "' [" + String(i) + "] stepwise "
                     + String(f.stepwise.min_width) + "x" + String(f.stepwise.min_height)
                     + ".." + String(f.stepwise.max_width) + "x" + String(f.stepwise.max_height), INFO);
        }
    }
    ::close(fd);
}

static void p4EnumFormats(const char *path) {
    int fd = ::open(path, O_RDWR);
    if (fd < 0) {
        p4CamLog(String("[CAM-P4/FMT] ") + path + " open failed errno=" + String(errno), WARN);
        return;
    }
    for (uint32_t i = 0; i < 16; i++) {
        struct v4l2_fmtdesc d = {};
        d.index = i;
        d.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &d) != 0) break;
        char cc[5];
        cc[0] = (char)(d.pixelformat & 0xFF);
        cc[1] = (char)((d.pixelformat >> 8) & 0xFF);
        cc[2] = (char)((d.pixelformat >> 16) & 0xFF);
        cc[3] = (char)((d.pixelformat >> 24) & 0xFF);
        cc[4] = 0;
        d.description[31] = 0;
        p4CamLog(String("[CAM-P4/FMT] ") + path + " [" + String(i) + "] '" + cc
                 + "' " + (const char *)d.description, INFO);
    }
    ::close(fd);
}

// Size-switch probe: the video node advertises the ACTIVE sensor mode only.
// Plain S_FMT never reprograms the sensor (upstream video_common rejects any
// size != current with EINVAL) — runtime switches go through
// VIDIOC_S_SENSOR_FMT (p4SetSensorFormat). This probe only records which
// sizes the CURRENT pipeline accepts.
static bool p4TrySizeAck(uint32_t w, uint32_t h) {
    int fd = ::open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (fd < 0) return false;
    struct v4l2_format f = {};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    f.fmt.pix.width = w;
    f.fmt.pix.height = h;
    f.fmt.pix.pixelformat = v4l2_fourcc('U','Y','V','Y');
    int r = ioctl(fd, VIDIOC_S_FMT, &f);
    bool ok = (r == 0 && f.fmt.pix.width == w && f.fmt.pix.height == h);
    p4CamLog(String("[CAM-P4/SFMT] try ") + String(w) + "x" + String(h) + " UYVY -> ret=" + String(r)
             + (r == 0 ? String(" now ") + String(f.fmt.pix.width) + "x" + String(f.fmt.pix.height) : String(" errno=") + String(errno)),
             ok ? INFO : WARN);
    ::close(fd);
    return ok;
}

static void p4TrySize(uint32_t w, uint32_t h) {
    (void)p4TrySizeAck(w, h);
}

static void p4ProbeCsiModes() {
    s_csiModeAck = 0;
    for (uint8_t i = 0; i < P4_CSI_MODE_COUNT; i++) {
        if (p4TrySizeAck(kP4CsiModes[i].w, kP4CsiModes[i].h)) s_csiModeAck |= (uint8_t)(1u << i);
    }
    // Restore the default flow size (last probe may have switched the sensor).
    p4TrySizeAck(800, 800);
}

// Index into kP4CsiModes, or -1 when the size is not a known sensor mode.
static int8_t p4CsiModeIndex(uint16_t w, uint16_t h) {
    for (uint8_t i = 0; i < P4_CSI_MODE_COUNT; i++) {
        if (kP4CsiModes[i].w == w && kP4CsiModes[i].h == h) return (int8_t)i;
    }
    return -1;
}

bool cameraP4ValidCsiMode(uint16_t w, uint16_t h) {
    return p4CsiModeIndex(w, h) >= 0;
}

// Exported: "WxH,WxH,..." of the known CSI sensor modes for resList, ordered
// by pixel count (largest first). Modes refused by the driver are still
// offered: selecting one runs the SCCB-direct switch with revert-on-failure.
//
// Ordering is by area, not table order, for a concrete reason: 1280x720 uses
// the experimental RAW10 profile that stalls the pipeline (CAM-62), while
// 800x1280 (1.02 MP) and 1280x960 (1.23 MP) are RAW8/binning modes that
// stream cleanly. Sorting by megapixels puts those ahead of 720p, so the
// default pick and the top of the list are modes that actually work.
String cameraP4CsiResList() {
    uint8_t order[P4_CSI_MODE_COUNT];
    for (uint8_t i = 0; i < P4_CSI_MODE_COUNT; i++) order[i] = i;
    // Insertion sort on (uint32) area — P4_CSI_MODE_COUNT is single digits.
    for (uint8_t i = 1; i < P4_CSI_MODE_COUNT; i++) {
        uint8_t j = i;
        while (j > 0) {
            uint32_t a = (uint32_t)kP4CsiModes[order[j - 1]].w * kP4CsiModes[order[j - 1]].h;
            uint32_t b = (uint32_t)kP4CsiModes[order[j]].w * kP4CsiModes[order[j]].h;
            if (a >= b) break;
            uint8_t t = order[j - 1]; order[j - 1] = order[j]; order[j] = t;
            j--;
        }
    }
    String out;
    for (uint8_t i = 0; i < P4_CSI_MODE_COUNT; i++) {
        if (out.length()) out += ',';
        out += String(kP4CsiModes[order[i]].w) + "x" + String(kP4CsiModes[order[i]].h);
    }
    return out;
}

// ---- Official sensor-mode switch via VIDIOC_S_SENSOR_FMT (CAM_P4 only) ----
// Upstream esp_video proof (video_device_common.c): plain S_FMT on the
// capture node can only REAFFIRM the current size (anything else -> EINVAL).
// The supported runtime switch is VIDIOC_S_SENSOR_FMT with a full
// esp_cam_sensor_format_t: esp_video rewrites the sensor registers AND
// rebuilds the CSI/ISP pipeline + stream buffers for the new size.
// The vendored shadow driver offers five OV5647 modes; this app adds a
// 1280x720 candidate profile. Register arrays come from ov5647_settings.h.
// Keep custom sensor-format descriptors and ISP metadata in static storage:
// the sensor driver retains the format pointer after set_format(), and
// ESP_Video later dereferences format->isp_info while starting CSI. The old
// stack-local descriptors caused CamInit's LoadProhibited at NULL+0x20 in
// csi_get_input_bayer_order.
#include "esp_video_ioctl.h"  // VIDIOC_S_SENSOR_FMT
#include "../sensor_ov5647/ov5647_settings.h"  // mode tables (moved up: needed below)

struct P4SensorFmtDef {
    uint16_t w, h;
    esp_cam_sensor_output_format_t pixfmt;
    const ov5647_reginfo_t *regs;
    int regs_size;
    uint8_t fps;
    int pclk, hts, vts;
    uint32_t mipi_clk;
    const char *name;
};
static const P4SensorFmtDef kP4SensorFmts[] = {
    // Fase 3 (Tarefa 5, DERIVED profile — facts only, zero GPL/ArduCAM bytes):
    // 2592x1944 stills. Scalars from our own bring-up notes: pixel clock
    // 87.5 MHz, hts 2844, vts 1968 (~15.6 fps), 2-lane MIPI link 218.75 MHz,
    // RAW10 full-res Bayer, 24 MHz xclk like every other entry. regs is
    // NULL until a register profile is derived on the .18 with the datasheet
    // (Linux ov5647.c is GPL-2.0 — prohibited; ArduCAM tables are DVP, not
    // MIPI) — p4SetSensorFormat() refuses null-regs entries, so the mode
    // lists for selection but always reverts until then. Stills-only: video
    // paths stay <=1920 wide (H.264 HW ceiling + 2 MB stream cache).
    {2592, 1944, ESP_CAM_SENSOR_PIXFORMAT_RAW10,
     nullptr, 0,
     15, 87500000, 2844, 1968, 218750000,
     "MIPI_2lane_24Minput_RAW10_2592x1944_derived"},
    {1920, 1080, ESP_CAM_SENSOR_PIXFORMAT_RAW10,
     ov5647_mipi_2lane_24Minput_1920x1080_raw10_30fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_1920x1080_raw10_30fps) / sizeof(ov5647_reginfo_t)) - 1,
     30, 81666700, 2416, 1104, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_1920x1080_30FPS,
     "MIPI_2lane_24Minput_RAW10_1920x1080_30fps"},
    {1280, 960, ESP_CAM_SENSOR_PIXFORMAT_RAW10,
     ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps) / sizeof(ov5647_reginfo_t)) - 1,
     45, 88333333, 1093, 1796, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_1280x960_45FPS,
     "MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps"},
    {1280, 720, ESP_CAM_SENSOR_PIXFORMAT_RAW10,
     ov5647_mipi_2lane_24Minput_1280x720_raw10_30fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_1280x720_raw10_30fps) / sizeof(ov5647_reginfo_t)) - 1,
     30, 88333333, 1796, 1640, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_1280x720_30FPS,
      "MIPI_2lane_24Minput_RAW10_1280x720_30fps"},
    {800, 1280, ESP_CAM_SENSOR_PIXFORMAT_RAW8,
     ov5647_mipi_2lane_24Minput_800x1280_raw8_50fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_800x1280_raw8_50fps) / sizeof(ov5647_reginfo_t)) - 1,
     50, 81666700, 984, 1896, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_800x1280_50FPS,
     "MIPI_2lane_24Minput_RAW8_800x1280_50fps"},
    {800, 800, ESP_CAM_SENSOR_PIXFORMAT_RAW8,
     ov5647_mipi_2lane_24Minput_800x800_raw8_50fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_800x800_raw8_50fps) / sizeof(ov5647_reginfo_t)) - 1,
     50, 81666700, 984, 1896, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_800x800_50FPS,
     "MIPI_2lane_24Minput_RAW8_800x800_50fps"},
    {800, 640, ESP_CAM_SENSOR_PIXFORMAT_RAW8,
     ov5647_mipi_2lane_24Minput_800x640_raw8_50fps,
     (int)(sizeof(ov5647_mipi_2lane_24Minput_800x640_raw8_50fps) / sizeof(ov5647_reginfo_t)) - 1,
     50, 81666700, 984, 1896, (uint32_t)OV5647_MIPI_CSI_LINE_RATE_800x640_50FPS,
     "MIPI_2lane_24Minput_RAW8_800x640_50fps"},
};
static const P4SensorFmtDef* p4SensorFmtFor(uint16_t w, uint16_t h) {
    for (uint8_t i = 0; i < sizeof(kP4SensorFmts) / sizeof(kP4SensorFmts[0]); i++) {
        if (kP4SensorFmts[i].w == w && kP4SensorFmts[i].h == h) return &kP4SensorFmts[i];
    }
    return nullptr;
}
static const esp_cam_sensor_isp_info_t kP4CustomIspInfo[] = {
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 87500000, 2844, 1968, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},  // Fase 3 5MP derived (inert until regs land)
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 81666700, 2416, 1104, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 88333333, 1093, 1796, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 88333333, 1796, 1640, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 81666700, 984, 1896, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 81666700, 984, 1896, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
    {{SENSOR_ISP_INFO_VERSION_DEFAULT, 81666700, 984, 1896, 0, 0, 0, ESP_CAM_SENSOR_BAYER_GBRG}},
};
// All per-mode tables must stay in lockstep: the lazy init below indexes the
// static format/ISP arrays by kP4SensorFmts position.
static_assert(sizeof(kP4SensorFmts) / sizeof(kP4SensorFmts[0]) == P4_CSI_MODE_COUNT,
              "kP4SensorFmts must have one entry per P4 CSI mode");
static_assert(sizeof(kP4CustomIspInfo) / sizeof(kP4CustomIspInfo[0]) == P4_CSI_MODE_COUNT,
              "kP4CustomIspInfo must have one entry per P4 CSI mode");
static esp_cam_sensor_format_t kP4CustomSensorFormats[P4_CSI_MODE_COUNT] = {};
static bool s_p4CustomSensorFormatsInitialized = false;

static esp_cam_sensor_format_t* p4PersistentSensorFmtFor(const P4SensorFmtDef* d) {
    if (!d) return nullptr;
    if (!s_p4CustomSensorFormatsInitialized) {
        for (size_t i = 0; i < sizeof(kP4SensorFmts) / sizeof(kP4SensorFmts[0]); i++) {
            const P4SensorFmtDef& def = kP4SensorFmts[i];
            esp_cam_sensor_format_t& fmt = kP4CustomSensorFormats[i];
            fmt.name = def.name;
            fmt.format = def.pixfmt;
            fmt.port = ESP_CAM_SENSOR_MIPI_CSI;
            fmt.xclk = 24000000;
            fmt.width = def.w;
            fmt.height = def.h;
            fmt.regs = def.regs;
            fmt.regs_size = def.regs_size;
            fmt.fps = def.fps;
            fmt.isp_info = &kP4CustomIspInfo[i];
            fmt.mipi_info.mipi_clk = def.mipi_clk;
            fmt.mipi_info.hs_settle = 0;
            fmt.mipi_info.lane_num = 2;
            fmt.mipi_info.line_sync_en = true;
            fmt.reserved = nullptr;
        }
        s_p4CustomSensorFormatsInitialized = true;
    }
    return &kP4CustomSensorFormats[(size_t)(d - kP4SensorFmts)];
}
// Programs the sensor + rebuilds CSI/ISP for w/h. Capture must be stopped
// (all callers close s_cap first). The descriptor and ISP pointers passed to
// VIDIOC_S_SENSOR_FMT are persistent because esp_cam_sensor retains them.
// Returns true when esp_video accepted it.
static bool p4SetSensorFormat(uint16_t w, uint16_t h) {
    const P4SensorFmtDef *d = p4SensorFmtFor(w, h);
    if (!d) return false;
    // Fase 3: entries without a register profile (5MP derived until HW work
    // lands) refuse BEFORE touching the bus — the caller reverts to the
    // previous mode, never a half-programmed sensor.
    if (!d->regs || d->regs_size <= 0) {
        p4CamLog(String("[CAM-P4/SENSOR-FMT] ") + String(w) + "x" + String(h)
                 + " has no register profile yet (HW homework) — revert", WARN);
        return false;
    }
    esp_cam_sensor_format_t *fmt = p4PersistentSensorFmtFor(d);
    if (!fmt) return false;
    int fd = ::open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        p4CamLog(String("[CAM-P4/SENSOR-FMT] open failed errno=") + String(errno), ERROR);
        return false;
    }
    int r = ioctl(fd, VIDIOC_S_SENSOR_FMT, fmt);
    int e = errno;
    ::close(fd);
    p4CamLog(String("[CAM-P4/SENSOR-FMT] ") + String(w) + "x" + String(h)
             + (r == 0 ? " accepted" : String(" rejected errno=") + String(e)),
             r == 0 ? INFO : WARN);
    return r == 0;
}
// ---- SCCB-direct sensor mode switch (CAM_P4 last resort) -------------------
// Kept as fallback when VIDIOC_S_SENSOR_FMT is refused: rewrites the sensor
// mode registers straight over SCCB (same tables the vendored driver uses),
// then S_FMTs the new size. Display shares the SCCB bus: a switch may glitch
// one display frame. Revert (previous table) runs on any failure.

#define P4_SCCB_ADDR 0x36
static bool p4SccbWrite(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(P4_SCCB_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(val);
    return Wire.endTransmission() == 0;
}
static bool p4SccbWriteTable(const ov5647_reginfo_t* t, const char* tag) {
    int n = 0;
    for (int i = 0; t[i].reg != OV5647_REG_END; i++) {
        if (t[i].reg == OV5647_REG_DELAY) { delay(t[i].val); continue; }
        if (!p4SccbWrite(t[i].reg, t[i].val)) {
            p4CamLog(String("[CAM-P4/SCCB] ") + tag + " fail reg=0x"
                     + String(t[i].reg, HEX) + " idx=" + String(i), ERROR);
            return false;
        }
        n++;
    }
    p4CamLog(String("[CAM-P4/SCCB] ") + tag + " " + String(n) + " regs OK", INFO);
    return true;
}
static const ov5647_reginfo_t* p4SccbTableFor(uint16_t w, uint16_t h) {
    if (w == 1920 && h == 1080) return ov5647_mipi_2lane_24Minput_1920x1080_raw10_30fps;
    if (w == 1280 && h == 960) return ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps;
    if (w == 1280 && h == 720) return ov5647_mipi_2lane_24Minput_1280x720_raw10_30fps;
    if (w == 800 && h == 1280) return ov5647_mipi_2lane_24Minput_800x1280_raw8_50fps;
    if (w == 800 && h == 800) return ov5647_mipi_2lane_24Minput_800x800_raw8_50fps;
    if (w == 800 && h == 640) return ov5647_mipi_2lane_24Minput_800x640_raw8_50fps;
    return nullptr;
}
static bool p4SccbSwitchMode(uint16_t w, uint16_t h) {
    const ov5647_reginfo_t* t = p4SccbTableFor(w, h);
    if (!t) return false;
    char tag[24];
    snprintf(tag, sizeof(tag), "mode %dx%d", w, h);
    if (!Wire.begin(CAM_PIN_SIOD, CAM_PIN_SIOC)) {
        p4CamLog("[CAM-P4/SCCB] Wire.begin failed", ERROR);
        return false;
    }
    bool ok = p4SccbWriteTable(ov5647_mipi_reset_regs, "reset")
           && p4SccbWriteTable(t, tag);
    Wire.end();
    return ok;
}

static bool p4OpenCaptureAt(uint16_t w, uint16_t h) {
    // Strict open: close + S_FMT exact + begin + UYVY + start. True only when
    // the pipeline actually runs at w/h (used for mode switches).
    if (s_capOpen) {
        s_cap.stopCapture();
        s_cap.end();
        s_capOpen = false;
    }
    if (!p4TrySizeAck(w, h)) return false;
    if (!s_cap.begin(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, 2)) {
        p4CamLog("[CAM-P4/CSI] capture device open failed", ERROR);
        return false;
    }
    if (!s_cap.setFormat(ESP_VIDEO_FORMAT_YUV422_UYVY)) {
        p4CamLog("[CAM-P4/CSI] setFormat(UYVY) failed", ERROR);
        s_cap.end();
        return false;
    }
    if (!s_cap.startCapture()) {
        p4CamLog("[CAM-P4/CSI] startCapture failed", ERROR);
        s_cap.end();
        return false;
    }
    s_w = s_cap.getWidth();
    s_h = s_cap.getHeight();
    if (s_w != w || s_h != h) {
        p4CamLog(String("[CAM-P4/CSI] clamp ") + String(w) + "x" + String(h)
                 + " -> " + String(s_w) + "x" + String(s_h), WARN);
        s_cap.stopCapture();
        s_cap.end();
        return false;
    }
    p4CamLog(String("[CAM-P4/CSI] streaming UYVY ") + String(s_w) + "x" + String(s_h), INFO);
    // Bound every future dequeue inside the driver (CAM-62): without this,
    // DQBUF blocks forever on a stalled sensor and wedges the calling task.
    // The ioctl targets the capture node itself, so it covers the fh that
    // s_cap holds too (same device object). If this core's esp_video predates
    // VIDIOC_S_DQBUF_TIMEOUT the guard is compiled out and the log below says
    // so — then only the heartbeat fast-fail + session re-sync protect us.
    s_dqbufTimeoutOn = false;
#ifdef VIDIOC_S_DQBUF_TIMEOUT
    {
        int tfd = ::open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
        if (tfd >= 0) {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 500000;
            int r = ioctl(tfd, VIDIOC_S_DQBUF_TIMEOUT, &tv);
            int e = errno;
            ::close(tfd);
            s_dqbufTimeoutOn = (r == 0);
            if (s_dqbufTimeoutOn) {
                p4CamLog("[CAM-P4/CSI] DQBUF timeout 1.5s ON", INFO);
            } else {
                p4CamLog(String("[CAM-P4/CSI] DQBUF timeout ioctl failed errno=") + e, WARN);
            }
        } else {
            p4CamLog(String("[CAM-P4/CSI] DQBUF timeout: open failed errno=") + errno, WARN);
        }
    }
#else
    p4CamLog("[CAM-P4/CSI] VIDIOC_S_DQBUF_TIMEOUT unsupported by this core (capture can block)", WARN);
#endif
    s_capOpen = true;
    return true;
}

static bool p4OpenCapture(uint16_t w, uint16_t h) {
    // Lenient boot-path open: exact size, else 800x800 fallback (legacy
    // framesize-enum requests land here). Mode discovery (p4ProbeCsiModes)
    // runs separately at begin, never inside a switch.
    if (s_capOpen) {
        s_cap.stopCapture();
        s_cap.end();
        s_capOpen = false;
    }
    p4CamLog(String("[CAM-P4/CSI] /dev video nodes: ") + p4ListVideoNodes());
    {
        int tfd = ::open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
        int terr = errno;
        p4CamLog(String("[CAM-P4/CSI] open ") + ESP_VIDEO_MIPI_CSI_DEVICE_NAME + " -> fd=" + String(tfd)
                 + (tfd < 0 ? String(" errno=") + String(terr) : String()), tfd < 0 ? WARN : INFO);
        if (tfd >= 0) ::close(tfd);
    }
    p4EnumFormats(ESP_VIDEO_MIPI_CSI_DEVICE_NAME);   // /dev/video0
    // Size map per pixelformat (HD override driver holds 640/800/1280/1080p/
    // 960 sensor modes): ENUM_FMT lists fourccs only, FRMSIZES reveals which
    // sizes the CSI+ISP pipeline actually accepts per format.
    p4EnumFrameSizes(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, v4l2_fourcc('B','A','8','1'), "BA81");
    p4EnumFrameSizes(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, v4l2_fourcc('R','G','B','P'), "RGBP");
    p4EnumFrameSizes(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, v4l2_fourcc('U','Y','V','Y'), "UYVY");
    p4EnumFrameSizes(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, v4l2_fourcc('J','P','E','G'), "JPEG");
    if (p4OpenCaptureAt(w, h)) return true;
    // Official switch: reprogram sensor + rebuild CSI/ISP, then open.
    if ((w != 800 || h != 800) && p4SetSensorFormat(w, h) && p4OpenCaptureAt(w, h)) {
        p4CamLog(String("[CAM-P4/CSI] sensor-fmt switch OK ") + String(w) + "x" + String(h), INFO);
        return true;
    }
    if (w == 800 && h == 800) return false;
    p4CamLog(String("[CAM-P4/CSI] falling back to 800x800"), WARN);
    return p4OpenCaptureAt(800, 800);
}
#endif

bool cameraP4Begin(uint16_t w, uint16_t h, uint8_t quality, uint8_t source) {
#if !P4_HAVE_ESP_VIDEO
    (void)w; (void)h; (void)quality; (void)source;
    return false;
#else
    if (s_ready) return true;
    P4BeginGuard beginGuard;  // gate the Wire presence probe off I2C0 (all returns covered)
    // A status-poll probe may be mid-flight on I2C0: wait (bounded) for it to
    // release the bus before ESP_Video claims it. Same task never nests, so no
    // deadlock; 2 s cap, then proceed (begin logs loudly on bus failure).
    for (uint8_t i = 0; i < 200 && s_detecting; i++) delay(10);
    s_source = (source == CAM_P4_SRC_USB) ? CAM_P4_SRC_USB : CAM_P4_SRC_MIPI;
    p4CamLog(String("[CAM-P4] begin source=") + (s_source == CAM_P4_SRC_USB ? "USB" : "MIPI")
             + " " + String(w) + "x" + String(h) + " | caps: " + cameraP4ProbeCaps());

#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) {
        s_w = w; s_h = h;
        s_quality = quality ? quality : 12;
        if (!p4UsbBegin(w, h)) return false;
        // Prime check: p4UsbBegin already proved MJPEG frames on the winning
        // candidate; a short confirm guards against instant disconnect.
        if (cameraP4Capture(nullptr, nullptr, 2000) == 0) {
            p4CamLog("[CAM-P4/USB] lost stream right after begin", ERROR);
            p4UsbEnd();
            return false;
        }
        s_ready = true;
        return true;
    }
#endif

    ESPVideoCamConfigClass camCfg;
    // SCCB bus: SDA=CAM_PIN_SIOD, SCL=CAM_PIN_SIOC (shared I2C, display bus).
    // begin(port, scl, sda): creates the master bus when no handle is given.
    // static_cast keeps this compiling whether i2c_port_num_t is an alias of
    // i2c_port_t or a distinct enum (IDF 5.x ng driver).
    // Probe SCCB before ESP_Video initializes/claims the shared I2C bus.
    p4ProbeSccb();
    if (!camCfg.begin(static_cast<i2c_port_num_t>(I2C_NUM_0), CAM_PIN_SIOC, CAM_PIN_SIOD)) {
        p4CamLog("[CAM-P4/CSI] SCCB i2c begin failed", ERROR);
        return false;
    }

    ESPVideoCSIConfigClass csiCfg;
    if (!csiCfg.begin(camCfg)) {
        p4CamLog("[CAM-P4/CSI] CSI config begin failed", ERROR);
        return false;
    }

    // One-shot VFS diagnostic (root cause of the historical
    // "Failed to register video VFS dev name=video0" ESP_ERR_NO_MEM failure):
    // the IDF static VFS table (s_vfs[], CONFIG_VFS_MAX_COUNT) was full at the
    // moment ESP_Video tried to register /dev/video0. Dump the registered paths
    // right here — before s_video.begin(csiCfg) triggers that registration —
    // so we can see exactly which slots are occupied and which is safe to free.
#if P4_HAVE_ESP_VIDEO
    {
        static bool s_vfsDumpDone = false;
        if (!s_vfsDumpDone) {
            s_vfsDumpDone = true;
            char *buf = nullptr;
            size_t bufLen = 0;
            FILE *mf = open_memstream(&buf, &bufLen);
            if (mf) {
                esp_vfs_dump_registered_paths(mf);
                fclose(mf);
            }
            if (buf && bufLen > 0) {
                String d = String(buf);
                free(buf);
                p4CamLog("[CAM-P4/VFS] registered paths (before video0):", INFO);
                int start = 0;
                while (start < (int)d.length()) {
                    int e = d.indexOf('\n', start);
                    if (e < 0) e = d.length();
                    String line = d.substring(start, e);
                    if (line.length() > 0) p4CamLog("[CAM-P4/VFS]  " + line, INFO);
                    start = e + 1;
                }
            } else {
                p4CamLog("[CAM-P4/VFS] dump buffer unavailable", WARN);
            }
        }
    }
#endif

#if P4_HAVE_ESP_VIDEO
    // ── Decisive probe: is the VFS slot table really the blocker? ──────────
    // The dump showed slot 7 free, yet /dev/video0 registration still fails.
    // A dummy register/unregister right here settles it: if the dummy
    // succeeds, free slots are NOT the problem and the failure is inside
    // esp_video (double registration? FD range?); if it fails, our model of
    // esp_get_free_index is wrong. Runs on every begin attempt (idempotent).
    {
        static const esp_vfs_t s_dummyVfs = {};
        esp_err_t dr = esp_vfs_register("/dev/dummy0", &s_dummyVfs, nullptr);
        p4CamLog(String("[CAM-P4/VFS] dummy register /dev/dummy0 -> ") + String(esp_err_to_name(dr)),
                 dr == ESP_OK ? INFO : WARN);
        if (dr == ESP_OK) {
            esp_err_t du = esp_vfs_unregister("/dev/dummy0");
            p4CamLog(String("[CAM-P4/VFS] dummy unregister -> ") + String(esp_err_to_name(du)),
                     du == ESP_OK ? INFO : WARN);
        }
    }
#endif

    s_w = w; s_h = h;   // note: moved after dump; preserved from original
    // Free two VFS slots for the camera: the JPEG pipeline needs video0 +
    // video20 + video10 (3 nodes) but only slot 7 is free at boot.
    // /dev/null (zero firmware references) and /dev/console (IDF REPL, no
    // stdin reads; commands arrive via web/telnet; Serial uses /dev/uart +
    // USB-JTAG) are both safe to drop. Restored in cameraP4End().
    {
        esp_err_t nu = esp_vfs_unregister("/dev/null");
        p4CamLog(String("[CAM-P4/VFS] unregister /dev/null -> ") + String(esp_err_to_name(nu)),
                 (nu == ESP_OK || nu == ESP_ERR_INVALID_STATE) ? INFO : WARN);
        esp_err_t cu = esp_vfs_unregister("/dev/console");
        p4CamLog(String("[CAM-P4/VFS] unregister /dev/console -> ") + String(esp_err_to_name(cu)),
                 (cu == ESP_OK || cu == ESP_ERR_INVALID_STATE) ? INFO : WARN);
    }
    // Stock init CSI|ISP (video0 + video20 META). JPEG comes from the HW
    // encoder driver (esp_driver_jpeg) fed with UYVY frames — the esp_video
    // JPEG_ENC device is compiled out of this core, and video0 offers no
    // JPEG. Two VFS slots were freed above for the two nodes.
    if (!s_video.begin(csiCfg)) {
        p4CamLog("[CAM-P4/CSI] ESPVideo begin failed", ERROR);
        return false;
    }

    s_w = w; s_h = h;
    s_quality = quality ? quality : 12;
    // Mode discovery first (S_FMT-only, restores 800x800): feeds resList and
    // the switch validator. Never runs inside a live switch (see setResolution).
    p4ProbeCsiModes();
    if (!p4OpenCapture(w, h)) {
        s_video.end();
        return false;
    }
    // s_ready gates cameraP4Capture() AND cameraP4SetQuality(): it must be up
    // BEFORE the prime/quality calls below, and is cleared on any failure.
    s_ready = true;
    // Prime check: one real JPEG frame proves the whole pipeline.
    if (cameraP4Capture(nullptr, nullptr, 3000) == 0) {
        p4CamLog("[CAM-P4/CSI] no JPEG frame within 3s", ERROR);
        s_cap.stopCapture();
        s_cap.end();
        s_capOpen = false;
        s_video.end();
        s_ready = false;
        return false;
    }
    cameraP4SetQuality(s_quality);
    return true;
#endif
}

void cameraP4End() {
#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) p4UsbEnd();
#endif
#if P4_HAVE_ESP_VIDEO
    if (s_capOpen) {
        s_cap.stopCapture();
        s_cap.end();
        s_capOpen = false;
    }
    s_video.end();
    // Camera deinit freed video0+video20: give /dev/console its slot back.
    // (/dev/null stays unregistered: zero firmware references.)
    {
        esp_err_t cr = esp_vfs_console_register();
        if (cr != ESP_OK && cr != ESP_ERR_INVALID_STATE) {
            p4CamLog(String("[CAM-P4/VFS] console re-register -> ") + String(esp_err_to_name(cr)), WARN);
        }
    }
    if (s_jpgEnc) {
        jpeg_del_encoder_engine(s_jpgEnc);
        s_jpgEnc = nullptr;
    }
    // s_jpgSlot (PSRAM) is kept across stop/start cycles: realloc-free.
#endif
    // Fresh heartbeat for the next session (CAM-62): stale timestamps would
    // fast-fail the new prime capture before it even tries the driver.
    s_lastDequeueOkMs = 0;
    s_lastCapTryMs = 0;
    s_lastCapRestartMs = 0;
    s_capStallFails = 0;
    s_dqbufTimeoutOn = false;
    s_ready = false;
}

bool cameraP4Ready() {
    return s_ready;
}

#if P4_HAVE_ESP_VIDEO
// HW-encode one UYVY frame to JPEG (P4 HW codec via esp_driver_jpeg).
// Returns the JPEG length in the reusable s_jpgSlot, or 0 on failure.
static size_t p4EncodeJpeg(const uint8_t *raw, size_t rawLen) {
    if (!raw || rawLen == 0 || s_w == 0 || s_h == 0) return 0;
    if (!s_jpgEnc) {
        jpeg_encode_engine_cfg_t eng;
        memset(&eng, 0, sizeof(eng));
        eng.intr_priority = 0;
        eng.timeout_ms = 1000;
        if (jpeg_new_encoder_engine(&eng, &s_jpgEnc) != ESP_OK || !s_jpgEnc) {
            s_jpgEnc = nullptr;
            p4CamLog("[CAM-P4/JPG] encoder create failed", ERROR);
            return 0;
        }
        p4CamLog("[CAM-P4/JPG] HW encoder ready", INFO);
    }
    if (!s_jpgSlot) {
        // Driver-owned allocation: the HW codec needs aligned/DMA-suitable
        // memory ("bit stream is not aligned" with plain heap_caps_malloc).
        jpeg_encode_memory_alloc_cfg_t mcfg;
        memset(&mcfg, 0, sizeof(mcfg));
        mcfg.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
        size_t got = 0;
        s_jpgSlot = (uint8_t *)jpeg_alloc_encoder_mem(P4_JPG_SLOT_SIZE, &mcfg, &got);
        if (!s_jpgSlot || got < P4_JPG_SLOT_SIZE) {
            p4CamLog("[CAM-P4/JPG] encoder mem alloc failed", ERROR);
            s_jpgSlot = nullptr;
            return 0;
        }
    }
    // s_quality 4..63, lower = better (esp_camera convention) -> HW 1..100.
    int q = 95 - ((int)s_quality - 4);
    if (q < 1) q = 1;
    if (q > 100) q = 100;
    jpeg_encode_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.height = s_h;
    cfg.width = s_w;
    cfg.src_type = JPEG_ENCODE_IN_FORMAT_YUV422;
    cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
    cfg.image_quality = (uint32_t)q;
    cfg.pixel_reverse = false;  // tune if image shows swapped colors
    uint32_t outLen = 0;
    esp_err_t e = jpeg_encoder_process(s_jpgEnc, &cfg, raw, (uint32_t)rawLen,
                                       s_jpgSlot, P4_JPG_SLOT_SIZE, &outLen);
    if (e != ESP_OK || outLen == 0) {
        p4CamLog(String("[CAM-P4/JPG] encode failed -> ") + String(esp_err_to_name(e)), WARN);
        return 0;
    }
    return (size_t)outLen;
}
#endif // P4_HAVE_ESP_VIDEO (p4EncodeJpeg uses the 3.3.12 JPEG API + s_jpgEnc/s_jpgSlot)

size_t cameraP4CaptureWithRaw(CameraP4ChunkWriter write, void* ctx,
                               CameraP4ChunkWriter rawWrite, void* rawCtx,
                               uint32_t timeoutMs) {
#if !P4_HAVE_ESP_VIDEO
    (void)write; (void)ctx; (void)rawWrite; (void)rawCtx; (void)timeoutMs;
    return 0;
#else
    if (!s_ready) return 0;
#if P4_HAVE_UVC
    // USB source: wait for a new pushed frame, then emit from the slot.
    if (s_source == CAM_P4_SRC_USB) {
        if (s_usbGone) return 0;
        uint32_t startGen = s_usbGen;
        uint32_t start = millis();
        bool fresh = false;
        while ((millis() - start) < timeoutMs) {
            if (s_usbGone) return 0;
            if (s_usbGen != startGen) { fresh = true; break; }
            delay(5);
        }
        if (!fresh) return 0;
        size_t len = 0;
        if (s_usbMux && xSemaphoreTake(s_usbMux, pdMS_TO_TICKS(50)) == pdTRUE) {
            len = s_usbLen;
            if (len > 0 && write) write(s_usbSlot, len, ctx);
            xSemaphoreGive(s_usbMux);
        }
        if (len > 0) {
            s_lastDequeueOkMs = millis();  // shared freshness heartbeat
            s_capStallFails = 0;
        }
        return len;
    }
#endif
    if (!s_capOpen || !s_cap.isCaptureStarted()) return 0;
    // Fast-fail while the pipeline is known-stalled (CAM-62): the dequeue
    // below (VIDIOC_DQBUF) blocks inside the driver when no frame arrives,
    // which wedged the HTTP task on a stalled 720p snapshot. A retry is
    // still attempted every P4_CAP_RETRY_MS so recovery is detected.
    uint32_t nowMs = millis();
    if (cameraCaptureFastFail(s_lastDequeueOkMs, s_lastCapTryMs, nowMs,
                              P4_CAP_STALL_MS, P4_CAP_RETRY_MS)) {
        return 0;
    }
    s_lastCapTryMs = nowMs;
    uint32_t start = nowMs;
    while ((millis() - start) < timeoutMs) {
        ESPVideoBufferClass buf = s_cap.captureBuffer();
        if (buf.valid() && buf.formatType() == ESP_VIDEO_FORMAT_YUV422_UYVY && buf.size() > 0) {
            s_lastDequeueOkMs = millis();
            s_capStallFails = 0;
            // Fase 1 (Tarefa 5): raw UYVY tap for the H.264 encoder. Emitted
            // BEFORE the JPEG encode so a slow JPEG path never starves the
            // recorder; a null rawWrite keeps the exact old behaviour.
            if (rawWrite) rawWrite(buf.data(), buf.size(), rawCtx);
            size_t n = p4EncodeJpeg(buf.data(), buf.size());
            if (n > 0) {
                if (write) write(s_jpgSlot, n, ctx);
                return n;  // RAW buffer reclaimed on scope exit; JPEG slot reused
            }
            return 0;  // encode failed: drop this frame, caller retries/timeout
        }
        delay(2);
    }
    // Bounded wait exhausted without a frame (the driver-level DQBUF timeout
    // fires first when supported): mark the stall so later attempts fast-fail
    // until the retry window or the loop-task session re-sync recovers it.
    s_capStallFails++;
    return 0;
#endif
}

size_t cameraP4Capture(CameraP4ChunkWriter write, void* ctx, uint32_t timeoutMs) {
    return cameraP4CaptureWithRaw(write, ctx, nullptr, nullptr, timeoutMs);
}

// Raw-only dequeue: hands every UYVY frame to rawWrite and SKIPS the JPEG
// encode entirely. A H.264 recording with no viewer is exactly that case —
// the MJPEG frame nobody consumes was costing more than the encode itself
// (measured: 800x640 went from ~4 fps to the encoder's pace once removed).
// Returns the number of bytes emitted, 0 on failure/timeout.
size_t cameraP4CaptureRaw(CameraP4ChunkWriter rawWrite, void* rawCtx,
                          uint32_t timeoutMs) {
#if !P4_HAVE_ESP_VIDEO
    (void)rawWrite; (void)rawCtx; (void)timeoutMs;
    return 0;
#else
    if (!s_ready || !rawWrite) return 0;
#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) return 0;  // no UYVY on USB
#endif
    if (!s_capOpen || !s_cap.isCaptureStarted()) return 0;
    uint32_t nowMs = millis();
    if (cameraCaptureFastFail(s_lastDequeueOkMs, s_lastCapTryMs, nowMs,
                              P4_CAP_STALL_MS, P4_CAP_RETRY_MS)) {
        return 0;
    }
    s_lastCapTryMs = nowMs;
    uint32_t start = nowMs;
    while ((millis() - start) < timeoutMs) {
        ESPVideoBufferClass buf = s_cap.captureBuffer();
        if (buf.valid() && buf.formatType() == ESP_VIDEO_FORMAT_YUV422_UYVY && buf.size() > 0) {
            s_lastDequeueOkMs = millis();
            s_capStallFails = 0;
            rawWrite(buf.data(), buf.size(), rawCtx);
            return buf.size();
        }
        delay(2);
    }
    s_capStallFails++;
    return 0;
#endif
}

bool cameraP4CaptureGrayThumb(uint8_t* dst, uint16_t dstW, uint16_t dstH,
                              uint32_t timeoutMs) {
#if !P4_HAVE_ESP_VIDEO
    (void)dst; (void)dstW; (void)dstH; (void)timeoutMs;
    return false;
#else
    if (!s_ready || !dst || !dstW || !dstH) return false;
#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) return false;  // no UYVY on USB
#endif
    if (!s_capOpen || !s_cap.isCaptureStarted()) return false;
    uint32_t nowMs = millis();
    if (cameraCaptureFastFail(s_lastDequeueOkMs, s_lastCapTryMs, nowMs,
                              P4_CAP_STALL_MS, P4_CAP_RETRY_MS)) {
        return false;
    }
    s_lastCapTryMs = nowMs;
    uint32_t start = nowMs;
    while ((millis() - start) < timeoutMs) {
        ESPVideoBufferClass buf = s_cap.captureBuffer();
        if (buf.valid() && buf.formatType() == ESP_VIDEO_FORMAT_YUV422_UYVY && buf.size() > 0) {
            // Sanity: buffer must hold a full s_w×s_h UYVY frame.
            if (s_w == 0 || s_h == 0 || buf.size() < (size_t)s_w * s_h * 2) return false;
            s_lastDequeueOkMs = millis();
            s_capStallFails = 0;
            cameraDownsampleUyvy(buf.data(), s_w, s_h, dst, dstW, dstH);
            return true;  // RAW buffer reclaimed on scope exit
        }
        delay(2);
    }
    s_capStallFails++;
    return false;
#endif
}

bool cameraP4CaptureStalledNow() {
#if !P4_HAVE_ESP_VIDEO
    return false;
#else
    if (!s_ready || s_source != CAM_P4_SRC_MIPI) return false;
    return cameraCaptureStalled(s_lastDequeueOkMs, millis(), P4_CAP_STALL_MS) ? true : false;
#endif
}

void cameraP4StallTick(uint32_t nowMs) {
#if !P4_HAVE_ESP_VIDEO
    (void)nowMs;
#else
    // Loop task only: session teardown races concurrent dequeues, so HTTP /
    // stream tasks never re-sync — they fast-fail instead (see capture()).
    if (!s_ready || s_source != CAM_P4_SRC_MIPI) return;
    if (!s_capOpen || !s_cap.isCaptureStarted()) return;
    if (!cameraCaptureRestartDue(s_lastDequeueOkMs, s_lastCapRestartMs, nowMs,
                                 P4_CAP_STALL_MS, P4_CAP_RESTART_MS)) {
        return;
    }
    s_lastCapRestartMs = nowMs;
    p4CamLog(String("[CAM-P4/CSI] stall (") + (uint32_t)s_capStallFails
             + " fails): re-syncing capture session", WARN);
    s_cap.stopCapture();
    delay(50);
    if (!s_cap.startCapture()) {
        p4CamLog("[CAM-P4/CSI] stall re-sync FAILED", ERROR);
        return;
    }
    p4CamLog("[CAM-P4/CSI] stall re-sync ok, waiting for frames", INFO);
#endif
}

bool cameraP4SetQuality(uint8_t quality) {
#if !P4_HAVE_ESP_VIDEO
    (void)quality;
    return false;
#else
    if (!s_ready) return false;
    if (quality < 4) quality = 4;
    if (quality > 63) quality = 63;
    s_quality = quality;
#if P4_HAVE_UVC
    // USB webcams encode JPEG on-device; quality is not settable — persist only.
    if (s_source == CAM_P4_SRC_USB) return true;
#endif
    // MIPI path captures UYVY + HW-encodes per frame: quality is applied at
    // encode time from s_quality (see p4EncodeJpeg). Sensor-side JPEG quality
    // is meaningless in RAW mode, so persist only.
    return true;
#endif
}

bool cameraP4SetFlip(bool hmirror, bool vflip) {
#if !P4_HAVE_ESP_VIDEO
    (void)hmirror; (void)vflip;
    return false;
#else
    if (!s_ready) return false;
#if P4_HAVE_UVC
    // No flip control on generic UVC in v1 — persist only.
    if (s_source == CAM_P4_SRC_USB) return true;
#endif
    return s_cap.setSensorHFlip(hmirror) && s_cap.setSensorVFlip(vflip);
#endif
}

// Switch the live MIPI pipeline to an ACKed sensor mode (see p4ProbeCsiModes).
// Unknown sizes keep the current mode and return true (lenient: legacy
// framesize-enum requests such as VGA stay on the working mode, as before).
// Known-but-unreachable modes attempt the switch and revert on failure,
// returning false so the UI can report it (camera keeps working).
bool cameraP4SetResolution(uint16_t w, uint16_t h) {
#if !P4_HAVE_ESP_VIDEO
    (void)w; (void)h;
    return false;
#else
    if (!s_ready) return false;
    if (w == s_w && h == s_h) return true;
    if (s_resSwitching) {
        p4CamLog("[CAM-P4/CSI] switch busy, retry", WARN);
        return false;
    }
    P4BeginGuard beginGuard;  // keep the Wire presence probe off I2C0 during rewrites
    P4SwitchGuard switchGuard;
#if P4_HAVE_UVC
    // USB: renegotiate by reopening the stream at the new size (rare op).
    if (s_source == CAM_P4_SRC_USB) {
        p4UsbEnd();
        if (!p4UsbBegin(w, h)) return false;
        s_w = w; s_h = h;
        return true;
    }
#endif
    int8_t mode = p4CsiModeIndex(w, h);
    if (mode < 0) return true;  // not a sensor mode: keep current, no error
    // 1) Driver path: S_FMT may just work (mode already active).
    if (p4OpenCaptureAt(w, h)) {
        if (cameraP4Capture(nullptr, nullptr, 3000) > 0) {
            p4CamLog(String("[CAM-P4/CSI] mode switch OK ") + String(w) + "x" + String(h)
                     + " (driver path)", INFO);
            return true;
        }
        p4CamLog(String("[CAM-P4/CSI] mode ") + String(w) + "x" + String(h)
                 + " opened but no frames", ERROR);
    }
    // 2) Official path: VIDIOC_S_SENSOR_FMT reprograms the sensor and
    // rebuilds CSI/ISP for the new size, then open.
    if (p4SetSensorFormat(w, h) && p4OpenCaptureAt(w, h)) {
        if (cameraP4Capture(nullptr, nullptr, 4000) > 0) {
            p4CamLog(String("[CAM-P4/CSI] sensor-fmt switch OK ") + String(w) + "x" + String(h), INFO);
            return true;
        }
        p4CamLog(String("[CAM-P4/CSI] sensor-fmt mode ") + String(w) + "x" + String(h)
                 + " opened but no frames", ERROR);
    }
    // 3) SCCB-direct path: rewrite the sensor mode registers, then open.
    // Display shares the bus: one frame may glitch.
    uint16_t oldW = s_w, oldH = s_h;
    p4CamLog(String("[CAM-P4/CSI] attempting SCCB switch to ") + String(w) + "x" + String(h), WARN);
    if (p4SccbSwitchMode(w, h) && p4OpenCaptureAt(w, h)) {
        if (cameraP4Capture(nullptr, nullptr, 4000) > 0) {
            p4CamLog(String("[CAM-P4/CSI] SCCB switch OK ") + String(w) + "x" + String(h), INFO);
            return true;
        }
        p4CamLog(String("[CAM-P4/CSI] SCCB mode ") + String(w) + "x" + String(h)
                 + " opened but no frames (CSI lock?), reverting", ERROR);
    } else {
        p4CamLog(String("[CAM-P4/CSI] SCCB switch to ") + String(w) + "x" + String(h)
                 + " failed, reverting", ERROR);
    }
    // Revert to the previous working mode and PROVE frames are back: an
    // unverified revert is how the camera ends up "open but silent".
    p4SccbSwitchMode(oldW, oldH);
    if (p4OpenCaptureAt(oldW, oldH) && cameraP4Capture(nullptr, nullptr, 4000) > 0) {
        p4CamLog("[CAM-P4/CSI] reverted to working mode", WARN);
        return false;
    }
    // Last resort before a power cycle: full sensor reprogram (official path).
    p4CamLog("[CAM-P4/CSI] revert silent, reprogramming sensor", ERROR);
    if (p4SetSensorFormat(oldW, oldH) && p4OpenCaptureAt(oldW, oldH) &&
        cameraP4Capture(nullptr, nullptr, 4000) > 0) {
        p4CamLog("[CAM-P4/CSI] sensor reprogram recovered", WARN);
    }
    return false;
#endif
}

const char* cameraP4Model() {
    return (s_source == CAM_P4_SRC_USB) ? "USB-UVC" : "OV5647";
}

bool cameraP4IsUsb() {
    return s_source == CAM_P4_SRC_USB;
}

void cameraP4ActualDims(uint16_t &w, uint16_t &h) {
    w = s_w;
    h = s_h;
}

uint8_t cameraP4FpsMax() {
#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) return s_usbNegFps ? s_usbNegFps : 30;
#endif
    // MIPI MJPEG. The old fixed 6 was measured long ago (and at HD); the
    // OV5647 RAW8 modes are rated 50 fps and the MJPEG wire delivered 6.8 fps
    // while the loop was still clamped to 6 — i.e. we were at our own ceiling,
    // not the sensor's. 15 leaves headroom for the small modes; the real
    // sustained rate is reported by the status pill (streamFps), and a mode
    // that cannot hold it simply publishes fewer frames (never a stall: the
    // stall path is queue-bound, not rate-bound).
    return 15;
}

// Fase 4 (Tarefa 5): H.264 path ceiling. MJPEG keeps 6 (measured); H.264
// skips the JPEG encode + MJPEG pump, so it runs to the sensor rate (30).
uint8_t cameraP4FpsMaxH264() {
#if P4_HAVE_UVC
    if (s_source == CAM_P4_SRC_USB) return s_usbNegFps ? s_usbNegFps : 30;
#endif
    return CAM_H264_FPS_MAX;  // MIPI H.264: sensor rate (GOP=fps, 1 IDR/s)
}

int cameraP4MaxFrameSize() {
    // OV5647 is 2592x1944 (5MP class) — same enum slot as the 5MP entry.
    return 24;
}

#endif // TARGET_BOARD_P4
