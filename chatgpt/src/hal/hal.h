#pragma once
// HAL master include — backward compatible with board_pins.h
// New code should include this instead of board_pins.h directly.

#include "../board_pins.h"

// Board identity — usable at compile-time (#if) or runtime (getBoardId())
#define BOARD_ID_UNSET     0
#define BOARD_ID_N16R8     1
#define BOARD_ID_8DI8DO    2
#define BOARD_ID_P4        3
#define BOARD_ID_S3_CAM    4

#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE == 1
#  define ACTIVE_BOARD_ID BOARD_ID_8DI8DO
#elif defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#  define ACTIVE_BOARD_ID BOARD_ID_P4
#elif defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM == 1
#  define ACTIVE_BOARD_ID BOARD_ID_S3_CAM
#else
#  define ACTIVE_BOARD_ID BOARD_ID_N16R8
#endif

// Runtime board detection
inline int getBoardId() {
#if ACTIVE_BOARD_ID == BOARD_ID_8DI8DO
    return BOARD_ID_8DI8DO;
#elif ACTIVE_BOARD_ID == BOARD_ID_P4
    return BOARD_ID_P4;
#elif ACTIVE_BOARD_ID == BOARD_ID_S3_CAM
    return BOARD_ID_S3_CAM;
#else
    return BOARD_ID_N16R8;
#endif
}

inline const char* getBoardName() {
#if ACTIVE_BOARD_ID == BOARD_ID_8DI8DO
    return "Waveshare 8DI8DO POE";
#elif ACTIVE_BOARD_ID == BOARD_ID_P4
    return "ESP32-P4 DevKit";
#elif ACTIVE_BOARD_ID == BOARD_ID_S3_CAM
    return "ESP32-S3 Camera";
#else
    return "ESP32-S3 N16R8 DevKit";
#endif
}
