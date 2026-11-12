#pragma once
// camera_p4_usb_isoc.h — verdict for the raw isochronous probe (CAM-82).
//
// The P4 USB path reports, from camera_p4_backend.cpp:
//   submits=<URB submit attempts> submitErr=<esp_err_t> packets=<completed
//   isoc packets> nonEmpty=<packets carrying >=1 byte> skipped=<SKIPPED packets>
//   perUs=<bytes/microframe from the endpoint descriptor>
// and nothing else. On the .18 (Sunplus 1b3f:1167 behind a Genesys 05e3:0610
// hub) that read "submits=80 submitErr=ESP_ERR_NOT_FOUND packets=114
// nonEmpty=0": URBs refused AND every completed packet zero-length. Deciding
// what that means — and what the operator must do — is pure logic, so it is
// unit-tested on Linux instead of guessed from a serial console.
//
// Physical background (USB 2.0 spec): a full/low-speed device behind a
// high-speed hub needs SPLIT transactions, and SPLIT is only defined for
// control, bulk and interrupt — never isochronous. An FS isoc camera behind a
// hub therefore completes packets with zero bytes no matter what the host
// does; the fix is a root port (or an HS camera), not firmware.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// High-speed isochronous: at most 3 transactions of 1024 B per microframe.
#define CAM_USB_ISOC_HS_MAX_PER_US 3072u
#define CAM_USB_ISOC_DIAG_CAP      40

typedef enum {
    CAM_USB_ISOC_OK = 0,     // packets arrived with payload
    CAM_USB_ISOC_IDLE,       // URBs accepted, no payload, no error
    CAM_USB_ISOC_BAD_PS,     // descriptor claims an impossible packet size
    CAM_USB_ISOC_NOTFOUND,   // host refused the URBs (endpoint/altsetting)
    CAM_USB_ISOC_ZERO_LEN,   // every completed packet carried 0 bytes
    CAM_USB_ISOC_SKIPPED,    // host skipped every packet (bus overload)
} CamUsbIsocVerdict;

typedef struct {
    unsigned submits;      // URB submit attempts
    int      submitErr;    // esp_err_t of the last submit (0 = accepted)
    int      packets;      // completed isoc packets
    int      nonEmpty;     // completed packets with >0 bytes
    int      skipped;      // completed packets with SKIPPED status
    unsigned perUs;        // bytes/microframe from the endpoint descriptor
    bool     fsBehindHub;  // full/low-speed device behind a hub
} CamUsbIsocSample;

// Order matters: a refused submit explains the rest, and a bogus descriptor
// size explains a refused submit. Only then does the payload tell the story.
static inline CamUsbIsocVerdict camUsbIsocVerdict(const CamUsbIsocSample* s) {
    if (!s) return CAM_USB_ISOC_IDLE;
    if (s->perUs == 0 || s->perUs > CAM_USB_ISOC_HS_MAX_PER_US) return CAM_USB_ISOC_BAD_PS;
    if (s->submitErr != 0) return CAM_USB_ISOC_NOTFOUND;
    if (s->skipped > 0 && s->skipped >= s->packets) return CAM_USB_ISOC_SKIPPED;
    if (s->packets > 0 && s->nonEmpty == 0) return CAM_USB_ISOC_ZERO_LEN;
    if (s->nonEmpty > 0) return CAM_USB_ISOC_OK;
    return CAM_USB_ISOC_IDLE;
}

// Token for `usbDiag` (<= CAM_USB_ISOC_DIAG_CAP). Never NULL.
static inline const char* camUsbIsocDiag(const CamUsbIsocSample* s, char* out, size_t cap) {
    static const char kBad[] = "isoc:ps";
    if (!out || cap == 0) return kBad;
    CamUsbIsocVerdict v = camUsbIsocVerdict(s);
    switch (v) {
        case CAM_USB_ISOC_BAD_PS:
            snprintf(out, cap, "isoc:ps=%u", s ? s->perUs : 0u);
            break;
        case CAM_USB_ISOC_NOTFOUND: snprintf(out, cap, "isoc:notfound"); break;
        case CAM_USB_ISOC_SKIPPED:  snprintf(out, cap, "isoc:skip");     break;
        case CAM_USB_ISOC_ZERO_LEN:
            snprintf(out, cap, "%s", (s && s->fsBehindHub) ? "isoc:zlen-hub" : "isoc:zlen");
            break;
        case CAM_USB_ISOC_OK:   snprintf(out, cap, "isoc:ok");   break;
        default:                snprintf(out, cap, "isoc:idle"); break;
    }
    return out;
}

// One line for the console: the verdict plus the action it implies.
static inline const char* camUsbIsocAdvice(CamUsbIsocVerdict v, bool fsBehindHub) {
    switch (v) {
        case CAM_USB_ISOC_BAD_PS:
            return "endpoint descriptor claims more than 3072 B/microframe: the alt walk picked a wrong setting";
        case CAM_USB_ISOC_NOTFOUND:
            return "host refuses every URB: endpoint/altsetting unusable at this speed";
        case CAM_USB_ISOC_SKIPPED:
            return "host skipped every packet: bus overload or no isochronous bandwidth left";
        case CAM_USB_ISOC_ZERO_LEN:
            return fsBehindHub
                ? "full-speed isochronous device behind a hub: USB 2.0 does not split isoc - plug it into a ROOT port (or use a high-speed camera)"
                : "packets complete but carry 0 bytes: wrong alt setting or the device is idle";
        case CAM_USB_ISOC_OK:
            return "isochronous data flowing";
        default:
            return "URBs accepted but no data arrived";
    }
}
