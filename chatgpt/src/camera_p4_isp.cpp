// camera_p4_isp.cpp — ESP32-P4 ISP colour pipeline from user space (CAM-84).
//
// Opens the ISP video device once and keeps it open: the driver resets its
// controls when the last file descriptor closes, so a per-frame open/close
// would silently wipe the profile. Everything here is plain V4L2
// (VIDIOC_S_EXT_CTRLS) — no IDF build, no component fork.
//
// Order matters: gamma/demosaic/sharpen/denoise shape the image, CCM and WB
// set its colour. Applying all of them once per profile change is enough.

#include "feature_flags.h"

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1

// Same scope rule as camera_p4_backend.cpp: the esp_video ISP headers exist
// only on the 3.3.12 P4 core used by the camera pack. Other P4 packs (3.3.5)
// get stubs below so they keep linking without a camera.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_CAM_P4)

#include "camera_p4_isp.h"
#include "camera_p4_backend.h"   // public API declarations
#include "log.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#include <linux/videodev2.h>
#include "esp_video_device.h"       // ESP_VIDEO_ISP1_DEVICE_NAME
#include "esp_video_isp_ioctl.h"    // V4L2_CID_USER_ESP_ISP_* + structs

static int  s_ispFd = -1;
static int  s_ispProfile = -2;      // -2 = never applied, -1 = off
static char s_ispDiag[24] = "";

// Same rule as the backend: every init/status message must reach the Web
// Serial Monitor, so no DEBUG level and no dedup swallowing.
extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static inline void ispLog(const String& msg, int level = INFO) {
    sendVirtualMessage(String("[CAM-P4/ISP] ") + msg, level, false, true);
}

static void ispSetDiag(const char* tag) {
    snprintf(s_ispDiag, sizeof(s_ispDiag), "%s", tag ? tag : "");
}

static int ispOpen() {
    if (s_ispFd >= 0) return s_ispFd;
    s_ispFd = ::open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
    if (s_ispFd < 0) {
        ispSetDiag("no-dev");
        ispLog(String("ISP device ") + ESP_VIDEO_ISP1_DEVICE_NAME +
                " unavailable (errno=" + String(errno) + ")", WARN);
        return -1;
    }
    return s_ispFd;
}

// One V4L2 extended-control write. Returns true on success.
static bool ispSetCtrl(uint32_t id, void* payload, size_t size) {
    if (ispOpen() < 0) return false;
    struct v4l2_ext_control ctrl = {};
    ctrl.id = id;
    ctrl.size = (uint32_t)size;
    ctrl.ptr = payload;
    struct v4l2_ext_controls ctrls = {};
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    return ioctl(s_ispFd, VIDIOC_S_EXT_CTRLS, &ctrls) == 0;
}

// Static blocks that never change between profiles: applied once per session.
static bool s_ispTuned = false;
static bool ispTuneOnce() {
    if (s_ispTuned) return true;
    bool ok = true;

    esp_video_isp_gamma_t gamma = {};
    gamma.enable = true;
    for (int i = 0; i < CAM_ISP_GAMMA_POINTS; i++) {
        gamma.points[i].x = kCamIspGamma[i].x;
        gamma.points[i].y = kCamIspGamma[i].y;
    }
    ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_GAMMA, &gamma, sizeof(gamma)) && ok;

    // Demosaic must be enabled EXPLICITLY on this silicon: a bare ISP enable
    // does not start it, and the JPEG encoder will happily compress flat or
    // striped data. Idempotent, so re-applying is harmless.
    esp_video_isp_demosaic_t dem = {};
    dem.enable = true;
    dem.gradient_ratio = 1.0f;
    ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_DEMOSAIC, &dem, sizeof(dem)) && ok;

    esp_video_isp_sharpen_t sharp = {};
    sharp.enable = true;
    sharp.h_thresh = 40;
    sharp.l_thresh = 10;
    sharp.h_coeff = 1.5f;
    sharp.m_coeff = 0.5f;
    const uint8_t sm[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    memcpy(sharp.matrix, sm, sizeof(sm));
    ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_SHARPEN, &sharp, sizeof(sharp)) && ok;

    // Bayer-domain denoise: level 8 of [2,20] — visible noise down, detail kept.
    esp_video_isp_bf_t bf = {};
    bf.enable = true;
    bf.level = 8;
    memcpy(bf.matrix, sm, sizeof(sm));
    ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_BF, &bf, sizeof(bf)) && ok;

    s_ispTuned = ok;
    if (!ok) ispSetDiag("tune");
    return ok;
}

// Apply one profile index (0..CAM_ISP_PROFILE_COUNT-1) or -1 for "colour off".
bool cameraP4IspApply(int profileIdx) {
    if (!ispTuneOnce()) return false;    // ISP missing or gamma refused
    if (profileIdx < -1 || profileIdx >= CAM_ISP_PROFILE_COUNT) profileIdx = CAM_ISP_PROFILE_DEFAULT;
    if (profileIdx == s_ispProfile) return true;   // already applied

    bool ok = true;
    if (profileIdx < 0) {
        esp_video_isp_ccm_t ccm = {};
        ccm.enable = false;
        ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_CCM, &ccm, sizeof(ccm)) && ok;
        esp_video_isp_wb_t wb = {};
        wb.enable = false;
        ok = ispSetCtrl(V4L2_CID_USER_ESP_ISP_WB, &wb, sizeof(wb)) && ok;
        ispSetDiag(ok ? "off" : "ccm");
        ispLog(String("colour correction off"), INFO);
    } else {
        const CamIspProfile& p = kCamIspProfiles[profileIdx];
        esp_video_isp_ccm_t ccm = {};
        ccm.enable = true;
        memcpy(ccm.matrix, p.ccm, sizeof(p.ccm));
        bool ccmOk = ispSetCtrl(V4L2_CID_USER_ESP_ISP_CCM, &ccm, sizeof(ccm));
        esp_video_isp_wb_t wb = {};
        wb.enable = true;
        wb.red_gain = p.wb_r;
        wb.blue_gain = p.wb_b;
        bool wbOk = ispSetCtrl(V4L2_CID_USER_ESP_ISP_WB, &wb, sizeof(wb));
        ok = ccmOk && wbOk;
        ispSetDiag(ok ? "ok" : (ccmOk ? "wb" : "ccm"));
        ispLog(String("profile ") + profileIdx + " '" + p.name + "' " + p.kelvin + "K" +
                (ok ? String(" applied") : String(" FAILED")), ok ? INFO : WARN);
    }
    if (ok) s_ispProfile = profileIdx;
    return ok;
}

// Called when the camera stops: the ISP device outlives a capture session, but
// the controls are re-applied on the next begin (they may be reset with the
// sensor pipeline).
void cameraP4IspReset() {
    s_ispProfile = -2;
    s_ispTuned = false;
}

void cameraP4IspEnd() {
    if (s_ispFd >= 0) { ::close(s_ispFd); s_ispFd = -1; }
    s_ispProfile = -2;
    s_ispTuned = false;
}

bool cameraP4IspAvailable() { return ispOpen() >= 0; }

const char* cameraP4IspDiag() { return s_ispDiag; }

#else   // P4 core without the esp_video ISP device (non-camera P4 packs)

bool cameraP4IspApply(int) { return false; }
void cameraP4IspReset() {}
void cameraP4IspEnd() {}
bool cameraP4IspAvailable() { return false; }
const char* cameraP4IspDiag() { return ""; }

#endif  // SELECTED_PACK == PACK_CAM_P4

#endif  // TARGET_BOARD_P4
