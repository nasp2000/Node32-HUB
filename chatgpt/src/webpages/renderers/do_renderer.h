// do_renderer.h
// Renderer for the /do digital output control page.

#pragma once
#include <Arduino.h>
#include "../../feature_flags.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

size_t streamDoPageToPrint(Print& out);

#endif // BOARD_HAS_TCA9554_DO
