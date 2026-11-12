#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_p4_usb_isoc.h turns the raw isochronous probe counters into a verdict
// + a `usbDiag` token. The first case reproduces the numbers measured on the
// .18 (Sunplus 1b3f:1167 behind a Genesys 05e3:0610 hub):
//   submits=80 submitErr=ESP_ERR_NOT_FOUND packets=114 nonEmpty=0
#include "../../chatgpt/src/camera_p4_usb_isoc.h"

static CamUsbIsocSample measured() {
    CamUsbIsocSample s = {};
    s.submits = 80;
    s.submitErr = 0x105;    // ESP_ERR_NOT_FOUND
    s.packets = 114;
    s.nonEmpty = 0;
    s.skipped = 0;
    s.perUs = 3072;         // alt 7 packet size seen in the log
    s.fsBehindHub = true;
    return s;
}

TEST_CASE("CAM-82: the .18 measurement maps to isoc:notfound (URBs refused)") {
    CamUsbIsocSample s = measured();
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_NOTFOUND);
    char db[CAM_USB_ISOC_DIAG_CAP];
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:notfound") == 0);
    CHECK(std::strlen(camUsbIsocAdvice(CAM_USB_ISOC_NOTFOUND, s.fsBehindHub)) > 0);

    // A refused submit explains everything else: even with zero packets the
    // verdict stays NOTFOUND rather than "idle".
    s.packets = 0;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_NOTFOUND);
    // ... and it is not the descriptor's fault while perUs is legal.
    CHECK(s.perUs <= CAM_USB_ISOC_HS_MAX_PER_US);
}

TEST_CASE("CAM-82: zero-length packets behind a hub name the real cause") {
    CamUsbIsocSample s = measured();
    s.submitErr = 0;          // URBs accepted this time
    s.packets = 120;
    s.nonEmpty = 0;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_ZERO_LEN);
    char db[CAM_USB_ISOC_DIAG_CAP];
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:zlen-hub") == 0);
    // Same numbers on a root port: not a hub problem, say something else.
    s.fsBehindHub = false;
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:zlen") == 0);
    CHECK(std::strcmp(camUsbIsocAdvice(CAM_USB_ISOC_ZERO_LEN, true),
                      camUsbIsocAdvice(CAM_USB_ISOC_ZERO_LEN, false)) != 0);
    // One byte is enough to prove the pipe works.
    s.nonEmpty = 1;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_OK);
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:ok") == 0);
}

TEST_CASE("CAM-82: descriptor/bandwidth verdicts and the diag slot") {
    CamUsbIsocSample s = measured();
    s.perUs = 0;                       // alt walk produced nothing
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_BAD_PS);
    char db[CAM_USB_ISOC_DIAG_CAP];
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:ps=0") == 0);
    s.perUs = CAM_USB_ISOC_HS_MAX_PER_US + 1;   // 3073 B/us: impossible on HS
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_BAD_PS);
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:ps=3073") == 0);
    CHECK(std::strlen(db) < CAM_USB_ISOC_DIAG_CAP);

    // All packets SKIPPED: bus overload, not a silent device.
    s.perUs = 3072; s.submitErr = 0; s.packets = 64; s.skipped = 64; s.nonEmpty = 0;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_SKIPPED);
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:skip") == 0);
    // Only some skipped and nothing arrived: still a zero-length pipe.
    s.skipped = 3;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_ZERO_LEN);

    // Accepted, nothing delivered, no error: idle.
    s.skipped = 0; s.packets = 0; s.nonEmpty = 0;
    CHECK(camUsbIsocVerdict(&s) == CAM_USB_ISOC_IDLE);
    CHECK(std::strcmp(camUsbIsocDiag(&s, db, sizeof(db)), "isoc:idle") == 0);

    // Defensive: null sample / null buffer never crash and never lie.
    CHECK(camUsbIsocVerdict(nullptr) == CAM_USB_ISOC_IDLE);
    CHECK(camUsbIsocDiag(nullptr, db, sizeof(db)) != nullptr);
    CHECK(camUsbIsocDiag(&s, nullptr, 0) != nullptr);
    CHECK(camUsbIsocAdvice(CAM_USB_ISOC_OK, false) != nullptr);
}
