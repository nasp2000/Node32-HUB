#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_p4_h264.h is a pure decision helper — zero ESP32 deps.
#include "../../chatgpt/src/camera_p4_h264.h"

TEST_CASE("codec clamp: MJPEG default, H264 opt-in only") {
    CHECK(cameraH264CodecClamp(0) == CAM_REC_CODEC_MJPEG);
    CHECK(cameraH264CodecClamp(1) == CAM_REC_CODEC_H264);
    CHECK(cameraH264CodecClamp(2) == CAM_REC_CODEC_MJPEG);
    CHECK(cameraH264CodecClamp(-1) == CAM_REC_CODEC_MJPEG);
    CHECK(cameraH264CodecClamp(99) == CAM_REC_CODEC_MJPEG);
}

TEST_CASE("bitrate clamp 1000-8000 kbps, default 4000") {
    CHECK(cameraH264BitrateClampKbps(4000) == 4000);
    CHECK(cameraH264BitrateClampKbps(1000) == 1000);
    CHECK(cameraH264BitrateClampKbps(8000) == 8000);
    CHECK(cameraH264BitrateClampKbps(0) == 1000);
    CHECK(cameraH264BitrateClampKbps(-5) == 1000);
    CHECK(cameraH264BitrateClampKbps(999) == 1000);
    CHECK(cameraH264BitrateClampKbps(8001) == 8000);
    CHECK(cameraH264BitrateClampKbps(100000) == 8000);
    CHECK(CAM_H264_BITRATE_DEF_KBPS == 4000);
}

TEST_CASE("MB/min estimate: 4 Mbps ~= 29.3 MB/min, 1 Mbps ~= 7.3") {
    CHECK(cameraH264MbPerMin(4000) == doctest::Approx(29.3).epsilon(0.01));
    CHECK(cameraH264MbPerMin(1000) == doctest::Approx(7.32).epsilon(0.01));
    CHECK(cameraH264MbPerMin(8000) == doctest::Approx(58.59).epsilon(0.01));
    // monotonic: more bitrate = more MB/min
    CHECK(cameraH264MbPerMin(8000) > cameraH264MbPerMin(1000));
}

TEST_CASE("HW geometry: align16 + UYVY len + support window") {
    CHECK(cameraH264Align16(1920) == 1920);
    CHECK(cameraH264Align16(1080) == 1088);  // the 1080p pad case
    CHECK(cameraH264Align16(800) == 800);
    CHECK(cameraH264Align16(720) == 720);
    CHECK(cameraH264Align16(1) == 16);
    // 1080p UYVY = 1920*1088*2
    CHECK(cameraH264UyvyLen(1920, 1080) == (size_t)1920 * 1088 * 2);
    // 800x800 needs no padding
    CHECK(cameraH264UyvyLen(800, 800) == (size_t)800 * 800 * 2);
    CHECK(cameraH264ResSupported(1920, 1080));
    CHECK(cameraH264ResSupported(800, 800));
    CHECK_FALSE(cameraH264ResSupported(1921, 1080));  // over HW width max
    CHECK_FALSE(cameraH264ResSupported(16, 16));      // under HW min
    CHECK_FALSE(cameraH264ResSupported(0, 0));
}

TEST_CASE("start codes: 3-byte and 4-byte forms, negatives") {
    const uint8_t sc3[] = {0x00, 0x00, 0x01, 0x67};
    const uint8_t sc4[] = {0x00, 0x00, 0x00, 0x01, 0x68};
    const uint8_t no[]  = {0x00, 0x01, 0x02, 0x03};
    CHECK(cameraH264StartCodeLen(sc3, sizeof(sc3)) == 3);
    CHECK(cameraH264StartCodeLen(sc4, sizeof(sc4)) == 4);
    CHECK(cameraH264StartCodeLen(no, sizeof(no)) == 0);
    CHECK(cameraH264StartCodeLen(sc3, 2) == 0);  // truncated
    // NAL type = low 5 bits
    CHECK(cameraH264NalType(sc3 + 3, 1) == 7);   // SPS
    CHECK(cameraH264NalType(sc4 + 4, 1) == 8);   // PPS
    CHECK(cameraH264NalType(nullptr, 0) == -1);
}

TEST_CASE("NAL scan: SPS+PPS+IDR blob counted by type") {
    // Annex B blob as the HW encoder emits for an IDR: SPS, PPS, IDR slice.
    const uint8_t blob[] = {
        0x00,0x00,0x00,0x01, 0x67, 0xAA,0xBB,       // SPS (7)
        0x00,0x00,0x00,0x01, 0x68, 0xCC,            // PPS (8)
        0x00,0x00,0x01,       0x65, 0xDD,0xEE,0xFF, // IDR (5, 3-byte code)
    };
    CamH264NalStats st;
    cameraH264ScanNals(blob, sizeof(blob), &st);
    CHECK(st.units == 3);
    CHECK(st.sps == 1);
    CHECK(st.pps == 1);
    CHECK(st.idr == 1);
    CHECK(st.other == 0);
    CHECK(cameraH264HasStreamStart(blob, sizeof(blob)));
}

TEST_CASE("NAL scan: P-frame only has no stream start; IDR-first fails") {
    // P slice alone (type 1): no SPS/PPS/IDR.
    const uint8_t pframe[] = {0x00,0x00,0x01, 0x61, 0x11,0x22};
    CamH264NalStats st;
    cameraH264ScanNals(pframe, sizeof(pframe), &st);
    CHECK(st.units == 1);
    CHECK(st.other == 1);
    CHECK(st.idr == 0);
    CHECK_FALSE(cameraH264HasStreamStart(pframe, sizeof(pframe)));
    // IDR before SPS/PPS: decoder cannot start here.
    const uint8_t idrFirst[] = {
        0x00,0x00,0x01, 0x65, 0xDD,
        0x00,0x00,0x01, 0x67, 0xAA,
        0x00,0x00,0x01, 0x68, 0xCC,
    };
    CHECK_FALSE(cameraH264HasStreamStart(idrFirst, sizeof(idrFirst)));
    // empty / null guards
    CamH264NalStats z;
    cameraH264ScanNals(nullptr, 0, &z);
    CHECK(z.units == 0);
    CHECK_FALSE(cameraH264HasStreamStart(nullptr, 0));
}

TEST_CASE(".264 filename: base_RES_fps.264") {
    char out[64];
    cameraH264FileName("2026-10-12_120000", "FHD", 12, out);
    CHECK(std::string(out) == "2026-10-12_120000_FHD_12.264");
    cameraH264FileName("NOSYNC_42", "VGA", 6, out);
    CHECK(std::string(out) == "NOSYNC_42_VGA_6.264");
}

TEST_CASE("Fase 4 fps: H.264 ceiling 30, clamp 1-30 (MJPEG keeps its own 6)") {
    CHECK(cameraH264FpsMax() == 30);
    CHECK(CAM_H264_FPS_MAX == 30);
    CHECK(cameraH264FpsClamp(12) == 12);
    CHECK(cameraH264FpsClamp(1) == 1);
    CHECK(cameraH264FpsClamp(30) == 30);
    CHECK(cameraH264FpsClamp(0) == 1);
    CHECK(cameraH264FpsClamp(-3) == 1);
    CHECK(cameraH264FpsClamp(31) == 30);
    CHECK(cameraH264FpsClamp(255) == 30);
}

TEST_CASE("container clamp: raw default, AVI opt-in only") {
    CHECK(cameraH264ContainerClamp(0) == CAM_H264_CONT_RAW);
    CHECK(cameraH264ContainerClamp(1) == CAM_H264_CONT_AVI);
    CHECK(cameraH264ContainerClamp(2) == CAM_H264_CONT_RAW);
    CHECK(cameraH264ContainerClamp(-1) == CAM_H264_CONT_RAW);
}

TEST_CASE("AVI-H264 layout: fixed 480-byte header, offsets, RIFF math") {
    CHECK(CAM_H264_AVI_STRF_CONTENT == 296);
    CHECK(CAM_H264_AVI_STRL_CONTENT == 372);
    CHECK(CAM_H264_AVI_HDRL_CONTENT == 448);
    CHECK(CAM_H264_AVI_HDR_BYTES == 480);
    CHECK(CAM_H264_AVI_MOVI_OFF == 480);
    CHECK(CAM_H264_AVI_EXT_OFF == 212);
    // placeholder (movi=4) and final headers share the size: offsets never move
    CHECK(cameraH264AviRiffSize(4) == 472 + 4);
    CHECK(cameraH264AviRiffSize(1000000) == 472 + 1000000);
    // MJPEG twin math for reference: 224-byte header -> riff = 216 + movi
    CHECK(CAM_H264_AVI_HDR_BYTES - 8 == 472);
    CHECK(std::string(CAM_H264_AVI_FCC) == "H264");
}

TEST_CASE("SPS/PPS extract: IDR blob yields ordered sets with start codes") {
    const uint8_t blob[] = {
        0x00,0x00,0x00,0x01, 0x67, 0xAA,0xBB,       // SPS (7)
        0x00,0x00,0x00,0x01, 0x68, 0xCC,            // PPS (8)
        0x00,0x00,0x01,       0x65, 0xDD,0xEE,0xFF, // IDR (5, skipped)
    };
    uint8_t out[64];
    size_t n = cameraH264ExtractSpsPps(blob, sizeof(blob), out, sizeof(out));
    // SPS: 4 + 3 payload; PPS: 4 + 2 payload = 13 bytes
    CHECK(n == 13);
    const uint8_t want[] = {
        0x00,0x00,0x00,0x01, 0x67, 0xAA,0xBB,
        0x00,0x00,0x00,0x01, 0x68, 0xCC,
    };
    CHECK(memcmp(out, want, sizeof(want)) == 0);
    // 3-byte input codes are normalised to 4-byte codes on output
    CamH264NalStats st;
    cameraH264ScanNals(out, n, &st);
    CHECK(st.sps == 1);
    CHECK(st.pps == 1);
    CHECK(st.idr == 0);
}

TEST_CASE("SPS/PPS extract: no sets, tiny cap, guards") {
    // P-frame only: nothing to extract.
    const uint8_t pframe[] = {0x00,0x00,0x01, 0x61, 0x11,0x22};
    uint8_t out[64];
    CHECK(cameraH264ExtractSpsPps(pframe, sizeof(pframe), out, sizeof(out)) == 0);
    // IDR-first blob still yields its trailing sets.
    const uint8_t idrFirst[] = {
        0x00,0x00,0x01, 0x65, 0xDD,
        0x00,0x00,0x01, 0x67, 0xAA,
        0x00,0x00,0x01, 0x68, 0xCC,
    };
    size_t n = cameraH264ExtractSpsPps(idrFirst, sizeof(idrFirst), out, sizeof(out));
    CHECK(n == 12);  // (4+2) + (4+2)
    // Cap too small for both sets: clean 0, no partial copy.
    uint8_t tiny[8];
    CHECK(cameraH264ExtractSpsPps(idrFirst, sizeof(idrFirst), tiny, sizeof(tiny)) == 0);
    // Cap fits SPS alone but not PPS: still 0 (all-or-nothing).
    const uint8_t blob[] = {
        0x00,0x00,0x00,0x01, 0x67, 0xAA,0xBB,0x11,0x22,  // SPS: 4+5
        0x00,0x00,0x00,0x01, 0x68, 0xCC,                // PPS: 4+2
    };
    uint8_t mid[10];
    CHECK(cameraH264ExtractSpsPps(blob, sizeof(blob), mid, sizeof(mid)) == 0);
    uint8_t fit[15];
    CHECK(cameraH264ExtractSpsPps(blob, sizeof(blob), fit, sizeof(fit)) == 15);
    // Guards.
    CHECK(cameraH264ExtractSpsPps(nullptr, 0, out, sizeof(out)) == 0);
    CHECK(cameraH264ExtractSpsPps(blob, sizeof(blob), nullptr, 0) == 0);
}

// --- UYVY -> encoder input repacking (silicon-dependent formats) ------------
// esp_h264's accepted raw format depends on silicon: P4 rev >= v3.0 takes
// O_UYY_E_VYY only (UYVY -> ESP_H264_ERR_ARG), the SW encoder takes YUYV/I420.
// The CSI pipeline gives us UYVY, so both layouts are derived here.

TEST_CASE("pack: O_UYY_E_VYY is 1.5 bytes/px, YUYV is 2 (aligned sizes)") {
    // 800x640 and 1920x1080 are already 16-aligned.
    CHECK(cameraH264InputLen(800, 640, CAM_H264_FMT_HW) == (size_t)800 * 640 * 3 / 2);
    CHECK(cameraH264InputLen(800, 640, CAM_H264_FMT_SW) == (size_t)800 * 640 * 2);
    // 1080p height aligns to 1088 -> the rows beyond 1080 are padding.
    CHECK(cameraH264InputLen(1920, 1080, CAM_H264_FMT_HW) == (size_t)1920 * 1088 * 3 / 2);
}

TEST_CASE("pack O_UYY_E_VYY: one 2x2 block maps to [U Y0 Y1 V Y2 Y3]") {
    // 4x2 UYVY. Pixel values chosen so every source byte is identifiable:
    //   row0: U0 Y1 U1 Y2 U2 Y3 U3 Y4     (U,V pairs per 2 px)
    //   row1: U4 Y5 U5 Y6 U6 Y7 U7 Y8
    uint8_t src[4 * 2 * 2] = {
        10, 1, 20, 2, 30, 3, 40, 4,     // row 0
        50, 5, 60, 6, 70, 7, 80, 8      // row 1
    };
    uint8_t dst[4 * 2 * 3];
    memset(dst, 0xEE, sizeof(dst));
    cameraH264PackUyvy(src, 4, 2, 4, dst, CAM_H264_FMT_HW);
    // Block column 0: U=10, top Y=(1,2), V=50, bottom Y=(5,6)
    CHECK(dst[0] == 10); CHECK(dst[1] == 1); CHECK(dst[2] == 2);
    CHECK(dst[3] == 50); CHECK(dst[4] == 5); CHECK(dst[5] == 6);
    // Block column 1: U=30, top Y=(3,4), V=70, bottom Y=(7,8)
    CHECK(dst[6] == 30); CHECK(dst[7] == 3); CHECK(dst[8] == 4);
    CHECK(dst[9] == 70); CHECK(dst[10] == 7); CHECK(dst[11] == 8);
}

TEST_CASE("pack O_UYY_E_VYY: row-pair stride is aw*3, chroma comes from the top row") {
    // 4x4: chroma identical on both rows, so the top-row U and the bottom-row
    // V must land in the same 6-byte block.
    uint8_t src[4 * 4 * 2];
    for (unsigned i = 0; i < sizeof(src) / 2; i++) {
        src[i * 2]     = 0x80;          // U on every row
        src[i * 2 + 1] = (uint8_t)i;    // luma counts up
    }
    uint8_t dst[4 * 4 * 3];
    memset(dst, 0, sizeof(dst));
    cameraH264PackUyvy(src, 4, 2, 4, dst, CAM_H264_FMT_HW);
    CHECK(dst[0] == 0x80);              // U from row 0
    CHECK(dst[3] == 0x80);              // V slot also from row 0
    // Second row-pair starts at offset aw*3 = 12 and is untouched (h=2).
    CHECK(dst[12] == 0);
}

TEST_CASE("pack YUYV (software encoder): U/V bytes swapped inside each pair") {
    // UYVY [U0 Y0 V0 Y1] -> YUYV [Y0 U0 Y1 V0]
    uint8_t src[2 * 2 * 2] = {
        11, 21, 31, 41,    // row 0
        51, 61, 71, 81     // row 1
    };
    uint8_t dst[2 * 2 * 2];
    memset(dst, 0, sizeof(dst));
    cameraH264PackUyvy(src, 2, 2, 2, dst, CAM_H264_FMT_SW);
    CHECK(dst[0] == 21); CHECK(dst[1] == 11);
    CHECK(dst[2] == 41); CHECK(dst[3] == 31);
    CHECK(dst[4] == 61); CHECK(dst[5] == 51);
    CHECK(dst[6] == 81); CHECK(dst[7] == 71);
}

TEST_CASE("pack YUYV: 4x2 frame fills exactly w*h*2 bytes (no overrun)") {
    // Regression: the SW path used to loop w*h times and write twice the
    // destination size, smashing the caller's heap (P4 software encoder).
    uint8_t src[4 * 2 * 2];
    for (unsigned i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i + 1);
    uint8_t dst[4 * 2 * 2 + 4];
    memset(dst, 0xEE, sizeof(dst));
    cameraH264PackUyvy(src, 4, 2, 4, dst, CAM_H264_FMT_SW);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(dst[i * 4]     == src[i * 4 + 1]);   // Y0
        CHECK(dst[i * 4 + 1] == src[i * 4]);       // U
        CHECK(dst[i * 4 + 2] == src[i * 4 + 3]);   // Y1
        CHECK(dst[i * 4 + 3] == src[i * 4 + 2]);   // V
    }
    // Canary: the 16-byte YUYV frame is the whole destination.
    CHECK(dst[16] == 0xEE); CHECK(dst[17] == 0xEE);
    CHECK(dst[18] == 0xEE); CHECK(dst[19] == 0xEE);
}

TEST_CASE("pack guards: null/zero arguments are no-ops, never writes") {
    uint8_t dst[8];
    memset(dst, 0xAB, sizeof(dst));
    uint8_t src[8] = {0};
    cameraH264PackUyvy(nullptr, 4, 2, 4, dst, CAM_H264_FMT_HW);
    cameraH264PackUyvy(src, 0, 2, 4, dst, CAM_H264_FMT_HW);
    cameraH264PackUyvy(src, 4, 0, 4, dst, CAM_H264_FMT_HW);
    cameraH264PackUyvy(src, 4, 2, 0, dst, CAM_H264_FMT_HW);
    cameraH264PackUyvy(src, 4, 2, 4, nullptr, CAM_H264_FMT_HW);
    for (unsigned i = 0; i < sizeof(dst); i++) CHECK(dst[i] == 0xAB);
}
