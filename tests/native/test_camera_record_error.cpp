#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstring>

// Compiles the PRODUCTION header directly (strategy 3: compile prod source).
// camera_record_error.h is a pure enum + message helper — zero ESP32 deps.
// CAM-3: the .18 P4 Cam answered a generic "Record start failed (SD?)"; this
// proves each failure cause maps to its own stable tag and API message.
#include "../../chatgpt/src/camera_record_error.h"

TEST_CASE("cause tags: stable short tokens for logs") {
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_OK), "ok") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_ALREADY_ACTIVE), "already_active") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_SD_UNAVAILABLE), "sd_unavailable") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_MKDIR_ROOT), "mkdir_root") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_MKDIR_FOLDER), "mkdir_folder") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_OPEN_FAILED), "open_failed") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseTag(CAM_REC_HEADER_FAILED), "header_failed") == 0);
    // Out-of-range value must not crash and must not masquerade as a real cause.
    CHECK(std::strcmp(cameraRecordStartCauseTag((CameraRecordStartResult)200), "unknown") == 0);
}

TEST_CASE("cause messages: each failure maps to a distinct API message") {
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_ALREADY_ACTIVE), "Already recording") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_SD_UNAVAILABLE),
                      "Record start failed (SD unavailable)") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_MKDIR_ROOT),
                      "Record start failed (mkdir /CAM)") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_MKDIR_FOLDER),
                      "Record start failed (mkdir folder)") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_OPEN_FAILED),
                      "Record start failed (open file)") == 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_HEADER_FAILED),
                      "Record start failed (header)") == 0);
    // Distinctness: the two mkdir causes and the two file causes must not share
    // a message, otherwise the granular report would be indistinguishable.
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_MKDIR_ROOT),
                      cameraRecordStartCauseMessage(CAM_REC_MKDIR_FOLDER)) != 0);
    CHECK(std::strcmp(cameraRecordStartCauseMessage(CAM_REC_OPEN_FAILED),
                      cameraRecordStartCauseMessage(CAM_REC_HEADER_FAILED)) != 0);
}

TEST_CASE("enum contract: OK is zero, every failure is non-zero") {
    CHECK(CAM_REC_OK == 0);
    CHECK(CAM_REC_ALREADY_ACTIVE != 0);
    CHECK(CAM_REC_SD_UNAVAILABLE != 0);
    CHECK(CAM_REC_MKDIR_ROOT != 0);
    CHECK(CAM_REC_MKDIR_FOLDER != 0);
    CHECK(CAM_REC_OPEN_FAILED != 0);
    CHECK(CAM_REC_HEADER_FAILED != 0);
}
