#pragma once
// camera_p4_uvc.h — UVC (host side) policy: control-transfer sizes, mode
// selection and the isochronous-only rule. Pure header (no ESP deps) so the
// decisions are unit-tested on Linux; camera_p4_backend.cpp owns the I/O.
//
// Facts behind these rules (measured on the .18 + the reference bring-ups):
//
// 1) VS_PROBE_CONTROL / VS_COMMIT_CONTROL are revision-sized:
//      UVC 1.0  26 B   (no framing-info / version-range fields)
//      UVC 1.1  34 B   (+ bmFramingInfo, bPreferredVersion, bMinVersion,
//                        bMaxVersion)
//      UVC 1.5  48 B   (+ bUsage, bBitDepthLuma, bmSettings,
//                        bMaxNumberOfRefFramesPlus1, bmRateControlModes,
//                        bmLayoutPerStream)
//    Asking for the wrong length gives a short read, and committing with it can
//    be silently rejected -> the camera never streams ("zero frames", exactly
//    the symptom seen with a mismatched probe length). The three layouts share
//    the first 26 bytes, so parsing the common prefix is always safe.
//
// 2) Isochronous only. UVC over bulk needs a descriptor that says so, and
//    plenty of hosts/apps choke on it (macOS AVFoundation throws on a
//    BULK-only UVC device). Our pump consumes isoc frames, so a device without
//    an isochronous VideoStreaming endpoint is refused up front instead of
//    timing out.
//
// 3) Portrait modes are a host hazard: non-standard portrait resolutions make
//    players/capture stacks fail (and the pump's own UI is landscape). Keep
//    them out of the advertised list; landscape modes only.

#include <stdint.h>
#include <stdbool.h>

// --- Control-transfer structure sizes --------------------------------------
#define CAM_UVC_PROBE_LEN_1_0 26u
#define CAM_UVC_PROBE_LEN_1_1 34u
#define CAM_UVC_PROBE_LEN_1_5 48u

// bcdUVC is the device's UVC revision (e.g. 0x0110, 0x0150).
static inline uint16_t cameraUvcProbeLen(uint16_t bcdUVC) {
    if (bcdUVC >= 0x0150) return CAM_UVC_PROBE_LEN_1_5;
    if (bcdUVC >= 0x0110) return CAM_UVC_PROBE_LEN_1_1;
    return CAM_UVC_PROBE_LEN_1_0;
}

// UVC 1.5 selectors are 0x02xx; older revisions use the 1-byte selectors.
static inline uint16_t cameraUvcProbeSelector(uint16_t bcdUVC) {
    return (bcdUVC >= 0x0150) ? 0x0200 : 0x0101;
}
static inline uint16_t cameraUvcCommitSelector(uint16_t bcdUVC) {
    return (bcdUVC >= 0x0150) ? 0x0201 : 0x0102;
}

// A short read (device sent fewer bytes than the revision's structure) is
// still usable: the first 26 bytes are common to every revision. Committing
// uses what the device actually returned, never more than its revision allows.
static inline uint16_t cameraUvcCommitLen(uint16_t bcdUVC, int gotBytes) {
    uint16_t want = cameraUvcProbeLen(bcdUVC);
    if (gotBytes <= 0) return 0;
    if ((uint16_t)gotBytes < want) return (uint16_t)gotBytes;
    return want;
}

// --- USB mode selection -----------------------------------------------------
// Our pump runs landscape JPEG frames; the pipeline holds up to 2 MB frames.
#define CAM_USB_MODE_MAX_W 1920
#define CAM_USB_MODE_MAX_H 1080

// 1 when a UVC-advertised mode may be offered/used here.
static inline bool cameraUsbModeKeep(uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) return false;
    if (w > CAM_USB_MODE_MAX_W || h > CAM_USB_MODE_MAX_H) return false;
    return w >= h;   // landscape/square only (portrait breaks hosts)
}

// Best mode = largest area that passes cameraUsbModeKeep(). Returns 1 and fills
// w/h, or 0 when nothing is usable (caller keeps its own default).
static inline int cameraUsbPickBest(const uint16_t (*modes)[2], int n,
                                    uint16_t* outW, uint16_t* outH) {
    if (!modes || n <= 0) return 0;
    uint32_t bestArea = 0;
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!cameraUsbModeKeep(modes[i][0], modes[i][1])) continue;
        uint32_t area = (uint32_t)modes[i][0] * (uint32_t)modes[i][1];
        if (area > bestArea) { bestArea = area; best = i; }
    }
    if (best < 0) return 0;
    if (outW) *outW = modes[best][0];
    if (outH) *outH = modes[best][1];
    return 1;
}

// Indices of the usable modes, largest area first (ties keep table order).
// Returns how many were written (<= outCap).
static inline int cameraUsbOrderModes(const uint16_t (*modes)[2], int n,
                                      int* outIdx, int outCap) {
    if (!modes || n <= 0 || !outIdx || outCap <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!cameraUsbModeKeep(modes[i][0], modes[i][1])) continue;
        const uint32_t area = (uint32_t)modes[i][0] * (uint32_t)modes[i][1];
        // Where this one belongs among the kept ones (area, descending).
        int pos = count;
        while (pos > 0) {
            int prev = outIdx[pos - 1];
            uint32_t prevArea = (uint32_t)modes[prev][0] * (uint32_t)modes[prev][1];
            if (prevArea >= area) break;
            pos--;
        }
        if (pos >= outCap) continue;            // does not beat the kept set
        const int last = (count < outCap) ? count : outCap - 1;
        for (int k = last; k > pos; k--) outIdx[k] = outIdx[k - 1];
        outIdx[pos] = i;
        if (count < outCap) count++;
    }
    return count;
}
