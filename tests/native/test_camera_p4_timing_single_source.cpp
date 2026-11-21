// Native tests for the single 1080p timing source (CAM-88).
//
// CAM-86 measured the mode streaming at ~8.2 fps while `sensorFmt` reported the
// register-table pair: the ISP timing lived in a second table
// (kP4CustomIspInfo[]) that still carried the fitted 2416x1104 pair, and the
// numbers in kP4SensorFmts were metadata nobody read.
//
// CAM-88 removes that second table: every row of kP4SensorFmts carries its own
// isp_info, built for 1920x1080 from the CAM_P4_1080P_* macros, and the format
// descriptor points at the row (`fmt.isp_info = &def.isp`). Part A pins the
// numbers and the frame rate they imply; part B pins the plumbing so the split
// cannot come back.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/camera_resolution.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

std::string readSrc(const char *rel) {
    std::string path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot read " << path);
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Bounded body: from `from` to the next `to` marker.
std::string slice(const std::string &text, const char *from, const char *to) {
    const size_t start = text.find(from);
    REQUIRE_MESSAGE(start != std::string::npos, "marker not found: " << from);
    const size_t end = text.find(to, start + 1);
    REQUIRE_MESSAGE(end != std::string::npos, "end marker not found: " << to);
    return text.substr(start, end - start);
}

// Drop whole-line `//` comments: the file documents the old fitted pair in
// prose, and only executable code must be free of it.
std::string stripLineComments(const std::string &text) {
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "//") != 0) {
            out += line;
            out += '\n';
        }
        pos = eol + 1;
    }
    return out;
}

size_t countOf(const std::string &text, const char *needle) {
    size_t n = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + std::strlen(needle))) {
        n++;
    }
    return n;
}

} // namespace

// ── Part A: the numbers, and the frame rate they imply ──────────────────────

TEST_CASE("the 1080p constants are the register-table pair") {
    CHECK(CAM_P4_1080P_HTS == 2271);
    CHECK(CAM_P4_1080P_VTS == 1199);
    CHECK(CAM_P4_1080P_PCLK == 81666700u);
    CHECK(CAM_P4_1080P_FPS == 30);
}

TEST_CASE("the register-table pair derives exactly 30.0 fps, the fitted one did not") {
    // 81.6667 MHz / (2271 * 1199) = 30.0 fps.
    CHECK(cameraP4ModeFps(CAM_P4_1080P_PCLK, CAM_P4_1080P_HTS, CAM_P4_1080P_VTS) == 30);
    // The pair CAM-86 measured as the APPLIED timing was fitted to 30 fps but
    // actually derives 30.6 -> 31, which is why the swap is a real change.
    CHECK(cameraP4ModeFps(CAM_P4_1080P_PCLK, 2416, 1104) == 31);
    CHECK(cameraP4ModeFps(CAM_P4_1080P_PCLK, 2416, 1104) != CAM_P4_1080P_FPS);
}

// ── Part B: one table only ─────────────────────────────────────────────────

TEST_CASE("the duplicate ISP table is gone") {
    const std::string cpp = stripLineComments(readSrc("camera_p4_backend.cpp"));

    CHECK(cpp.find("kP4CustomIspInfo") == std::string::npos);
    // No row may carry the fitted pair as timing again.
    CHECK(cpp.find("2416") == std::string::npos);
    CHECK(cpp.find("1104") == std::string::npos);
    // The scalar trio that nobody read is gone too: the row owns an isp_info.
    CHECK(cpp.find("int pclk, hts, vts;") == std::string::npos);
    CHECK(cpp.find("esp_cam_sensor_isp_info_t isp;") != std::string::npos);
}

TEST_CASE("every mode row carries one isp_info and the format points at its row") {
    const std::string cpp = stripLineComments(readSrc("camera_p4_backend.cpp"));
    const std::string table =
        slice(cpp, "static const P4SensorFmtDef kP4SensorFmts[] = {",
              "static const P4SensorFmtDef* p4SensorFmtFor(");

    CHECK(countOf(table, "{{SENSOR_ISP_INFO_VERSION_DEFAULT") == (size_t)CAM_RES_MODE_COUNT);
    CHECK(countOf(cpp, "fmt.isp_info = &def.isp;") == 1);
    CHECK(cpp.find("fmt.isp_info = &kP4CustomIspInfo") == std::string::npos);
}

TEST_CASE("the applied 1080p row is built from the macros, HTS before VTS") {
    const std::string cpp = stripLineComments(readSrc("camera_p4_backend.cpp"));
    const std::string table =
        slice(cpp, "static const P4SensorFmtDef kP4SensorFmts[] = {",
              "static const P4SensorFmtDef* p4SensorFmtFor(");

    const std::string row = slice(table, "{1920, 1080, ESP_CAM_SENSOR_PIXFORMAT_RAW10,",
                                  "\"MIPI_2lane_24Minput_RAW10_1920x1080_30fps\"");

    // The isp_info initializer takes (version, pclk, hts, vts): the macros must
    // land in that order, which is the order the real struct declares.
    CHECK(row.find("{{SENSOR_ISP_INFO_VERSION_DEFAULT, CAM_P4_1080P_PCLK, CAM_P4_1080P_HTS, "
                   "CAM_P4_1080P_VTS,") != std::string::npos);
    // No literal timing in the row: the macros are the only source.
    CHECK(row.find("2271") == std::string::npos);
    CHECK(row.find("1199") == std::string::npos);
    CHECK(row.find("81666700") == std::string::npos);
}
