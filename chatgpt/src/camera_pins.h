#pragma once
// camera_pins.h — Validates that all required CAM_PIN_* macros are defined.
//
// Do NOT define camera pins here.
// Pin assignments must come from board_pins.h under the active TARGET_BOARD_* block:
//   TARGET_BOARD_S3_CAM  — ESP32-S3 with onboard DVP camera connector (XCLK=15, etc.)
//   TARGET_BOARD_P4      — Waveshare ESP32-P4 DevKit (MIPI CSI-2; DVP pins = -1)
//
// To add a custom board: define all CAM_PIN_* in user_config.h STEP 2 before this
// header is included (via camera_manager.cpp → camera_pins.h).

// Default lamp pin — boards without a lamp set this to -1.
#ifndef CAM_PIN_LAMP
  #define CAM_PIN_LAMP  -1
#endif

// CAM_IS_MIPI_CSI: set to 1 for ESP32-P4 (MIPI CSI-2 hardware lanes).
// On P4, DVP pins (D0-D7, VSYNC, HREF, PCLK) are -1; the driver uses internal lanes.
#ifndef CAM_IS_MIPI_CSI
  #define CAM_IS_MIPI_CSI  0
#endif

// Validate that the mandatory pins have been defined by board_pins.h.
// If any are missing, the board does not have camera support configured.
#ifndef CAM_PIN_XCLK
  #error "ENABLE_CAMERA=1 but CAM_PIN_XCLK is undefined. Select TARGET_BOARD_S3_CAM or TARGET_BOARD_P4 in user_config.h STEP 1, or define all CAM_PIN_* manually in STEP 2."
#endif
#ifndef CAM_PIN_SIOD
  #error "CAM_PIN_SIOD undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_SIOC
  #error "CAM_PIN_SIOC undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_D7
  #error "CAM_PIN_D7 undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_D0
  #error "CAM_PIN_D0 undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_VSYNC
  #error "CAM_PIN_VSYNC undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_HREF
  #error "CAM_PIN_HREF undefined — camera pins not configured for this board."
#endif
#ifndef CAM_PIN_PCLK
  #error "CAM_PIN_PCLK undefined — camera pins not configured for this board."
#endif
